#include "PadVRController.h"
#include "TriggerIPC.h"
#include "MathUtil.h"

#include <cstring>
#include <cstdio>
#include <cmath>

namespace padvr {

PadVRController::PadVRController()
    : m_serial("padvr_gaze_ctrl_0"),
      m_modelNumber("padVR Gaze Controller") {
    m_ipc = std::make_unique<TriggerIPC>();
}

PadVRController::~PadVRController() = default;

vr::EVRInitError PadVRController::Activate(uint32_t unObjectId) {
    m_objectId = unObjectId;
    m_props = vr::VRProperties()->TrackedDeviceToPropertyContainer(m_objectId);

    vr::VRProperties()->SetStringProperty(m_props, vr::Prop_ModelNumber_String, m_modelNumber.c_str());
    vr::VRProperties()->SetStringProperty(m_props, vr::Prop_ManufacturerName_String, "padVR");
    vr::VRProperties()->SetStringProperty(m_props, vr::Prop_SerialNumber_String, m_serial.c_str());
    vr::VRProperties()->SetStringProperty(m_props, vr::Prop_RenderModelName_String, "{padvr}controller");
    vr::VRProperties()->SetInt32Property(m_props, vr::Prop_DeviceClass_Int32, vr::TrackedDeviceClass_Controller);
    vr::VRProperties()->SetInt32Property(m_props, vr::Prop_ControllerRoleHint_Int32, vr::TrackedControllerRole_RightHand);

    // Our profile JSON declares the components we expose; SteamVR uses
    // controller_type to pick which binding files to apply. Reporting as
    // "knuckles" (Index controller) gets us SteamVR's joystick-mode menu
    // navigation bindings on /input/thumbstick — true analog axis with no
    // trackpad-touch ceremony, and no scroll bounce on stick release.
    vr::VRProperties()->SetStringProperty(m_props, vr::Prop_InputProfilePath_String, "{padvr}/input/padvr_profile.json");
    vr::VRProperties()->SetStringProperty(m_props, vr::Prop_ControllerType_String, "knuckles");

    vr::VRDriverInput()->CreateScalarComponent(m_props, "/input/trigger/value",
        &m_triggerValue, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedOneSided);
    vr::VRDriverInput()->CreateBooleanComponent(m_props, "/input/trigger/click", &m_triggerClick);
    vr::VRDriverInput()->CreateBooleanComponent(m_props, "/input/system/click", &m_systemClick);

    // Knuckles-style thumbstick — primary nav path for Index bindings.
    vr::VRDriverInput()->CreateScalarComponent(m_props, "/input/thumbstick/x",
        &m_thumbstickX, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    vr::VRDriverInput()->CreateScalarComponent(m_props, "/input/thumbstick/y",
        &m_thumbstickY, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    vr::VRDriverInput()->CreateBooleanComponent(m_props, "/input/thumbstick/click", &m_thumbstickClick);

    // Compat mirror — same data, Touch-style path. Cheap to expose.
    vr::VRDriverInput()->CreateScalarComponent(m_props, "/input/joystick/x",
        &m_joystickX, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    vr::VRDriverInput()->CreateScalarComponent(m_props, "/input/joystick/y",
        &m_joystickY, vr::VRScalarType_Absolute, vr::VRScalarUnits_NormalizedTwoSided);
    vr::VRDriverInput()->CreateBooleanComponent(m_props, "/input/joystick/click", &m_joystickClick);

    m_active = true;
    m_ipc->Start();

    vr::VRDriverLog()->Log("[padVR] Controller activated");
    return vr::VRInitError_None;
}

void PadVRController::Deactivate() {
    m_active = false;
    if (m_ipc) m_ipc->Stop();
    m_objectId = vr::k_unTrackedDeviceIndexInvalid;
}

// "Chest mount" geometry for the synth controller's reported pose:
//
//   * Position: HMD position translated DOWN by kDownOffsetM along world-Y.
//     Keeps the controller's apparent location at chest height regardless of
//     where the user looks.
//   * Orientation: HMD orientation composed with a LOCAL pitch-up rotation of
//     kPitchUpDeg degrees around the controller's right-axis. So when the
//     head looks forward, the controller looks forward+up; when the user
//     turns or tilts, the controller tracks accordingly.
//
// Math sanity check (head looking dead forward):
//   controller_pos = HMD - (0, 0.5, 0)
//   ray_dir        = HMD_forward rotated 45 deg up = (0, sin45, -cos45)
//   ray hits panel at HMD + 0.5*forward when t=0.707 -> impact at HMD eye
//   level, 0.5m in front. That's the SteamVR dashboard sweet spot.
//
// Tunables — adjust as taste / panel distance demand:
static constexpr double kDownOffsetM = 0.50;
static constexpr double kPitchUpDeg  = 27.0;

vr::DriverPose_t PadVRController::MakePoseFromHmd(bool connected) {
    vr::DriverPose_t pose = {};
    pose.poseTimeOffset = 0;
    pose.qWorldFromDriverRotation = { 1, 0, 0, 0 };
    pose.qDriverFromHeadRotation = { 1, 0, 0, 0 };
    pose.shouldApplyHeadModel = false;
    pose.willDriftInYaw = false;
    pose.deviceIsConnected = connected;

    if (!connected) {
        pose.poseIsValid = false;
        pose.result = vr::TrackingResult_Uninitialized;
        return pose;
    }

    vr::TrackedDevicePose_t devicePoses[vr::k_unMaxTrackedDeviceCount] = {};
    vr::VRServerDriverHost()->GetRawTrackedDevicePoses(0.0f, devicePoses, vr::k_unMaxTrackedDeviceCount);

    const vr::TrackedDevicePose_t& hmd = devicePoses[vr::k_unTrackedDeviceIndex_Hmd];
    if (!hmd.bPoseIsValid) {
        pose.poseIsValid = false;
        pose.result = vr::TrackingResult_Running_OutOfRange;
        return pose;
    }

    Vec3 pos;
    Quat rot;
    MatrixToPose(hmd.mDeviceToAbsoluteTracking, pos, rot);

    // World-down translation: pull the reported origin straight down so the
    // laser's visual source sits ~chest height beneath the headset.
    pose.vecPosition[0] = pos.x;
    pose.vecPosition[1] = pos.y - kDownOffsetM;
    pose.vecPosition[2] = pos.z;

    // Local pitch-up: q_final = q_hmd * q_pitch where q_pitch is a rotation
    // around +X by +kPitchUpDeg. Positive +X rotation in SteamVR's RHS
    // (Y-up, -Z forward) tilts the forward vector toward +Y, i.e. up.
    constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
    const double half = (kPitchUpDeg * kDeg2Rad) * 0.5;
    const double cw = std::cos(half);
    const double sw = std::sin(half);
    // Quaternion multiply: q_hmd * (cw, sw, 0, 0)
    const Quat r = rot;
    pose.qRotation.w = r.w * cw - r.x * sw;
    pose.qRotation.x = r.w * sw + r.x * cw;
    pose.qRotation.y = r.y * cw + r.z * sw;
    pose.qRotation.z = -r.y * sw + r.z * cw;

    pose.poseIsValid = true;
    pose.result = vr::TrackingResult_Running_OK;
    return pose;
}

vr::DriverPose_t PadVRController::GetPose() {
    return MakePoseFromHmd(m_active);
}

void PadVRController::RunFrame() {
    if (!m_active || m_objectId == vr::k_unTrackedDeviceIndexInvalid) return;

    TriggerIPC::Snapshot snap;
    if (m_ipc) m_ipc->Read(snap);

    // Activation gate: synth controller is "live" iff a conventional gamepad
    // is plugged in (and visible to XInput in the companion process).
    const bool shouldBeOn = snap.gamepadPresent;

    vr::DriverPose_t pose = MakePoseFromHmd(shouldBeOn);
    vr::VRServerDriverHost()->TrackedDevicePoseUpdated(m_objectId, pose, sizeof(pose));

    // When inactive, zero every input so we don't leak stale pad state into
    // SteamVR Input when the gamepad is unplugged.
    const float triggerVal   = shouldBeOn ? snap.triggerValue : 0.0f;
    const bool  triggerClick = shouldBeOn && snap.triggerClick;
    const float joyX         = shouldBeOn ? snap.joystickX    : 0.0f;
    const float joyY         = shouldBeOn ? snap.joystickY    : 0.0f;
    const bool  joyClick     = shouldBeOn && snap.joystickClick;

    vr::VRDriverInput()->UpdateScalarComponent(m_triggerValue,  triggerVal,  0.0);
    vr::VRDriverInput()->UpdateBooleanComponent(m_triggerClick, triggerClick, 0.0);

    // Thumbstick (primary, Knuckles convention) — continuous analog axis.
    vr::VRDriverInput()->UpdateScalarComponent(m_thumbstickX,     joyX,     0.0);
    vr::VRDriverInput()->UpdateScalarComponent(m_thumbstickY,     joyY,     0.0);
    vr::VRDriverInput()->UpdateBooleanComponent(m_thumbstickClick, joyClick, 0.0);

    // Joystick mirror (Touch convention) — same data, different name.
    vr::VRDriverInput()->UpdateScalarComponent(m_joystickX,       joyX,     0.0);
    vr::VRDriverInput()->UpdateScalarComponent(m_joystickY,       joyY,     0.0);
    vr::VRDriverInput()->UpdateBooleanComponent(m_joystickClick,  joyClick, 0.0);

#ifndef NDEBUG
    // Throttled debug: log on activation / trigger edges.
    static bool s_lastActive = false;
    static bool s_lastClick = false;
    bool actEdge = (shouldBeOn != s_lastActive);
    bool clkEdge = (triggerClick != s_lastClick);
    if (actEdge || clkEdge) {
        char buf[200];
        std::snprintf(buf, sizeof(buf),
            "[padVR] active=%d trig=%.2f click=%d stick=(%.2f, %.2f) sclk=%d",
            shouldBeOn ? 1 : 0, triggerVal, triggerClick ? 1 : 0,
            joyX, joyY, joyClick ? 1 : 0);
        vr::VRDriverLog()->Log(buf);
    }
    s_lastActive = shouldBeOn;
    s_lastClick = triggerClick;
#endif
}

} // namespace padvr
