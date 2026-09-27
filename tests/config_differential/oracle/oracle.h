#pragma once

#include <cstdint>
#include <string>
#include <vector>

// The published build's config reader and startup, as the differential test sees
// them. The oracle library compiles published/ with its namespaces renamed, so
// this header names nothing from it: every type here is plain.

namespace prey_config_oracle {

// One registered hotkey: a virtual-key code and the modifiers its guard requires,
// as cameraunlock::input::KeyModifiers numbers them (Ctrl 1, Shift 2).
struct Binding {
    unsigned modifiers = 0;
    int vk = 0;
};

struct Result {
    // preyht::Config as the published LoadFromFile and Sanitize left it.
    uint16_t udp_port = 0;
    float yaw_sens = 0, pitch_sens = 0, roll_sens = 0;
    bool invert_yaw = false, invert_pitch = false, invert_roll = false;
    float local_smoothing = 0, remote_smoothing = 0, deadzone = 0;
    std::string toggle_key, yaw_mode_key, position_key;
    bool world_space_yaw = false;
    bool compensate_reticle = false;
    bool compensate_markers = false;
    float field_of_view = 0;
    bool match_weapon_fov = false;
    bool dump_camera = false;
    bool early_inject = false;
    bool disable_coverage_buffer = false;
    bool compensate_body = false;
    bool body_follows_head = false;
    bool hide_body_nodes = false;
    bool trace_body_nodes = false;
    bool force_flashlight = false;
    bool compensate_flashlight = false;
    float flashlight_scale = 0;
    bool trace_lights = false;
    bool trace_light_reader = false;
    bool trace_camera_readers = false;
    uint64_t clean_camera_for_reader = 0;
    uint64_t clean_camera_reader_end = 0;
    bool position_enabled = false;
    float pos_sens_x = 0, pos_sens_y = 0, pos_sens_z = 0;
    bool invert_pos_x = false, invert_pos_y = false, invert_pos_z = false;
    float pos_limit_x = 0, pos_limit_y = 0, pos_limit_z = 0, pos_limit_z_back = 0;
    float pivot_forward = 0, pivot_up = 0;
    bool log_to_file = false;
    std::string log_path;
    std::vector<std::string> load_notes;
    std::vector<std::string> sanitize_notes;

    // What the published startup made of it.
    bool tracking_enabled = false;
    // cameraunlock::TrackingMode's numbers: 0 rotation and position, 1 rotation only.
    int tracking_mode = 0;
    bool world_yaw = false;
    std::vector<Binding> toggle;
    std::vector<Binding> cycle_tracking_mode;
    std::vector<Binding> yaw_mode;
    std::vector<Binding> body_follows_head_key;
    std::vector<Binding> cycle_tracker_source;
};

// The published startup on the file at @p path: LoadFromFile into a default
// Config, Sanitize, then the startup state and the registered hotkeys.
Result Startup(const std::string& path);

}  // namespace prey_config_oracle
