// padvr_companion.exe — polls XInput and writes pad state into the
// shared-memory channel the driver reads each frame.
//
// Run it after SteamVR is up. Exits cleanly on Ctrl+C.

#include <windows.h>
#include <xinput.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <atomic>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <thread>
#include <chrono>

namespace {
constexpr const char* kMappingName = "Local\\padVR_TriggerIPC_v1";
constexpr uint32_t kMagic = 0x30525650; // 'PVR0'
constexpr uint32_t kVersion = 4;
constexpr uint8_t kClickThreshold = 200; // XInput trigger range 0..255

// SteamVR vrmonitor URIs we trigger from gamepad system button.
constexpr const char* kURIDashboardToggle = "vrmonitor://debugcommands/system_dashboard_toggle";
constexpr const char* kURIRecenter        = "vrmonitor://debugcommands/seated_and_standing_position_reset";
constexpr int         kHoldThresholdMs    = 500;

// Guide / Xbox button bit. Not exposed by standard XInputGetState — only by
// the undocumented XInputGetStateEx (ordinal 100 in xinput1_4.dll).
constexpr WORD XINPUT_GAMEPAD_GUIDE = 0x0400;

struct XINPUT_STATE_EX {
    DWORD          dwPacketNumber;
    XINPUT_GAMEPAD Gamepad;
};
using XInputGetStateEx_t = DWORD (WINAPI*)(DWORD, XINPUT_STATE_EX*);

// Must mirror padvr::TriggerSharedState exactly.
struct TriggerSharedState {
    uint32_t magic;
    uint32_t version;
    std::atomic<uint64_t> sequence;

    float    triggerValue;
    uint8_t  triggerClick;
    uint8_t  gamepadPresent;
    uint8_t  _pad0[2];

    float    joystickX;
    float    joystickY;
    uint8_t  joystickClick;
    uint8_t  _pad1[3];
};

std::atomic<bool> g_quit{false};

BOOL WINAPI CtrlHandler(DWORD dwType) {
    if (dwType == CTRL_C_EVENT || dwType == CTRL_BREAK_EVENT || dwType == CTRL_CLOSE_EVENT) {
        g_quit.store(true, std::memory_order_release);
        return TRUE;
    }
    return FALSE;
}

// Standard XInput-style radial deadzone, normalised to -1..1.
float NormalizeStick(SHORT v, SHORT deadzone) {
    if (v >  deadzone) return float(v - deadzone) / float(32767 - deadzone);
    if (v < -deadzone) return float(v + deadzone) / float(32768 - deadzone);
    return 0.0f;
}

// Walk the running process list for vrmonitor.exe / vrserver.exe. We use
// this as our gate: while SteamVR is off, the companion stays fully passive
// (no XInput polling, no URI dispatch) so Steam's Big Picture launcher
// retains exclusive ownership of the Guide button on the desktop.
bool IsSteamVRRunning() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32 pe{};
    pe.dwSize = sizeof(pe);
    bool found = false;
    if (Process32First(snap, &pe)) {
        do {
            if (_stricmp(pe.szExeFile, "vrmonitor.exe") == 0 ||
                _stricmp(pe.szExeFile, "vrserver.exe")  == 0) {
                found = true;
                break;
            }
        } while (Process32Next(snap, &pe));
    }
    CloseHandle(snap);
    return found;
}
}

