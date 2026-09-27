#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/config_table.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/config/legacy_import.h"
#include "cameraunlock/data/position_settings.h"
#include "cameraunlock/effects/head_follow_light.h"
#include "cameraunlock/math/smoothing_utils.h"
#include "cameraunlock/tracking/tracking_mode.h"

namespace preyht {

/// CameraUnlock.ini beside Prey.exe, in cameraunlock-core's canonical format.
/// ConfigOwner is its one reader and writer; ConfigTableFor() lists its rows.
///
/// There is deliberately no sensitivity, inversion, deadzone or neck pivot here.
/// The tracker owns pose shaping, and a backwards axis is a boundary-conversion
/// bug to fix in the camera code, not a knob to hand the player.
struct Config {
    int  udp_port = 4242;
    bool enable_on_startup = true;

    // true = yaw about the world up axis (horizon-locked); false = yaw about the
    // camera's own up axis. The space suit is always head-relative whatever this
    // says, because floating has no stable up.
    bool world_space_yaw = true;

    // The tracking mode at startup, as the pair the mode hotkey saves.
    bool rotation_enabled = true;
    bool position_enabled = true;

    // Two smoothing parameters, picked per connection from the packet source
    // address. Both cover rotation and position.
    float local_smoothing  = static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing);
    float remote_smoothing = static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing);

    // Metres. CryEngine world units are metres too, so the offset applies 1:1.
    float limit_x      = cameraunlock::PositionSettings{}.limit_x;
    float limit_y      = cameraunlock::PositionSettings{}.limit_y;
    float limit_y_down = cameraunlock::PositionSettings{}.limit_y_down;
    float limit_z      = cameraunlock::PositionSettings{}.limit_z;
    float limit_z_back = cameraunlock::PositionSettings{}.limit_z_back;

    // Turn the flashlight beam with the head, faster than the view, so it lands
    // on what you turned to look at rather than short of it. The number and the
    // reasoning are the fleet's - see cameraunlock/effects/head_follow_light.h.
    bool  compensate_flashlight = true;
    float flashlight_scale      = cameraunlock::effects::kDefaultLightMultiplier;

    // Hotkey lists as the canonical format writes them.
    std::string toggle_key              = "End, Ctrl+Shift+Y";
    std::string cycle_tracking_mode_key = "PageUp, Ctrl+Shift+G";
    std::string yaw_mode_key            = "PageDown, Ctrl+Shift+H";
    std::string cycle_tracker_source_key = "Ctrl+Shift+U";
    std::string body_follows_head_key    = "Delete, Ctrl+Shift+J";

    // Project Prey's world-anchored HUD markers through the head-tracked view so
    // they stay on their objects.
    bool compensate_markers = true;

    // Horizontal field of view in degrees, written into Prey's own cl_hfov (and
    // the vertical cl_fov derived from it). 0 leaves the game's Field of View
    // slider in charge.
    float field_of_view = 0.0f;

    // Draw the first-person weapon through the same lens as the world.
    bool match_weapon_fov = true;

    // Inject the head pose when the game sets the view camera rather than only
    // for the render, so culling and the HUD see it too.
    bool early_inject = true;

    // Turn off Prey's software occlusion culling, which is rasterised from a
    // camera the mod cannot reach.
    bool disable_coverage_buffer = true;

    // Take charge of the camera the first-person body's render object is built
    // from.
    bool compensate_body = true;

    // Carry the first-person body with the head while the space suit is on. Its
    // hotkey saves it.
    bool body_follows_head = true;

    // Diagnostics, all off for play.
    bool dump_camera = false;
    bool trace_body_nodes = false;
    bool hide_body_nodes = false;
    bool force_flashlight = false;
    bool trace_lights = false;
    bool trace_light_reader = false;
    bool trace_camera_readers = false;
    uint64_t clean_camera_for_reader = 0;
    uint64_t clean_camera_reader_end = 0;

    bool log_to_file = true;
    std::string log_path = "HeadTracking.log";

    cameraunlock::PositionSettings AsPositionSettings() const {
        cameraunlock::PositionSettings p;
        p.limit_x      = limit_x;
        p.limit_y      = limit_y;
        p.limit_y_down = limit_y_down;
        p.limit_z      = limit_z;
        p.limit_z_back = limit_z_back;
        // Position shares the rotation smoothing parameters; the connection flag
        // that picks between them lives on the processor, not here.
        p.local_smoothing  = local_smoothing;
        p.remote_smoothing = remote_smoothing;
        return p;
    }

    /// Resolve a relative log path against the executable's directory, so the
    /// log lands next to the config instead of wherever the game set its CWD.
    static std::string ResolveLogPath(const std::string& configured);
};

/// The rows of CameraUnlock.ini. The mode pair, WorldSpaceYaw and BodyFollowsHead
/// are Writable: their hotkeys save them. EnableOnStartup is not, so End never
/// reaches the file.
cameraunlock::config::ConfigTable<Config> ConfigTableFor();

/// The display name the file's header names the game by, as data/games.json
/// spells it.
extern const char* const kGameDisplayName;

/// The frozen reader in legacy_config/ as the owner's import: it reads the
/// HeadTracking.ini an older build read and maps it into Config.
cameraunlock::config::LegacyImport<Config> LegacyImportFor();

/// The owner of CameraUnlock.ini in @p directory, a full path, with the
/// HeadTracking.ini every earlier build read beside it as the legacy file, which
/// it imports once while CameraUnlock.ini is absent and never writes. @p defaults
/// is where Defaults.ini is: the player's own in the mod, a scratch file in a
/// test. @p status_sink shows the player's one-line messages, or is empty.
cameraunlock::config::ConfigOwnerOptions<Config> OwnerOptions(
    const std::wstring& directory, cameraunlock::config::DefaultsFile defaults,
    std::function<void(const std::string&)> status_sink = {});

/// The tracking mode the session starts in.
cameraunlock::TrackingMode StartupMode(const Config& config);

/// The folder Prey.exe runs from, as a full path.
std::wstring ExeDirectory();

}  // namespace preyht
