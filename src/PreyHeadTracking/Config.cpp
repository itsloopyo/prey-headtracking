#include "preyht/Config.hpp"

#include <windows.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "legacy_config/legacy_config.h"

namespace preyht {

bool Config::LoadFromFile(const std::string& path, Config& out) {
    legacy::Config read;
    read.load_notes = std::move(out.load_notes);
    if (!legacy::LoadConfig(path, read)) return false;

    out.udp_port = read.udp_port;
    out.yaw_sens = read.yaw_sens;
    out.pitch_sens = read.pitch_sens;
    out.roll_sens = read.roll_sens;
    out.invert_yaw = read.invert_yaw;
    out.invert_pitch = read.invert_pitch;
    out.invert_roll = read.invert_roll;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.deadzone = read.deadzone;
    out.toggle_key = read.toggle_key;
    out.yaw_mode_key = read.yaw_mode_key;
    out.position_key = read.position_key;
    out.world_space_yaw = read.world_space_yaw;
    out.compensate_reticle = read.compensate_reticle;
    out.compensate_markers = read.compensate_markers;
    out.field_of_view = read.field_of_view;
    out.match_weapon_fov = read.match_weapon_fov;
    out.dump_camera = read.dump_camera;
    out.early_inject = read.early_inject;
    out.disable_coverage_buffer = read.disable_coverage_buffer;
    out.compensate_body = read.compensate_body;
    out.body_follows_head = read.body_follows_head;
    out.hide_body_nodes = read.hide_body_nodes;
    out.trace_body_nodes = read.trace_body_nodes;
    out.force_flashlight = read.force_flashlight;
    out.compensate_flashlight = read.compensate_flashlight;
    out.flashlight_scale = read.flashlight_scale;
    out.trace_lights = read.trace_lights;
    out.trace_light_reader = read.trace_light_reader;
    out.trace_camera_readers = read.trace_camera_readers;
    out.clean_camera_for_reader = read.clean_camera_for_reader;
    out.clean_camera_reader_end = read.clean_camera_reader_end;
    out.position_enabled = read.position_enabled;
    out.pos_sens_x = read.pos_sens_x;
    out.pos_sens_y = read.pos_sens_y;
    out.pos_sens_z = read.pos_sens_z;
    out.invert_pos_x = read.invert_pos_x;
    out.invert_pos_y = read.invert_pos_y;
    out.invert_pos_z = read.invert_pos_z;
    out.pos_limit_x = read.pos_limit_x;
    out.pos_limit_y = read.pos_limit_y;
    out.pos_limit_z = read.pos_limit_z;
    out.pos_limit_z_back = read.pos_limit_z_back;
    out.pivot_forward = read.pivot_forward;
    out.pivot_up = read.pivot_up;
    out.log_to_file = read.log_to_file;
    out.log_path = read.log_path;
    out.load_notes = std::move(read.load_notes);
    return true;
}

namespace {
/// Clamp one configured float into range, describing the change if it moved.
/// Non-finite values (strtod accepts "inf" and "nan") go to the default.
void Clamp(float& value, float lo, float hi, float fallback, const char* name,
           std::vector<std::string>& notes) {
    const float before = value;
    if (!std::isfinite(value)) {
        value = fallback;
    } else if (value < lo) {
        value = lo;
    } else if (value > hi) {
        value = hi;
    }
    if (value != before) {
        notes.push_back(std::string(name) + " was out of range; using " +
                        std::to_string(value) + " instead.");
    }
}
}  // namespace

std::vector<std::string> Config::Sanitize() {
    std::vector<std::string> notes;
    Clamp(yaw_sens,   0.1f, 3.0f, 1.0f, "[Tracking] YawSensitivity",   notes);
    Clamp(pitch_sens, 0.1f, 3.0f, 1.0f, "[Tracking] PitchSensitivity", notes);
    Clamp(roll_sens,  0.1f, 3.0f, 1.0f, "[Tracking] RollSensitivity",  notes);
    Clamp(deadzone,   0.0f, 30.0f, 0.0f, "[Tracking] Deadzone",        notes);
    // 0 is the "leave the game alone" value and has to survive the clamp, so the
    // range below only applies once a FOV has actually been asked for. The floor
    // is the game's own cl_hfov minimum; the ceiling is past anything playable
    // and exists so a typo cannot hand the engine a degenerate frustum.
    if (field_of_view != 0.0f) {
        Clamp(field_of_view, 25.0f, 170.0f, 0.0f, "[Camera] FieldOfView", notes);
    }
    // A non-finite scale would reach the engine as a NaN beam quaternion, written
    // into a live entity through IEntity::SetPosRotScale. 0 turns the beam not at
    // all; past 5x it leaves the screen before the head has moved far.
    Clamp(flashlight_scale, 0.0f, cameraunlock::effects::kMaxLightMultiplier,
          cameraunlock::effects::kDefaultLightMultiplier, "[Camera] FlashlightScale", notes);
    Clamp(local_smoothing,  0.0f, 1.0f, static_cast<float>(cameraunlock::math::kDefaultLocalSmoothing),
          "[Tracking] LocalSmoothing",  notes);
    Clamp(remote_smoothing, 0.0f, 1.0f, static_cast<float>(cameraunlock::math::kDefaultRemoteSmoothing),
          "[Tracking] RemoteSmoothing", notes);
    Clamp(pos_sens_x, 0.0f, 5.0f, 1.0f, "[Position] SensitivityX", notes);
    Clamp(pos_sens_y, 0.0f, 5.0f, 1.0f, "[Position] SensitivityY", notes);
    Clamp(pos_sens_z, 0.0f, 5.0f, 1.0f, "[Position] SensitivityZ", notes);
    constexpr cameraunlock::PositionSettings kPosDefaults{};
    Clamp(pos_limit_x,      0.01f, 0.5f, kPosDefaults.limit_x,      "[Position] LimitX",     notes);
    Clamp(pos_limit_y,      0.01f, 0.5f, kPosDefaults.limit_y,      "[Position] LimitY",     notes);
    Clamp(pos_limit_z,      0.01f, 0.5f, kPosDefaults.limit_z,      "[Position] LimitZ",     notes);
    Clamp(pos_limit_z_back, 0.01f, 0.5f, kPosDefaults.limit_z_back, "[Position] LimitZBack", notes);
    Clamp(pivot_forward, 0.0f, 0.5f, 0.0f, "[Position] PivotForward", notes);
    Clamp(pivot_up,      0.0f, 0.5f, 0.0f, "[Position] PivotUp",      notes);
    return notes;
}

std::string Config::ResolveLogPath(const std::string& configured) {
    const std::string name = configured.empty() ? "HeadTracking.log" : configured;
    std::filesystem::path p(name);
    if (p.is_absolute()) return p.string();
    const std::filesystem::path ini(DefaultIniPathNextToHostExe());
    return (ini.parent_path() / p).string();
}

std::string Config::DefaultIniPathNextToHostExe() {
    char buf[MAX_PATH] = {};
    const auto n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return "HeadTracking.ini";
    std::filesystem::path p(buf);
    return (p.parent_path() / "HeadTracking.ini").string();
}

}  // namespace preyht
