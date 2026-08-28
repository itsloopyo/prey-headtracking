#pragma once

#include <cstdint>

namespace preyht::watch {

/// Catch the next thread that WRITES to `address`, whichever thread it is.
///
/// Writing the beam's transform and watching the wall could not answer which code
/// aims the light - the writes landed, survived the render, and changed nothing,
/// and perturbing candidate matrices to find the reader crashed the game. Finding
/// the writer answers it directly.
///
/// Debug registers were tried first and caught nothing: they are per-thread, and
/// arming the thread the engine updates on misses a write that happens elsewhere.
/// Removing write access from the page catches it wherever it lives. One shot:
/// the page is made writable again as soon as the first writer is logged, so the
/// game runs on normally.
///
/// Suppress the mod's own write while tracing or it trips its own trap.
bool ArmPageWrite(uintptr_t address);

}  // namespace preyht::watch
