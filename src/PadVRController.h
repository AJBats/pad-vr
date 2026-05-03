#pragma once

#include <openvr_driver.h>
#include <string>
#include <memory>

namespace padvr {

class TriggerIPC;

class PadVRController : public vr::ITrackedDeviceServerDriver {
public:
    PadVRController();
    ~PadVRController();

    // ITrackedDeviceServerDriver
    vr::EVRInitError Activate(uint32_t unObjectId) override;
    void Deactivate() override;
    void EnterStandby() override {}
    void* GetComponent(const char* pchComponentNameAndVersion) override { return nullptr; }
    void DebugRequest(const char* pchRequest, char* pchResponseBuffer, uint32_t unResponseBufferSize) override {
        if (unResponseBufferSize >= 1) pchResponseBuffer[0] = 0;
    }
    vr::DriverPose_t GetPose() override;

    // Called from the provider every server tick.
    void RunFrame();

    const std::string& GetSerial() const { return m_serial; }

private:
    vr::DriverPose_t MakePoseFromHmd(bool connected);

    std::string m_serial;
    std::string m_modelNumber;
    uint32_t m_objectId = vr::k_unTrackedDeviceIndexInvalid;
    vr::PropertyContainerHandle_t m_props = vr::k_ulInvalidPropertyContainer;

    vr::VRInputComponentHandle_t m_triggerValue  = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_triggerClick  = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_systemClick   = vr::k_ulInvalidInputComponentHandle;
    // Index/Knuckles-shaped joystick. SteamVR's knuckles bindings route
    // /input/thumbstick through joystick mode for dashboard/menu nav, which
    // means continuous analog deflection -> continuous scroll, with no
    // trackpad touch semantics or scroll-bounce on release.
    vr::VRInputComponentHandle_t m_thumbstickX     = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_thumbstickY     = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_thumbstickClick = vr::k_ulInvalidInputComponentHandle;
    // Compat mirror on /input/joystick/* for apps that bind by that name
    // (Touch convention) instead of /input/thumbstick (Knuckles convention).
    vr::VRInputComponentHandle_t m_joystickX       = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_joystickY       = vr::k_ulInvalidInputComponentHandle;
    vr::VRInputComponentHandle_t m_joystickClick   = vr::k_ulInvalidInputComponentHandle;

    bool m_active = false;
    std::unique_ptr<TriggerIPC> m_ipc;
};

} // namespace padvr
