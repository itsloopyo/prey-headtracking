// Behaviour lock for the camera basis maths.
//
// These are the two halves that have to agree: the rotation ApplyHeadRotation
// puts into the engine's camera, and the projection the reticle is placed from.
// A projection derived from different assumptions than the camera composition
// agrees at small single-axis angles and then drifts on combined poses, which is
// exactly the failure the reticle litmus tests in AGENTS.md exist to catch. They
// are cheap to run here and expensive to notice in game.

#include "utility/CameraMath.hpp"

#include <cmath>
#include <iostream>
#include <limits>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

bool Near(float a, float b, float eps = 1e-4f) {
    return std::isfinite(a) && std::fabs(a - b) <= eps;
}

using preyht::AimScreenPos;
using preyht::Dot;
using preyht::Length;
using preyht::Mat34;
using preyht::Quat4;
using preyht::Sub;
using preyht::V3;

/// Right = +X, forward = +Y, up = +Z, at the origin. Level, facing north.
Mat34 LevelCamera() {
    return Mat34{{1.0f, 0.0f, 0.0f, 0.0f,
                  0.0f, 1.0f, 0.0f, 0.0f,
                  0.0f, 0.0f, 1.0f, 0.0f}};
}

/// The same camera pitched fully down: forward = -Z, up = +Y.
Mat34 LookingDownCamera() {
    return Mat34{{1.0f, 0.0f,  0.0f, 0.0f,
                  0.0f, 0.0f,  1.0f, 0.0f,
                  0.0f, -1.0f, 0.0f, 0.0f}};
}

bool NearV3(const V3& a, const V3& b, float eps = 1e-4f) {
    return Near(a.x, b.x, eps) && Near(a.y, b.y, eps) && Near(a.z, b.z, eps);
}

bool IsOrthonormal(const Mat34& m, float eps = 1e-4f) {
    const V3 r = m.Right(), f = m.Forward(), u = m.Up();
    return Near(Length(r), 1.0f, eps) && Near(Length(f), 1.0f, eps) &&
           Near(Length(u), 1.0f, eps) && Near(Dot(r, f), 0.0f, eps) &&
           Near(Dot(f, u), 0.0f, eps) && Near(Dot(r, u), 0.0f, eps);
}

// ---- rotation composition ----------------------------------------------------

void TestZeroPoseIsIdentity() {
    std::cout << "rotation composition:\n";
    for (bool worldYaw : {false, true}) {
        Mat34 cam = LevelCamera();
        preyht::ApplyHeadRotation(cam, 0.0f, 0.0f, 0.0f, worldYaw);
        Check(NearV3(cam.Right(), {1.0f, 0.0f, 0.0f}) &&
              NearV3(cam.Forward(), {0.0f, 1.0f, 0.0f}) &&
              NearV3(cam.Up(), {0.0f, 0.0f, 1.0f}),
              worldYaw ? "a zero pose leaves the basis untouched (world yaw)"
                       : "a zero pose leaves the basis untouched (camera-local yaw)");
    }
}

void TestSingleAxisRotations() {
    Mat34 yawed = LevelCamera();
    preyht::ApplyHeadRotation(yawed, 90.0f, 0.0f, 0.0f, false);
    Check(NearV3(yawed.Forward(), {-1.0f, 0.0f, 0.0f}) &&
          NearV3(yawed.Up(), {0.0f, 0.0f, 1.0f}),
          "camera-local yaw +90 swings forward onto -X and leaves up alone");

    Mat34 pitched = LevelCamera();
    preyht::ApplyHeadRotation(pitched, 0.0f, 90.0f, 0.0f, false);
    Check(NearV3(pitched.Forward(), {0.0f, 0.0f, 1.0f}) &&
          NearV3(pitched.Right(), {1.0f, 0.0f, 0.0f}),
          "pitch +90 swings forward onto world up and leaves right alone");

    Mat34 rolled = LevelCamera();
    preyht::ApplyHeadRotation(rolled, 0.0f, 0.0f, 90.0f, false);
    Check(NearV3(rolled.Right(), {0.0f, 0.0f, -1.0f}) &&
          NearV3(rolled.Up(), {1.0f, 0.0f, 0.0f}) &&
          NearV3(rolled.Forward(), {0.0f, 1.0f, 0.0f}),
          "roll +90 leans up towards +right and leaves forward alone");
}

