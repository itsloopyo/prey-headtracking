#include "preyht/Config.hpp"

#include <windows.h>

#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cameraunlock/config/config_concepts.g.h"
#include "cameraunlock/config/hotkey_codec.h"
#include "cameraunlock/config/value_codecs.h"
#include "cameraunlock/input/key_bindings.h"

#include "legacy_config/legacy_config.h"

namespace preyht {

namespace {

namespace config = cameraunlock::config;
using config::DroppedValue;
using config::ImportResult;
using config::PoseShapingValue;
using cameraunlock::input::KeyBinding;
using cameraunlock::input::KeyModifiers;

// Horizontal degrees. The floor is the game's own cl_hfov minimum; the ceiling is
// past anything playable and exists so a typo cannot hand the engine a
// degenerate frustum.
constexpr float kMinFieldOfView = 25.0f;
constexpr float kMaxFieldOfView = 170.0f;

// 0, the game's own field of view, or a chosen one from kMinFieldOfView to
// kMaxFieldOfView. Between 0 and kMinFieldOfView is no field of view a person
// could play at, so those values are refused like any value outside the range.
class FieldOfViewCodec {
public:
    using Value = float;

    config::CodecParseResult<float> Parse(std::string_view text) const {
        config::CodecParseResult<float> read = inner_.Parse(text);
        if (!read.ok() || (read.value != 0.0f && read.value < kMinFieldOfView))
            return {0.0f, "0, or a number from 25 to 170"};
        return read;
    }

    std::string Render(float value) const { return inner_.Render(value); }

