#pragma once

#include <cstdint>

#include "CameraMath.hpp"

/// Reaching and writing the one CCamera the engine draws with.
///
/// Prey keeps a single view camera inside CSystem and hands it to everything, so
/// every hook in this mod ends up doing the same four things: resolve it through
/// the ISystem vtable, read its frustum parameters, write a matrix into it, and
/// put the clean matrix back afterwards. Those live here rather than being
/// repeated per detour, because the last two are the ones that go subtly wrong -
/// a matrix written without rebuilding the cached frustum culls the world against
/// the un-turned camera, and a restore skipped on an exception compounds instead
/// of correcting itself.
namespace preyht::camera {

/// Both engine calls are non-static members taking no arguments beyond `this`,
/// so under the MS x64 ABI the object is the only parameter and arrives in RCX.
using GetViewCameraFn = void*(__fastcall*)(void* pSystem);
using UpdateFrustumFn = void(__fastcall*)(void* camera);
/// CSystem::Render and Prey's HUD projection update take one argument each - the
/// object - so a single signature covers both detours and the restore helper.
using MemberVoidFn    = void(__fastcall*)(void* self);

/// Per-build addresses, resolved once from the matched build profile. Zero until
/// the camera hook binds them.
struct Binding {
    GetViewCameraFn get_view_camera   = nullptr;
    UpdateFrustumFn update_frustum    = nullptr;
    uint32_t        matrix_offset     = 0;  ///< Matrix34 within the CCamera
    uint32_t        fov_offset        = 0;  ///< m_fov, VERTICAL, radians
    uint32_t        proj_ratio_offset = 0;  ///< m_ProjectionRatio
};

void Bind(const Binding& b);

/// Address of the engine's live view-camera matrix, or 0 if it is not reachable.
/// `outCamera` receives the CCamera itself when non-null.
uintptr_t MatrixAddress(void* pSystem, void** outCamera);

/// The live camera's projection ratio, `(width / pixelAspect) / height`. False
/// when it is unreadable or not positive - a frustum the engine has not set up
/// yet reads as zero, and everything downstream divides by it.
bool ReadProjectionRatio(void* camera, float& projRatio);

/// Vertical FOV in radians alongside the projection ratio, straight off the live
/// camera. False when either is unreadable or outside what the engine will
/// accept - dividing by the tangent of a zero FOV throws the reticle to infinity.
bool ReadFrustum(void* camera, float& fovRadians, float& projRatio);

/// Install a matrix into the engine's camera and rebuild what it caches from it.
///
/// Writing the Matrix34 is not enough. CCamera keeps the frustum it implies in
/// derived fields (+0x7c..+0xa0), rebuilt by CCamera::UpdateFrustum, and the
/// engine culls against THOSE, not against the matrix. Write the matrix alone and
/// the world is drawn through the head-tracked camera while it is culled against
/// the un-turned one, so scenery at the edge of a head turn is thrown away before
/// it is drawn - geometry visibly popping out of the view exactly when the head
/// moves off aim.
///
/// A basis that is not finite is refused rather than written: the pose comes off
/// a socket bound to every interface, and the engine culls and draws from this
/// matrix and the frustum it rebuilds from it.
///
/// Aiming the frustum this way is the whole fix, and it is the right one: the
/// alternative of widening the culling cone was tried in titanfall-2-headtracking
/// and rejected in game - it flickered and still culled on a super ultrawide,
/// because the frustum grows in TANGENT, so on a very wide monitor the widening
/// needed is enormous while the head turn needs none of it. Aiming costs nothing
/// and has no angular limit.
bool WriteMatrix(uintptr_t mtxAddr, void* camera, const Mat34& m);

/// Run an engine function with `restore` guaranteed to go back into the camera.
///
/// Without the guarantee, an exception unwinding out of the callee would leave
/// the head-rotated matrix in the engine's camera for the next game update, and
/// the frame after that would read it back as "clean" and rotate it again - the
/// error compounds instead of correcting itself.
void CallThenRestore(MemberVoidFn orig, void* self, uintptr_t mtxAddr, void* camera,
                     const Mat34& restore);

}  // namespace preyht::camera
