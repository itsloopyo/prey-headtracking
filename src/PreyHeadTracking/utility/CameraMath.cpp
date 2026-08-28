#include "CameraMath.hpp"

namespace preyht {

void ApplyHeadRotation(Mat34& cam, float yawDeg, float pitchDeg, float rollDeg,
                       bool worldYaw) {
    V3 R = cam.Right(), F = cam.Forward(), U = cam.Up();

    const float yr = yawDeg   * kDegToRad;
    const float pr = pitchDeg * kDegToRad;
    const float rl = rollDeg  * kDegToRad;

    // Local yaw: rotate right/forward about the camera up axis.
    if (!worldYaw) {
        const float c = std::cos(yr), s = std::sin(yr);
        const V3 nR = Add(Scale(R, c), Scale(F, s));
        const V3 nF = Add(Scale(R, -s), Scale(F, c));
        R = nR; F = nF;
    }

    // Pitch: rotate forward/up about the (current) right axis.
    {
        const float c = std::cos(pr), s = std::sin(pr);
        const V3 nF = Add(Scale(F, c), Scale(U, s));
        const V3 nU = Add(Scale(F, -s), Scale(U, c));
        F = nF; U = nU;
    }

    // Roll: rotate right/up about the (current) forward axis. Positive roll here
    // leans the camera's up axis to the right (gains a +right component); (R, F, U)
    // is right-handed (R x F = U), so that is the positive rotation about forward.
    {
        const float c = std::cos(rl), s = std::sin(rl);
        const V3 nR = Add(Scale(R, c), Scale(U, -s));
        const V3 nU = Add(Scale(R, s), Scale(U, c));
        R = nR; U = nU;
    }

    // World yaw: spin the whole basis about world +Z so the horizon is fixed.
    if (worldYaw) {
        R = RotateZ(R, yr);
        F = RotateZ(F, yr);
        U = RotateZ(U, yr);
    }

    cam.SetRight(R);
    cam.SetForward(F);
    cam.SetUp(U);
}

V3 UnprojectAim(const Mat34& clean, float baseX, float baseY, float tanH, float tanV) {
    const float u = (baseX - 0.5f) * 2.0f * tanH;   // right, per unit forward
    const float v = (0.5f - baseY) * 2.0f * tanV;   // up, per unit forward
    return Add(Add(Scale(clean.Right(), u), Scale(clean.Up(), v)), clean.Forward());
}

AimScreenPos ProjectAim(const Mat34& modified, const V3& aim, float tanH, float tanV) {
    AimScreenPos out;
    const float ax = Dot(aim, modified.Right());
    const float ay = Dot(aim, modified.Up());
    const float az = Dot(aim, modified.Forward());
    out.camera_relative = {ax, ay, az};

    if (az > 1e-3f) {
        out.ndc_x = ax / az / tanH;
        out.ndc_y = ay / az / tanV;
    } else {
        // The head has turned far enough that the aim direction is level with or
        // behind the rendered view, where it has no finite projection. Send the
        // reticle off the edge on the same bearing and let the clamp park it
        // there, so it leaves the screen the way the shot went instead of
        // jumping to the opposite side as the divide changes sign.
        const float len = std::sqrt(ax * ax + ay * ay);
        if (len <= 1e-6f) return out;
        out.ndc_x = 2.0f * ax / len;
        out.ndc_y = 2.0f * ay / len;
    }

    // A non-finite result is not a position anywhere on screen, and it does not
    // announce itself downstream: the clamp to 0..1 passes NaN straight through
    // (both of its compares are false) and the "unchanged, skip the engine call"
    // test is false as well, so a NaN would be handed to Prey's Flash reticle on
    // every single frame.
    if (!std::isfinite(out.ndc_x) || !std::isfinite(out.ndc_y)) return out;

    // Normalized screen y runs DOWN the screen; the camera's up axis runs up it.
    out.x = 0.5f + 0.5f * out.ndc_x;
    out.y = 0.5f - 0.5f * out.ndc_y;
    out.valid = true;
    return out;
}

AxisAngle RotationBetween(const Mat34& from, const Mat34& to) {
    AxisAngle out;

    const V3 cr = from.Right(), cf = from.Forward(), cu = from.Up();
    const V3 mr = to.Right(),   mf = to.Forward(),   mu = to.Up();
    const float r00 = mr.x * cr.x + mf.x * cf.x + mu.x * cu.x;
    const float r11 = mr.y * cr.y + mf.y * cf.y + mu.y * cu.y;
    const float r22 = mr.z * cr.z + mf.z * cf.z + mu.z * cu.z;
    const float r21 = mr.z * cr.y + mf.z * cf.y + mu.z * cu.y;
    const float r12 = mr.y * cr.z + mf.y * cf.z + mu.y * cu.z;
    const float r02 = mr.x * cr.z + mf.x * cf.z + mu.x * cu.z;
    const float r20 = mr.z * cr.x + mf.z * cf.x + mu.z * cu.x;
    const float r10 = mr.y * cr.x + mf.y * cf.x + mu.y * cu.x;
    const float r01 = mr.x * cr.y + mf.x * cf.y + mu.x * cu.y;

    float trace = r00 + r11 + r22;
    if (trace > 3.0f) trace = 3.0f;
    if (trace < -1.0f) trace = -1.0f;
    const float angle = std::acos((trace - 1.0f) * 0.5f);
    if (!(angle > kMinRotationRad)) return out;

    const float s2 = 2.0f * std::sin(angle);
    V3 axis{ (r21 - r12) / s2, (r02 - r20) / s2, (r10 - r01) / s2 };
    const float alen = Length(axis);
    if (!(alen > 1e-5f)) return out;

    out.axis  = {axis.x / alen, axis.y / alen, axis.z / alen};
    out.angle = angle;
    out.valid = true;
    return out;
}

bool ComposeRotation(const Quat4& d, const Quat4& q, Quat4& out) {
    const float nx = d.w * q.x + d.x * q.w + d.y * q.z - d.z * q.y;
    const float ny = d.w * q.y - d.x * q.z + d.y * q.w + d.z * q.x;
    const float nz = d.w * q.z + d.x * q.y - d.y * q.x + d.z * q.w;
    const float nw = d.w * q.w - d.x * q.x - d.y * q.y - d.z * q.z;
    const float n = std::sqrt(nx * nx + ny * ny + nz * nz + nw * nw);
    if (!(n > 1e-5f)) return false;
    out = {nx / n, ny / n, nz / n, nw / n};
    return true;
}

}  // namespace preyht
