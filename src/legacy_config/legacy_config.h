#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "cameraunlock/config/legacy_import.h"

// The HeadTracking.ini reader as the pre-canonical builds ran it, frozen. It reads a
// file an older build read and nothing else, and it never changes: a player can
// update from any older build, and their file has to be read exactly as that build
// read it. tests/config_differential holds it to the published build's reader.

namespace preyht::legacy {

// preyht::Config and its defaults as they stood when the reader was frozen,
// spelled as literals so a later default in the mod or in core changes what a new
// file holds, never what an old file without the key meant.
struct Config {
    uint16_t udp_port = 4242;

    float yaw_sens     = 1.0f;
    float pitch_sens   = 1.0f;
    float roll_sens    = 1.0f;
    bool  invert_yaw   = false;
    bool  invert_pitch = false;
    bool  invert_roll  = false;
    float local_smoothing  = 0.0f;
    float remote_smoothing = 0.15f;
    float deadzone     = 0.0f;

    std::string toggle_key   = "End";
    std::string yaw_mode_key = "PageDown";
    std::string position_key = "PageUp";

    bool world_space_yaw = true;
    bool compensate_reticle = true;
    bool compensate_markers = false;
    float field_of_view = 0.0f;
    bool match_weapon_fov = true;
    bool dump_camera = false;
    bool early_inject = true;
    bool disable_coverage_buffer = true;
    bool compensate_body = true;
    bool body_follows_head = true;
    bool hide_body_nodes = false;
    bool trace_body_nodes = false;
    bool force_flashlight = false;
    bool  compensate_flashlight = true;
    float flashlight_scale      = 1.5f;
    bool trace_lights = false;
    bool trace_light_reader = false;
    bool trace_camera_readers = false;
    uint64_t clean_camera_for_reader = 0;
    uint64_t clean_camera_reader_end = 0;

    bool  position_enabled = true;
    float pos_sens_x       = 1.0f;
    float pos_sens_y       = 1.0f;
    float pos_sens_z       = 1.0f;
    bool  invert_pos_x     = false;
    bool  invert_pos_y     = false;
    bool  invert_pos_z     = false;
    float pos_limit_x      = 0.30f;
    float pos_limit_y      = 0.20f;
    float pos_limit_z      = 0.40f;
    float pos_limit_z_back = 0.10f;
    float pivot_forward    = 0.0f;
    float pivot_up         = 0.0f;

    bool log_to_file = true;
    std::string log_path;

    // Retired keys and refused ports, phrased for the player, as the published
    // build collected them.
    std::vector<std::string> load_notes;
};

// Reads the file at @p path into @p out, starting from the values @p out holds,
// through core's frozen IniReader. Returns false only when the file exists and
// cannot be opened. Writes nothing.
bool LoadConfig(const std::string& path, Config& out);

// The published build's Config::Sanitize: clamps every number into the range the
// build documented and returns what it changed. Startup ran it straight after
// LoadConfig, so the settings a session ran on are what it leaves.
std::vector<std::string> Sanitize(Config& config);

// The published build's hotkey name reader (HeadTracking.cpp ParseVk): a friendly
// key name, or a numeric code, as a Win32 virtual-key code; 0 for a name it cannot
// place, which left that action on its Ctrl+Shift chord alone.
int ParseVk(const std::string& name);

// Every section and key LoadConfig reads, the retired keys it only notes included.
const std::vector<cameraunlock::config::LegacyKey>& Keys();

}  // namespace preyht::legacy
