#pragma once

#include <atomic>
#include <thread>

namespace padvr {

// Shared-memory IPC: companion process writes XInput trigger / stick /
// presence state into a named file mapping; driver reads it once per frame.
// Lock-free, single producer, single consumer.
struct TriggerSharedState {
    uint32_t magic;             // 'PVR0'
    uint32_t version;           // 4
    std::atomic<uint64_t> sequence;

    // Trigger
    float    triggerValue;      // 0..1, max(LT, RT) across connected pads
    uint8_t  triggerClick;      // 0/1

    // Presence — driver uses this as the "is the synth controller live?" gate.
    uint8_t  gamepadPresent;    // 0/1
    uint8_t  _pad0[2];

    // Left thumbstick of the first connected pad, deadzoned + normalised.
    float    joystickX;         // -1..1
    float    joystickY;         // -1..1
    uint8_t  joystickClick;     // 0/1, left-stick press
    uint8_t  _pad1[3];
};

class TriggerIPC {
public:
    TriggerIPC();
    ~TriggerIPC();

    struct Snapshot {
        float triggerValue = 0.0f;
        bool  triggerClick = false;
        bool  gamepadPresent = false;
        float joystickX = 0.0f;
        float joystickY = 0.0f;
        bool  joystickClick = false;
    };

    void Start();
    void Stop();
    void Read(Snapshot& out);

private:
    void* m_mapping = nullptr;     // HANDLE
    TriggerSharedState* m_state = nullptr;
    bool m_owner = false;
};

} // namespace padvr
