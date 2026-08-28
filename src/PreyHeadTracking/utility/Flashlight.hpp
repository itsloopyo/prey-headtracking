#pragma once

#include <cstdint>

#include "CameraMath.hpp"

/// Turning the flashlight beam with the head, faster than the view.
///
/// The beam is an ArkLight component on a child entity of the player. It is not a
/// dynamic light (nothing appears through AddDynamicLightSource when it is
/// switched on) and it never reads the view camera, so neither of the hooks that
/// fix the body or the markers can reach it. What it does have is an entity, and
/// that entity's transform IS the beam.
///
/// It turns 1.5x the head rotation, not 1x, for the reason repo-headtracking
/// gives: a player who turns their head keeps their eyes on what they turned
/// towards, so their gaze sits past the centre of the screen, and a light matched
/// to the view alone lands short of what they are actually looking at.
namespace preyht::flashlight {

/// Per-build addresses and the player's settings, resolved once from the matched
/// build profile.
struct Binding {
    uintptr_t arklight_set_addr        = 0;  ///< the setter that hands over a live component
    uint32_t  arklight_entity_off      = 0;  ///< light entity pointer inside that component
    uintptr_t arklight_vtable          = 0;  ///< the ArkLight class, so a light is identified by type
    uintptr_t entity_set_transform_addr = 0; ///< IEntity::SetPosRotScale
    float     scale                    = 1.5f;
    bool      trace_lights             = false;
    bool      trace_light_reader       = false;
};

/// Hook the ArkLight setter, which is the only handle that hands over a live
/// pointer to one. False leaves the beam vanilla and nothing hooked.
bool Install(const Binding& b);

/// Diagnostic: log each distinct dynamic light once, so a light can be
/// identified by which pointer APPEARS when the beam is switched on.
bool InstallDynamicLightTrace(uintptr_t addDynLightAddr);

/// The clean and head-tracked bases for the frame being drawn. Published by the
/// camera hook; the beam is picked out by sitting on the eye and pointing where
/// the UN-tracked view points, so both are needed and both must be this frame's.
void PublishViewBasis(const Mat34& clean, const Mat34& modified);

/// Turn every head-mounted beam by the head rotation, scaled. Must run during the
/// game's update, not at the top of the render: a rotation written at the top of
/// CSystem::Render survives the whole render untouched and still lights the room
/// in the un-turned direction, because the light's render parameters are already
/// built by then.
void ApplyTracking();

/// Put the game's own beam rotation back once the frame has been drawn with ours.
void RestoreTracking();

}  // namespace preyht::flashlight