    bool Equal(float a, float b) const { return inner_.Equal(a, b); }

private:
    config::FloatCodec inner_{0.0f, kMaxFieldOfView};
};

// The Ctrl+Shift letters every pre-canonical build bound beside each nav key,
// hard-coded, which the import folds into each action's list.
constexpr KeyModifiers kChord = KeyModifiers::kCtrl | KeyModifiers::kShift;

// A legacy hotkey name, read as the published build read it, with the action's
// hard-coded chord after it. The code goes through core's N1 and N3
// normalisations, so a Ctrl, Shift or Alt key on its own is left unbound and
// logged, and the chord stays. A name the published build could not place bound
// nothing, and binds nothing here either.
std::string LegacyHotkey(const std::string& name, const char* key, int chordLetter,
                         std::vector<DroppedValue>& dropped) {
    std::string list =
        config::LegacyVirtualKeyToBindings(legacy::ParseVk(name), "Hotkeys", key, dropped);
    const std::string chord = cameraunlock::input::FormatKeyBindings({KeyBinding{kChord, chordLetter}});
    return list.empty() ? chord : list + ", " + chord;
}

// input names the legacy file, HeadTracking.ini, which the frozen reader opens by
// its ANSI path, as the published build did.
ImportResult RunLegacyImport(const config::LegacyInput& input, Config& out) {
    // The frozen reader finds the file the way the published build did, with
    // std::filesystem::exists on the ANSI path, and reads no file as the defaults.
    std::error_code ec;
    const bool present = std::filesystem::exists(input.ansi_path, ec);

    legacy::Config read;
    if (!legacy::LoadConfig(input.ansi_path, read))
        return ImportResult::Refused("HeadTracking.ini exists but could not be opened");
    legacy::Sanitize(read);

    const legacy::Config shipped;
    std::vector<DroppedValue> dropped;
    std::vector<PoseShapingValue> shaping;

    out.udp_port = read.udp_port;
    out.enable_on_startup = true;
    out.world_space_yaw = read.world_space_yaw;
    // [Position] Enabled only chose the startup mode: 6DOF, or rotation only. The
    // mode hotkey always cycled all three.
    out.rotation_enabled = true;
    out.position_enabled = read.position_enabled;
    out.local_smoothing = read.local_smoothing;
    out.remote_smoothing = read.remote_smoothing;
    out.limit_x = read.pos_limit_x;
    // The published build had one vertical limit and applied it both ways.
    out.limit_y = read.pos_limit_y;
    out.limit_y_down = read.pos_limit_y;
    out.limit_z = read.pos_limit_z;
    out.limit_z_back = read.pos_limit_z_back;
    out.compensate_flashlight = read.compensate_flashlight;
    out.flashlight_scale = read.flashlight_scale;
    out.toggle_key = LegacyHotkey(read.toggle_key, "ToggleKey", 'Y', dropped);
    out.cycle_tracking_mode_key = LegacyHotkey(read.position_key, "PositionKey", 'G', dropped);
    out.yaw_mode_key = LegacyHotkey(read.yaw_mode_key, "YawModeKey", 'H', dropped);
    out.compensate_markers = read.compensate_markers;
    out.field_of_view = read.field_of_view;
    out.match_weapon_fov = read.match_weapon_fov;
    out.early_inject = read.early_inject;
    out.disable_coverage_buffer = read.disable_coverage_buffer;
    out.compensate_body = read.compensate_body;
    out.body_follows_head = read.body_follows_head;
    out.dump_camera = read.dump_camera;
    out.trace_body_nodes = read.trace_body_nodes;
    out.hide_body_nodes = read.hide_body_nodes;
    out.force_flashlight = read.force_flashlight;
    out.trace_lights = read.trace_lights;
    out.trace_light_reader = read.trace_light_reader;
    out.trace_camera_readers = read.trace_camera_readers;
    out.clean_camera_for_reader = read.clean_camera_for_reader;
    out.clean_camera_reader_end = read.clean_camera_reader_end;
    out.log_to_file = read.log_to_file;
    out.log_path = read.log_path;

    // Pose shaping is the tracker's. Every value the build shipped is identity, so
    // nothing is folded into the camera code; a value the player changed is
    // dropped and logged.
    config::LegacyPoseShaping(read.yaw_sens, shipped.yaw_sens, "Tracking", "YawSensitivity", shaping, dropped);
    config::LegacyPoseShaping(read.pitch_sens, shipped.pitch_sens, "Tracking", "PitchSensitivity", shaping, dropped);
    config::LegacyPoseShaping(read.roll_sens, shipped.roll_sens, "Tracking", "RollSensitivity", shaping, dropped);
    config::LegacyPoseShaping(read.invert_yaw, shipped.invert_yaw, "Tracking", "InvertYaw", shaping, dropped);
    config::LegacyPoseShaping(read.invert_pitch, shipped.invert_pitch, "Tracking", "InvertPitch", shaping, dropped);
    config::LegacyPoseShaping(read.invert_roll, shipped.invert_roll, "Tracking", "InvertRoll", shaping, dropped);
    config::LegacyPoseShaping(read.deadzone, shipped.deadzone, "Tracking", "Deadzone", shaping, dropped);
    config::LegacyPoseShaping(read.pos_sens_x, shipped.pos_sens_x, "Position", "SensitivityX", shaping, dropped);
    config::LegacyPoseShaping(read.pos_sens_y, shipped.pos_sens_y, "Position", "SensitivityY", shaping, dropped);
    config::LegacyPoseShaping(read.pos_sens_z, shipped.pos_sens_z, "Position", "SensitivityZ", shaping, dropped);
    config::LegacyPoseShaping(read.invert_pos_x, shipped.invert_pos_x, "Position", "InvertX", shaping, dropped);
    config::LegacyPoseShaping(read.invert_pos_y, shipped.invert_pos_y, "Position", "InvertY", shaping, dropped);
    config::LegacyPoseShaping(read.invert_pos_z, shipped.invert_pos_z, "Position", "InvertZ", shaping, dropped);

    // The crosshair always follows the aim now.
    if (!read.compensate_reticle)
        dropped.push_back({config::DropRule::Reticle, "Camera", "CompensateReticle", "false"});

    // The tracker owns the neck pivot. The build shipped it off, at 0.
    config::LegacyTrackerPivot(read.pivot_forward, shipped.pivot_forward, "Position", "PivotForward", dropped);
    config::LegacyTrackerPivot(read.pivot_up, shipped.pivot_up, "Position", "PivotUp", dropped);

    // A setting the player never changed from what the published build shipped
    // follows Defaults.ini. A hotkey's chord was hard-coded, so the key its name
    // reads as says whether the player changed it.
    using C = config::schema::Concept;
    config::LegacyFollowsDefaultsIni follows;
    follows.Setting(C::UdpPort, read.udp_port, shipped.udp_port);
    follows.NotInLegacy(C::EnableOnStartup);
    follows.Setting(C::WorldSpaceYaw, read.world_space_yaw, shipped.world_space_yaw);
    follows.TrackingMode(read.position_enabled, shipped.position_enabled);
    follows.Setting(C::LocalSmoothing, read.local_smoothing, shipped.local_smoothing);
    follows.Setting(C::RemoteSmoothing, read.remote_smoothing, shipped.remote_smoothing);
    follows.Setting(C::PositionLimitX, read.pos_limit_x, shipped.pos_limit_x);
    follows.Setting(C::PositionLimitY, read.pos_limit_y, shipped.pos_limit_y);
    follows.Setting(C::PositionLimitYDown, read.pos_limit_y, shipped.pos_limit_y);
    follows.Setting(C::PositionLimitZ, read.pos_limit_z, shipped.pos_limit_z);
    follows.Setting(C::PositionLimitZBack, read.pos_limit_z_back, shipped.pos_limit_z_back);
    follows.Setting(C::LightFollowsHead, read.compensate_flashlight, shipped.compensate_flashlight);
    follows.Setting(C::LightMultiplier, read.flashlight_scale, shipped.flashlight_scale);
    follows.Setting(C::ToggleKey, legacy::ParseVk(read.toggle_key) == legacy::ParseVk(shipped.toggle_key));
    follows.Setting(C::CycleTrackingModeKey,
                    legacy::ParseVk(read.position_key) == legacy::ParseVk(shipped.position_key));
    follows.Setting(C::YawModeKey, legacy::ParseVk(read.yaw_mode_key) == legacy::ParseVk(shipped.yaw_mode_key));

    if (!present) return ImportResult::Absent(std::move(dropped), std::move(shaping), follows.Concepts());
    return ImportResult::Imported(std::move(dropped), std::move(shaping), follows.Concepts());
}

}  // namespace

const char* const kGameDisplayName = "Prey";

config::ConfigTable<Config> ConfigTableFor() {
    using C = config::schema::Concept;
    config::ConfigTable<Config> table{Config{}};
    table.Concept<C::UdpPort>(&Config::udp_port)
        .Concept<C::EnableOnStartup>(&Config::enable_on_startup)
        .Concept<C::WorldSpaceYaw>(&Config::world_space_yaw).Writable()
        .Comment("true locks head yaw to the world's up axis, so the horizon stays level\n"
                 "at any pitch. false turns about the camera's own up axis. In the space\n"
                 "suit yaw is always the camera's own, since floating has no stable up.")
        .Concept<C::RotationEnabled>(&Config::rotation_enabled).Writable()
        .Concept<C::LocalSmoothing>(&Config::local_smoothing)
        .Concept<C::RemoteSmoothing>(&Config::remote_smoothing)
        .Concept<C::PositionEnabled>(&Config::position_enabled).Writable()
        .Concept<C::PositionLimitX>(&Config::limit_x)
        .Concept<C::PositionLimitY>(&Config::limit_y)
        .Concept<C::PositionLimitYDown>(&Config::limit_y_down)
        .Concept<C::PositionLimitZ>(&Config::limit_z)
        .Concept<C::PositionLimitZBack>(&Config::limit_z_back)
        .Concept<C::LightFollowsHead>(&Config::compensate_flashlight)
        .Concept<C::LightMultiplier>(&Config::flashlight_scale)
        .Concept<C::ToggleKey>(&Config::toggle_key)
        .Concept<C::CycleTrackingModeKey>(&Config::cycle_tracking_mode_key)
        .Concept<C::YawModeKey>(&Config::yaw_mode_key)
        .Local("Hotkeys", "CycleTrackerSourceKey", &Config::cycle_tracker_source_key, config::HotkeyCodec(),
               "Steps to the next app sending to the tracker port, when one you are not\n"
               "using got there first and holds it.")
        .Local("Hotkeys", "BodyFollowsHeadKey", &Config::body_follows_head_key, config::HotkeyCodec(),
               "Turns BodyFollowsHead on or off, and saves it.")
        .Local("Camera", "CompensateMarkers", &Config::compensate_markers, config::BoolCodec(),
               "Project the world-anchored HUD markers (objective markers, interactable\n"
               "diamonds) through the head-tracked view so they stay on their objects.")
        .Local("Camera", "FieldOfView", &Config::field_of_view, FieldOfViewCodec(),
               "Horizontal field of view in degrees. 0 leaves Prey's own Field of View\n"
               "slider in charge. 25 to 170 is written straight into the engine, past the\n"
               "slider's 120 limit, and holds while it is set.")
        .Local("Camera", "MatchWeaponFieldOfView", &Config::match_weapon_fov, config::BoolCodec(),
               "Draw the gun in your hands through the same lens as the world, so the\n"
               "barrel and the crosshair agree when the head turns. false leaves Prey's\n"
               "own weapon field of view alone.")
        .Local("Camera", "EarlyInject", &Config::early_inject, config::BoolCodec(),
               "Apply the head pose when the game sets the view camera, so culling, the\n"
               "HUD and the flashlight see it too. false only turns the rendered image.")
        .Local("Camera", "DisableCoverageBuffer", &Config::disable_coverage_buffer, config::BoolCodec(),
               "Turn off Prey's software occlusion culling, which culls a head-turned view\n"
               "against the un-turned one so geometry vanishes at the edge of a turn.")
        .Local("Camera", "CompensateBody", &Config::compensate_body, config::BoolCodec(),
               "Build the first-person body from the game's own camera, so it does not\n"
               "swing across the screen at twice the head's rotation.")
        .Local("Camera", "BodyFollowsHead", &Config::body_follows_head, config::BoolCodec(),
               "Carry the space suit's collar and shoulders with your head while the suit\n"
               "is on. BodyFollowsHeadKey turns it on or off in game and saves it.")
        .Writable()
        .Local("Camera", "DumpCamera", &Config::dump_camera, config::BoolCodec(),
               "Diagnostic: log the view camera and the crosshair projection twice a\n"
               "second. Off for play.")
        .Local("Camera", "TraceBodyNodes", &Config::trace_body_nodes, config::BoolCodec(),
               "Diagnostic: sample the render node the body is drawn from. Off for play.")
        .Local("Camera", "HideBodyNodes", &Config::hide_body_nodes, config::BoolCodec(),
               "Diagnostic: draw nothing for the body's render node. Off for play.")
        .Local("Camera", "ForceFlashlight", &Config::force_flashlight, config::BoolCodec(),
               "Diagnostic: force the flashlight on regardless of save progress.")
        .Local("Camera", "TraceLights", &Config::trace_lights, config::BoolCodec(),
               "Diagnostic: log each distinct dynamic light once. Off for play.")
        .Local("Camera", "TraceLightReader", &Config::trace_light_reader, config::BoolCodec(),
               "Diagnostic: report every instruction that reads the flashlight's\n"
               "intensity. Off for play.")
        .Local("Camera", "TraceCameraReaders", &Config::trace_camera_readers, config::BoolCodec(),
               "Diagnostic: log every distinct caller of the engine's view-camera getter\n"
               "once, as an address. Off for play.")
        .Local("Camera", "CleanCameraForReader", &Config::clean_camera_for_reader, config::Hex64Codec(),
               "Diagnostic: the address of one view-camera reader to hand the clean\n"
               "camera to. 0x0 hands it to none.")
        .Engine()
        .Local("Camera", "CleanCameraReaderEnd", &Config::clean_camera_reader_end, config::Hex64Codec(),
               "Diagnostic: the upper end of a range of reader addresses, for bisecting.\n"
               "0x0 matches the one address above.")
        .Engine()
        .Local("Logging", "LogToFile", &Config::log_to_file, config::BoolCodec(),
               "Write HeadTracking.log next to Prey.exe. It starts empty at every launch,\n"
               "and the previous session is kept as HeadTracking.prev.log.")
        .Local("Logging", "LogPath", &Config::log_path, config::StringCodec(),
               "The log file, relative to the folder Prey.exe is in, or a full path.");
    return table;
}

config::LegacyImport<Config> LegacyImportFor() {
    config::LegacyImport<Config> import;
    import.run = &RunLegacyImport;
    import.keys = legacy::Keys();
    return import;
}

config::ConfigOwnerOptions<Config> OwnerOptions(const std::wstring& directory, config::DefaultsFile defaults,
                                                std::function<void(const std::string&)> status_sink) {
    config::ConfigOwnerOptions<Config> options;
    options.path = directory + L"\\CameraUnlock.ini";
    options.table = ConfigTableFor();
    options.import = LegacyImportFor();
    options.legacy_path = directory + L"\\HeadTracking.ini";
    options.header.display_name = kGameDisplayName;
    options.defaults = std::move(defaults);
    options.status_sink = std::move(status_sink);
    return options;
}

cameraunlock::TrackingMode StartupMode(const Config& config) {
    return cameraunlock::DecodeTrackingMode(config.rotation_enabled, config.position_enabled).value();
}

std::wstring ExeDirectory() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) throw std::runtime_error("cannot read the path of the game's executable");
    return std::filesystem::path(std::wstring(buf, n)).parent_path().wstring();
}

std::string Config::ResolveLogPath(const std::string& configured) {
    const std::string name = configured.empty() ? "HeadTracking.log" : configured;
    std::filesystem::path p(name);
    if (p.is_absolute()) return p.string();
    return (std::filesystem::path(ExeDirectory()) / p).string();
}

}  // namespace preyht
