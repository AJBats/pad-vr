#include "TriggerIPC.h"

#include <windows.h>
#include <openvr_driver.h>
#include <cstring>

namespace padvr {

static constexpr const char* kMappingName = "Local\\padVR_TriggerIPC_v1";
static constexpr uint32_t kMagic = 0x30525650; // 'PVR0' little-endian

TriggerIPC::TriggerIPC() = default;

TriggerIPC::~TriggerIPC() { Stop(); }

void TriggerIPC::Start() {
    HANDLE h = CreateFileMappingA(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
        0, sizeof(TriggerSharedState), kMappingName);
    if (!h) {
        vr::VRDriverLog()->Log("[padVR] CreateFileMapping failed");
        return;
    }
    m_owner = (GetLastError() != ERROR_ALREADY_EXISTS);
    m_mapping = h;

    void* view = MapViewOfFile(h, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, sizeof(TriggerSharedState));
    if (!view) {
        vr::VRDriverLog()->Log("[padVR] MapViewOfFile failed");
        CloseHandle(h);
        m_mapping = nullptr;
        return;
    }
    m_state = static_cast<TriggerSharedState*>(view);

    if (m_owner) {
        std::memset(m_state, 0, sizeof(*m_state));
        m_state->magic = kMagic;
        m_state->version = 4;
        m_state->sequence.store(0, std::memory_order_release);
    }

    vr::VRDriverLog()->Log(m_owner ? "[padVR] IPC owner" : "[padVR] IPC attached");
}

void TriggerIPC::Stop() {
    if (m_state) {
        UnmapViewOfFile(m_state);
        m_state = nullptr;
    }
    if (m_mapping) {
        CloseHandle(static_cast<HANDLE>(m_mapping));
        m_mapping = nullptr;
    }
}

void TriggerIPC::Read(Snapshot& out) {
    out = {};
    if (!m_state) return;
    if (m_state->magic != kMagic) return;
    // sequence is bumped by the writer per update; reading is best-effort.
    out.triggerValue   = m_state->triggerValue;
    out.triggerClick   = m_state->triggerClick != 0;
    out.gamepadPresent = m_state->gamepadPresent != 0;
    out.joystickX      = m_state->joystickX;
    out.joystickY      = m_state->joystickY;
    out.joystickClick  = m_state->joystickClick != 0;
}

} // namespace padvr
