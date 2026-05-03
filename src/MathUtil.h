#pragma once

#include <openvr_driver.h>
#include <cmath>

namespace padvr {

struct Quat { double w, x, y, z; };
struct Vec3 { double x, y, z; };

// Convert a 3x4 row-major rigid transform (as SteamVR returns in
// HmdMatrix34_t) into a translation + quaternion pair suitable for
// stuffing into DriverPose_t.
inline void MatrixToPose(const vr::HmdMatrix34_t& m, Vec3& outPos, Quat& outRot) {
    outPos.x = m.m[0][3];
    outPos.y = m.m[1][3];
    outPos.z = m.m[2][3];

    const double trace = m.m[0][0] + m.m[1][1] + m.m[2][2];
    if (trace > 0.0) {
        double s = 0.5 / std::sqrt(trace + 1.0);
        outRot.w = 0.25 / s;
        outRot.x = (m.m[2][1] - m.m[1][2]) * s;
        outRot.y = (m.m[0][2] - m.m[2][0]) * s;
        outRot.z = (m.m[1][0] - m.m[0][1]) * s;
    } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
        double s = 2.0 * std::sqrt(1.0 + m.m[0][0] - m.m[1][1] - m.m[2][2]);
        outRot.w = (m.m[2][1] - m.m[1][2]) / s;
        outRot.x = 0.25 * s;
        outRot.y = (m.m[0][1] + m.m[1][0]) / s;
        outRot.z = (m.m[0][2] + m.m[2][0]) / s;
    } else if (m.m[1][1] > m.m[2][2]) {
        double s = 2.0 * std::sqrt(1.0 + m.m[1][1] - m.m[0][0] - m.m[2][2]);
        outRot.w = (m.m[0][2] - m.m[2][0]) / s;
        outRot.x = (m.m[0][1] + m.m[1][0]) / s;
        outRot.y = 0.25 * s;
        outRot.z = (m.m[1][2] + m.m[2][1]) / s;
    } else {
        double s = 2.0 * std::sqrt(1.0 + m.m[2][2] - m.m[0][0] - m.m[1][1]);
        outRot.w = (m.m[1][0] - m.m[0][1]) / s;
        outRot.x = (m.m[0][2] + m.m[2][0]) / s;
        outRot.y = (m.m[1][2] + m.m[2][1]) / s;
        outRot.z = 0.25 * s;
    }
}

} // namespace padvr
