#pragma once

#include <cmath>

/// Camera basis maths in CryEngine axes, with no engine or Windows dependency.
///
/// Everything here is pure: same inputs, same outputs, no memory reads, no
/// logging, no engine calls. That is what makes the rotation composition and the
/// reticle projection testable on their own (see tests/camera_math_tests.cpp),
/// which matters because the two have to encode the SAME composition - a
/// projection derived from different assumptions agrees at small single-axis
/// angles and then drifts on combined poses.
namespace preyht {

/// X = right, Y = forward, Z = up; right-handed, world up = +Z. Plain floats so
/// the basis math matches the engine's convention exactly with no
/// quaternion-convention round-trips.
struct V3 { float x, y, z; };

/// CryEngine stores a Quat as Vec3 v then float w, so x, y, z, w in memory.
struct Quat4 { float x, y, z, w; };

inline V3 Add(const V3& a, const V3& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 Sub(const V3& a, const V3& b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline float Dot(const V3& a, const V3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Scale(const V3& a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline float Length(const V3& a) { return std::sqrt(Dot(a, a)); }

/// The entity's forward: (0,1,0) turned by its rotation. CryEngine entities face
/// +Y, so this is what a light's beam points down.
inline V3 RotateY(const Quat4& q) {
    const float x = q.x, y = q.y, z = q.z, w = q.w;
    return { 2.0f * (x * y - z * w),
             1.0f - 2.0f * (x * x + z * z),
             2.0f * (y * z + x * w) };
}

/// Rotate vector v about world up (+Z) by angle (radians).
inline V3 RotateZ(const V3& v, float a) {
    const float c = std::cos(a), s = std::sin(a);
    return {c * v.x - s * v.y, s * v.x + c * v.y, v.z};
}

inline V3 Cross(const V3& a, const V3& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}

/// Rotate v about a unit axis by angle (radians), Rodrigues.
inline V3 RotateAbout(const V3& v, const V3& axis, float angle) {
    const float c = std::cos(angle), s = std::sin(angle);
    return Add(Add(Scale(v, c), Scale(Cross(axis, v), s)),
               Scale(axis, Dot(axis, v) * (1.0f - c)));
}

/// CryEngine Matrix34 is row-major float[12]: rows are (Rx Fx Ux Tx),
/// (Ry Fy Uy Ty), (Rz Fz Uz Tz). Column 0 is the right axis, column 1 forward,
/// column 2 up, column 3 translation.
struct Mat34 {
    float m[12];
    V3 Right()   const { return {m[0], m[4], m[8]}; }
    V3 Forward() const { return {m[1], m[5], m[9]}; }
    V3 Up()      const { return {m[2], m[6], m[10]}; }
    V3 Trans()   const { return {m[3], m[7], m[11]}; }
    void SetRight  (const V3& v) { m[0] = v.x; m[4] = v.y; m[8]  = v.z; }
    void SetForward(const V3& v) { m[1] = v.x; m[5] = v.y; m[9]  = v.z; }
    void SetUp     (const V3& v) { m[2] = v.x; m[6] = v.y; m[10] = v.z; }
    void SetTrans  (const V3& v) { m[3] = v.x; m[7] = v.y; m[11] = v.z; }
};

constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;

/// True when every element of the basis is a real number.
///
/// The pose arrives over a UDP socket bound to every interface, so it is
/// attacker-reachable, and a single non-finite angle turns the whole basis into
/// NaN through the cosines below. The engine culls and draws from that matrix
/// and its cached frustum, so it must never be written; a caller that sees false
/// here leaves the game's own camera alone for the frame.
inline bool IsFinite(const Mat34& m) {
    for (float v : m.m) {
        if (!std::isfinite(v)) return false;
    }
    return true;
}

/// A rotation between two orthonormal bases, as an axis and an angle in radians.
/// `valid` is false when the two bases are the same to within `kMinRotationRad`,
/// where the axis is not determined.
struct AxisAngle {
    V3    axis  = {0.0f, 0.0f, 0.0f};
    float angle = 0.0f;
    bool  valid = false;
};

/// Below this the head counts as centred and the axis is noise.
constexpr float kMinRotationRad = 1e-4f;

/// Build the new camera basis from the clean basis and the head pose.
///
/// Rotation composition (CameraUnlock standard YPR): yaw about up, pitch about
/// the camera's right axis, roll about the camera's forward axis. In camera-local
/// yaw mode all three are applied in the camera frame; in world-yaw mode pitch
/// and roll are local while yaw rotates the whole orientation about world +Z so
/// the horizon stays level regardless of where the camera points.
///
/// The angles are already in ENGINE convention - the caller flips the tracker's
/// yaw and roll before handing them over.
void ApplyHeadRotation(Mat34& cam, float yawDeg, float pitchDeg, float rollDeg,
                       bool worldYaw);

/// The world-space direction the game's own reticle position names.
///
/// Prey parks the reticle below the middle of the screen (a shipped
/// g_reticleYPercentage of 0.575 here), so treating the camera's forward axis as
/// the aim and adding an offset to the base would compensate the wrong direction
/// and leave a fixed error behind. Unprojecting the base position instead makes
/// an identity pose reproduce it exactly.
///
/// `baseX` / `baseY` are normalized screen coordinates (x right, y DOWN);
/// `tanH` / `tanV` are the tangents of the half field of view.
V3 UnprojectAim(const Mat34& clean, float baseX, float baseY, float tanH, float tanV);

/// Where an aim vector lands on screen, with the intermediates the camera dump
/// reports - a sign fault and a distance fault fit the final position equally
/// well, and only the same frame's intermediates separate them.
struct AimScreenPos {
    V3    camera_relative{0.0f, 0.0f, 0.0f};  ///< aim in the modified basis
    float ndc_x = 0.0f;
    float ndc_y = 0.0f;
    float x = 0.0f;  ///< normalized screen, 0..1 across
    float y = 0.0f;  ///< normalized screen, 0..1 DOWN
    bool  valid = false;
};

/// Project a world-space aim vector into normalized screen coordinates through
/// the head-tracked basis. `aim` is measured from the rendering eye.
///
/// The projection is taken from the modified basis itself rather than rebuilt
/// from the pose angles, so it cannot fall out of agreement with what
/// ApplyHeadRotation did: world-space yaw, camera-local yaw and roll all reduce
/// to the same three dot products.
///
/// `valid` is false when the aim has no bearing on screen at all, or when the
/// projection is not a finite number, which leaves the reticle where it was
/// rather than throwing it somewhere arbitrary.
AimScreenPos ProjectAim(const Mat34& modified, const V3& aim, float tanH, float tanV);

/// The rotation that carries the `from` basis onto the `to` basis.
///
/// Taken from the two bases rather than recomposed from the pose angles, so it
/// matches whatever composition was applied.
AxisAngle RotationBetween(const Mat34& from, const Mat34& to);

/// Hamilton product `delta * q`, normalized. Zero-length results are reported as
/// invalid rather than normalized into a NaN quaternion.
bool ComposeRotation(const Quat4& delta, const Quat4& q, Quat4& out);

}  // namespace preyht
