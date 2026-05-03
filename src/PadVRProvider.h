#pragma once

#include <openvr_driver.h>
#include <memory>

namespace padvr {

class PadVRController;

class PadVRProvider : public vr::IServerTrackedDeviceProvider {
public:
    PadVRProvider();
    ~PadVRProvider();
    vr::EVRInitError Init(vr::IVRDriverContext* pDriverContext) override;
    void Cleanup() override;
    const char* const* GetInterfaceVersions() override { return vr::k_InterfaceVersions; }
    void RunFrame() override;
    bool ShouldBlockStandbyMode() override { return false; }
    void EnterStandby() override {}
    void LeaveStandby() override {}

private:
    std::unique_ptr<PadVRController> m_controller;
};

} // namespace padvr
