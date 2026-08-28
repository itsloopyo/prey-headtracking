#pragma once

#include <cstdint>

namespace preyht::aim {

/// Per-build addresses for CryEngine's physics ray. Zero until the camera hook
/// matches a build profile and binds them.
struct Binding {
    uintptr_t physical_world_ptr = 0;  ///< address of gEnv->pPhysicalWorld
    uint32_t  rwi_vtable_off     = 0;  ///< IPhysicalWorld::RayWorldIntersection(SRWIParams&, ...)
};

void Bind(const Binding& b);

/// How far away the thing the gun is pointing at is, in metres.
///
/// `org` is the world position the shot leaves from and `dir` a unit vector
/// along the aim. Returns false when the query is not bound, when the ray hit
/// nothing inside its range, or when the call faulted - in every one of those
/// cases the caller has no impact point and must fall back to projecting the
/// aim direction, which is the right answer for a target at infinity.
bool Distance(const float org[3], const float dir[3], float& outMetres);

}  // namespace preyht::aim
