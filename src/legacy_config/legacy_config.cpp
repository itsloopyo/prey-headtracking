#include "legacy_config/legacy_config.h"

#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "cameraunlock/config/ini_reader.h"

namespace preyht::legacy {

namespace {

void NoteRetiredKey(const cameraunlock::IniReader& reader, Config& out,
                    const char* section, const char* key, const char* advice) {
    if (reader.ReadString(section, key, "").empty()) return;
    out.load_notes.push_back(std::string("[") + section + "] " + key + " is retired and "
                               "IGNORED. " + advice);
}

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

bool LoadConfig(const std::string& path, Config& out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) return true;

    cameraunlock::IniReader ini;
    if (!ini.Open(path)) return false;

    const int port = ini.ReadInt("Network", "UdpPort", out.udp_port);
    if (port < 1024 || port > 65535) {
        out.load_notes.push_back("[Network] UdpPort " + std::to_string(port) +
                                 " is outside 1024-65535 and was ignored; listening on " +
                                 std::to_string(out.udp_port) + " instead.");
    } else {
        out.udp_port = static_cast<uint16_t>(port);
    }

    out.yaw_sens     = ini.ReadFloat ("Tracking", "YawSensitivity",   out.yaw_sens);
    out.pitch_sens   = ini.ReadFloat ("Tracking", "PitchSensitivity", out.pitch_sens);
    out.roll_sens    = ini.ReadFloat ("Tracking", "RollSensitivity",  out.roll_sens);
    out.invert_yaw   = ini.ReadBool  ("Tracking", "InvertYaw",        out.invert_yaw);
    out.invert_pitch = ini.ReadBool  ("Tracking", "InvertPitch",      out.invert_pitch);
    out.invert_roll  = ini.ReadBool  ("Tracking", "InvertRoll",       out.invert_roll);
    out.local_smoothing  = ini.ReadFloat("Tracking", "LocalSmoothing",  out.local_smoothing);
    out.remote_smoothing = ini.ReadFloat("Tracking", "RemoteSmoothing", out.remote_smoothing);
    static constexpr const char* kSmoothingAdvice =
        "Smoothing is now two keys: [Tracking] LocalSmoothing (default 0, for a tracker on "
        "this machine) and [Tracking] RemoteSmoothing (default 0.15, for one on the network). "
        "The old value is not migrated because its meaning changed.";
    NoteRetiredKey(ini, out, "Tracking", "Smoothing", kSmoothingAdvice);
    NoteRetiredKey(ini, out, "Position", "Smoothing", kSmoothingAdvice);
    out.deadzone     = ini.ReadFloat ("Tracking", "Deadzone",         out.deadzone);

    out.toggle_key   = ini.ReadString("Hotkeys", "ToggleKey",   out.toggle_key.c_str());
    out.yaw_mode_key = ini.ReadString("Hotkeys", "YawModeKey",  out.yaw_mode_key.c_str());
    out.position_key = ini.ReadString("Hotkeys", "PositionKey", out.position_key.c_str());

