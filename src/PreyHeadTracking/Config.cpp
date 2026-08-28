#include "preyht/Config.hpp"

#include <windows.h>

#include <cmath>
#include <cstdlib>
#include <filesystem>

#include "cameraunlock/config/ini_reader.h"

namespace preyht {

// Collected during the load and reported by the caller, because the config is
// read before the log file is open - warning from in here would only ever reach
// a debugger.
static void NoteRetiredKey(const cameraunlock::IniReader& reader, Config& out,
                           const char* section, const char* key, const char* advice) {
    if (reader.ReadString(section, key, "").empty()) return;
    out.load_notes.push_back(std::string("[") + section + "] " + key + " is retired and "
                               "IGNORED. " + advice);
}

bool Config::LoadFromFile(const std::string& path, Config& out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) return true;  // defaults are fine

    cameraunlock::IniReader ini;
    if (!ini.Open(path)) return false;

    // Validated here rather than in Sanitize, because the narrowing to uint16_t
    // is itself the failure: ReadInt hands back an int, so 70000 would bind 4464
    // and 65536 would bind whatever port the OS handed out, in both cases leaving
    // the tracker talking to nothing with no way to tell why.
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
    // The old smoothing value is deliberately NOT migrated: it carried a hidden
    // 0.15 floor, so the number in an existing config does not mean what it used
    // to, and copying it into only one of the two new keys would be a guess about
    // which connection the player was on.
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
    Clamp(flashlight_scale, 0.0f, 5.0f, 1.5f, "[Camera] FlashlightScale", notes);
    Clamp(local_smoothing,  0.0f, 1.0f, 0.0f,  "[Tracking] LocalSmoothing",  notes);
    Clamp(remote_smoothing, 0.0f, 1.0f, 0.15f, "[Tracking] RemoteSmoothing", notes);
    Clamp(pos_sens_x, 0.0f, 5.0f, 1.0f, "[Position] SensitivityX", notes);
    Clamp(pos_sens_y, 0.0f, 5.0f, 1.0f, "[Position] SensitivityY", notes);
    Clamp(pos_sens_z, 0.0f, 5.0f, 1.0f, "[Position] SensitivityZ", notes);
    Clamp(pos_limit_x,      0.01f, 0.5f, 0.30f, "[Position] LimitX",     notes);
    Clamp(pos_limit_y,      0.01f, 0.5f, 0.20f, "[Position] LimitY",     notes);
    Clamp(pos_limit_z,      0.01f, 0.5f, 0.40f, "[Position] LimitZ",     notes);
    Clamp(pos_limit_z_back, 0.01f, 0.5f, 0.10f, "[Position] LimitZBack", notes);
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
