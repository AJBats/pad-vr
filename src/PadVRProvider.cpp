#include "PadVRProvider.h"
#include "PadVRController.h"

namespace padvr {

PadVRProvider::PadVRProvider() = default;
PadVRProvider::~PadVRProvider() = default;

vr::EVRInitError PadVRProvider::Init(vr::IVRDriverContext* pDriverContext) {
    VR_INIT_SERVER_DRIVER_CONTEXT(pDriverContext);

    vr::VRDriverLog()->Log("[padVR] Provider Init");

    m_controller = std::make_unique<PadVRController>();

    // Register the controller as a tracked device. Serial number must be
    // unique across all SteamVR drivers.
    vr::VRServerDriverHost()->TrackedDeviceAdded(
        m_controller->GetSerial().c_str(),
        vr::TrackedDeviceClass_Controller,
        m_controller.get());

    return vr::VRInitError_None;
}

void PadVRProvider::Cleanup() {
    vr::VRDriverLog()->Log("[padVR] Provider Cleanup");
    m_controller.reset();
    VR_CLEANUP_SERVER_DRIVER_CONTEXT();
}

void PadVRProvider::RunFrame() {
    if (m_controller) {
        m_controller->RunFrame();
    }
}

} // namespace padvr