int main() {
    // Single-instance guard. If a padvr_companion process is already running
    // (typical: user double-clicks the shell:startup shortcut after one's
    // already auto-started on login), exit silently. The mutex is auto-
    // released when whichever process owns it exits.
    HANDLE singletonMutex = CreateMutexA(nullptr, FALSE, "Local\\padvr_companion_singleton");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        if (singletonMutex) CloseHandle(singletonMutex);
        return 0;
    }

    SetConsoleCtrlHandler(CtrlHandler, TRUE);

    // Resolve XInputGetStateEx (ordinal 100) so we can read the Guide/Xbox
    // button bit (0x0400). xinput1_4.dll ships with Win10+. If unavailable
    // — or if Steam's overlay swallows the Guide press first — fall back to
    // the Menu/Start button via plain XInputGetState (search for
    // FALLBACK_TO_START below).
    HMODULE hXInput = LoadLibraryA("xinput1_4.dll");
    XInputGetStateEx_t pXInputGetStateEx = nullptr;
    if (hXInput) {
        pXInputGetStateEx = reinterpret_cast<XInputGetStateEx_t>(
            GetProcAddress(hXInput, reinterpret_cast<LPCSTR>(100)));
    }
    if (!pXInputGetStateEx) {
        std::printf("warn: XInputGetStateEx not available — Guide button unreadable\n");
    }

    HANDLE h = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(TriggerSharedState), kMappingName);
    if (!h) {
        std::fprintf(stderr, "CreateFileMapping failed: %lu\n", GetLastError());
        return 1;
    }
    const bool owner = (GetLastError() != ERROR_ALREADY_EXISTS);

    void* view = MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(TriggerSharedState));
    if (!view) {
        std::fprintf(stderr, "MapViewOfFile failed: %lu\n", GetLastError());
        CloseHandle(h);
        return 1;
    }
    auto* state = static_cast<TriggerSharedState*>(view);

    if (owner || state->magic != kMagic || state->version != kVersion) {
        std::memset(state, 0, sizeof(*state));
        state->magic = kMagic;
        state->version = kVersion;
        state->sequence.store(0, std::memory_order_release);
    }

    std::printf("padvr_companion v%u running (owner=%d). Ctrl+C to quit.\n",
                kVersion, owner ? 1 : 0);

    uint64_t seq = 0;
    DWORD slotsConnected = 0;
    auto lastReport = std::chrono::steady_clock::now();
    uint8_t lastBest = 0;

    // Re-check the SteamVR process list every ~2s — cheap, and that latency
    // is invisible because SteamVR itself takes ages to come up.
    bool vrRunning = false;
    auto lastVRCheck = std::chrono::steady_clock::now() - std::chrono::seconds(10);

    // System-button state machine lives outside the loop body so we can
    // reset it cleanly when SteamVR drops, avoiding a phantom dashboard
    // toggle when SteamVR comes back up mid-press.
    bool sysHeld = false;
    bool recenterFired = false;
    std::chrono::steady_clock::time_point sysPressTime{};

    while (!g_quit.load(std::memory_order_acquire)) {
        // ---- SteamVR presence gate ----
        auto checkNow = std::chrono::steady_clock::now();
        if (checkNow - lastVRCheck >= std::chrono::seconds(2)) {
            bool prev = vrRunning;
            vrRunning = IsSteamVRRunning();
#ifndef NDEBUG
            if (vrRunning != prev) {
                std::printf("[steamvr] %s\n", vrRunning ? "detected" : "stopped");
            }
#else
            (void)prev;
#endif
            lastVRCheck = checkNow;
        }

        if (!vrRunning) {
            // Idle: no XInput polling (so Steam keeps the Guide button), no
            // URI dispatch, shared mapping zeroed so the driver — even if
            // somehow loaded — sees gamepadPresent=false.
            state->triggerValue   = 0.0f;
            state->triggerClick   = 0;
            state->gamepadPresent = 0;
            state->joystickX      = 0.0f;
            state->joystickY      = 0.0f;
            state->joystickClick  = 0;
            state->sequence.store(++seq, std::memory_order_release);

            // Reset the Guide-button state machine in case we're idling
            // mid-press. Otherwise next active loop could fire a phantom
            // dashboard-toggle on the inferred "release".
            sysHeld = false;
            recenterFired = false;

            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }

        // ---- Active path: SteamVR is up ----
        DWORD nowConnected = 0;
        uint8_t bestTrig = 0;
        SHORT joyLX = 0, joyLY = 0;
        bool joyLS = false;
        bool sysBtn = false;
        bool gotPrimary = false;

        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            // Always read Ex variant when available so we get the Guide bit.
            // Fall back to standard XInputGetState if Ex is missing.
            WORD wButtons = 0;
            XINPUT_GAMEPAD pad{};
            if (pXInputGetStateEx) {
                XINPUT_STATE_EX xse{};
                if (pXInputGetStateEx(i, &xse) != ERROR_SUCCESS) continue;
                pad = xse.Gamepad;
                wButtons = pad.wButtons;
            } else {
                XINPUT_STATE xs{};
                if (XInputGetState(i, &xs) != ERROR_SUCCESS) continue;
                pad = xs.Gamepad;
                wButtons = pad.wButtons;
            }
            nowConnected |= (1u << i);
            if (pad.bLeftTrigger  > bestTrig) bestTrig = pad.bLeftTrigger;
            if (pad.bRightTrigger > bestTrig) bestTrig = pad.bRightTrigger;
            // Aggregate system button across all pads (any pad's press counts).
            // FALLBACK_TO_START: if Guide proves unreliable on this machine
            // (Steam overlay grabbing it, etc.), swap XINPUT_GAMEPAD_GUIDE
            // for XINPUT_GAMEPAD_START here.
            if (wButtons & XINPUT_GAMEPAD_GUIDE) sysBtn = true;
            if (!gotPrimary) {
                joyLX = pad.sThumbLX;
                joyLY = pad.sThumbLY;
                joyLS = (wButtons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
                gotPrimary = true;
            }
        }

        // System-button tap-vs-hold state machine.
        //   tap (released < 500ms)  -> open/close SteamVR dashboard
        //   hold (>= 500ms)         -> recenter view (fires once at threshold)
        //   release after hold      -> nothing further
        if (sysBtn && !sysHeld) {
            sysHeld = true;
            sysPressTime = std::chrono::steady_clock::now();
            recenterFired = false;
        } else if (sysBtn && sysHeld && !recenterFired) {
            auto held = std::chrono::steady_clock::now() - sysPressTime;
            if (held >= std::chrono::milliseconds(kHoldThresholdMs)) {
#ifndef NDEBUG
                std::printf("[sys] hold -> recenter\n");
#endif
                ShellExecuteA(nullptr, "open", kURIRecenter, nullptr, nullptr, SW_HIDE);
                recenterFired = true;
            }
        } else if (!sysBtn && sysHeld) {
            if (!recenterFired) {
#ifndef NDEBUG
                std::printf("[sys] tap -> dashboard toggle\n");
#endif
                ShellExecuteA(nullptr, "open", kURIDashboardToggle, nullptr, nullptr, SW_HIDE);
            }
            sysHeld = false;
        }

        state->triggerValue   = float(bestTrig) / 255.0f;
        state->triggerClick   = (bestTrig >= kClickThreshold) ? 1 : 0;
        state->gamepadPresent = (nowConnected != 0) ? 1 : 0;
        state->joystickX      = NormalizeStick(joyLX, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        state->joystickY      = NormalizeStick(joyLY, XINPUT_GAMEPAD_LEFT_THUMB_DEADZONE);
        state->joystickClick  = joyLS ? 1 : 0;
        state->sequence.store(++seq, std::memory_order_release);

#ifndef NDEBUG
        if (nowConnected != slotsConnected) {
            std::printf("[xinput] connected slot mask: 0x%lx\n", nowConnected);
        }
        slotsConnected = nowConnected;

        auto now = std::chrono::steady_clock::now();
        bool crossed = (bestTrig >= kClickThreshold) != (lastBest >= kClickThreshold);
        if (crossed || now - lastReport >= std::chrono::seconds(2)) {
            std::printf("[xinput] trig=%3u (%.2f) tclk=%d stick=(%+.2f,%+.2f) sclk=%d sys=%d slots=0x%lx\n",
                bestTrig, state->triggerValue, state->triggerClick,
                state->joystickX, state->joystickY, state->joystickClick,
                sysBtn ? 1 : 0, nowConnected);
            lastReport = now;
        }
        lastBest = bestTrig;
#else
        (void)slotsConnected; (void)lastReport; (void)lastBest;
#endif

        std::this_thread::sleep_for(std::chrono::milliseconds(8)); // ~120 Hz
    }

    UnmapViewOfFile(view);
    CloseHandle(h);
    return 0;
}
