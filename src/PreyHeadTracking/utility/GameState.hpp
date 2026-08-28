#pragma once

namespace preyht {

/// True while the player is actually playing, as opposed to sitting in a menu,
/// on a loading screen, or in another window.
///
/// Prey takes the mouse for gameplay - the OS cursor is hidden AND clipped to the
/// game window - and gives it back for every state that stops play: the main
/// menu, the pause menu, the TranScribe, the inventory, the options and save/load
/// screens. One test therefore covers all of them, and unlike an engine flag it
/// carries no per-build address to re-derive after a patch.
///
/// Result is cached briefly, so callers on the render path may ask every frame.
bool InActiveGameplay();

}  // namespace preyht
