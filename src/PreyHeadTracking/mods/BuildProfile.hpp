#pragma once

#include <cstdint>

#include "cameraunlock/memory/pe_fingerprint.h"

namespace preyht {

/// Per-build profile for reaching the engine through the global ISystem.
///
/// Everything the hook needs is resolved at runtime from that one pointer plus
/// vtable slot offsets, so a patch that only relinks function bodies changes
/// nothing here. Append-only across Prey patches (see "Maintain compatibility
/// across new patches"): add a profile, never edit one, so a player who has not
/// taken the patch keeps working.
///
/// Campaign builds only - see the refused-build list.
struct BuildProfile {
    const char*                         name;
    cameraunlock::memory::PeFingerprint fp;
    uint64_t                            system_ptr_rva;          // gEnv->pSystem
    uint64_t                            multiplayer_flag_rva;    // gEnv->bMultiplayer
    uint32_t                            render_vtable_off;       // ISystem::Render
    uint32_t                            view_camera_vtable_off;  // ISystem::GetViewCamera
    uint32_t                            set_view_camera_vtable_off;  // ISystem::SetViewCamera
    uint32_t                            matrix_offset;           // Matrix34 within the CCamera
    uint64_t                            update_frustum_rva;      // CCamera::UpdateFrustum
    uint32_t                            camera_fov_off;          // CCamera::m_fov, vertical, radians
    uint32_t                            camera_proj_ratio_off;   // CCamera::m_ProjectionRatio
    uint64_t                            hud_element_rva;         // fetches the "DanielleHUD" element
    uint64_t                            hud_call2f_rva;          // CallFunction(element, name, float, float)
    uint64_t                            game_config_ptr_rva;     // pointer to the CVar value block
    uint32_t                            reticle_y_percent_off;   // g_reticleYPercentage within it
    uint32_t                            cl_fov_off;              // cl_fov, vertical degrees, same block
    uint32_t                            cl_hfov_off;             // cl_hfov, horizontal degrees, same block
    uint64_t                            draw_near_fov_rva;       // r_DrawNearFoV's own float, the first-person weapon's lens
    uint64_t                            hud_projection_rva;      // per-frame HUD marker projection
    uint64_t                            focus_update_rva;        // reads the camera to pick the focus target
    uint64_t                            body_camera_reader_rva;  // the GetViewCamera call site the body is built from
    uint64_t                            cvars_ptr_rva;           // pointer to the 3D engine CVar block
    uint32_t                            coverage_buffer_off;     // e_CoverageBuffer within it
    uint32_t                            force_flashlight_off;    // pl_forceFlashlight in the GAME CVar block
    uint64_t                            add_dyn_light_rva;       // C3DEngine::AddDynamicLightSource
    uint64_t                            arklight_set_rva;        // ArkLight setter, hands over a live component
    uint32_t                            arklight_entity_off;     // the light entity pointer inside that component
    uint64_t                            arklight_vtable_rva;     // the ArkLight class itself, so a light is identified by type
    uint64_t                            entity_set_transform_rva;// IEntity::SetPosRotScale, the only way a transform change is noticed
    uint64_t                            physical_world_ptr_rva;  // gEnv->pPhysicalWorld
    uint32_t                            rwi_vtable_off;          // IPhysicalWorld::RayWorldIntersection
    uint64_t                            entity_render_rva;       // CEntityObject::Render, where a camera-space node is placed
    uint32_t                            node_flags_off;          // the byte carrying the camera-space bit
    uint32_t                            node_cam_offset_ptr_off; // -> Vec3, the node's camera-space offset
    uint32_t                            entity_world_tm_vtable_off; // IEntity's world matrix getter

    /// A profile landed as a placeholder the moment a patch was spotted, before
    /// its addresses were re-derived, must not activate. Everything here is
    /// non-zero on a real build except matrix_offset, which is legitimately 0.
    constexpr bool IsComplete() const {
        return system_ptr_rva != 0 && multiplayer_flag_rva != 0 &&
               render_vtable_off != 0 && view_camera_vtable_off != 0 &&
               update_frustum_rva != 0;
    }

    /// Reticle compensation is a separate gate from IsComplete: a profile added
    /// for a new patch can carry the camera addresses before anyone has re-derived
    /// the HUD ones, and head tracking should run on it in the meantime.
    constexpr bool HasReticlePath() const {
        return camera_fov_off != 0 && camera_proj_ratio_off != 0 && hud_element_rva != 0 &&
               hud_call2f_rva != 0 && game_config_ptr_rva != 0 && reticle_y_percent_off != 0;
    }

    /// Gated separately again, for the same reason: a profile for a new patch can
    /// carry the camera addresses before this one has been re-derived.
    constexpr bool HasHudProjection() const {
        return hud_projection_rva != 0 && focus_update_rva != 0;
    }

    /// Gated separately again. Without the aim ray the reticle still follows the
    /// head's ROTATION correctly; only the parallax a lean introduces is left
    /// uncorrected, which is bounded and shrinks with distance.
    constexpr bool HasAimTrace() const {
        return physical_world_ptr_rva != 0 && rwi_vtable_off != 0;
    }

    /// Gated separately again. Without these the beam still lights the room, it
    /// just does not turn with the head.
    constexpr bool HasFlashlight() const {
        return arklight_set_rva != 0 && arklight_entity_off != 0;
    }

    /// The first-person body's placement, which is where the space suit lives.
    /// Without these the body still renders; it just cannot be moved off the
    /// camera-space offset the game gave it.
    constexpr bool HasBodyPlacement() const {
        return entity_render_rva != 0 && node_flags_off != 0 &&
               node_cam_offset_ptr_off != 0 && entity_world_tm_vtable_off != 0;
    }
};

/// What the running PreyDll.dll resolved to, and - when it resolved to nothing -
/// a line that says why in terms the player can act on.
struct ProfileMatch {
    const BuildProfile* profile = nullptr;  ///< null means stay dormant
    const char*         reason  = nullptr;  ///< null when a profile matched
    const char*         newest_known_name = nullptr;
    /// True when the build was recognised and deliberately declined, as opposed
    /// to simply not being known. The two want different wording: pointing a
    /// Mooncrash player at the releases page would send them after an update
    /// that is never coming.
    bool                refused = false;
};

/// Match a fingerprint against the refused builds first, then the known
/// profiles. Refused builds are checked first so the dormant log line tells the
/// truth: the Mooncrash binary's TimeDateStamp is newer than the campaign's, so
/// without that check it is reported as "newer than any build this mod knows
/// about - check for an update", which invites exactly the wrong fix.
ProfileMatch MatchBuildProfile(const cameraunlock::memory::PeFingerprint& running);

}  // namespace preyht