void TestWorldYawMatchesLocalYawOnALevelCamera() {
    Mat34 local = LevelCamera();
    Mat34 world = LevelCamera();
    preyht::ApplyHeadRotation(local, 25.0f, 0.0f, 0.0f, false);
    preyht::ApplyHeadRotation(world, 25.0f, 0.0f, 0.0f, true);
    Check(NearV3(local.Forward(), world.Forward()) && NearV3(local.Up(), world.Up()),
          "with the camera level the two yaw modes agree");
}

void TestWorldYawLookingDownIsAViewAxisSpin() {
    // AGENTS.md litmus 4: looking straight down, head yaw is a pure spin about
    // the view axis - the world turns, the aim point does not leave the centre.
    Mat34 cam = LookingDownCamera();
    preyht::ApplyHeadRotation(cam, 30.0f, 0.0f, 0.0f, true);
    Check(NearV3(cam.Forward(), {0.0f, 0.0f, -1.0f}),
          "world yaw while looking down leaves the view axis where it was");
    Check(IsOrthonormal(cam), "world yaw while looking down keeps the basis orthonormal");
}

void TestCombinedPoseStaysOrthonormal() {
    for (bool worldYaw : {false, true}) {
        Mat34 cam = LevelCamera();
        preyht::ApplyHeadRotation(cam, 33.0f, -21.0f, 17.0f, worldYaw);
        Check(IsOrthonormal(cam), worldYaw
              ? "a combined pose keeps the basis orthonormal (world yaw)"
              : "a combined pose keeps the basis orthonormal (camera-local yaw)");
    }
}

// ---- reticle projection ------------------------------------------------------

// Square tangents, so a rigid rotation of the aim offset stays a rigid rotation
// in NDC. With Prey's real 16:9 ratio the same rotation is stretched across, and
// that stretch is the projection doing its job rather than a drift.
constexpr float kTanH = 0.6f;
constexpr float kTanV = 0.6f;

// Prey's shipped g_reticleYPercentage: the reticle sits BELOW the middle of the
// screen, which is why the aim direction has to be unprojected from it rather
// than assumed to be the camera's forward axis.
constexpr float kBaseX = 0.5f;
constexpr float kBaseY = 0.575f;

AimScreenPos PlaceReticle(float yaw, float pitch, float roll, bool worldYaw,
                          const Mat34& start = LevelCamera(),
                          float baseX = kBaseX, float baseY = kBaseY) {
    const Mat34 clean = start;
    Mat34 modified = clean;
    preyht::ApplyHeadRotation(modified, yaw, pitch, roll, worldYaw);
    const V3 aim = preyht::UnprojectAim(clean, baseX, baseY, kTanH, kTanV);
    return preyht::ProjectAim(modified, aim, kTanH, kTanV);
}

void TestIdentityPoseReproducesTheBasePosition() {
    std::cout << "reticle projection:\n";
    const AimScreenPos p = PlaceReticle(0.0f, 0.0f, 0.0f, false);
    Check(p.valid && Near(p.x, kBaseX) && Near(p.y, kBaseY),
          "a centred head puts the reticle back on the game's own base position");
}

void TestPurePitchMovesTheReticleVertically() {
    // AGENTS.md litmus 2.
    const AimScreenPos up = PlaceReticle(0.0f, 15.0f, 0.0f, false);
    const AimScreenPos down = PlaceReticle(0.0f, -15.0f, 0.0f, false);
    Check(up.valid && Near(up.x, kBaseX), "pitching up moves the reticle straight down the screen");
    Check(down.valid && Near(down.x, kBaseX), "pitching down moves it straight up, with no sideways wander");
    Check(up.y > kBaseY && down.y < kBaseY,
          "pitching up drives the aim point down the screen, and down drives it up");
}

void TestPureRollAboutTheAimPointLeavesItAlone() {
    // AGENTS.md litmus 1, phrased for a camera whose roll is OUTERMOST: an aim
    // point already at the centre of the projection is on the roll axis, so it
    // cannot move.
    const AimScreenPos p = PlaceReticle(0.0f, 0.0f, 25.0f, false, LevelCamera(), 0.5f, 0.5f);
    Check(p.valid && Near(p.x, 0.5f) && Near(p.y, 0.5f),
          "rolling about an aim point that is dead centre does not move it");
}