    out.world_space_yaw = ini.ReadBool("Camera", "WorldSpaceYaw", out.world_space_yaw);
    out.compensate_reticle = ini.ReadBool("Camera", "CompensateReticle", out.compensate_reticle);
    out.compensate_markers = ini.ReadBool("Camera", "CompensateMarkers", out.compensate_markers);
    out.field_of_view   = ini.ReadFloat("Camera", "FieldOfView", out.field_of_view);
    out.match_weapon_fov =
        ini.ReadBool("Camera", "MatchWeaponFieldOfView", out.match_weapon_fov);
    out.dump_camera     = ini.ReadBool("Camera", "DumpCamera",    out.dump_camera);
    out.early_inject    = ini.ReadBool("Camera", "EarlyInject",   out.early_inject);
    out.disable_coverage_buffer =
        ini.ReadBool("Camera", "DisableCoverageBuffer", out.disable_coverage_buffer);
    out.compensate_body = ini.ReadBool("Camera", "CompensateBody", out.compensate_body);
    out.body_follows_head =
        ini.ReadBool("Camera", "BodyFollowsHead", out.body_follows_head);
    out.trace_body_nodes = ini.ReadBool("Camera", "TraceBodyNodes", out.trace_body_nodes);
    out.hide_body_nodes  = ini.ReadBool("Camera", "HideBodyNodes",  out.hide_body_nodes);
    out.force_flashlight = ini.ReadBool("Camera", "ForceFlashlight", out.force_flashlight);
    out.compensate_flashlight =
        ini.ReadBool("Camera", "CompensateFlashlight", out.compensate_flashlight);
    out.flashlight_scale = ini.ReadFloat("Camera", "FlashlightScale", out.flashlight_scale);
    out.trace_lights = ini.ReadBool("Camera", "TraceLights", out.trace_lights);
    out.trace_light_reader =
        ini.ReadBool("Camera", "TraceLightReader", out.trace_light_reader);
    out.trace_camera_readers =
        ini.ReadBool("Camera", "TraceCameraReaders", out.trace_camera_readers);
    out.clean_camera_for_reader = static_cast<uint64_t>(
        std::strtoull(ini.ReadString("Camera", "CleanCameraForReader", "0").c_str(), nullptr, 0));
    out.clean_camera_reader_end = static_cast<uint64_t>(
        std::strtoull(ini.ReadString("Camera", "CleanCameraReaderEnd", "0").c_str(), nullptr, 0));
    out.position_enabled = ini.ReadBool ("Position", "Enabled",      out.position_enabled);
    out.pos_sens_x       = ini.ReadFloat("Position", "SensitivityX", out.pos_sens_x);
    out.pos_sens_y       = ini.ReadFloat("Position", "SensitivityY", out.pos_sens_y);
    out.pos_sens_z       = ini.ReadFloat("Position", "SensitivityZ", out.pos_sens_z);
    out.invert_pos_x     = ini.ReadBool ("Position", "InvertX",      out.invert_pos_x);
    out.invert_pos_y     = ini.ReadBool ("Position", "InvertY",      out.invert_pos_y);
    out.invert_pos_z     = ini.ReadBool ("Position", "InvertZ",      out.invert_pos_z);
    out.pos_limit_x      = ini.ReadFloat("Position", "LimitX",       out.pos_limit_x);
    out.pos_limit_y      = ini.ReadFloat("Position", "LimitY",       out.pos_limit_y);
    out.pos_limit_z      = ini.ReadFloat("Position", "LimitZ",       out.pos_limit_z);
    out.pos_limit_z_back = ini.ReadFloat("Position", "LimitZBack",   out.pos_limit_z_back);
    out.pivot_forward    = ini.ReadFloat("Position", "PivotForward", out.pivot_forward);
    out.pivot_up         = ini.ReadFloat("Position", "PivotUp",      out.pivot_up);

    NoteRetiredKey(ini, out, "Camera", "SetViewCameraRva",
                   "Camera addresses now come from a build profile matched to your game's "
                   "PreyDll.dll, and are no longer configurable.");
    NoteRetiredKey(ini, out, "Camera", "MatrixOffset",
                   "The view-camera matrix offset now comes from the same build profile.");

