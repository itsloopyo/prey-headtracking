#pragma once

#include <cstdint>

/// Holding three of Prey's own console variables at values the mod needs.
///
/// All three write into a CVar VALUE block rather than going through the console:
/// the console's change handlers clamp, and the engine derives everything else
/// from the stored value anyway. They are written every frame rather than once
/// because each is two or three reads and, once it has taken, no write at all -
/// cheaper than reasoning about when the engine might reset its own CVar.
namespace preyht::cvars {

/// Per-build addresses, resolved once from the matched build profile. A zero
/// offset is how a feature stays off: the caller leaves out what the player did
/// not ask for or the profile does not carry, and nothing here has a second
/// opinion about it.
struct Binding {
    uintptr_t game_block_ptr       = 0;  ///< address of the pointer to the game CVar block
    uintptr_t engine_block_ptr     = 0;  ///< ditto for the 3D engine's block
    uint32_t  coverage_buffer_off  = 0;  ///< e_CoverageBuffer, engine block
    uint32_t  force_flashlight_off = 0;  ///< pl_forceFlashlight, game block
    uint32_t  cl_fov_off           = 0;  ///< cl_fov, vertical degrees, game block
    uint32_t  cl_hfov_off          = 0;  ///< cl_hfov, horizontal degrees, game block
    uintptr_t draw_near_fov_addr   = 0;  ///< r_DrawNearFoV's float, addressed directly
    float     field_of_view        = 0.0f;  ///< horizontal degrees; 0 leaves the slider in charge
};

void Bind(const Binding& b);

/// Switch off Prey's software occlusion culling.
///
/// The coverage buffer is rasterised from a camera snapshot the mod does not
/// reach, so a head-turned view is culled against the un-turned one and geometry
/// is thrown away before it is drawn. Rebuilding the frustum on the engine's view
/// camera does nothing for it, because it is not that camera being read.
void SuppressCoverageBuffer();

/// Force the flashlight on so the beam can be worked on from a save that has not
/// reached one yet. Diagnostic.
void ForceFlashlight();

/// True while a FieldOfView override is configured and reachable. The caller
/// checks this before resolving the live camera, so a session that is not
/// overriding the FOV never pays for the lookup.
bool FieldOfViewRequested();

/// Hold the configured field of view in Prey's own FOV CVars.
///
/// Prey does expose a field of view - a slider in the video options, and the
/// `cl_hfov` (horizontal degrees) / `cl_fov` (vertical degrees) console pair
/// behind it - so this is not a capability the mod adds. What it adds is a value
/// the mod can set without the console, and one the console will not accept: both
/// CVars are clamped to 120 degrees by the change handler that the console runs
/// and a direct write does not.
///
/// The write goes into the CVar value block rather than the camera because the
/// engine derives everything else from it. The player camera reads cl_hfov to
/// build the frustum it hands to CCamera::SetFrustum, so culling, the HUD's own
/// projection and this mod's reticle compensation - which reads m_fov back off
/// the live camera every frame - all follow with nothing further to tell.
///
/// cl_fov is written alongside, not left behind: the weapon's near-object FOV
/// (`r_DrawNearFoV`) is set from cl_fov whenever the game re-evaluates it, so a
/// stale cl_fov renders the first-person weapon at the old field of view while
/// the world is drawn at the new one.
///
/// `projRatio` comes from the live camera, so a resolution change is picked up
/// for free.
void ApplyFieldOfView(float projRatio);

/// Draw the first-person weapon through the same lens as the world.
///
/// Prey renders held weapons and hands in a separate near pass with its own
/// field of view, `r_DrawNearFoV`, which is registered at 55 degrees vertical -
/// the vertical equivalent of the default Field of View slider at 16:9. Raise
/// the slider and the world widens while the weapon keeps the narrower lens, so
/// the weapon is magnified by tan(clFov/2) / tan(nearFov/2) about the centre of
/// the screen. At the maximum slider on a 16:9 display that is 88.5 against 55
/// degrees, a factor of 1.87.
///
/// Nothing shows that magnification while the view and the weapon turn together.
/// Head tracking separates them: the weapon is fixed to the body, so a head turn
/// sweeps it across the screen, and the near lens sweeps it 1.87 times as far as
/// the world behind it. The barrel then points well off the crosshair even
/// though the shot still lands on it.
///
/// Prey's own FOV handler (RVA 0x1726FF0) does exactly this write, gated on a
/// player-state flag that is clear in normal play, and `r_DrawNearFoV` is on the
/// console whitelist at RVA 0x1765D70 alongside cl_fov, so this is a value the
/// game expects to be set. At the default slider it is already equal to cl_fov
/// and this writes nothing.
void MatchWeaponFieldOfView();

}  // namespace preyht::cvars
