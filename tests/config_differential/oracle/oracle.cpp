// Compiled into prey_config_oracle only, with `cameraunlock` and `preyht` renamed
// by the preprocessor (tests/config_differential/CMakeLists.txt), so the published
// reader and the core sources it built against sit beside the current ones in one
// test binary without a symbol in common.

#include "oracle.h"

#include <cctype>
#include <cstdlib>

#include "preyht/Config.hpp"

namespace prey_config_oracle {

namespace {

// e87ed9e:src/PreyHeadTracking/mods/HeadTracking.cpp ParseVk, with the VK::
// constants of core ee8cc72's hotkey_poller.h spelled as their values.
int ParseVk(const std::string& name) {
    if (name.empty()) return 0;

    std::string n;
    n.reserve(name.size());
    for (char c : name) n.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return n[0];

    if (std::isdigit(static_cast<unsigned char>(n[0]))) {
        int v = static_cast<int>(std::strtol(n.c_str(), nullptr, 0));
        if (v > 0 && v <= 0xFF) return v;
    }
    if (n.size() >= 2 && n[0] == 'F' && std::isdigit(static_cast<unsigned char>(n[1]))) {
        int idx = std::atoi(n.c_str() + 1);
        if (idx >= 1 && idx <= 24) return 0x6F + idx;
    }
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return n[0];
    if (n == "HOME")     return 0x24;
    if (n == "END")      return 0x23;
    if (n == "INSERT")   return 0x2D;
    if (n == "DELETE")   return 0x2E;
    if (n == "SPACE")    return 0x20;
    if (n == "PAGEUP")   return 0x21;
    if (n == "PAGEDOWN") return 0x22;
    if (n == "ESCAPE" || n == "ESC") return 0x1B;
    return 0;
}

// ChordGuarded requires Ctrl and Shift; NavGuarded requires neither.
constexpr unsigned kChord = 1u | 2u;

std::vector<Binding> NavThenChord(int vk, int letter) {
    std::vector<Binding> out;
    if (vk != 0) out.push_back(Binding{0, vk});
    out.push_back(Binding{kChord, letter});
    return out;
}

}  // namespace

Result Startup(const std::string& path) {
    // e87ed9e:src/PreyHeadTracking/Framework.cpp DoInitialize.
    preyht::Config c;
    preyht::Config::LoadFromFile(path, c);
    Result r;
    r.sanitize_notes = c.Sanitize();

    r.udp_port = c.udp_port;
    r.yaw_sens = c.yaw_sens;
    r.pitch_sens = c.pitch_sens;
    r.roll_sens = c.roll_sens;
    r.invert_yaw = c.invert_yaw;
    r.invert_pitch = c.invert_pitch;
    r.invert_roll = c.invert_roll;
    r.local_smoothing = c.local_smoothing;
    r.remote_smoothing = c.remote_smoothing;
    r.deadzone = c.deadzone;
    r.toggle_key = c.toggle_key;
    r.yaw_mode_key = c.yaw_mode_key;
    r.position_key = c.position_key;
    r.world_space_yaw = c.world_space_yaw;
    r.compensate_reticle = c.compensate_reticle;
    r.compensate_markers = c.compensate_markers;
    r.field_of_view = c.field_of_view;
    r.match_weapon_fov = c.match_weapon_fov;
    r.dump_camera = c.dump_camera;
    r.early_inject = c.early_inject;
    r.disable_coverage_buffer = c.disable_coverage_buffer;
    r.compensate_body = c.compensate_body;
    r.body_follows_head = c.body_follows_head;
    r.hide_body_nodes = c.hide_body_nodes;
    r.trace_body_nodes = c.trace_body_nodes;
    r.force_flashlight = c.force_flashlight;
    r.compensate_flashlight = c.compensate_flashlight;
    r.flashlight_scale = c.flashlight_scale;
    r.trace_lights = c.trace_lights;
    r.trace_light_reader = c.trace_light_reader;
    r.trace_camera_readers = c.trace_camera_readers;
    r.clean_camera_for_reader = c.clean_camera_for_reader;
    r.clean_camera_reader_end = c.clean_camera_reader_end;
    r.position_enabled = c.position_enabled;
    r.pos_sens_x = c.pos_sens_x;
    r.pos_sens_y = c.pos_sens_y;
    r.pos_sens_z = c.pos_sens_z;
    r.invert_pos_x = c.invert_pos_x;
    r.invert_pos_y = c.invert_pos_y;
    r.invert_pos_z = c.invert_pos_z;
    r.pos_limit_x = c.pos_limit_x;
    r.pos_limit_y = c.pos_limit_y;
    r.pos_limit_z = c.pos_limit_z;
    r.pos_limit_z_back = c.pos_limit_z_back;
    r.pivot_forward = c.pivot_forward;
    r.pivot_up = c.pivot_up;
    r.log_to_file = c.log_to_file;
    r.log_path = c.log_path;
    r.load_notes = c.load_notes;

    // e87ed9e:src/PreyHeadTracking/mods/HeadTracking.cpp OnInitialize: tracking
    // starts on, the DOF mode from [Position] Enabled, the yaw mode from the file,
    // and the hotkeys in the order they were registered.
    r.tracking_enabled = true;
    r.tracking_mode = c.position_enabled ? 0 : 1;
    r.world_yaw = c.world_space_yaw;
    r.toggle = NavThenChord(ParseVk(c.toggle_key), 'Y');
    r.yaw_mode = NavThenChord(ParseVk(c.yaw_mode_key), 'H');
    r.cycle_tracking_mode = NavThenChord(ParseVk(c.position_key), 'G');
    r.cycle_tracker_source = {Binding{kChord, 'U'}};
    r.body_follows_head_key = {Binding{0, 0x2E}, Binding{kChord, 'J'}};
    return r;
}

}  // namespace prey_config_oracle