void TestPitchAndRollRotateRatherThanWander() {
    // AGENTS.md litmus 3. This camera applies roll LAST, about the final view
    // axis, so the offset must ROTATE about the centre - rigidly, keeping its
    // radius. An offset that grows or shrinks means the two halves disagree.
    const AimScreenPos pitchOnly = PlaceReticle(0.0f, 15.0f, 0.0f, false);
    const AimScreenPos withRoll  = PlaceReticle(0.0f, 15.0f, 20.0f, false);
    const float r0 = std::sqrt(pitchOnly.ndc_x * pitchOnly.ndc_x +
                               pitchOnly.ndc_y * pitchOnly.ndc_y);
    const float r1 = std::sqrt(withRoll.ndc_x * withRoll.ndc_x +
                               withRoll.ndc_y * withRoll.ndc_y);
    Check(pitchOnly.valid && withRoll.valid && Near(r0, r1, 1e-3f),
          "adding roll to pitch rotates the offset about the centre and keeps its radius");
    Check(std::fabs(withRoll.ndc_x - pitchOnly.ndc_x) > 1e-3f,
          "and it really does rotate - roll is not being dropped on the floor");
}

void TestWorldYawLookingDownHoldsTheReticleStill() {
    // AGENTS.md litmus 4, through the projection rather than the basis: the aim
    // point sits on the spin axis, so the world turns under a still reticle.
    const AimScreenPos p =
        PlaceReticle(30.0f, 0.0f, 0.0f, true, LookingDownCamera(), 0.5f, 0.5f);
    Check(p.valid && Near(p.x, 0.5f) && Near(p.y, 0.5f),
          "world yaw while looking down leaves a centred reticle at the centre");
}

void TestAimBehindTheViewLeavesOnTheSameBearing() {
    Mat34 clean = LevelCamera();
    Mat34 modified = clean;
    preyht::ApplyHeadRotation(modified, 170.0f, 0.0f, 0.0f, false);
    const V3 aim = preyht::UnprojectAim(clean, kBaseX, kBaseY, kTanH, kTanV);
    const AimScreenPos p = preyht::ProjectAim(modified, aim, kTanH, kTanV);
    const float r = std::sqrt(p.ndc_x * p.ndc_x + p.ndc_y * p.ndc_y);
    Check(p.valid && Near(r, 2.0f, 1e-3f),
          "an aim point behind the view leaves the screen on its own bearing, not the opposite side");
}

void TestHudStageScaleMatchesWhatPreyDraws() {
    // Measured in game, and the numbers are the whole point of the conversion:
    // 3840x1080 drew the shipped base of 0.575 at 0.6495 down the screen, and
    // 1440x1080 drew a HUD x of 0.3949 at 0.3594 across it. A 16:9 render draws
    // both where they were asked for.
    const preyht::HudStageScale wide = preyht::HudScale(3840.0f / 1080.0f);
    Check(Near(wide.x, 1.0f) && Near(preyht::HudToScreen(0.575f, wide.y), 0.65f, 1e-3f),
          "past 16:9 the HUD stage is matched to the width and stretches y (0.575 draws at 0.650)");

    const preyht::HudStageScale narrow = preyht::HudScale(1440.0f / 1080.0f);
    Check(Near(narrow.y, 1.0f) && Near(preyht::HudToScreen(0.3949f, narrow.x), 0.3599f, 1e-3f),
          "below 16:9 it is matched to the height and stretches x (0.3949 draws at 0.360)");

    const preyht::HudStageScale native = preyht::HudScale(16.0f / 9.0f);
    Check(Near(native.x, 1.0f) && Near(native.y, 1.0f),
          "at 16:9 a HUD coordinate is a screen coordinate and neither axis is touched");

    Check(Near(preyht::ScreenToHud(preyht::HudToScreen(0.3f, wide.y), wide.y), 0.3f) &&
              Near(preyht::ScreenToHud(preyht::HudToScreen(0.3f, narrow.x), narrow.x), 0.3f),
          "and the two conversions are inverses, so an unmoved reticle comes back unmoved");
}

