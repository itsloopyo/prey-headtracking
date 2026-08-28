#pragma once

#include <cstdint>

namespace preyht::hud {

/// Per-build addresses for Prey's Flash HUD reticle path. All zero until the
/// camera hook matches a build profile and binds them.
struct Binding {
    uintptr_t get_element  = 0;  ///< IUIElement* (*)() - fetches the "DanielleHUD" element
    uintptr_t call_fn_2f   = 0;  ///< void (*)(IUIElement*, const char* fn, float, float)
    uintptr_t config_ptr   = 0;  ///< address of the pointer to the game's CVar value block
    uint32_t  reticle_y_off = 0; ///< g_reticleYPercentage within that block
};

void Bind(const Binding& b);

/// Where Prey would draw the reticle with no head tracking, in normalized
/// screen coordinates (x right, y down, both 0..1). x is a fixed 0.5; y follows
/// the player's g_reticleYPercentage setting. False if the engine state is not
/// readable yet.
bool BasePosition(float& x, float& y);

/// Move Prey's own reticle. Does nothing when the position is unchanged, so a
/// still head costs no engine calls.
void SetPosition(float x, float y);

/// Hand the reticle back to the game, once. No-op if we never moved it.
void Restore();

}  // namespace preyht::hud
