#include "BuildProfile.hpp"

#include <array>

namespace preyht {

namespace {

constexpr std::array<BuildProfile, 1> kKnownProfiles{{
    // Steam campaign build, TimeDateStamp 0x5D1CB240 (2019-07-03).
    // gEnv->pSystem      = DAT_18224da60 (RVA 0x224DA60)
    // gEnv->bMultiplayer = DAT_18224db81 (RVA 0x224DB81), the byte returned by
    //                      the "IsMultiplayer" script bind at RVA 0xD27D20.
    // ISystem vtable     = RVA 0x1D9B9C8; +0x048 Render (RVA 0xE0BA30, which
    //                      hands this+0x788 to the 3D engine), +0x388
    //                      GetViewCamera (returns this+0x788), +0x380
    //                      SetViewCamera.
    // CCamera begins with its Matrix34, so the matrix offset is 0. The rest of
    // the struct is laid out by the SetViewCameraFov script bind at RVA 0xD2B420,
    // which writes m_fov at +0x30 (vertical, radians), the width/height ints at
    // +0x38/+0x3C and m_ProjectionRatio at +0x40.
    //
    // Reticle path: RVA 0x1665780 returns the "DanielleHUD" Flash element and
    // 0x11797C0 calls a two-float function on it. Prey's own per-frame reticle
    // update (RVA 0x15ABC40) drives reticlePosition(x, y) through exactly this
    // pair. The base position is a literal 0.5 across and g_reticleYPercentage
    // down, which lives at +0x2D0 in the CVar value block pointed to by the
    // global at RVA 0x2C16830.
    { "steam-win64-20190703", { 0x5D1CB240u, 0x02E1D000u, 0x00000000u },
      0x224DA60u, 0x224DB81u, 0x048u, 0x388u, 0x380u, 0x0u,
      // CCamera::UpdateFrustum, RVA 0x121D70 - the tail call of the
      // SetViewCameraFov script bind, which is CCamera::SetFrustum in all but
      // name. Rebuilds the cached frustum from the matrix and the projection.
      0x121D70u,
      0x30u, 0x40u, 0x1665780u, 0x11797C0u, 0x2C16830u, 0x2D0u,
      // cl_fov at +0x4 and cl_hfov at +0x8 in that same block, read off their
      // registration in the client CVar setup at RVA 0x17180E0. cl_hfov is the
      // horizontal field of view the player camera builds its frustum from; the
      // change handler at RVA 0x1726540 keeps cl_fov as its vertical equivalent
      // and clamps both to 25..120 degrees.
      0x4u, 0x8u,
      // r_DrawNearFoV's float storage, RVA 0x2B1C64C. Taken from the ICVar
      // object the renderer looks up by name: ICVar::GetFVal (vtable +0x20) is
      // `MOV RAX,[RCX+0x48]; MOVSS XMM0,[RAX]`, and the pointer at +0x48 on the
      // live object is this address. The registration at RVA 0xECD2E1 passes the
      // same address and a default of 55 degrees.
      0x2B1C64Cu,
      // RVA 0x1667440 is the per-frame HUD projection update: it reads the view
      // camera through the ISystem vtable (+0x388), builds the projection from
      // its matrix, fov, near/far and projection ratio, and then walks the
      // marker arrays through markerAddArray / markerUpdateArray.
      0x1667440u,
      // RVA 0x1585320 is called from inside that update and is NOT a HUD-only
      // consumer: it reads the view camera and the reticle offset at +0x17EC to
      // resolve what the player is looking at. It gets the clean camera back.
      0x1585320u,
      // The GetViewCamera call site the first-person body's render object is
      // built from, inside the render-object setup at RVA 0x973400. Found by
      // bisecting every camera reader with CleanCameraForReader until the body
      // stopped swinging; it is the return address, so it sits just past the
      // call at 0x97456F. Only this one of that function's reads matters.
      0x974575u,
      // The 3D engine's CVar block. e_CameraFreeze sits at +0x1CC, which is what
      // the coverage-buffer prep reads, and e_CoverageBuffer at +0x314 - both
      // read straight off the registration in the CVar setup at RVA 0x235400.
      0x243A688u, 0x314u, 0x8C4u, 0x212EA0u, 0x14737B0u, 0x38u,
        0x1E405E8u,                                   // ArkLight vtable
        0x90AF90u,                                    // IEntity::SetPosRotScale
      // gEnv->pPhysicalWorld at RVA 0x224D9C8, identified by the 3D engine
      // caching it into its own DAT_18243a670 at RVA 0x2042C0 and by both of
      // the engine's SRWIParams ray call sites loading it. The ray itself is
      // vtable slot +0x118, IPhysicalWorld::RayWorldIntersection(SRWIParams&,
      // nameTag, iCaller) - the struct-parameter overload, which is the one
      // both of those call sites use.
      0x224D9C8u, 0x118u,
      // CEntityObject::Render, RVA 0x973400. Its camera-space branch is where the
      // first-person body - the space suit, in the exterior sections - is placed:
      // with bit 1 of the byte at node+0xAC set and a Vec3 pointer at node+0x68,
      // it turns that offset through the view camera's rotation, writes it as the
      // render object's translation, and forces the object's own rotation to
      // identity. So the pair (camera, offset) is the whole of where the suit
      // lands on screen.
      // The body's rotation comes from the entity's world matrix, through the
      // getter at vtable +0xE8 that the render itself calls.
      0x973400u, 0xACu, 0x68u, 0xE8u },
}};

/// Builds this mod will not touch on purpose, and why.
struct RefusedBuild {
    cameraunlock::memory::PeFingerprint fp;
    const char*                         reason;
};

constexpr std::array<RefusedBuild, 1> kRefusedBuilds{{
    // Prey: Mooncrash / Typhon Hunter, shipped as its own PreyDll.dll under
    // Whiplash\Binaries. Typhon Hunter is Prey's multiplayer mode, where a mod
    // that changes one player's view of a shared world does not belong. Do not
    // turn this into a build profile: it would need its gEnv->bMultiplayer
    // address re-derived for this binary and the multiplayer check proven in an
    // actual match first, and neither has been done.
    { { 0x5D2352B3u, 0x02FB9000u, 0x00000000u },
      "this is the Prey: Mooncrash / Typhon Hunter build. Typhon Hunter is "
      "multiplayer, so head tracking deliberately does not run on it" },
}};

}  // namespace

ProfileMatch MatchBuildProfile(const cameraunlock::memory::PeFingerprint& running) {
    // The top of kKnownProfiles is the diagnostic primary: it is the newest build
    // the mod knows about, so it is what an unrecognised build is compared
    // against to say which side of it the player is on.
    const BuildProfile& primary = kKnownProfiles.front();
    ProfileMatch out;
    out.newest_known_name = primary.name;

    for (const auto& refused : kRefusedBuilds) {
        if (running.Matches(refused.fp)) {
            out.reason  = refused.reason;
            out.refused = true;
            return out;
        }
    }

    for (const auto& p : kKnownProfiles) {
        if (!running.Matches(p.fp)) continue;
        if (p.IsComplete()) {
            out.profile = &p;
            return out;
        }
        out.reason = "this build has a placeholder profile whose addresses have not been "
                     "derived yet";
        return out;
    }

    out.reason =
        running.TimeDateStamp > primary.fp.TimeDateStamp
            ? "this build is NEWER than any this mod knows about - check the releases page "
              "for an updated mod"
        : running.TimeDateStamp < primary.fp.TimeDateStamp
            ? "this build is OLDER than any this mod knows about - let Steam finish updating"
            : "same build date but a different size or checksum - a repacked or modified "
              "PreyDll.dll, which this mod will not engage on";
    return out;
}

}  // namespace preyht