void TestNonFiniteProjectionIsRejected() {
    // The pose arrives over UDP from any host that can reach the machine, and a
    // projection that is not a number does not announce itself downstream: the
    // clamp to 0..1 passes NaN through and the "unchanged, skip it" compare is
    // false, so it would be handed to Prey's Flash reticle every frame.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    Check(!preyht::ProjectAim(LevelCamera(), V3{nan, nan, nan}, kTanH, kTanV).valid,
          "an aim vector that is not a number is refused rather than projected");
    Check(!preyht::ProjectAim(LevelCamera(), V3{0.0f, 1.0f, 0.0f}, 0.0f, kTanV).valid,
          "and so is a frustum with no horizontal extent, which divides to a NaN");
}

void TestDegenerateAimIsRejected() {
    const AimScreenPos p = preyht::ProjectAim(LevelCamera(), V3{0.0f, 0.0f, 0.0f}, kTanH, kTanV);
    Check(!p.valid, "an aim vector with no direction at all is refused rather than projected");
}

void TestLeanSwingsTheReticleTowardsTheImpactPoint() {
    // The parallax case: the shot leaves the clean eye, the frame renders from a
    // leaned one, so the reticle has to swing by lean/distance to stay on the
    // impact point. Near and far must both land on it - agreement at one range
    // only is the fixed-depth fault.
    const Mat34 clean = LevelCamera();
    Mat34 modified = clean;
    modified.SetTrans(V3{0.30f, 0.0f, 0.0f});   // 30cm lean to the right

    const V3 aimDir = preyht::UnprojectAim(clean, 0.5f, 0.5f, kTanH, kTanV);
    const V3 unit = preyht::Scale(aimDir, 1.0f / Length(aimDir));

    for (float dist : {2.0f, 40.0f}) {
        const V3 impact = preyht::Add(clean.Trans(), preyht::Scale(unit, dist));
        const AimScreenPos p =
            preyht::ProjectAim(modified, Sub(impact, modified.Trans()), kTanH, kTanV);
        const float expectedNdc = -0.30f / dist / kTanH;
        Check(p.valid && Near(p.ndc_x, expectedNdc, 1e-3f) && Near(p.ndc_y, 0.0f, 1e-3f),
              dist < 10.0f ? "leaning right swings the reticle left onto a near impact point"
                           : "and onto a far one by the smaller angle the same lean subtends");
    }
}

// ---- the finiteness gate on the engine write ---------------------------------

void TestNonFiniteBasisIsRejected() {
    std::cout << "camera write gate:\n";
    Check(preyht::IsFinite(LevelCamera()), "a real camera basis passes the gate");

    // What a hostile or simply broken tracker produces: the pose pipeline takes a
    // difference between consecutive samples, so two samples near the float limit
    // overflow to an infinity, and the cosines below turn the whole basis into
    // NaN. The engine culls and draws from this matrix and from the frustum it
    // rebuilds off it, so the write has to be refused.
    Mat34 poisoned = LevelCamera();
    preyht::ApplyHeadRotation(poisoned, 0.0f, std::numeric_limits<float>::infinity(), 0.0f,
                              false);
    Check(!preyht::IsFinite(poisoned),
          "a non-finite pitch makes a basis the gate refuses to write");

    Mat34 nanTranslation = LevelCamera();
    nanTranslation.SetTrans(V3{0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f});
    Check(!preyht::IsFinite(nanTranslation),
          "and a NaN in the translation alone is caught too, not just in the axes");
}

// ---- flashlight rotation -----------------------------------------------------

void TestRotationBetweenIdenticalBasesIsNotAxisAngle() {
    std::cout << "flashlight rotation:\n";
    const Mat34 cam = LevelCamera();
    Check(!preyht::RotationBetween(cam, cam).valid,
          "a centred head yields no rotation axis, so the beam is left alone");
}

void TestRotationBetweenRecoversTheHeadTurn() {
    const Mat34 clean = LevelCamera();
    Mat34 modified = clean;
    preyht::ApplyHeadRotation(modified, 20.0f, 0.0f, 0.0f, false);

    const preyht::AxisAngle rot = preyht::RotationBetween(clean, modified);
    Check(rot.valid && Near(rot.angle, 20.0f * preyht::kDegToRad, 1e-3f),
          "a 20 degree head yaw is recovered as a 20 degree rotation");
    Check(Near(std::fabs(rot.axis.z), 1.0f, 1e-3f),
          "and about the up axis, which is what a yaw is");
    Check(Near(rot.angle * 1.5f, 30.0f * preyht::kDegToRad, 1e-3f),
          "so the 1.5x beam scale asks for 30 degrees, matching what was measured in game");
}

