#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cameraunlock/data/tracking_pose.h"
#include "cameraunlock/data/position_settings.h"

namespace preyht {

/// Runtime configuration. Loaded from `HeadTracking.ini` next to the game
/// executable at DLL attach time. All fields have sane defaults so a missing
/// INI is fine.
struct Config {
    // [Network]
    uint16_t udp_port      = 4242;

    // [Tracking]
    float    yaw_sens      = 1.0f;
    float    pitch_sens    = 1.0f;
    float    roll_sens     = 1.0f;
    bool     invert_yaw    = false;
    bool     invert_pitch  = false;
    bool     invert_roll   = false;
    // Smoothing is chosen per connection: local for a tracker on this machine
    // (loopback), remote for a device on the network. Both cover rotation and
    // position.
    float    local_smoothing  = 0.0f;
    float    remote_smoothing = 0.15f;
    float    deadzone      = 0.0f;

    // [Hotkeys] - nav-cluster virtual-key names matched by hotkey_poller.
    // Ctrl+Shift+Y/G/H chord equivalents are registered unconditionally in code.
    std::string toggle_key    = "End";
    std::string yaw_mode_key  = "PageDown";  // toggle world-space vs camera-local yaw
    std::string position_key  = "PageUp";    // cycle 6DOF / rotation-only / position-only

    // [Camera]
    // Yaw axis. true (default) = horizon-locked: head-yaw rotates around the
    //   world up-axis regardless of camera pitch. false = camera-local: head-yaw
    //   rotates around the camera's current up-axis. Runtime-toggled by yaw_mode_key.
    bool world_space_yaw = true;

    // Move Prey's own crosshair to where the mouse-controlled aim lands in the
    // head-tracked image, so a shot goes where the crosshair is drawn. Off leaves
    // it pinned to the centre of the screen, which stops meaning "where the gun
    // points" as soon as the head turns.
    bool compensate_reticle = true;

    // Project Prey's world-anchored HUD markers through the head-tracked view
    // so they stay on their objects instead of sitting still on screen while
    // the head is turned. Hooks the HUD's own per-frame projection, which feeds
    // nothing but the HUD, so aim stays decoupled.
    bool compensate_markers = false;

    // Horizontal field of view in degrees, written into Prey's own cl_hfov (and
    // the vertical cl_fov derived from it). 0 leaves the game's Field of View
    // slider in charge. Set here, it wins over the slider for the whole session.
    float field_of_view = 0.0f;

    // Draw the first-person weapon through the same lens as the world. Prey
    // renders held weapons in a near pass fixed at 55 degrees vertical, so
    // raising the Field of View slider magnifies the weapon relative to the
    // scene and a head turn sweeps it further across the screen than the world.
    bool match_weapon_fov = true;

    // Diagnostic (read-only): once the hook is live, log the view-camera matrix
    // periodically so the matrix layout can be confirmed in-game. Off for play.
    bool dump_camera = false;

    // Inject the head pose when the game sets the view camera rather than only
    // for the render, so culling and the HUD see it too. Requires the focus hook
    // to keep aim on the mouse; without that it aims the interaction ray with the
    // head.
    bool early_inject = true;

    // Turn off Prey's software occlusion culling. It is rasterised from a camera
    // snapshot the mod cannot reach, so it culls a head-turned view against the
    // un-turned one and geometry disappears at the edge of a head turn.
    bool disable_coverage_buffer = true;

    // Take charge of the camera the first-person body's render object is built
    // from. Off, Prey builds it from the head-tracked camera and then renders
    // through that camera again, so the body swings across the screen at twice
    // the head's rotation.
    bool compensate_body = true;

    // Carry the first-person body with the head while it is drawn, so the space
    // suit turns with you instead of staying where the character is facing. Only
    // ever applied while the suit is on - Prey draws the held item as part of the
    // same object, so on foot it would take the gun with it. Delete or
    // Ctrl+Shift+J turns the whole thing off in game.
    bool body_follows_head = true;

    // Diagnostic: draw nothing for the body's node, so a shot of the world
    // without it subtracts to the suit's exact silhouette.
    bool hide_body_nodes = false;

    // Diagnostic: sample the camera-space render node the body is drawn from,
    // with the offset vector it is placed from.
    bool trace_body_nodes = false;

    // Diagnostic: force the flashlight on regardless of save progress, so the
    // beam can be worked on from an early save.
    bool force_flashlight = false;

    // Turn the flashlight beam with the head, faster than the view, so it lands
    // on what you turned to look at rather than short of it.
    bool  compensate_flashlight = true;
    float flashlight_scale      = 1.5f;

