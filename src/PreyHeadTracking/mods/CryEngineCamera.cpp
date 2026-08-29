#include "CryEngineCamera.hpp"

#include "BuildProfile.hpp"
#include "Framework.hpp"
#include "HeadTracking.hpp"
#include "utility/AimTrace.hpp"
#include "utility/CameraMath.hpp"
#include "utility/Flashlight.hpp"
#include "utility/GameCVars.hpp"
#include "utility/GameState.hpp"
#include "utility/Logging.hpp"
#include "utility/PreyHud.hpp"
#include "utility/SafeMemory.hpp"
#include "utility/ViewCamera.hpp"

#include "cameraunlock/hooks/hook_manager.h"
#include "cameraunlock/memory/pe_fingerprint.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <iterator>

namespace preyht {

namespace {

using camera::MemberVoidFn;
using cameraunlock::hooks::HookManager;
using cameraunlock::hooks::HookStatus;

using GetViewCameraFn = camera::GetViewCameraFn;
using SetViewCameraFn = void(__fastcall*)(void* pSystem, void* cam);

/// How far the view camera's translation has to be from the origin before the
/// ENGINE considers it a real camera, in metres.
///
/// This is Prey's own number, read out of CSystem::Render: before it draws the
/// world it tests |Tx|, |Ty| and |Tz| against 0.05 and, if all three are inside
/// it, skips the world and draws 2D only. A camera still at the origin is one the
/// game has not positioned yet - the main menu, a level load - and the scene
/// behind it is not ready to be traversed.
///
/// That makes it a hard limit on positional tracking, not a nicety. The lean
/// offset is up to 0.40m, eight times this epsilon, so adding it to a camera
/// sitting at the origin makes an unset camera look set, the engine renders the
/// world through it, and its async visibility jobs walk a scene that does not
/// exist yet. That crashes on a job worker inside the octree traversal, reading
/// whatever the uninitialised node pointers happen to hold. Found the hard way:
/// it needs a live tracker (a centred one adds nothing and never trips it) and it
/// only fires during a load, which is exactly when the camera is parked at zero.
constexpr float kCameraValidEpsilon = 0.05f;

/// A whole CCamera, snapshotted before the pose goes in. 0x240 is the size
/// CSystem copies.
constexpr size_t kCameraCopyBytes = 0x240;

/// Every 30 frames, about twice a second. Slow enough not to flood the file, fast
/// enough to catch a transient the player is describing by hand.
constexpr unsigned kDumpEveryFrames = 30;

/// The reticle dump is slower still - it is one long line and the camera dump is
/// the one that answers "is a pose going in at all".
constexpr unsigned kReticleDumpEveryFrames = 120;

/// A millimetre of lean. Below that the parallax is far under a pixel at any
/// range, and skipping the aim ray keeps rotation-only tracking exactly as it was.
constexpr float kMinLeanForRayMetres = 1e-3f;

/// The reticle is only handed back to the game when the head is genuinely
/// centred; 1e-4 of a screen is well under a pixel at any resolution Prey runs at.
constexpr float kReticleAtBaseEpsilon = 1e-4f;

/// How far below the eye the first-person body's origin sits, in metres, when the
/// player is wearing the space suit - and what tells that apart from walking
/// around the station.
///
/// Measured off the node the body is drawn from: 0.46m floating in the airlock
/// and 0.69m free-floating outside, both in the suit, where the origin is up at
/// the chest; 1.00m crouched and 1.70m standing on foot, where it is on the floor
/// at the character's feet. A single threshold does not fit that - the suit's own
/// range reaches 0.9 while a level load settles, and crouched sits at exactly
/// 1.00, so any line between them flickers frame by frame somewhere.
///
/// So it enters the suit state well below anything seen on foot, leaves it well
/// above anything seen in the suit, and holds whatever it had in the band
/// between. Crouching lands in that band and therefore changes nothing: on foot
/// the state is already "no suit", and the only way out of the suit is to stand
/// up, at 1.70.
///
/// The floor is there because the offset is live data the game rewrites every
/// frame, and a frame it has not written yet reads as all zeroes.
constexpr float kBodyOriginFloorMetres      = 0.20f;
constexpr float kSuitBodyOriginEnterMetres  = 0.90f;
constexpr float kSuitBodyOriginLeaveMetres  = 1.10f;

// --- Engine handles, written by the install pass on the frame-tick thread and
// read by the detours on whichever thread the engine renders from. Atomic because
// nothing in the code establishes that those are the same thread.
std::atomic<void*>           g_pSystem{nullptr};
std::atomic<uintptr_t>       g_preyDllBase{0};
std::atomic<uintptr_t>       g_multiplayerFlag{0};
std::atomic<HeadTracking*>   g_hookTracking{nullptr};

std::atomic<MemberVoidFn>    g_origRender{nullptr};
std::atomic<MemberVoidFn>    g_origHudProjection{nullptr};
std::atomic<MemberVoidFn>    g_origFocusUpdate{nullptr};
std::atomic<SetViewCameraFn> g_origSetViewCamera{nullptr};
std::atomic<GetViewCameraFn> g_origGetViewCamera{nullptr};

// --- Settings the detours read every frame.
std::atomic<bool> g_earlyInject{false};
std::atomic<bool> g_reticleEnabled{false};
std::atomic<bool> g_dumpCamera{false};
std::atomic<bool> g_traceCameraReaders{false};

// --- Camera substitution for individual GetViewCamera callers.
/// Wrapped in a struct so the whole snapshot goes through SafeRead in one guarded
/// copy: a probe of the first few bytes says nothing about the rest of the range.
struct CameraSnapshot { unsigned char bytes[kCameraCopyBytes]; };
alignas(16) CameraSnapshot g_cleanCameraCopy{};
std::atomic<bool>      g_cleanCopyValid{false};
std::atomic<uintptr_t> g_cleanCameraForReader{0};   // caller RVA, or range low
std::atomic<uintptr_t> g_cleanCameraReaderEnd{0};   // range high, 0 = exact match only
std::atomic<uintptr_t> g_bodyCameraReader{0};       // the call site that pins the body
std::atomic<bool>      g_poseApplied{false};        // is a pose in the camera right now

// --- The frame's camera pair, written and read only from the game's update,
// which is one function on one thread, so the detours nested inside it see a
// consistent pair.
Mat34 g_frameClean{};
Mat34 g_frameModified{};
bool  g_frameInjected = false;
/// The same frame's head rotation, as an axis and an angle, so the per-node
/// render detour does not recompute it for every object the engine draws.
AxisAngle g_frameHeadRot{};

// --- CEntityObject::Render, where a camera-space node's offset is resolved.
using EntityRenderFn = void(__fastcall*)(void*, void*, void*, uint32_t, void*, void*);
std::atomic<EntityRenderFn> g_origEntityRender{nullptr};
std::atomic<uint32_t>       g_nodeFlagsOff{0};
std::atomic<uint32_t>       g_nodeOffsetPtrOff{0};
std::atomic<bool>           g_traceBodyNodes{false};
std::atomic<bool>           g_bodyFollowsHead{false};
/// Whether the player is in the space suit, decided per frame from the body - see
/// WearingSpaceSuit. Read by the yaw mode as well as by the body edit, so it is
/// kept up to date whenever the render hook is live, not only while the body is
/// being carried.
std::atomic<bool>           g_wearingSuit{false};
/// Diagnostic: draw nothing for the body's node. Whatever vanishes from the
/// frame is what this call site places, which is how the node was identified, and
/// a shot of the world without it subtracts to the suit's exact silhouette.
std::atomic<bool>           g_hideBodyNodes{false};

/// IEntity's world matrix getter. The render takes the body's ORIENTATION from
/// here - the camera-space branch replaces only the translation - so this is what
/// has to turn for the suit to follow the head.
using GetWorldTmFn = float*(__fastcall*)(void*);
std::atomic<uint32_t>       g_entityWorldTmSlot{0};

/// True while the engine is running a networked session (gEnv->bMultiplayer).
/// Prey's campaign never sets it. It is defence in depth rather than the thing
/// that keeps head tracking out of Typhon Hunter, Prey's multiplayer mode: that
/// ships as a separate PreyDll.dll with its own fingerprint and no build profile,
/// so the mod never hooks it at all. This check has therefore never been
/// exercised in a live match, which is exactly why it fails CLOSED - an
/// unreadable flag suppresses head tracking rather than permitting it.
bool InMultiplayerSession() {
    uint8_t flag = 0;
    if (!SafeRead(g_multiplayerFlag.load(std::memory_order_relaxed), flag)) return true;
    if (flag == 0) return false;

    static bool s_logged = false;
    if (!s_logged) {
        PHT_LOG(Info, "Multiplayer session detected (gEnv->bMultiplayer); head tracking is "
                      "suppressed - the game renders vanilla for as long as it lasts.");
        s_logged = true;
    }
    return true;
}

/// How far away the thing the gun is pointing at is, or 0 for "no impact point".
///
/// The reticle marks a POINT, not a direction. While the head only rotates the
/// two project to the same place, because the frame is drawn from the eye the
/// shot leaves; a lean moves the render eye up to 40cm off that one and separates
/// them. Reaching the point needs the distance, so the aim ray goes through the
/// engine's physics - not smoothed, not rate-limited, and never replaced by a
/// fixed convergence range, all of which put the reticle on one side of the
/// bullet hole up close and the other side across the room. A definite no-hit is
/// a target at infinity, where the aim DIRECTION is already the right answer.
float ImpactDistance(const V3& eye, const V3& lean, const V3& unit) {
    if (Length(lean) <= kMinLeanForRayMetres) return 0.0f;
    const float org[3] = {eye.x, eye.y, eye.z};
    const float dir[3] = {unit.x, unit.y, unit.z};
    float dist = 0.0f;
    return aim::Distance(org, dir, dist) ? dist : 0.0f;
}

/// Move Prey's reticle to where the mouse-controlled aim lands in the rendered
/// image.
///
/// The gun still points where the mouse put it - that is the whole point of
/// confining the head pose to the render - so the moment the head turns, the spot
/// the reticle sits on stops being the spot the shot hits. Prey draws its reticle
/// at a normalized screen position it hands to Flash, and moves it itself every
/// frame while the controller's free-aim reticle is up, so the fix is to hand it
/// the position that same aim direction projects to in the modified view.
void UpdateReticle(void* cam, const Mat34& clean, const Mat34& modified) {
    float fov = 0.0f, projRatio = 0.0f;
    if (!camera::ReadFrustum(cam, fov, projRatio)) return;

    float baseX = 0.0f, baseY = 0.0f;
    if (!hud::BasePosition(baseX, baseY)) return;

    const float tanV = std::tan(fov * 0.5f);   // m_fov is the VERTICAL angle
    const float tanH = tanV * projRatio;

    // Prey's reticle position is a coordinate on a 16:9 HUD stage, not a screen
    // fraction - see HudScale. The whole projection below is screen geometry, so
    // the base comes off the stage on the way in and the result goes back onto it
    // on the way out. At 16:9 both conversions are the identity.
    const HudStageScale stage = HudScale(projRatio);
    const float baseScreenX = HudToScreen(baseX, stage.x);
    const float baseScreenY = HudToScreen(baseY, stage.y);

    const V3 aimDir = UnprojectAim(clean, baseScreenX, baseScreenY, tanH, tanV);
    const V3 eye    = clean.Trans();
    const V3 lean   = Sub(modified.Trans(), clean.Trans());

    // A camera whose basis has collapsed unprojects to no direction at all, and
    // the reciprocal below would hand an infinity to the engine's physics ray.
    const float aimLen = Length(aimDir);
    if (!(aimLen > 0.0f)) return;

    const V3 unit = Scale(aimDir, 1.0f / aimLen);
    const float dist = ImpactDistance(eye, lean, unit);
    const V3 aim = (dist > 0.0f) ? Sub(Add(eye, Scale(unit, dist)), modified.Trans())
                                 : aimDir;

    const AimScreenPos at = ProjectAim(modified, aim, tanH, tanV);
    if (!at.valid) return;

    // Clamped as a SCREEN position, before it goes back onto the stage: past 16:9
    // the stage runs off the top and bottom of the display, so a stage-space
    // clamp would park an off-screen aim off the screen.
    const float hudX = ScreenToHud(Clamp01(at.x), stage.x);
    const float hudY = ScreenToHud(Clamp01(at.y), stage.y);

    if (g_dumpCamera.load(std::memory_order_relaxed)) {
        static unsigned s_n = 0;
        if ((s_n++ % kReticleDumpEveryFrames) == 0) {
            // Distance, lean and the resulting screen position on ONE line: read
            // from separate lines they fit a sign fault and a distance fault
            // equally well, and the arithmetic that separates them needs the
            // same frame's numbers.
            PHT_LOG(Info, "reticle: fov=%.4f rad (%.1f deg) projRatio=%.4f tanH=%.4f "
                          "base=(%.4f %.4f) stage=(%.4f %.4f) dist=%.2f "
                          "lean=(%.3f %.3f %.3f) aimcam=(%.4f %.4f %.4f) ndc=(%.4f %.4f) "
                          "screen=(%.4f %.4f) -> (%.4f %.4f)",
                    fov, fov / kDegToRad, projRatio, tanH, baseX, baseY, stage.x, stage.y,
                    dist, lean.x, lean.y, lean.z, at.camera_relative.x, at.camera_relative.y,
                    at.camera_relative.z, at.ndc_x, at.ndc_y, at.x, at.y, hudX, hudY);
        }
    }

    // A centred head leaves the game's own reticle position correct, so give it
    // back rather than holding it there - Prey moves the reticle itself in
    // controller free-aim, and rewriting the same value every frame would pin it.
    if (std::fabs(hudX - baseX) < kReticleAtBaseEpsilon &&
        std::fabs(hudY - baseY) < kReticleAtBaseEpsilon) {
        hud::Restore();
        return;
    }
    hud::SetPosition(hudX, hudY);
}

/// Decide whether the head pose applies this frame and, if so, build the camera
/// the player should see from the one the game set.
///
/// Shared by the render hook and the HUD projection hook, and it has to be: a
/// marker projected through a different camera than the world it is pinned to
/// would sit beside its object instead of on it. Both hooks run inside one game
/// frame and read the same pose - it is published once per frame from the present
/// hook - so they cannot disagree.
bool BuildTrackedCamera(uintptr_t mtxAddr, Mat34& clean, Mat34& modified) {
    auto* tracking = g_hookTracking.load(std::memory_order_acquire);
    if (tracking == nullptr || !tracking->Enabled() ||
        !InActiveGameplay() || InMultiplayerSession()) {
        return false;
    }

    const auto pose = tracking->CurrentPose();
    const auto posn = tracking->CurrentPosition();
    const bool rot = pose.IsValid();
    const bool pos = tracking->PositionEnabled() && posn.valid;
    if (!rot && !pos) return false;

    if (!SafeRead(mtxAddr, clean)) return false;

    // Leave a camera the engine has not positioned yet exactly as it found it -
    // see kCameraValidEpsilon.
    const V3 camPos = clean.Trans();
    if (std::fabs(camPos.x) <= kCameraValidEpsilon &&
        std::fabs(camPos.y) <= kCameraValidEpsilon &&
        std::fabs(camPos.z) <= kCameraValidEpsilon) {
        return false;
    }

    static unsigned s_dumpCounter = 0;
    if (g_dumpCamera.load(std::memory_order_relaxed) &&
        (s_dumpCounter++ % kDumpEveryFrames) == 0) {
        const V3 t = clean.Trans(), f = clean.Forward();
        PHT_LOG(Info, "view camera: pos=(%.2f %.2f %.2f) fwd=(%.3f %.3f %.3f) | "
                      "ypr=(%.2f %.2f %.2f) off=(%.3f %.3f %.3f) rot=%d pos=%d",
                t.x, t.y, t.z, f.x, f.y, f.z,
                pose.yaw, pose.pitch, pose.roll, posn.x, posn.y, posn.z,
                rot ? 1 : 0, pos ? 1 : 0);
    }

    modified = clean;
    if (rot) {
        // World-space yaw turns the head about the world's up axis, which is the
        // right answer on foot and meaningless in the space suit: the player
        // floats at any angle out there, so "up" stops being a direction that has
        // anything to do with the head, and a head turn while lying sideways
        // swings the view about the station's axis instead of the player's own.
        // In the suit the yaw is head-relative whatever the setting says.
        // Tracker convention -> engine convention. The protocol says nothing about
        // which way its positives point, so this cannot be derived from CryEngine
        // being right-handed Z-up; it is settled in game, and yaw, roll and x all
        // came back mirrored. Confirmed by turning the head and watching the view
        // go the other way. Doing it here, at the boundary, keeps all four sign
        // conversions in one place - pitch is the odd one out and needs none.
        ApplyHeadRotation(modified, -pose.yaw, pose.pitch, -pose.roll,
                          tracking->WorldSpaceYaw() &&
                              !g_wearingSuit.load(std::memory_order_relaxed));
    }
    if (pos) {
        // Lean along the CLEAN camera's own axes - the offset is applied in the
        // original view space, before the head rotation, so it follows where the
        // body is facing rather than where the head is looking. This is what
        // witcher-3-headtracking does (camera_modifier.cpp, "position offset in
        // original view space"), and it is deliberately NOT the horizon-locked
        // basis this used to build.
        //
        // That basis came from flattening the camera's forward vector onto the
        // horizontal plane, which collapses as the camera approaches vertical:
        // look straight down and (f.x, f.y) is rounding error, so normalising it
        // produced a full-length vector pointing wherever the noise said, and it
        // swept as the player moved. The lean then travelled in an arc instead of
        // holding still, and with the player's own body a hand's width from the
        // lens the parallax threw the view around violently - worst looking
        // straight down, where the body fills the frame. Blending the heading
        // towards the up axis narrowed the window but did not remove it, because
        // the basis is genuinely undefined there. The camera's own axes are
        // defined everywhere, so there is nothing left to degenerate.
        //
        // The cost is that leaning in while looking down moves the view along the
        // look direction rather than through the room. That is bounded by the
        // travel limits and is what the witcher-3 mod ships.
        //
        // Position arrives in metres (the wire carries centimetres; the core
        // converts) and CryEngine world units are metres, so it applies 1:1. The
        // pipeline's z runs out the BACK of the head, so a forward lean is
        // negative z and the flip belongs here at the engine boundary; x is
        // mirrored for the same reason yaw and roll are.
        const V3 off = Add(Add(Scale(clean.Right(), -posn.x),
                               Scale(clean.Up(), posn.y)),
                           Scale(clean.Forward(), -posn.z));
        modified.SetTrans(Add(clean.Trans(), off));
    }
    return true;
}

/// Publish the frame's camera pair to everything that consumes it - the focus
/// detour nested inside the injection window, and the flashlight beam.
void PublishFrameBasis(const Mat34& clean, const Mat34& modified) {
    g_frameClean    = clean;
    g_frameModified = modified;
    g_frameHeadRot  = RotationBetween(clean, modified);
    g_frameInjected = true;
    flashlight::PublishViewBasis(clean, modified);
}

/// Inject the head pose into the engine's view camera for the duration of the
/// world render, then put the clean matrix back.
///
/// This is the whole of the aim-decoupling design. The engine keeps ONE view
/// camera inside CSystem and hands it to everything, so the only question is how
/// long the head pose is allowed to live in it. The frame order is
///
///     game update -> SetViewCamera -> HUD projection -> Render -> (next frame)
///
/// and everything that decides what the player is looking at - interaction focus,
/// weapon aim, raycasts, AI sight - reads it in the first two thirds of that.
/// Confining the pose to Render keeps every one of those readers on the clean,
/// mouse-controlled camera, which is the point of the mod.
///
/// Both of the earlier injection points were measured in game and both couple
/// gameplay to the head:
///   - Injecting in SetViewCamera and never restoring leaves the head-rotated
///     camera in CSystem for the whole frame, so the next update tick reads it.
///   - Injecting in SetViewCamera and restoring at the end of Render still hands
///     it to the update code that runs between the two: turning the head moved
///     Prey's interaction focus onto whatever the head pointed at, with the
///     mouse untouched.
/// The cost of the render-only window is that the HUD is drawn from the clean
/// camera, so its world-anchored markers and its centre crosshair sit where they
/// would be if the player were looking straight ahead. That is a cosmetic offset;
/// the alternative changes what the game does.
void __fastcall RenderDetour(void* pSystem) {
    const MemberVoidFn orig = g_origRender.load(std::memory_order_relaxed);

    // Early injection owns the camera from SetViewCamera onwards, so the render
    // path must not apply the pose a second time on top of it.
    if (g_earlyInject.load(std::memory_order_relaxed)) {
        orig(pSystem);
        flashlight::RestoreTracking();
        return;
    }

    void* cam = nullptr;
    const uintptr_t mtxAddr = camera::MatrixAddress(pSystem, &cam);
    Mat34 clean{}, modified{};
    if (mtxAddr == 0 || !BuildTrackedCamera(mtxAddr, clean, modified)) {
        hud::Restore();
        orig(pSystem);
        return;
    }

    // Before the matrix goes in, so the engine's camera is in its clean state
    // for the duration of the call into the game's UI.
    if (g_reticleEnabled.load(std::memory_order_relaxed)) {
        UpdateReticle(cam, clean, modified);
    }

    if (!camera::WriteMatrix(mtxAddr, cam, modified)) {
        hud::Restore();
        orig(pSystem);
        return;
    }
    camera::CallThenRestore(orig, pSystem, mtxAddr, cam, clean);
}

/// Refresh the snapshot the body hook hands out, EVERY frame, before anything
/// decides whether a pose is going in.
///
/// It used to be refreshed only on the frames that injected, which meant that the
/// moment tracking stopped applying - End pressed, a menu opened - the body hook
/// went on handing out a camera frozen at the last tracked frame. The world then
/// rendered from a stale camera while the culling, which reads the live one, still
/// followed the mouse: the view appeared to lock up while the frustum kept moving.
void RefreshCleanCameraSnapshot(void* cam, uintptr_t mtxAddr) {
    bool ok = false;
    if (mtxAddr != 0 && (g_bodyCameraReader.load(std::memory_order_relaxed) != 0 ||
                         g_cleanCameraForReader.load(std::memory_order_relaxed) != 0)) {
        ok = SafeRead(reinterpret_cast<uintptr_t>(cam), g_cleanCameraCopy);
    }
    g_cleanCopyValid.store(ok, std::memory_order_release);
}

/// Put the head pose into the view camera the moment the game sets it, and leave
/// it there for the rest of the frame.
///
/// Confining the pose to the render leaves geometry culled out of a turned view:
/// something snapshots the camera for culling BEFORE the render and is not
/// reached by writing it there. Injecting at the source covers every consumer in
/// the frame, which is what fixes the culling.
///
/// On its own this costs the decoupling, because the game's own readers run in
/// this window too and the interaction focus starts following the head. That is
/// measured, and it is why the render-only window existed for so long. It is
/// survivable now only because FocusUpdateDetour hands the focus system the clean
/// camera back for its own call - the two belong together, and turning this on
/// without that hook aims the interaction ray with the head.
void __fastcall SetViewCameraDetour(void* pSystem, void* cam) {
    g_origSetViewCamera.load(std::memory_order_relaxed)(pSystem, cam);
    if (!g_earlyInject.load(std::memory_order_relaxed)) return;

    void* viewCam = nullptr;
    const uintptr_t mtxAddr = camera::MatrixAddress(pSystem, &viewCam);
    RefreshCleanCameraSnapshot(viewCam, mtxAddr);

    Mat34 clean{}, modified{};
    if (mtxAddr == 0 || !BuildTrackedCamera(mtxAddr, clean, modified)) {
        // Nothing is being injected, so nothing may be substituted either - the
        // engine's own camera is already the correct one for every reader.
        g_frameInjected = false;
        g_poseApplied.store(false, std::memory_order_release);
        hud::Restore();
        return;
    }

    // In this mode the pose stays in the camera for the rest of the frame, so the
    // focus system would otherwise read a head-tracked camera and aim the
    // interaction ray with the head - which is exactly what made this mode
    // unusable before the focus hook existed.
    PublishFrameBasis(clean, modified);

    // The reticle has to be placed from here too. The render hook is the usual
    // home for it, but in this mode it does not touch the camera, and by the time
    // it runs the engine's camera IS the tracked one - so it no longer has a
    // clean camera to measure the aim against. Here we still have both.
    if (g_reticleEnabled.load(std::memory_order_relaxed)) {
        UpdateReticle(viewCam, clean, modified);
    }

    // No restore: the game overwrites the whole camera through this very function
    // on the next frame, so nothing accumulates. The flag follows the write, not
    // the intent: a refused write leaves the game's own camera in place, and the
    // body hook must not then be told a pose is in there.
    const bool applied = camera::WriteMatrix(mtxAddr, viewCam, modified);
    g_poseApplied.store(applied, std::memory_order_release);

    // The beam has to be turned HERE, not in the render. Writing it at the top of
    // CSystem::Render survives the whole render untouched and still lights the
    // room in the un-turned direction, which means the light's render parameters
    // are already built by then - during the update, of which this call is part.
    flashlight::ApplyTracking();
}

/// One half of the body edit: what was written, and what to put back.
struct BodyEdit {
    uintptr_t addr = 0;   ///< 0 when this half was not written
    V3        offset{};
    Mat34     matrix{};
};

/// Its own function with no unwindable objects in scope, which is what __finally
/// requires (MSVC C2712 forbids it alongside C++ object destruction).
void CallThenRestoreBody(EntityRenderFn orig, void* node, void* p2, void* p3, uint32_t p4,
                         void* p5, void* p6, const BodyEdit& vec, const BodyEdit& mtx) {
    __try {
        orig(node, p2, p3, p4, p5, p6);
    } __finally {
        if (vec.addr != 0) SafeWrite(vec.addr, vec.offset);
        if (mtx.addr != 0) SafeWrite(mtx.addr, mtx.matrix);
    }
}

/// The entity's world matrix, through the getter the render itself calls. Null
/// when the entity or its vtable is not readable, which leaves the body alone.
float* EntityWorldMatrix(void* entity) {
    uintptr_t vtable = 0, getter = 0;
    if (!SafeRead(reinterpret_cast<uintptr_t>(entity), vtable) || vtable == 0) return nullptr;
    if (!SafeRead(vtable + g_entityWorldTmSlot.load(std::memory_order_relaxed), getter) ||
        getter == 0) {
        return nullptr;
    }
    return reinterpret_cast<GetWorldTmFn>(getter)(entity);
}

/// Diagnostic: report the camera-space node the engine draws, with the offset it
/// is placed from and where it sits against the eye. Sampled rather than logged
/// once, because the offset is live data the game rewrites every frame - read a
/// single time, on the first frame the node appears, it comes back all zeroes and
/// says the opposite of the truth.
void TraceBodyNode(void* node, uint8_t flags, const V3& offset) {
    static unsigned s_calls = 0;
    if ((s_calls++ % 240) != 0) return;

    Mat34 m{};
    const bool haveM = SafeRead(reinterpret_cast<uintptr_t>(node) + 8, m);
    const V3 t = haveM ? m.Trans() : V3{0.0f, 0.0f, 0.0f};
    const V3 eye = g_frameClean.Trans();
    PHT_LOG(Info, "body node %p flags=0x%02X offset=(%.4f %.4f %.4f) pos-eye=(%.3f %.3f %.3f) "
                  "fwd=(%.2f %.2f %.2f) cleanfwd=(%.2f %.2f %.2f)",
            node, flags, offset.x, offset.y, offset.z,
            t.x - eye.x, t.y - eye.y, t.z - eye.z,
            haveM ? m.Forward().x : 0.0f, haveM ? m.Forward().y : 0.0f,
            haveM ? m.Forward().z : 0.0f,
            g_frameClean.Forward().x, g_frameClean.Forward().y, g_frameClean.Forward().z);
}

/// Carry the first-person body with the head while it is drawn.
///
/// In the exterior sections the player is inside a space suit, and the suit and
/// its helmet collar fill the bottom of the screen. Left alone they stay facing
/// where the character faces, so turning the head looks across the inside of the
/// suit instead of turning inside it - reported as "the suit stays fixed in place
/// when I move my head".
///
/// Prey draws all of that as ONE camera-space render node, resolved from two
/// things this detour can reach: a Vec3 on the node, which is the body's offset
/// from the eye in the camera's own axes, and the entity's world matrix, whose
/// rotation the render matrix keeps (the camera-space branch replaces only the
/// translation). Turning both by the head pose - the offset so the body swings
/// around the eye, the matrix so it faces the same way - is a rigid rotation of
/// the body about the head's own pivot, which is what a suit does when the person
/// inside it looks around.
///
/// Both halves are needed and one of them hides: with the offset pointing
/// straight down and the head only yawing, rotating about the body's own origin
/// and rotating about the eye are the same thing, so turning the facing alone
/// looks perfect until the first head pitch, where the body swings out of frame.
///
/// The two values belong to the engine and the next frame reads them, so both go
/// back in a __finally before this returns. Nothing else in the frame sees the
/// body moved: this is the render, and the game's own readers have already run.
/// Is the player in the space suit, judged by where the body is hung off the eye?
///
/// Carrying the body with the head is right in the suit and wrong outside it:
/// Prey draws the arms and the item in your hands as part of the same object, so
/// on foot it would take the gun with it and leave the barrel pointing away from
/// the crosshair the shot goes to. Nothing readable from here says "suit" in so
/// many words - the model is `objects/characters/player/player.cdf` either way -
/// but the body's own origin does: chest height while floating in the suit, floor
/// height while standing.
bool WearingSpaceSuit(float originDistanceMetres) {
    bool suit = g_wearingSuit.load(std::memory_order_relaxed);
    if (originDistanceMetres > kBodyOriginFloorMetres) {
        if (originDistanceMetres < kSuitBodyOriginEnterMetres) {
            suit = true;
        } else if (originDistanceMetres > kSuitBodyOriginLeaveMetres) {
            suit = false;
        }
    }
    g_wearingSuit.store(suit, std::memory_order_relaxed);

    static std::atomic<int> s_reported{-1};
    const int now = suit ? 1 : 0;
    if (s_reported.exchange(now, std::memory_order_relaxed) != now) {
        PHT_LOG(Info, "%s: the first-person body's origin is %.2fm below the eye. The body %s, "
                      "and head yaw turns about %s.",
                suit ? "Space suit on" : "No space suit", originDistanceMetres,
                suit ? "and the item in your hands follow your head"
                     : "and the item in your hands stay where the character is facing",
                suit ? "your own head, since there is no up in zero gravity"
                     : "the configured axis");
    }
    return suit;
}

void __fastcall EntityRenderDetour(void* node, void* p2, void* p3, uint32_t p4,
                                   void* p5, void* p6) {
    const EntityRenderFn orig = g_origEntityRender.load(std::memory_order_relaxed);
    const bool follows = g_bodyFollowsHead.load(std::memory_order_relaxed);
    const bool trace   = g_traceBodyNodes.load(std::memory_order_relaxed);

    // The cheap path, and the one every other object in the scene takes. The suit
    // reading is taken whatever the body is doing, because the yaw mode depends on
    // it too.
    const bool hide = g_hideBodyNodes.load(std::memory_order_relaxed);
    if (node == nullptr || (!g_frameHeadRot.valid && !hide) ||
        !g_poseApplied.load(std::memory_order_acquire)) {
        orig(node, p2, p3, p4, p5, p6);
        return;
    }

    // Only nodes the engine has already flagged as camera-space, which is the
    // branch the first-person body takes.
    const uintptr_t n = reinterpret_cast<uintptr_t>(node);
    uint8_t   flags   = 0;
    uintptr_t vecAddr = 0;
    if (!SafeRead(n + g_nodeFlagsOff.load(std::memory_order_relaxed), flags) ||
        (flags & 2) == 0 ||
        !SafeRead(n + g_nodeOffsetPtrOff.load(std::memory_order_relaxed), vecAddr) ||
        vecAddr == 0) {
        orig(node, p2, p3, p4, p5, p6);
        return;
    }

    V3 offset{};
    if (!SafeRead(vecAddr, offset)) {
        orig(node, p2, p3, p4, p5, p6);
        return;
    }
    if (trace) TraceBodyNode(node, flags, offset);
    const bool suit = WearingSpaceSuit(Length(offset));
    if (hide) return;
    if (!follows || !suit) {
        orig(node, p2, p3, p4, p5, p6);
        return;
    }

    BodyEdit vec, mtx;
    {
        // The offset is stored in the CAMERA's axes - a node 46cm below the eye
        // reads as (0, 0, -0.46) whatever the camera is doing - so the head
        // rotation has to be expressed in those axes before it can turn it.
        //
        // In world axes it is indistinguishable while the character faces down a
        // world axis, which is what the save this was developed against does, and
        // wrong by the character's heading everywhere else: measured with the
        // character turned, the world-axis version threw the suit clean out of
        // frame on pitch and on roll. That is what "it goes the opposite way, and
        // it doesn't respond to roll at all" was.
        //
        // The LEAN needs nothing: the engine resolves this offset against the
        // camera the mod hands it and the renderer places it against the one it
        // is drawing through, so a head that has moved carries the body with it
        // already. Adding the lean here as well double-counts it, which is
        // visible the moment a lean and a turn happen together.
        const V3 w = g_frameHeadRot.axis;
        const V3 axis{Dot(w, g_frameClean.Right()), Dot(w, g_frameClean.Forward()),
                      Dot(w, g_frameClean.Up())};
        const V3 turned = RotateAbout(offset, axis, g_frameHeadRot.angle);
        if (SafeWrite(vecAddr, turned)) {
            vec.addr   = vecAddr;
            vec.offset = offset;
        }
    }
    {
        float* const wm = EntityWorldMatrix(p2);
        Mat34 world{};
        if (wm != nullptr && SafeRead(reinterpret_cast<uintptr_t>(wm), world)) {
            Mat34 turned = world;
            turned.SetRight  (RotateAbout(world.Right(),   g_frameHeadRot.axis,
                                          g_frameHeadRot.angle));
            turned.SetForward(RotateAbout(world.Forward(), g_frameHeadRot.axis,
                                          g_frameHeadRot.angle));
            turned.SetUp    (RotateAbout(world.Up(),       g_frameHeadRot.axis,
                                          g_frameHeadRot.angle));
            if (SafeWrite(reinterpret_cast<uintptr_t>(wm), turned)) {
                mtx.addr   = reinterpret_cast<uintptr_t>(wm);
                mtx.matrix = world;
            }
        }
    }

    if (vec.addr == 0 && mtx.addr == 0) {
        orig(node, p2, p3, p4, p5, p6);
        return;
    }
    CallThenRestoreBody(orig, node, p2, p3, p4, p5, p6, vec, mtx);
}

/// A return address as an RVA into PreyDll.dll. The base is the one the install
/// pass resolved rather than a fresh GetModuleHandleW: this runs on every one of
/// the engine's ~30 camera reads per frame, and the loader lock has no business
/// on that path.
uintptr_t CallerRva(void* returnAddress) {
    return reinterpret_cast<uintptr_t>(returnAddress) -
           g_preyDllBase.load(std::memory_order_relaxed);
}

/// Is this GetViewCamera call coming from `wantRva`, or from anywhere in
/// [wantRva, endRva) when a range is configured?
bool CallerMatches(void* returnAddress, uintptr_t wantRva, uintptr_t endRva) {
    const uintptr_t rva = CallerRva(returnAddress);
    return (endRva != 0) ? (rva >= wantRva && rva < endRva) : (rva == wantRva);
}

/// Diagnostic: record who reads the view camera, by caller. Each distinct caller
/// is logged once, as an RVA; expect about 30 in gameplay.
void TraceCameraReader(void* returnAddress) {
    static constexpr int kMaxCallers = 64;
    static void* s_seen[kMaxCallers] = {};
    static int   s_count = 0;
    for (int i = 0; i < s_count; ++i) {
        if (s_seen[i] == returnAddress) return;
    }
    if (s_count >= kMaxCallers) return;
    s_seen[s_count++] = returnAddress;
    PHT_LOG(Info, "camera reader #%d: RVA 0x%llX", s_count,
            (unsigned long long)CallerRva(returnAddress));
}

/// Hand individual camera readers a camera of our choosing.
///
/// Prey builds the render object for the first-person body from the view camera,
/// and the render applies the view camera again, so left alone the body swings
/// across the screen at twice the head's rotation. It reads as the camera being
/// flung yards behind the player - worst with the mouse pitched down, where the
/// body already lies across the view and rotating up sweeps it through the middle
/// of the screen. The camera is barely moving; the body is.
///
/// Only ONE of this function's several camera reads matters, so the whole call is
/// not redirected - that would hand the clean camera to every object it builds.
/// The call site was found by bisecting the readers with CleanCameraForReader,
/// and the same two diagnostics re-derive it when a patch moves it.
void* __fastcall GetViewCameraDetour(void* pSystem) {
    void* cam = g_origGetViewCamera.load(std::memory_order_relaxed)(pSystem);
    void* ret = _ReturnAddress();

    // Only while a pose is actually in the camera. With tracking off there is
    // nothing to undo, and substituting anything would hand out a stale camera.
    const bool canSubstitute = g_cleanCopyValid.load(std::memory_order_acquire);
    const uintptr_t bodyReader = g_bodyCameraReader.load(std::memory_order_relaxed);
    if (bodyReader != 0 && canSubstitute &&
        g_poseApplied.load(std::memory_order_acquire) &&
        CallerMatches(ret, bodyReader, 0)) {
        return &g_cleanCameraCopy;
    }

    // Diagnostic: hand one nominated caller (or a whole RVA range, for bisecting)
    // the same camera the body's call site gets - the clean one turned by
    // BodyCameraScale. CSystem::Render reaches the camera through its own member
    // rather than this getter, so the world still renders head-tracked and the
    // test stays meaningful.
    const uintptr_t want = g_cleanCameraForReader.load(std::memory_order_relaxed);
    if (want != 0 && canSubstitute &&
        CallerMatches(ret, want, g_cleanCameraReaderEnd.load(std::memory_order_relaxed))) {
        return &g_cleanCameraCopy;
    }

    if (g_traceCameraReaders.load(std::memory_order_relaxed)) TraceCameraReader(ret);
    return cam;
}

/// Give Prey's focus system back the camera the mouse is pointing.
///
/// This is the reason the marker fix cost the interaction ray the first time.
/// The HUD projection update does not only project markers: it also calls this,
/// which reads the view camera AND the reticle position at +0x17EC and projects
/// them into the world to work out what the player is looking at. Wrapping the
/// whole HUD update in the head pose therefore aimed the interaction ray with the
/// head - look away from a prompt and it dropped, exactly as reported.
///
/// So the pose goes in for the HUD update and comes straight back out again for
/// the duration of this one call. Markers still project through the turned view;
/// what the player can reach still follows the mouse.
void __fastcall FocusUpdateDetour(void* player) {
    const MemberVoidFn orig = g_origFocusUpdate.load(std::memory_order_relaxed);
    if (!g_frameInjected) {
        // Called outside the window we injected in, so the camera is already the
        // game's own and there is nothing to undo.
        orig(player);
        return;
    }

    void* cam = nullptr;
    const uintptr_t mtxAddr =
        camera::MatrixAddress(g_pSystem.load(std::memory_order_acquire), &cam);
    if (mtxAddr == 0 || !camera::WriteMatrix(mtxAddr, cam, g_frameClean)) {
        orig(player);
        return;
    }
    camera::CallThenRestore(orig, player, mtxAddr, cam, g_frameModified);
}

/// Project Prey's world-anchored HUD through the head-tracked view.
///
/// Prey builds the marker projection once per frame in here, off
/// gEnv->pSystem->GetViewCamera(), and this runs between the game's own
/// SetViewCamera and the render. With the pose confined to the render, the
/// markers were therefore projected through the un-turned camera and sat still on
/// screen while the world slid under them.
///
/// Injecting HERE is what buys the fix without costing the decoupling. This
/// function feeds the HUD and nothing else - it builds the projection, then walks
/// the marker arrays - so interaction focus, weapon aim, raycasts and AI sight
/// still read the clean camera the game set. Injecting any earlier (at
/// SetViewCamera) puts the pose in front of those readers too, which is measured
/// to move Prey's interaction focus onto whatever the head is pointed at.
///
/// Writing the camera at this point in the frame is as safe as what the engine
/// already does: SetViewCamera has just overwritten the whole thing a moment
/// earlier.
void __fastcall HudProjectionDetour(void* self) {
    const MemberVoidFn orig = g_origHudProjection.load(std::memory_order_relaxed);

    // With the pose already in the camera for the whole frame, the HUD projection
    // reads a tracked camera. Injecting again here would rotate the markers twice
    // and put them further off their objects than doing nothing at all.
    if (g_earlyInject.load(std::memory_order_relaxed)) {
        orig(self);
        return;
    }

    void* cam = nullptr;
    const uintptr_t mtxAddr =
        camera::MatrixAddress(g_pSystem.load(std::memory_order_acquire), &cam);
    Mat34 clean{}, modified{};
    if (mtxAddr == 0 || !BuildTrackedCamera(mtxAddr, clean, modified) ||
        !camera::WriteMatrix(mtxAddr, cam, modified)) {
        orig(self);
        return;
    }

    PublishFrameBasis(clean, modified);
    camera::CallThenRestore(orig, self, mtxAddr, cam, clean);
    g_frameInjected = false;
}

// --- Install ----------------------------------------------------------------

/// The engine handles a profile resolves to, all read through the live ISystem
/// vtable rather than from fixed addresses, so a patch that only relinks function
/// bodies needs no new RVAs.
struct EngineHandles {
    uintptr_t vtable          = 0;
    uintptr_t system_object   = 0;
    uintptr_t render_fn       = 0;
    uintptr_t get_view_camera = 0;
};

/// False means "not ready yet" rather than "broken": CSystem is constructed some
/// way into startup, so the caller retries next frame.
bool ResolveEngine(uintptr_t base, const BuildProfile& profile, EngineHandles& out) {
    if (!SafeRead(base + profile.system_ptr_rva, out.system_object) ||
        out.system_object == 0) {
        return false;
    }
    if (!SafeRead(out.system_object, out.vtable) || out.vtable == 0) return false;
    if (!SafeRead(out.vtable + profile.render_vtable_off, out.render_fn) ||
        out.render_fn == 0) {
        return false;
    }
    return SafeRead(out.vtable + profile.view_camera_vtable_off, out.get_view_camera) &&
           out.get_view_camera != 0;
}

/// Everything a detour reads must be in place before any hook is armed.
void BindEngineState(uintptr_t base, const BuildProfile& profile, const EngineHandles& engine) {
    g_multiplayerFlag.store(base + profile.multiplayer_flag_rva, std::memory_order_relaxed);
    g_preyDllBase.store(base, std::memory_order_relaxed);
    g_pSystem.store(reinterpret_cast<void*>(engine.system_object), std::memory_order_release);

    camera::Binding cam;
    cam.get_view_camera   = reinterpret_cast<GetViewCameraFn>(engine.get_view_camera);
    cam.update_frustum    = reinterpret_cast<camera::UpdateFrustumFn>(
        base + profile.update_frustum_rva);
    cam.matrix_offset     = profile.matrix_offset;
    cam.fov_offset        = profile.camera_fov_off;
    cam.proj_ratio_offset = profile.camera_proj_ratio_off;
    camera::Bind(cam);
}

/// A zero offset is how each CVar feature stays off, so what the player did not
/// ask for and what the profile does not carry are simply left out here.
void BindGameCVars(uintptr_t base, const BuildProfile& profile, const Config& cfg) {
    cvars::Binding b;
    if (profile.game_config_ptr_rva != 0) {
        b.game_block_ptr = base + profile.game_config_ptr_rva;
        if (cfg.force_flashlight) b.force_flashlight_off = profile.force_flashlight_off;
        // Both FOV features read cl_fov, so the offsets are bound whenever the
        // profile carries them; it is the non-zero field_of_view that arms the
        // override, and draw_near_fov_addr that arms the weapon lens.
        b.cl_fov_off  = profile.cl_fov_off;
        b.cl_hfov_off = profile.cl_hfov_off;
        if (cfg.field_of_view > 0.0f && profile.cl_hfov_off != 0) {
            b.field_of_view = cfg.field_of_view;
        }
        if (cfg.match_weapon_fov && profile.cl_fov_off != 0 && profile.draw_near_fov_rva != 0) {
            b.draw_near_fov_addr = base + profile.draw_near_fov_rva;
        }
    }
    if (cfg.disable_coverage_buffer && profile.cvars_ptr_rva != 0) {
        b.engine_block_ptr    = base + profile.cvars_ptr_rva;
        b.coverage_buffer_off = profile.coverage_buffer_off;
    }
    cvars::Bind(b);
}

void BindReticle(uintptr_t base, const BuildProfile& profile, const Config& cfg) {
    if (!cfg.compensate_reticle || !profile.HasReticlePath()) return;

    hud::Binding binding;
    binding.get_element   = base + profile.hud_element_rva;
    binding.call_fn_2f    = base + profile.hud_call2f_rva;
    binding.config_ptr    = base + profile.game_config_ptr_rva;
    binding.reticle_y_off = profile.reticle_y_percent_off;
    hud::Bind(binding);
    g_reticleEnabled.store(true, std::memory_order_release);

    if (profile.HasAimTrace()) {
        aim::Binding trace;
        trace.physical_world_ptr = base + profile.physical_world_ptr_rva;
        trace.rwi_vtable_off     = profile.rwi_vtable_off;
        aim::Bind(trace);
    }
    PHT_LOG(Info, "Reticle compensation on: the crosshair follows the mouse-controlled aim "
                  "into the head-tracked view, so shots land where it is drawn.%s",
            profile.HasAimTrace()
                ? " Leaning is corrected too, against the distance to whatever the aim ray "
                  "hits."
                : " Leaning is not corrected on this build - no aim ray.");
}

/// Must run AFTER InstallEarlyInjectHook: the beam is turned from the
/// SetViewCamera detour, because a rotation written at the top of CSystem::Render
/// is too late for the light's render parameters. With early injection off - by
/// config, or because its hook did not take - there is no call site to turn it
/// from, and saying so beats a "Flashlight tracking on" line for a beam that
/// never moves.
void InstallFlashlightHooks(uintptr_t base, const BuildProfile& profile, const Config& cfg) {
    if (cfg.compensate_flashlight && !g_earlyInject.load(std::memory_order_acquire)) {
        PHT_LOG(Warn, "CompensateFlashlight is on but early injection is not, so the beam has "
                      "no point in the frame to be turned from and will follow the mouse. Set "
                      "EarlyInject = true to get it back.");
    }
    if ((cfg.compensate_flashlight || cfg.trace_lights) && profile.HasFlashlight()) {
        flashlight::Binding b;
        b.arklight_set_addr         = base + profile.arklight_set_rva;
        b.arklight_entity_off       = profile.arklight_entity_off;
        b.arklight_vtable           = profile.arklight_vtable_rva == 0
                                          ? 0 : base + profile.arklight_vtable_rva;
        b.entity_set_transform_addr = profile.entity_set_transform_rva == 0
                                          ? 0 : base + profile.entity_set_transform_rva;
        b.scale                     = cfg.flashlight_scale;
        b.trace_lights              = cfg.trace_lights;
        b.trace_light_reader        = cfg.trace_light_reader;
        if (flashlight::Install(b)) {
            PHT_LOG(Info, "Flashlight tracking on: the beam turns %.2fx the head rotation, so "
                          "it lands on what you turned to look at rather than short of it.",
                    cfg.flashlight_scale);
        }
    }

    if (cfg.trace_lights && profile.add_dyn_light_rva != 0) {
        if (flashlight::InstallDynamicLightTrace(base + profile.add_dyn_light_rva)) {
            PHT_LOG(Info, "TraceLights on: logging each distinct dynamic light once.");
        } else {
            PHT_LOG(Warn, "TraceLights: could not hook AddDynamicLightSource at RVA 0x%llX.",
                    static_cast<unsigned long long>(profile.add_dyn_light_rva));
        }
    }
}

/// The getter hook serves the body compensation and both camera-reader
/// diagnostics; none of them can run without it.
void InstallCameraReaderHook(const BuildProfile& profile, const Config& cfg,
                             uintptr_t getViewCameraFn) {
    if (cfg.compensate_body && profile.body_camera_reader_rva != 0) {
        g_bodyCameraReader.store(profile.body_camera_reader_rva, std::memory_order_relaxed);
    }
    g_cleanCameraForReader.store(cfg.clean_camera_for_reader, std::memory_order_relaxed);
    g_cleanCameraReaderEnd.store(cfg.clean_camera_reader_end, std::memory_order_relaxed);

    if (g_bodyCameraReader.load(std::memory_order_relaxed) == 0 &&
        !cfg.trace_camera_readers && cfg.clean_camera_for_reader == 0) {
        return;
    }

    auto& mh = HookManager::Instance();
    void* target = reinterpret_cast<void*>(getViewCameraFn);
    GetViewCameraFn orig = nullptr;
    if (mh.CreateHook(target, reinterpret_cast<void*>(&GetViewCameraDetour),
                      reinterpret_cast<void**>(&orig)) != HookStatus::Ok) {
        return;
    }
    g_origGetViewCamera.store(orig, std::memory_order_release);
    if (mh.EnableHook(target) != HookStatus::Ok) {
        // Without the getter hook the body cannot be compensated, and a stale
        // non-zero RVA would just be dead weight. The hook is removed rather than
        // left created, so nothing can arm it later behind our back.
        g_bodyCameraReader.store(0, std::memory_order_relaxed);
        g_origGetViewCamera.store(nullptr, std::memory_order_release);
        mh.RemoveHook(target);
        PHT_LOG(Warn, "CryEngineCamera: could not hook GetViewCamera; the first-person "
                      "body will swing with the head.");
        return;
    }

    if (cfg.trace_camera_readers) {
        g_traceCameraReaders.store(true, std::memory_order_release);
        PHT_LOG(Info, "TraceCameraReaders on: logging each distinct caller of "
                      "GetViewCamera once, as an RVA into PreyDll.dll.");
    }
    if (g_bodyCameraReader.load(std::memory_order_relaxed) != 0) {
        PHT_LOG(Info, "First-person body compensation on: the render object for the "
                      "player's own body is built from the clean camera, so it stays where "
                      "the character is facing instead of swinging with the head.");
    }
}

/// Hook the placement of camera-space render nodes, so the first-person body's
/// own offset can be turned as well as the camera it is resolved against.
///
/// This runs for every entity object the engine draws, so the detour's first act
/// is a scale compare that sends all of them straight through. It is installed
/// whenever the body is being compensated rather than only when the offset dial
/// is non-zero, because the dial is walked from a key in game.
void InstallBodyPlacementHook(uintptr_t base, const BuildProfile& profile, const Config& cfg) {
    if (!cfg.compensate_body || !profile.HasBodyPlacement()) return;

    g_nodeFlagsOff.store(profile.node_flags_off, std::memory_order_relaxed);
    g_nodeOffsetPtrOff.store(profile.node_cam_offset_ptr_off, std::memory_order_relaxed);
    g_entityWorldTmSlot.store(profile.entity_world_tm_vtable_off, std::memory_order_relaxed);
    g_traceBodyNodes.store(cfg.trace_body_nodes, std::memory_order_relaxed);
    g_bodyFollowsHead.store(cfg.body_follows_head, std::memory_order_relaxed);
    g_hideBodyNodes.store(cfg.hide_body_nodes, std::memory_order_relaxed);

    auto& mh = HookManager::Instance();
    void* target = reinterpret_cast<void*>(base + profile.entity_render_rva);
    EntityRenderFn orig = nullptr;
    if (mh.CreateHook(target, reinterpret_cast<void*>(&EntityRenderDetour),
                      reinterpret_cast<void**>(&orig)) != HookStatus::Ok) {
        PHT_LOG(Warn, "CryEngineCamera: could not hook the entity render at RVA 0x%llX; the "
                      "body's own offset cannot be turned.",
                (unsigned long long)profile.entity_render_rva);
        return;
    }
    g_origEntityRender.store(orig, std::memory_order_release);
    if (mh.EnableHook(target) != HookStatus::Ok) {
        g_origEntityRender.store(nullptr, std::memory_order_release);
        mh.RemoveHook(target);
        return;
    }
    if (cfg.body_follows_head) {
        PHT_LOG(Info, "The first-person body follows the head: the space suit turns with you "
                      "instead of staying where the character is facing. Prey draws the held "
                      "item as part of the same object, so that comes with it. Delete (or "
                      "Ctrl+Shift+J) toggles this in game.");
    }
    if (cfg.trace_body_nodes) {
        PHT_LOG(Info, "TraceBodyNodes on: sampling the camera-space render node the body is "
                      "drawn from, with the offset it is placed from.");
    }
}

/// The one hook the mod cannot do without: failing it disengages entirely.
bool InstallRenderHook(uintptr_t base, const BuildProfile& profile, uintptr_t renderFn,
                       HeadTracking* tracking) {
    auto& mh = HookManager::Instance();
    void* target = reinterpret_cast<void*>(renderFn);
    MemberVoidFn orig = nullptr;
    if (mh.CreateHook(target, reinterpret_cast<void*>(&RenderDetour),
                      reinterpret_cast<void**>(&orig)) != HookStatus::Ok) {
        PHT_LOG(Error, "CryEngineCamera: CreateHook failed for CSystem::Render @ 0x%llX; "
                       "disengaging.", (unsigned long long)renderFn);
        return false;
    }
    g_origRender.store(orig, std::memory_order_relaxed);
    g_hookTracking.store(tracking, std::memory_order_release);
    if (mh.EnableHook(target) != HookStatus::Ok) {
        PHT_LOG(Error, "CryEngineCamera: EnableHook failed for CSystem::Render @ 0x%llX; "
                       "disengaging.", (unsigned long long)renderFn);
        g_hookTracking.store(nullptr, std::memory_order_release);
        mh.RemoveHook(target);  // leaving it created would let EnableAllHooks arm it later
        return false;
    }
    PHT_LOG(Info, "CryEngineCamera: hooked CSystem::Render @ 0x%llX (RVA 0x%llX, %s); the head "
                  "pose is injected into the view camera for the render only.",
            (unsigned long long)renderFn, (unsigned long long)(renderFn - base), profile.name);
    return true;
}

/// Early injection leaves the head pose in the view camera for the whole frame,
/// so Prey's own readers see it too and the interaction ray starts following the
/// head - that is measured, and it is why the render-only window existed for so
/// long. FocusUpdateDetour is what buys the decoupling back, so the two are one
/// feature: without the focus hook this is not installed at all, and the mod
/// falls back to injecting for the render only.
void InstallEarlyInjectHook(const BuildProfile& profile, const EngineHandles& engine,
                            bool focusHooked) {
    if (!focusHooked) {
        PHT_LOG(Warn, "EarlyInject is on but the focus hook is not live on this build, so early "
                      "injection is NOT being installed - it would aim Prey's interaction ray "
                      "with the head. The pose goes into the view camera for the render only: "
                      "aim stays on the mouse, and geometry may pop at the edge of a head turn.");
        return;
    }

    uintptr_t setViewFn = 0;
    if (!SafeRead(engine.vtable + profile.set_view_camera_vtable_off, setViewFn) ||
        setViewFn == 0) {
        return;
    }

    auto& mh = HookManager::Instance();
    void* target = reinterpret_cast<void*>(setViewFn);
    SetViewCameraFn orig = nullptr;
    if (mh.CreateHook(target, reinterpret_cast<void*>(&SetViewCameraDetour),
                      reinterpret_cast<void**>(&orig)) != HookStatus::Ok) {
        return;
    }
    g_origSetViewCamera.store(orig, std::memory_order_release);
    if (mh.EnableHook(target) != HookStatus::Ok) {
        g_origSetViewCamera.store(nullptr, std::memory_order_release);
        mh.RemoveHook(target);
        return;
    }

    g_earlyInject.store(true, std::memory_order_release);
    PHT_LOG(Info, "Early injection on: the head pose goes into the view camera at "
                  "SetViewCamera, so culling and the HUD see it too. The focus system is "
                  "handed the clean camera back, so aim stays on the mouse.");
}

/// Optional and independent: losing these costs marker alignment, not tracking,
/// so a failure leaves the camera hook running rather than disengaging.
///
/// The two hooks go live together or not at all. Without the focus hook the head
/// would aim the interaction ray, which is worse than markers sitting still.
bool InstallHudHooks(uintptr_t base, const BuildProfile& profile, const Config& cfg) {
    if (!(cfg.compensate_markers || cfg.early_inject) || !profile.HasHudProjection()) {
        return false;
    }

    auto& mh = HookManager::Instance();
    void* hudTarget = reinterpret_cast<void*>(base + profile.hud_projection_rva);
    MemberVoidFn hudOrig = nullptr;
    if (mh.CreateHook(hudTarget, reinterpret_cast<void*>(&HudProjectionDetour),
                      reinterpret_cast<void**>(&hudOrig)) != HookStatus::Ok) {
        PHT_LOG(Warn, "CryEngineCamera: could not hook the HUD projection; markers will sit "
                      "still on screen while the head is turned.");
        return false;
    }
    g_origHudProjection.store(hudOrig, std::memory_order_release);

    void* focusTarget = reinterpret_cast<void*>(base + profile.focus_update_rva);
    MemberVoidFn focusOrig = nullptr;
    bool focusHooked =
        mh.CreateHook(focusTarget, reinterpret_cast<void*>(&FocusUpdateDetour),
                      reinterpret_cast<void**>(&focusOrig)) == HookStatus::Ok;
    if (focusHooked) {
        g_origFocusUpdate.store(focusOrig, std::memory_order_release);
        focusHooked = mh.EnableHook(focusTarget) == HookStatus::Ok;
    }
    if (!focusHooked) {
        g_origFocusUpdate.store(nullptr, std::memory_order_release);
        mh.RemoveHook(focusTarget);
        mh.RemoveHook(hudTarget);
        g_origHudProjection.store(nullptr, std::memory_order_release);
        PHT_LOG(Warn, "CryEngineCamera: could not hook the focus update; leaving marker "
                      "compensation OFF so the interaction ray keeps following the mouse.");
        return false;
    }

    if (mh.EnableHook(hudTarget) != HookStatus::Ok) {
        g_origHudProjection.store(nullptr, std::memory_order_release);
        mh.RemoveHook(hudTarget);
        PHT_LOG(Warn, "CryEngineCamera: could not enable the HUD projection hook; markers "
                      "will sit still on screen while the head is turned.");
        return false;
    }
    PHT_LOG(Info, "HUD marker compensation on: world-anchored markers are projected "
                  "through the head-tracked view, so they stay on their objects; the "
                  "focus system still reads the mouse-controlled camera.");
    return true;
}

}  // namespace

void ToggleBodyFollowsHead() {
    const bool follows = !g_bodyFollowsHead.load(std::memory_order_relaxed);
    g_bodyFollowsHead.store(follows, std::memory_order_relaxed);

    if (g_origEntityRender.load(std::memory_order_relaxed) == nullptr) {
        PHT_LOG(Warn, "The first-person body cannot be moved on this build - the render hook is "
                      "not live - so this key does nothing.");
        return;
    }
    PHT_LOG(Info, "First-person body %s.",
            follows ? "now follows the head: the suit turns with you, and the held item with it"
                    : "left where the character is facing, which is how the game draws it");
}

std::optional<std::string> CryEngineCamera::OnInitialize() {
    g_dumpCamera.store(Framework::Get().Cfg().dump_camera, std::memory_order_release);
    PHT_LOG(Info, "CryEngineCamera: deferring engine resolution until PreyDll is mapped.");
    return std::nullopt;
}

void CryEngineCamera::OnFrame() {
    if (!m_hookInstalled) {
        InstallHook();
        return;
    }
    cvars::SuppressCoverageBuffer();
    cvars::ForceFlashlight();
    if (cvars::FieldOfViewRequested()) {
        void* cam = nullptr;
        float projRatio = 0.0f;
        if (camera::MatrixAddress(g_pSystem.load(std::memory_order_acquire), &cam) != 0 &&
            camera::ReadProjectionRatio(cam, projRatio)) {
            cvars::ApplyFieldOfView(projRatio);
        }
    }
    // Last, so an overridden cl_fov reaches the weapon on the frame it is set
    // rather than the frame after.
    cvars::MatchWeaponFieldOfView();
}

const BuildProfile* CryEngineCamera::ResolveProfile(void* preyDll) {
    cameraunlock::memory::PeFingerprint running{};
    if (!cameraunlock::memory::ReadPeFingerprint(static_cast<HMODULE>(preyDll), running)) {
        // Transient - the module is mapped but its headers are not readable yet.
        // Retrying costs one compare per frame; latching dormancy here would
        // disengage the mod for the whole session over a momentary failure.
        return nullptr;
    }
    if (!m_fpLogged) {
        PHT_LOG(Info, "PreyDll.dll fingerprint: TimeDateStamp=0x%08X SizeOfImage=0x%08X "
                      "CheckSum=0x%08X",
                running.TimeDateStamp, running.SizeOfImage, running.CheckSum);
        m_fpLogged = true;
    }

    const ProfileMatch match = MatchBuildProfile(running);
    if (match.profile != nullptr) return match.profile;

    if (match.refused) {
        PHT_LOG(Warn, "CryEngineCamera: %s. Staying dormant - the game renders vanilla and "
                      "nothing is hooked.", match.reason);
    } else {
        PHT_LOG(Warn, "CryEngineCamera: no usable build profile (%s). Staying dormant - the "
                      "game renders vanilla and nothing is hooked. Newest known profile: %s.",
                match.reason, match.newest_known_name);
    }
    m_hookInstalled = true;  // dormant is terminal for this run
    return nullptr;
}

bool CryEngineCamera::InstallHook() {
    const HMODULE preyDll = GetModuleHandleW(L"PreyDll.dll");
    if (preyDll == nullptr) return false;  // not mapped yet; retry next frame
    const uintptr_t base = reinterpret_cast<uintptr_t>(preyDll);

    const BuildProfile* profile = ResolveProfile(preyDll);
    if (profile == nullptr) return false;

    EngineHandles engine{};
    if (!ResolveEngine(base, *profile, engine)) return false;  // retry next frame

    const auto& cfg = Framework::Get().Cfg();
    BindEngineState(base, *profile, engine);
    BindGameCVars(base, *profile, cfg);
    BindReticle(base, *profile, cfg);
    InstallCameraReaderHook(*profile, cfg, engine.get_view_camera);

    if (!InstallRenderHook(base, *profile, engine.render_fn, &m_tracking)) {
        m_hookInstalled = true;
        return false;
    }
    // The HUD hooks go in first: the focus hook is one of them, and early
    // injection is only safe to install once that is live. The flashlight goes
    // last because it is turned from the detour early injection owns.
    const bool focusHooked = InstallHudHooks(base, *profile, cfg);
    if (cfg.early_inject) InstallEarlyInjectHook(*profile, engine, focusHooked);
    InstallFlashlightHooks(base, *profile, cfg);
    InstallBodyPlacementHook(base, *profile, cfg);

    m_hookInstalled = true;
    return true;
}

void CryEngineCamera::OnShutdown() {
    // Stops the detour touching tracking state after teardown. The hook itself
    // stays armed until HookManager::Shutdown removes it; from here on the detour
    // just forwards to the engine.
    g_hookTracking.store(nullptr, std::memory_order_release);
}

}  // namespace preyht