    out.log_to_file  = ini.ReadBool  ("Logging", "LogToFile", out.log_to_file);
    out.log_path     = ini.ReadString("Logging", "LogPath",   out.log_path.c_str());
    return true;
}

std::vector<std::string> Sanitize(Config& c) {
    std::vector<std::string> notes;
    Clamp(c.yaw_sens,   0.1f, 3.0f, 1.0f, "[Tracking] YawSensitivity",   notes);
    Clamp(c.pitch_sens, 0.1f, 3.0f, 1.0f, "[Tracking] PitchSensitivity", notes);
    Clamp(c.roll_sens,  0.1f, 3.0f, 1.0f, "[Tracking] RollSensitivity",  notes);
    Clamp(c.deadzone,   0.0f, 30.0f, 0.0f, "[Tracking] Deadzone",        notes);
    if (c.field_of_view != 0.0f) {
        Clamp(c.field_of_view, 25.0f, 170.0f, 0.0f, "[Camera] FieldOfView", notes);
    }
    Clamp(c.flashlight_scale, 0.0f, 5.0f, 1.5f, "[Camera] FlashlightScale", notes);
    Clamp(c.local_smoothing,  0.0f, 1.0f, 0.0f,  "[Tracking] LocalSmoothing",  notes);
    Clamp(c.remote_smoothing, 0.0f, 1.0f, 0.15f, "[Tracking] RemoteSmoothing", notes);
    Clamp(c.pos_sens_x, 0.0f, 5.0f, 1.0f, "[Position] SensitivityX", notes);
    Clamp(c.pos_sens_y, 0.0f, 5.0f, 1.0f, "[Position] SensitivityY", notes);
    Clamp(c.pos_sens_z, 0.0f, 5.0f, 1.0f, "[Position] SensitivityZ", notes);
    Clamp(c.pos_limit_x,      0.01f, 0.5f, 0.30f, "[Position] LimitX",     notes);
    Clamp(c.pos_limit_y,      0.01f, 0.5f, 0.20f, "[Position] LimitY",     notes);
    Clamp(c.pos_limit_z,      0.01f, 0.5f, 0.40f, "[Position] LimitZ",     notes);
    Clamp(c.pos_limit_z_back, 0.01f, 0.5f, 0.10f, "[Position] LimitZBack", notes);
    Clamp(c.pivot_forward, 0.0f, 0.5f, 0.0f, "[Position] PivotForward", notes);
    Clamp(c.pivot_up,      0.0f, 0.5f, 0.0f, "[Position] PivotUp",      notes);
    return notes;
}

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

const std::vector<cameraunlock::config::LegacyKey>& Keys() {
    static const std::vector<cameraunlock::config::LegacyKey> keys = {
        {"Network", "UdpPort"},
        {"Tracking", "YawSensitivity"},
        {"Tracking", "PitchSensitivity"},
        {"Tracking", "RollSensitivity"},
        {"Tracking", "InvertYaw"},
        {"Tracking", "InvertPitch"},
        {"Tracking", "InvertRoll"},
        {"Tracking", "LocalSmoothing"},
        {"Tracking", "RemoteSmoothing"},
        {"Tracking", "Smoothing"},
        {"Position", "Smoothing"},
        {"Tracking", "Deadzone"},
        {"Hotkeys", "ToggleKey"},
        {"Hotkeys", "YawModeKey"},
        {"Hotkeys", "PositionKey"},
        {"Camera", "WorldSpaceYaw"},
        {"Camera", "CompensateReticle"},
        {"Camera", "CompensateMarkers"},
        {"Camera", "FieldOfView"},
        {"Camera", "MatchWeaponFieldOfView"},
        {"Camera", "DumpCamera"},
        {"Camera", "EarlyInject"},
        {"Camera", "DisableCoverageBuffer"},
        {"Camera", "CompensateBody"},
        {"Camera", "BodyFollowsHead"},
        {"Camera", "TraceBodyNodes"},
        {"Camera", "HideBodyNodes"},
        {"Camera", "ForceFlashlight"},
        {"Camera", "CompensateFlashlight"},
        {"Camera", "FlashlightScale"},
        {"Camera", "TraceLights"},
        {"Camera", "TraceLightReader"},
        {"Camera", "TraceCameraReaders"},
        {"Camera", "CleanCameraForReader"},
        {"Camera", "CleanCameraReaderEnd"},
        {"Position", "Enabled"},
        {"Position", "SensitivityX"},
        {"Position", "SensitivityY"},
        {"Position", "SensitivityZ"},
        {"Position", "InvertX"},
        {"Position", "InvertY"},
        {"Position", "InvertZ"},
        {"Position", "LimitX"},
        {"Position", "LimitY"},
        {"Position", "LimitZ"},
        {"Position", "LimitZBack"},
        {"Position", "PivotForward"},
        {"Position", "PivotUp"},
        {"Camera", "SetViewCameraRva"},
        {"Camera", "MatrixOffset"},
        {"Logging", "LogToFile"},
        {"Logging", "LogPath"},
    };
    return keys;
}

}  // namespace preyht::legacy