    // Diagnostic: log each ArkLight the engine hands over, and arm a one-shot
    // page watch on the first head-mounted beam to catch whatever writes its
    // rotation.
    bool trace_lights = false;
    bool trace_light_reader = false;

    // Diagnostic: log each distinct caller of GetViewCamera once, as an RVA, to
    // find which engine code reads the camera and when.
    bool trace_camera_readers = false;

    // Diagnostic: RVA of one GetViewCamera caller that should receive the clean
    // camera instead of the head-tracked one. Used to find which engine code
    // positions the first-person body. 0 = nobody.
    uint64_t clean_camera_for_reader = 0;
    // Upper bound for a range of reader RVAs, for bisecting. 0 = exact match.
    uint64_t clean_camera_reader_end = 0;

    // [Position] - 6DOF positional tracking. Decoupled like rotation: the head
    // offset is added to the rendered camera position only. The wire carries
    // centimetres and the core converts to metres, which is what these limits
    // are in; CryEngine world units are metres too, so the offset applies 1:1.
    bool     position_enabled = true;
    float    pos_sens_x       = 1.0f;
    float    pos_sens_y       = 1.0f;
    float    pos_sens_z       = 1.0f;
    bool     invert_pos_x     = false;
    bool     invert_pos_y     = false;
    bool     invert_pos_z     = false;
    float    pos_limit_x      = 0.30f;
    float    pos_limit_y      = 0.20f;
    float    pos_limit_z      = 0.40f;   // forward lean (generous)
    float    pos_limit_z_back = 0.10f;   // backward lean (restricted, avoids clipping)
    // Lever from the head's rotation pivot (in the neck) to the point the tracker
    // watches, in metres. Subtracts the translation that is only the head turning.
    // 0 disables it. See PivotForward / PivotUp in HeadTracking.ini.
    float    pivot_forward    = 0.0f;
    float    pivot_up         = 0.0f;

    // [Logging]
    bool log_to_file = true;
    std::string log_path;  // resolved at runtime if empty

    /// Problems found while reading the INI - a key the mod no longer honours, a
    /// value it cannot use - phrased for the player. Filled during the load and
    /// logged once the log file is open, because the config is read first.
    std::vector<std::string> load_notes;

    cameraunlock::SensitivitySettings AsSensitivity() const {
        cameraunlock::SensitivitySettings s;
        s.yaw          = yaw_sens;
        s.pitch        = pitch_sens;
        s.roll         = roll_sens;
        s.invert_yaw   = invert_yaw;
        s.invert_pitch = invert_pitch;
        s.invert_roll  = invert_roll;
        return s;
    }

    cameraunlock::DeadzoneSettings AsDeadzone() const {
        cameraunlock::DeadzoneSettings d;
        d.yaw = d.pitch = d.roll = deadzone;
        return d;
    }

    cameraunlock::PositionSettings AsPositionSettings() const {
        cameraunlock::PositionSettings p;
        p.sensitivity_x = pos_sens_x;
        p.sensitivity_y = pos_sens_y;
        p.sensitivity_z = pos_sens_z;
        p.invert_x      = invert_pos_x;
        p.invert_y      = invert_pos_y;
        p.invert_z      = invert_pos_z;
        p.limit_x       = pos_limit_x;
        // The clamp is [-limit_y_down, +limit_y]. The INI exposes one vertical
        // limit, so mirror it rather than leaving the downward budget on the
        // core's default and silently decoupled from the configured value.
        p.limit_y       = pos_limit_y;
        p.limit_y_down  = pos_limit_y;
        p.limit_z       = pos_limit_z;
        p.limit_z_back  = pos_limit_z_back;
        // Position shares the rotation smoothing parameters; the connection flag
        // that picks between them lives on the processor, not here.
        p.local_smoothing  = local_smoothing;
        p.remote_smoothing = remote_smoothing;
        return p;
    }

    /// Load from an INI file. Missing fields keep their defaults.
    /// Returns false only if the file exists but cannot be parsed.
    static bool LoadFromFile(const std::string& path, Config& out);

    /// Force every numeric field back into the range the mod documents, and
    /// report what was changed. The INI is a system boundary: strtod accepts
    /// "inf" and "nan", and a non-finite sensitivity turns the whole view matrix
    /// into NaN the moment it is written into the live engine camera.
    std::vector<std::string> Sanitize();

    /// Locate the INI next to the host process's executable.
    static std::string DefaultIniPathNextToHostExe();

    /// Resolve a relative log path against the executable's directory, so the
    /// log lands next to the INI instead of wherever the game set its CWD.
    static std::string ResolveLogPath(const std::string& configured);
};

}  // namespace preyht