void TestRotateAboutMatchesTheHeadRotation() {
    std::cout << "first-person body:\n";
    const V3 up{0.0f, 0.0f, 1.0f};
    Check(NearV3(preyht::RotateAbout(V3{1.0f, 0.0f, 0.0f}, up, 90.0f * preyht::kDegToRad),
                 V3{0.0f, 1.0f, 0.0f}, 1e-3f),
          "a quarter turn about world up carries +X onto +Y");

    // What the body edit does: the offset it turns has to end up where the head
    // pose would carry it, or the suit rotates about its own origin instead of
    // about the eye and swings out of frame the moment the head pitches.
    const Mat34 clean = LevelCamera();
    Mat34 modified = clean;
    preyht::ApplyHeadRotation(modified, 20.0f, -15.0f, 10.0f, true);
    const preyht::AxisAngle head = preyht::RotationBetween(clean, modified);

    const V3 offset{0.0f, 0.0f, -0.46f};   // the suit, 46cm below the eye
    const V3 turned = preyht::RotateAbout(offset, head.axis, head.angle);
    Check(head.valid && Near(Length(turned), Length(offset), 1e-4f),
          "turning the body's offset by the head pose keeps its distance from the eye");
    Check(Near(Dot(turned, modified.Up()), Dot(offset, clean.Up()), 1e-3f) &&
          Near(Dot(turned, modified.Right()), Dot(offset, clean.Right()), 1e-3f) &&
          Near(Dot(turned, modified.Forward()), Dot(offset, clean.Forward()), 1e-3f),
          "and leaves it in the same place relative to the head, which is the whole fix");
}

void TestComposeRotation() {
    const Quat4 identity{0.0f, 0.0f, 0.0f, 1.0f};
    const Quat4 q{0.0f, 0.0f, 0.3826834f, 0.9238795f};   // 45 degrees about +Z
    Quat4 out{};
    Check(preyht::ComposeRotation(identity, q, out) &&
          Near(out.x, q.x) && Near(out.y, q.y) && Near(out.z, q.z) && Near(out.w, q.w),
          "composing with no rotation gives the beam's own orientation back");

    Check(preyht::ComposeRotation(q, q, out) && Near(out.z, 0.7071068f) && Near(out.w, 0.7071068f),
          "two 45 degree turns about the same axis make 90");

    Check(!preyht::ComposeRotation(Quat4{0.0f, 0.0f, 0.0f, 0.0f}, Quat4{0.0f, 0.0f, 0.0f, 0.0f}, out),
          "a degenerate product is refused rather than normalized into NaN");
}

}  // namespace

int main() {
    TestZeroPoseIsIdentity();
    TestSingleAxisRotations();
    TestWorldYawMatchesLocalYawOnALevelCamera();
    TestWorldYawLookingDownIsAViewAxisSpin();
    TestCombinedPoseStaysOrthonormal();

    TestIdentityPoseReproducesTheBasePosition();
    TestPurePitchMovesTheReticleVertically();
    TestPureRollAboutTheAimPointLeavesItAlone();
    TestPitchAndRollRotateRatherThanWander();
    TestWorldYawLookingDownHoldsTheReticleStill();
    TestAimBehindTheViewLeavesOnTheSameBearing();
    TestHudStageScaleMatchesWhatPreyDraws();
    TestNonFiniteProjectionIsRejected();
    TestDegenerateAimIsRejected();
    TestLeanSwingsTheReticleTowardsTheImpactPoint();

    TestNonFiniteBasisIsRejected();

    TestRotationBetweenIdenticalBasesIsNotAxisAngle();
    TestRotationBetweenRecoversTheHeadTurn();
    TestRotateAboutMatchesTheHeadRotation();
    TestComposeRotation();

    if (g_failures == 0) {
        std::cout << "\nAll camera math checks passed.\n";
        return 0;
    }
    std::cout << "\n" << g_failures << " check(s) FAILED.\n";
    return 1;
}
