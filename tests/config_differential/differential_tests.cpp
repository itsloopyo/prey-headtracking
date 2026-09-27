// The differential test for the config conversion.
//
// Oracle: the reader of the newest published build (the rolling dev pre-release,
// e87ed9e, core ee8cc72), vendored under oracle/published/ byte for byte, with the
// startup code that turned its output into the session's state.
// Import: the frozen reader in src/legacy_config/ and its map into
// Config, the owner's LegacyImport, through the mod's startup code.
// Migration: the config owner in a scratch folder holding only the file as
// HeadTracking.ini, which it imports into a new CameraUnlock.ini, then the
// canonical reader and table on that file. Every owner reads a scratch
// Defaults.ini the first load creates with the built-in values, so a row whose
// imported value is the built-in one migrates as `default`.
//
// Every input runs through all three: the published build's shipped
// HeadTracking.ini (its ZIPs and its launcher seed hold the same bytes), no file,
// an empty file and the corpus core generates from the shipped file.
//
// Comparison 1, oracle against import, compares every field of the published
// Config after Sanitize, floats bit for bit, the notes it collected, the startup
// state and the registered hotkeys. The fields the canonical format dropped
// (sensitivity, inversion, deadzone, CompensateReticle, the neck pivot) and the
// legacy hotkey names come from the frozen reader; every other field from the
// import's map. Its differences are the approved ones: normalisation N3 (a hotkey
// on a Ctrl, Shift or Alt key alone is not bound and the action keeps its chord)
// and N1 (a hotkey on 0xFF is not bound). No commit since e87ed9e changed how
// the file is read.
//
// Comparison 2, import against migration, compares every field of Config and
// finds no difference, apart from the two defaults the conversion moves to what
// the build shipped in its HeadTracking.ini, which only an install with no file
// sees: CompensateMarkers (false in code, true in the shipped file) and LogPath
// (empty in code, which meant HeadTracking.log, and HeadTracking.log in the
// shipped file). The values the conversion drops, which the import reports and
// the owner logs, are each checked: PoseShaping for a sensitivity, inversion or
// deadzone the player changed, Reticle for CompensateReticle=false, TrackerPivot
// for a pivot the player set, ModifierKey (N3) and KeyCodeOutOfRange (N1).
//
// A row the player never changed from what the published build shipped follows
// Defaults.ini (owner rule of 2026-09-26): the import lists it in
// follows_defaults_ini, the tracking mode pair as one unit. The test derives
// that list from what the frozen reader read against the frozen defaults and
// holds the import's list to it on every input. Every present input also
// migrates over a Defaults.ini that differs from the built-in value on every
// global row: an untouched row is written `default` and takes that file's
// value, and a changed row keeps the player's.
//
// After every load HeadTracking.ini keeps its bytes and its last write time, and
// the folder holds it and CameraUnlock.ini and nothing else. Every input is also
// migrated from a read-only HeadTracking.ini, which has to give the same file
// and keep its read-only attribute. A second load reads CameraUnlock.ini, does
// not import, and changes neither file.
//
// The distinct migrated files are written beside the executable under
// migrated\, for lint-migrated.mjs to run core's canonical config lint over.
//
// What is recorded here, and checked by hash below:
//   - The published builds: only the dev pre-release, at e87ed9e. No v* tag and no
//     predecessor repo exists.
//   - oracle/published/src and include: e87ed9e:src/PreyHeadTracking/Config.cpp,
//     e87ed9e:include/preyht/Config.hpp and, for ParseVk, which oracle.cpp
//     transcribes, e87ed9e:src/PreyHeadTracking/mods/HeadTracking.cpp.
//   - oracle/published/core: the core sources those include, at ee8cc72, the pin
//     e87ed9e built against.
//   - The frozen import: src/legacy_config/. It compiles core's
//     IniReader, which hashes equal to ee8cc72's.
//   - inputs/: the published build's shipped HeadTracking.ini. The build writes no
//     file at first launch.

#include <windows.h>

#include <bcrypt.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <cameraunlock/config/canonical_ini.h>
#include <cameraunlock/config/config_owner.h>
#include <cameraunlock/config/config_table.h>
#include <cameraunlock/config/defaults_file.h>
#include <cameraunlock/config/legacy_import.h>
#include <cameraunlock/config/testing/ini_mutations.h>
#include <cameraunlock/input/key_bindings.h>

#include "legacy_config/legacy_config.h"
#include "preyht/Config.hpp"
#include "oracle/oracle.h"

namespace {

namespace fs = std::filesystem;
using cameraunlock::config::testing::GenerateIniMutations;
using cameraunlock::config::testing::IniMutation;
using cameraunlock::config::testing::MutationKey;

int g_failures = 0;

void Check(bool condition, const std::string& name) {
    std::cout << (condition ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!condition) ++g_failures;
}

void Fail(const std::string& name) {
    std::cout << "  [FAIL] " << name << "\n";
    ++g_failures;
}

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("cannot write " + path.string());
    out << bytes;
}

std::string Sha256(const std::string& bytes) {
    unsigned char digest[32] = {};
    const NTSTATUS status = BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0,
                                       reinterpret_cast<PUCHAR>(const_cast<char*>(bytes.data())),
                                       static_cast<ULONG>(bytes.size()), digest, sizeof(digest));
    if (status != 0) throw std::runtime_error("BCryptHash failed");
    std::string hex;
    char two[3];
    for (const unsigned char b : digest) {
        std::snprintf(two, sizeof(two), "%02x", b);
        hex += two;
    }
    return hex;
}

bool SameBits(float a, float b) { return std::memcmp(&a, &b, sizeof(float)) == 0; }

FILETIME WriteTime(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        throw std::runtime_error("cannot stat " + path.string());
    return data.ftLastWriteTime;
}

bool SameTime(const FILETIME& a, const FILETIME& b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

bool IsReadOnly(const fs::path& path) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) throw std::runtime_error("cannot stat " + path.string());
    return (attributes & FILE_ATTRIBUTE_READONLY) != 0;
}

// A fresh folder under %TEMP% for the whole run, removed at the end.
class Scratch {
public:
    Scratch() {
        root_ = fs::temp_directory_path() /
                ("prey-config-differential-" + std::to_string(GetCurrentProcessId()));
        Clear();
        fs::create_directories(root_);
    }
    ~Scratch() { Clear(); }

    fs::path Folder(const char* kind) {
        const fs::path dir = root_ / (std::string(kind) + "-" + std::to_string(next_++));
        fs::create_directories(dir);
        return dir;
    }

    // The run's one Defaults.ini, outside every game folder. The first owner
    // creates it with the built-in values and every later one reads it.
    cameraunlock::config::DefaultsFile Defaults() const {
        return cameraunlock::config::DefaultsFile::At(BuiltinPath().wstring());
    }

    // A second Defaults.ini, which SkewedDefaultsTests writes from the built-in
    // one with every global row changed.
    cameraunlock::config::DefaultsFile Skewed() const {
        return cameraunlock::config::DefaultsFile::At(SkewedPath().wstring());
    }
    fs::path BuiltinPath() const { return root_ / "global" / "Defaults.ini"; }
    fs::path SkewedPath() const { return root_ / "skewed" / "Defaults.ini"; }

private:
    void Clear() {
        if (!fs::exists(root_)) return;
        for (const auto& entry : fs::recursive_directory_iterator(root_))
            SetFileAttributesW(entry.path().c_str(), FILE_ATTRIBUTE_NORMAL);
        fs::remove_all(root_);
    }

    fs::path root_;
    int next_ = 0;
};

struct Input {
    std::string name;
    bool present = true;
    std::string bytes;
};

const fs::path kDir = PREY_DIFFERENTIAL_DIR;
const fs::path kRepo = PREY_REPO_DIR;

std::string PublishedShipped() { return ReadBytes(kDir / "inputs" / "shipped-dev-e87ed9e.ini"); }

// Every key the frozen reader reads, with a valid value other than the shipped
// one and, for each range it clamps or refuses, values on both sides of it.
std::vector<MutationKey> Descriptors() {
    const auto k = [](const char* section, const char* key, const char* alternate,
                      std::vector<std::string> out_of_range, bool hotkey = false) {
        MutationKey d;
        d.section = section;
        d.key = key;
        d.alternate = alternate;
        d.out_of_range = std::move(out_of_range);
        d.hotkey = hotkey;
        return d;
    };
    return {
        k("Network", "UdpPort", "5252", {"1023", "65536"}),
        k("Tracking", "YawSensitivity", "1.5", {"0.05", "3.5"}),
        k("Tracking", "PitchSensitivity", "1.5", {"0.05", "3.5"}),
        k("Tracking", "RollSensitivity", "1.5", {"0.05", "3.5"}),
        k("Tracking", "InvertYaw", "true", {}),
        k("Tracking", "InvertPitch", "true", {}),
        k("Tracking", "InvertRoll", "true", {}),
        k("Tracking", "LocalSmoothing", "0.25", {"-0.5", "1.5"}),
        k("Tracking", "RemoteSmoothing", "0.5", {"-1", "2"}),
        k("Tracking", "Smoothing", "0.5", {}),
        k("Position", "Smoothing", "0.5", {}),
        k("Tracking", "Deadzone", "2", {"-1", "31"}),
        k("Hotkeys", "ToggleKey", "F8", {"0x100", "F25"}, true),
        k("Hotkeys", "YawModeKey", "F9", {"0x100", "F25"}, true),
        k("Hotkeys", "PositionKey", "F10", {"0x100", "F25"}, true),
        k("Camera", "WorldSpaceYaw", "false", {}),
        k("Camera", "CompensateReticle", "false", {}),
        k("Camera", "CompensateMarkers", "false", {}),
        k("Camera", "FieldOfView", "110", {"24", "171"}),
        k("Camera", "MatchWeaponFieldOfView", "false", {}),
        k("Camera", "DumpCamera", "true", {}),
        k("Camera", "EarlyInject", "false", {}),
        k("Camera", "DisableCoverageBuffer", "false", {}),
        k("Camera", "CompensateBody", "false", {}),
        k("Camera", "BodyFollowsHead", "false", {}),
        k("Camera", "TraceBodyNodes", "true", {}),
        k("Camera", "HideBodyNodes", "true", {}),
        k("Camera", "ForceFlashlight", "true", {}),
        k("Camera", "CompensateFlashlight", "false", {}),
        k("Camera", "FlashlightScale", "2.5", {"-1", "6"}),
        k("Camera", "TraceLights", "true", {}),
        k("Camera", "TraceLightReader", "true", {}),
        k("Camera", "TraceCameraReaders", "true", {}),
        k("Camera", "CleanCameraForReader", "0x1234", {}),
        k("Camera", "CleanCameraReaderEnd", "0x5678", {}),
        k("Position", "Enabled", "false", {}),
        k("Position", "SensitivityX", "1.5", {"-1", "6"}),
        k("Position", "SensitivityY", "1.5", {"-1", "6"}),
        k("Position", "SensitivityZ", "1.5", {"-1", "6"}),
        k("Position", "InvertX", "true", {}),
        k("Position", "InvertY", "true", {}),
        k("Position", "InvertZ", "true", {}),
        k("Position", "LimitX", "0.25", {"0.001", "0.6"}),
        k("Position", "LimitY", "0.3", {"0.001", "0.6"}),
        k("Position", "LimitZ", "0.45", {"0.001", "0.6"}),
        k("Position", "LimitZBack", "0.05", {"0.001", "0.6"}),
        k("Position", "PivotForward", "0.1", {"-0.1", "0.6"}),
        k("Position", "PivotUp", "0.03", {"-0.1", "0.6"}),
        k("Camera", "SetViewCameraRva", "0x1000", {}),
        k("Camera", "MatrixOffset", "0x788", {}),
        k("Logging", "LogToFile", "false", {}),
        k("Logging", "LogPath", "Prey.log", {}),
    };
}

// The Ctrl, Shift and Alt virtual-key codes N3 unbinds.
constexpr int kModifierCodes[] = {0x10, 0x11, 0x12, 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5};

bool IsModifierCode(int code) {
    return std::find(std::begin(kModifierCodes), std::end(kModifierCodes), code) != std::end(kModifierCodes);
}

std::string Replaced(std::string text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    if (at == std::string::npos) throw std::runtime_error("the text has no " + from);
    return text.replace(at, from.size(), to);
}

std::vector<Input> Inputs() {
    std::vector<Input> inputs;
    inputs.push_back({"shipped config of the published build (dev, e87ed9e)", true, PublishedShipped()});
    inputs.push_back({"no file", false, {}});
    inputs.push_back({"empty file", true, {}});
    for (IniMutation& m : GenerateIniMutations(PublishedShipped(), preyht::legacy::Keys(), Descriptors()))
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
    // The corpus's hotkey alternates are plain keys, so N3 and N1 get inputs of
    // their own.
    for (const std::string line : {"ToggleKey   = End", "YawModeKey  = PageDown", "PositionKey = PageUp"}) {
        const std::string key = line.substr(0, line.find(' '));
        std::vector<int> codes(std::begin(kModifierCodes), std::end(kModifierCodes));
        codes.push_back(0xFF);
        for (const int code : codes) {
            char value[8];
            std::snprintf(value, sizeof(value), "0x%02X", code);
            inputs.push_back({"hotkey code: " + key + "=" + value, true,
                              Replaced(PublishedShipped(), line, key + " = " + value)});
        }
    }
    return inputs;
}

using Binding = prey_config_oracle::Binding;

bool SameBindings(const std::vector<Binding>& a, const std::vector<Binding>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i].modifiers != b[i].modifiers || a[i].vk != b[i].vk) return false;
    return true;
}

// The frozen reader as the published startup ran it: LoadConfig into a default
// frozen Config, then Sanitize, and the startup state and hotkeys from that.
prey_config_oracle::Result FromFrozen(const preyht::legacy::Config& f, std::vector<std::string> sanitize) {
    prey_config_oracle::Result r;
    r.udp_port = f.udp_port;
    r.yaw_sens = f.yaw_sens;
    r.pitch_sens = f.pitch_sens;
    r.roll_sens = f.roll_sens;
    r.invert_yaw = f.invert_yaw;
    r.invert_pitch = f.invert_pitch;
    r.invert_roll = f.invert_roll;
    r.local_smoothing = f.local_smoothing;
    r.remote_smoothing = f.remote_smoothing;
    r.deadzone = f.deadzone;
    r.toggle_key = f.toggle_key;
    r.yaw_mode_key = f.yaw_mode_key;
    r.position_key = f.position_key;
    r.world_space_yaw = f.world_space_yaw;
    r.compensate_reticle = f.compensate_reticle;
    r.compensate_markers = f.compensate_markers;
    r.field_of_view = f.field_of_view;
    r.match_weapon_fov = f.match_weapon_fov;
    r.dump_camera = f.dump_camera;
    r.early_inject = f.early_inject;
    r.disable_coverage_buffer = f.disable_coverage_buffer;
    r.compensate_body = f.compensate_body;
    r.body_follows_head = f.body_follows_head;
    r.hide_body_nodes = f.hide_body_nodes;
    r.trace_body_nodes = f.trace_body_nodes;
    r.force_flashlight = f.force_flashlight;
    r.compensate_flashlight = f.compensate_flashlight;
    r.flashlight_scale = f.flashlight_scale;
    r.trace_lights = f.trace_lights;
    r.trace_light_reader = f.trace_light_reader;
    r.trace_camera_readers = f.trace_camera_readers;
    r.clean_camera_for_reader = f.clean_camera_for_reader;
    r.clean_camera_reader_end = f.clean_camera_reader_end;
    r.position_enabled = f.position_enabled;
    r.pos_sens_x = f.pos_sens_x;
    r.pos_sens_y = f.pos_sens_y;
    r.pos_sens_z = f.pos_sens_z;
    r.invert_pos_x = f.invert_pos_x;
    r.invert_pos_y = f.invert_pos_y;
    r.invert_pos_z = f.invert_pos_z;
    r.pos_limit_x = f.pos_limit_x;
    r.pos_limit_y = f.pos_limit_y;
    r.pos_limit_z = f.pos_limit_z;
    r.pos_limit_z_back = f.pos_limit_z_back;
    r.pivot_forward = f.pivot_forward;
    r.pivot_up = f.pivot_up;
    r.log_to_file = f.log_to_file;
    r.log_path = f.log_path;
    r.load_notes = f.load_notes;
    r.sanitize_notes = std::move(sanitize);

    static constexpr unsigned kChord = 1u | 2u;
    const auto navThenChord = [](const std::string& name, int letter) {
        std::vector<Binding> out;
        if (const int vk = preyht::legacy::ParseVk(name); vk != 0) out.push_back(Binding{0, vk});
        out.push_back(Binding{kChord, letter});
        return out;
    };
    r.tracking_enabled = true;
    r.tracking_mode = f.position_enabled ? 0 : 1;
    r.world_yaw = f.world_space_yaw;
    r.toggle = navThenChord(f.toggle_key, 'Y');
    r.yaw_mode = navThenChord(f.yaw_mode_key, 'H');
    r.cycle_tracking_mode = navThenChord(f.position_key, 'G');
    r.cycle_tracker_source = {Binding{kChord, 'U'}};
    r.body_follows_head_key = {Binding{0, 0x2E}, Binding{kChord, 'J'}};
    return r;
}

std::vector<std::string> Differences(const prey_config_oracle::Result& a, const prey_config_oracle::Result& b) {
    std::vector<std::string> d;
    const auto field = [&d](bool same, const char* name) {
        if (!same) d.push_back(name);
    };
    field(a.udp_port == b.udp_port, "udp_port");
    field(SameBits(a.yaw_sens, b.yaw_sens), "yaw_sens");
    field(SameBits(a.pitch_sens, b.pitch_sens), "pitch_sens");
    field(SameBits(a.roll_sens, b.roll_sens), "roll_sens");
    field(a.invert_yaw == b.invert_yaw, "invert_yaw");
    field(a.invert_pitch == b.invert_pitch, "invert_pitch");
    field(a.invert_roll == b.invert_roll, "invert_roll");
    field(SameBits(a.local_smoothing, b.local_smoothing), "local_smoothing");
    field(SameBits(a.remote_smoothing, b.remote_smoothing), "remote_smoothing");
    field(SameBits(a.deadzone, b.deadzone), "deadzone");
    field(a.toggle_key == b.toggle_key, "toggle_key");
    field(a.yaw_mode_key == b.yaw_mode_key, "yaw_mode_key");
    field(a.position_key == b.position_key, "position_key");
    field(a.world_space_yaw == b.world_space_yaw, "world_space_yaw");
    field(a.compensate_reticle == b.compensate_reticle, "compensate_reticle");
    field(a.compensate_markers == b.compensate_markers, "compensate_markers");
    field(SameBits(a.field_of_view, b.field_of_view), "field_of_view");
    field(a.match_weapon_fov == b.match_weapon_fov, "match_weapon_fov");
    field(a.dump_camera == b.dump_camera, "dump_camera");
    field(a.early_inject == b.early_inject, "early_inject");
    field(a.disable_coverage_buffer == b.disable_coverage_buffer, "disable_coverage_buffer");
    field(a.compensate_body == b.compensate_body, "compensate_body");
    field(a.body_follows_head == b.body_follows_head, "body_follows_head");
    field(a.hide_body_nodes == b.hide_body_nodes, "hide_body_nodes");
    field(a.trace_body_nodes == b.trace_body_nodes, "trace_body_nodes");
    field(a.force_flashlight == b.force_flashlight, "force_flashlight");
    field(a.compensate_flashlight == b.compensate_flashlight, "compensate_flashlight");
    field(SameBits(a.flashlight_scale, b.flashlight_scale), "flashlight_scale");
    field(a.trace_lights == b.trace_lights, "trace_lights");
    field(a.trace_light_reader == b.trace_light_reader, "trace_light_reader");
    field(a.trace_camera_readers == b.trace_camera_readers, "trace_camera_readers");
    field(a.clean_camera_for_reader == b.clean_camera_for_reader, "clean_camera_for_reader");
    field(a.clean_camera_reader_end == b.clean_camera_reader_end, "clean_camera_reader_end");
    field(a.position_enabled == b.position_enabled, "position_enabled");
    field(SameBits(a.pos_sens_x, b.pos_sens_x), "pos_sens_x");
    field(SameBits(a.pos_sens_y, b.pos_sens_y), "pos_sens_y");
    field(SameBits(a.pos_sens_z, b.pos_sens_z), "pos_sens_z");
    field(a.invert_pos_x == b.invert_pos_x, "invert_pos_x");
    field(a.invert_pos_y == b.invert_pos_y, "invert_pos_y");
    field(a.invert_pos_z == b.invert_pos_z, "invert_pos_z");
    field(SameBits(a.pos_limit_x, b.pos_limit_x), "pos_limit_x");
    field(SameBits(a.pos_limit_y, b.pos_limit_y), "pos_limit_y");
    field(SameBits(a.pos_limit_z, b.pos_limit_z), "pos_limit_z");
    field(SameBits(a.pos_limit_z_back, b.pos_limit_z_back), "pos_limit_z_back");
    field(SameBits(a.pivot_forward, b.pivot_forward), "pivot_forward");
    field(SameBits(a.pivot_up, b.pivot_up), "pivot_up");
    field(a.log_to_file == b.log_to_file, "log_to_file");
    field(a.log_path == b.log_path, "log_path");
    field(a.load_notes == b.load_notes, "load notes");
    field(a.sanitize_notes == b.sanitize_notes, "sanitize notes");
    field(a.tracking_enabled == b.tracking_enabled, "startup: tracking enabled");
    field(a.tracking_mode == b.tracking_mode, "startup: tracking mode");
    field(a.world_yaw == b.world_yaw, "startup: yaw mode");
    field(SameBindings(a.toggle, b.toggle), "hotkeys: toggle");
    field(SameBindings(a.yaw_mode, b.yaw_mode), "hotkeys: yaw mode");
    field(SameBindings(a.cycle_tracking_mode, b.cycle_tracking_mode), "hotkeys: cycle tracking mode");
    field(SameBindings(a.cycle_tracker_source, b.cycle_tracker_source), "hotkeys: cycle tracker source");
    field(SameBindings(a.body_follows_head_key, b.body_follows_head_key), "hotkeys: body follows head");
    return d;
}

void FrozenSourceTests() {
    std::cout << "Frozen sources\n";
    struct Recorded {
        fs::path path;
        const char* sha256;
    };
    const fs::path published = kDir / "oracle" / "published";
    const fs::path core = kRepo / "cameraunlock-core" / "cpp";
    const Recorded recorded[] = {
        {published / "src" / "Config.cpp", "df0b023ccf1d8dc559dae3a05d7a8f1934bef0a815c70954d2fdcb4595f7d30d"},
        {published / "src" / "HeadTracking.cpp", "d1f5c19d3c7548f262313c5cb7f73098900d6445d6cc5dc78973eaa22e139b61"},
        {published / "include/preyht/Config.hpp", "ab89510593b7a1d25bcde15733d117ea243c2e63a76e26a29bf1e1cda14478ca"},
        {published / "core/include/cameraunlock/config/ini_reader.h",
         "a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"},
        {published / "core/include/cameraunlock/data/position_settings.h",
         "b24dceb8e25475aebc5a468a5c7362a4a4e64204d183d1408525345f32f547f5"},
        {published / "core/include/cameraunlock/data/tracking_pose.h",
         "7cdb1ad02461e4cf37d3f2e2f9eedde12a9ed3238a61f8906302596589d2f629"},
        {published / "core/include/cameraunlock/effects/head_follow_light.h",
         "05c1af3befc789e7dfc665c459ee026cdd94c7a9286a805cd20f77c21606b24c"},
        {published / "core/include/cameraunlock/math/angle_utils.h",
         "d7a905270933e3cb0c4c361d29d3fd701655498cbcd1875ea79d180468bdbe6a"},
        {published / "core/include/cameraunlock/math/smoothing_utils.h",
         "fc2146f8c585e5f610c7234e302f59de4945679cfa28ff479ca47477ec073f22"},
        {published / "core/src/config/ini_reader.cpp",
         "e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"},
        {core / "include/cameraunlock/config/ini_reader.h",
         "a7ffb44210ff59672fa97e8e5feaa2cb3e81938fcc0334a384c68bc371b3857a"},
        {core / "src/config/ini_reader.cpp", "e01515c2656aaf533bae4350dc45b702c3e3d4043935743dcc9bd5ea581fbe1c"},
        {kDir / "inputs" / "shipped-dev-e87ed9e.ini",
         "32eaa6f7bc65bbd74eb119dd5caafb0cff8c26ac8243f99e0d5b89d3412fdda6"},
        {kRepo / "src/legacy_config/legacy_config.h", "58202c0a046d838089e048df260cecec8b7f4b89828e2cc5b0b56f56727cd3a7"},
        {kRepo / "src/legacy_config/legacy_config.cpp", "b5dc59b727c4694b766fc6ed568f244804b47565994c85f076a8ae97b65c5255"},
    };
    for (const Recorded& r : recorded)
        Check(Sha256(ReadBytes(r.path)) == r.sha256, r.path.lexically_relative(kRepo).generic_string() +
                                                         " hashes as recorded");
}

// The import as the owner runs it on <file>, starting from the table's defaults,
// the frozen struct its reader filled and the notes Sanitize gave.
struct Imported {
    cameraunlock::config::ImportResult result;
    preyht::legacy::Config frozen;
    std::vector<std::string> sanitize_notes;
    preyht::Config config;
};

Imported RunImport(const fs::path& file) {
    Imported i{cameraunlock::config::ImportResult::Imported({}), {}, {}, preyht::ConfigTableFor().defaults()};
    preyht::legacy::LoadConfig(file.string(), i.frozen);
    i.sanitize_notes = preyht::legacy::Sanitize(i.frozen);
    cameraunlock::config::LegacyInput input;
    input.path = file.wstring();
    input.ansi_path = file.string();
    i.result = preyht::LegacyImportFor().run(input, i.config);
    return i;
}

std::vector<Binding> Plain(const std::string& key, const std::string& list) {
    const auto parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok()) throw std::runtime_error(key + "=" + list + " does not parse: " + parsed.error);
    std::vector<Binding> out;
    for (const auto& b : parsed.bindings) out.push_back(Binding{static_cast<unsigned>(b.modifiers), b.vk});
    return out;
}

// Every setting the runtime Config still has comes from the import's map, so a
// field the map mis-copies differs from the published build. Only what Config no
// longer holds comes from the frozen reader: the dropped settings, the legacy
// hotkey names and the notes.
prey_config_oracle::Result FromImport(const Imported& i) {
    const preyht::Config& c = i.config;
    prey_config_oracle::Result r = FromFrozen(i.frozen, i.sanitize_notes);
    r.udp_port = static_cast<uint16_t>(c.udp_port);
    r.local_smoothing = c.local_smoothing;
    r.remote_smoothing = c.remote_smoothing;
    r.world_space_yaw = c.world_space_yaw;
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
    r.pos_limit_x = c.limit_x;
    r.pos_limit_y = c.limit_y;
    r.pos_limit_z = c.limit_z;
    r.pos_limit_z_back = c.limit_z_back;
    r.log_to_file = c.log_to_file;
    r.log_path = c.log_path;

    r.tracking_enabled = c.enable_on_startup;
    r.tracking_mode = static_cast<int>(preyht::StartupMode(c));
    r.world_yaw = c.world_space_yaw;
    r.toggle = Plain("ToggleKey", c.toggle_key);
    r.yaw_mode = Plain("YawModeKey", c.yaw_mode_key);
    r.cycle_tracking_mode = Plain("CycleTrackingModeKey", c.cycle_tracking_mode_key);
    r.cycle_tracker_source = Plain("CycleTrackerSourceKey", c.cycle_tracker_source_key);
    r.body_follows_head_key = Plain("BodyFollowsHeadKey", c.body_follows_head_key);
    return r;
}

// Every field of the runtime Config.
std::vector<std::string> ConfigDifferences(const preyht::Config& a, const preyht::Config& b) {
    std::vector<std::string> d;
    const auto field = [&d](bool same, const char* name) {
        if (!same) d.push_back(name);
    };
    field(a.udp_port == b.udp_port, "udp_port");
    field(a.enable_on_startup == b.enable_on_startup, "enable_on_startup");
    field(a.world_space_yaw == b.world_space_yaw, "world_space_yaw");
    field(a.rotation_enabled == b.rotation_enabled, "rotation_enabled");
    field(a.position_enabled == b.position_enabled, "position_enabled");
    field(SameBits(a.local_smoothing, b.local_smoothing), "local_smoothing");
    field(SameBits(a.remote_smoothing, b.remote_smoothing), "remote_smoothing");
    field(SameBits(a.limit_x, b.limit_x), "limit_x");
    field(SameBits(a.limit_y, b.limit_y), "limit_y");
    field(SameBits(a.limit_y_down, b.limit_y_down), "limit_y_down");
    field(SameBits(a.limit_z, b.limit_z), "limit_z");
    field(SameBits(a.limit_z_back, b.limit_z_back), "limit_z_back");
    field(a.compensate_flashlight == b.compensate_flashlight, "compensate_flashlight");
    field(SameBits(a.flashlight_scale, b.flashlight_scale), "flashlight_scale");
    field(a.toggle_key == b.toggle_key, "toggle_key");
    field(a.cycle_tracking_mode_key == b.cycle_tracking_mode_key, "cycle_tracking_mode_key");
    field(a.yaw_mode_key == b.yaw_mode_key, "yaw_mode_key");
    field(a.cycle_tracker_source_key == b.cycle_tracker_source_key, "cycle_tracker_source_key");
    field(a.body_follows_head_key == b.body_follows_head_key, "body_follows_head_key");
    field(a.compensate_markers == b.compensate_markers, "compensate_markers");
    field(SameBits(a.field_of_view, b.field_of_view), "field_of_view");
    field(a.match_weapon_fov == b.match_weapon_fov, "match_weapon_fov");
    field(a.early_inject == b.early_inject, "early_inject");
    field(a.disable_coverage_buffer == b.disable_coverage_buffer, "disable_coverage_buffer");
    field(a.compensate_body == b.compensate_body, "compensate_body");
    field(a.body_follows_head == b.body_follows_head, "body_follows_head");
    field(a.dump_camera == b.dump_camera, "dump_camera");
    field(a.trace_body_nodes == b.trace_body_nodes, "trace_body_nodes");
    field(a.hide_body_nodes == b.hide_body_nodes, "hide_body_nodes");
    field(a.force_flashlight == b.force_flashlight, "force_flashlight");
    field(a.trace_lights == b.trace_lights, "trace_lights");
    field(a.trace_light_reader == b.trace_light_reader, "trace_light_reader");
    field(a.trace_camera_readers == b.trace_camera_readers, "trace_camera_readers");
    field(a.clean_camera_for_reader == b.clean_camera_for_reader, "clean_camera_for_reader");
    field(a.clean_camera_reader_end == b.clean_camera_reader_end, "clean_camera_reader_end");
    field(a.log_to_file == b.log_to_file, "log_to_file");
    field(a.log_path == b.log_path, "log_path");
    return d;
}

std::vector<fs::path> Listing(const fs::path& dir) {
    std::vector<fs::path> names;
    for (const auto& entry : fs::directory_iterator(dir)) names.push_back(entry.path().filename());
    return names;
}

std::vector<fs::path> Sorted(std::vector<fs::path> names) {
    std::sort(names.begin(), names.end());
    return names;
}

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines)
        if (line.find(text) != std::string::npos) return true;
    return false;
}

using cameraunlock::config::schema::Concept;

// The rows the import must leave to Defaults.ini: every one whose legacy value is
// what the published build shipped, derived here from the frozen reader's output
// and its own defaults, the tracking mode as one unit.
std::set<Concept> UntouchedRows(const preyht::legacy::Config& read) {
    const preyht::legacy::Config shipped;
    std::set<Concept> rows;
    const auto row = [&rows](bool unchanged, Concept id) {
        if (unchanged) rows.insert(id);
    };
    using preyht::legacy::ParseVk;
    row(read.udp_port == shipped.udp_port, Concept::UdpPort);
    row(true, Concept::EnableOnStartup);
    row(read.world_space_yaw == shipped.world_space_yaw, Concept::WorldSpaceYaw);
    row(read.position_enabled == shipped.position_enabled, Concept::RotationEnabled);
    row(read.position_enabled == shipped.position_enabled, Concept::PositionEnabled);
    row(read.local_smoothing == shipped.local_smoothing, Concept::LocalSmoothing);
    row(read.remote_smoothing == shipped.remote_smoothing, Concept::RemoteSmoothing);
    row(read.pos_limit_x == shipped.pos_limit_x, Concept::PositionLimitX);
    row(read.pos_limit_y == shipped.pos_limit_y, Concept::PositionLimitY);
    row(read.pos_limit_y == shipped.pos_limit_y, Concept::PositionLimitYDown);
    row(read.pos_limit_z == shipped.pos_limit_z, Concept::PositionLimitZ);
    row(read.pos_limit_z_back == shipped.pos_limit_z_back, Concept::PositionLimitZBack);
    row(read.compensate_flashlight == shipped.compensate_flashlight, Concept::LightFollowsHead);
    row(read.flashlight_scale == shipped.flashlight_scale, Concept::LightMultiplier);
    row(ParseVk(read.toggle_key) == ParseVk(shipped.toggle_key), Concept::ToggleKey);
    row(ParseVk(read.position_key) == ParseVk(shipped.position_key), Concept::CycleTrackingModeKey);
    row(ParseVk(read.yaw_mode_key) == ParseVk(shipped.yaw_mode_key), Concept::YawModeKey);
    return rows;
}

// Every global row of the table: the rows a file nobody changed leaves to
// Defaults.ini.
const std::set<Concept> kAllRows = {
    Concept::UdpPort, Concept::EnableOnStartup, Concept::WorldSpaceYaw, Concept::RotationEnabled,
    Concept::PositionEnabled, Concept::LocalSmoothing, Concept::RemoteSmoothing, Concept::PositionLimitX,
    Concept::PositionLimitY, Concept::PositionLimitYDown, Concept::PositionLimitZ, Concept::PositionLimitZBack,
    Concept::LightFollowsHead, Concept::LightMultiplier, Concept::ToggleKey, Concept::CycleTrackingModeKey,
    Concept::YawModeKey,
};

// A Defaults.ini value other than the built-in one on every global row this
// game binds, as the built-in file spells each line and as SkewedConfig() reads
// the new value.
struct SkewedLine {
    const char* builtin;
    const char* skewed;
};
const SkewedLine kSkewedLines[] = {
    {"UdpPort=4242", "UdpPort=4343"},
    {"EnableOnStartup=true", "EnableOnStartup=false"},
    {"WorldSpaceYaw=true", "WorldSpaceYaw=false"},
    {"RotationEnabled=true", "RotationEnabled=false"},
    {"LocalSmoothing=0.0", "LocalSmoothing=0.3"},
    {"RemoteSmoothing=0.15", "RemoteSmoothing=0.4"},
    {"PositionLimitX=0.3", "PositionLimitX=0.25"},
    {"PositionLimitY=0.2", "PositionLimitY=0.15"},
    {"PositionLimitYDown=0.2", "PositionLimitYDown=0.12"},
    {"PositionLimitZ=0.4", "PositionLimitZ=0.35"},
    {"PositionLimitZBack=0.1", "PositionLimitZBack=0.07"},
    {"LightFollowsHead=true", "LightFollowsHead=false"},
    {"LightMultiplier=1.5", "LightMultiplier=2.5"},
    {"ToggleKey=End, Ctrl+Shift+Y", "ToggleKey=F9"},
    {"CycleTrackingModeKey=PageUp, Ctrl+Shift+G", "CycleTrackingModeKey=F10"},
    {"YawModeKey=PageDown, Ctrl+Shift+H", "YawModeKey=F11"},
};

// The settings a file of nothing but `default` runs on over the skewed
// Defaults.ini. The tracking mode there is position only.
preyht::Config SkewedConfig() {
    preyht::Config c = preyht::ConfigTableFor().defaults();
    c.udp_port = 4343;
    c.enable_on_startup = false;
    c.world_space_yaw = false;
    c.rotation_enabled = false;
    c.position_enabled = true;
    c.local_smoothing = 0.3f;
    c.remote_smoothing = 0.4f;
    c.limit_x = 0.25f;
    c.limit_y = 0.15f;
    c.limit_y_down = 0.12f;
    c.limit_z = 0.35f;
    c.limit_z_back = 0.07f;
    c.compensate_flashlight = false;
    c.flashlight_scale = 2.5f;
    c.toggle_key = "F9";
    c.cycle_tracking_mode_key = "F10";
    c.yaw_mode_key = "F11";
    return c;
}

// @p c with every row in @p follows taken from @p over.
preyht::Config OverDefaults(preyht::Config c, const std::set<Concept>& follows, const preyht::Config& over) {
    const auto take = [&follows](Concept id) { return follows.count(id) != 0; };
    if (take(Concept::UdpPort)) c.udp_port = over.udp_port;
    if (take(Concept::EnableOnStartup)) c.enable_on_startup = over.enable_on_startup;
    if (take(Concept::WorldSpaceYaw)) c.world_space_yaw = over.world_space_yaw;
    if (take(Concept::RotationEnabled)) c.rotation_enabled = over.rotation_enabled;
    if (take(Concept::PositionEnabled)) c.position_enabled = over.position_enabled;
    if (take(Concept::LocalSmoothing)) c.local_smoothing = over.local_smoothing;
    if (take(Concept::RemoteSmoothing)) c.remote_smoothing = over.remote_smoothing;
    if (take(Concept::PositionLimitX)) c.limit_x = over.limit_x;
    if (take(Concept::PositionLimitY)) c.limit_y = over.limit_y;
    if (take(Concept::PositionLimitYDown)) c.limit_y_down = over.limit_y_down;
    if (take(Concept::PositionLimitZ)) c.limit_z = over.limit_z;
    if (take(Concept::PositionLimitZBack)) c.limit_z_back = over.limit_z_back;
    if (take(Concept::LightFollowsHead)) c.compensate_flashlight = over.compensate_flashlight;
    if (take(Concept::LightMultiplier)) c.flashlight_scale = over.flashlight_scale;
    if (take(Concept::ToggleKey)) c.toggle_key = over.toggle_key;
    if (take(Concept::CycleTrackingModeKey)) c.cycle_tracking_mode_key = over.cycle_tracking_mode_key;
    if (take(Concept::YawModeKey)) c.yaw_mode_key = over.yaw_mode_key;
    return c;
}

std::string ConceptKey(Concept id) {
    return cameraunlock::config::schema::kConcepts[static_cast<std::size_t>(id)].key;
}

// The legacy file as a load must leave it: its bytes, its last write time and,
// for a read-only copy, its read-only attribute.
struct LegacyState {
    std::string bytes;
    FILETIME written{};
    bool read_only = false;
};

LegacyState StateOf(const fs::path& file) { return {ReadBytes(file), WriteTime(file), IsReadOnly(file)}; }

bool Unchanged(const fs::path& file, const LegacyState& before) {
    return ReadBytes(file) == before.bytes && SameTime(WriteTime(file), before.written)
        && IsReadOnly(file) == before.read_only;
}

struct Migrated {
    preyht::Config config;
    std::string bytes;
};

// Puts <bytes> (or no file) in a fresh folder as HeadTracking.ini, read-only when
// @p readOnly, lets the mod's owner load, and checks what the load leaves behind.
// Returns the settings the session runs on and the CameraUnlock.ini it created.
Migrated Migrate(Scratch& scratch, const Input& input, const Imported& imported, bool readOnly) {
    using cameraunlock::config::ConfigLoadStatus;
    using cameraunlock::config::ConfigOwner;

    const std::string name = input.name + (readOnly ? " (read-only)" : "");
    const fs::path dir = scratch.Folder("migrate");
    const fs::path legacy = dir / "HeadTracking.ini";
    const fs::path file = dir / "CameraUnlock.ini";
    LegacyState before;
    if (input.present) {
        WriteBytes(legacy, input.bytes);
        if (readOnly) SetFileAttributesW(legacy.c_str(), FILE_ATTRIBUTE_READONLY);
        before = StateOf(legacy);
    }
    const std::vector<fs::path> expectedListing = input.present
        ? std::vector<fs::path>{"CameraUnlock.ini", "HeadTracking.ini"}
        : std::vector<fs::path>{"CameraUnlock.ini"};

    ConfigOwner<preyht::Config> owner(preyht::OwnerOptions(dir.wstring(), scratch.Defaults()));
    const auto loaded = owner.Load();
    const ConfigLoadStatus expected = input.present ? ConfigLoadStatus::Migrated : ConfigLoadStatus::Created;
    if (loaded.status != expected) {
        Fail(name + ": the owner's load is " + cameraunlock::config::ConfigLoadStatusName(loaded.status) +
             ", not " + cameraunlock::config::ConfigLoadStatusName(expected) + " (" + loaded.reason + ")");
        return {loaded.config, {}};
    }
    if (input.present && !Unchanged(legacy, before)) Fail(name + ": the import changed HeadTracking.ini");
    if (Sorted(Listing(dir)) != expectedListing)
        Fail(name + ": the folder holds more than HeadTracking.ini and CameraUnlock.ini");

    const std::string migrated = ReadBytes(file);
    if (input.present) {
        for (const auto& dropped : imported.result.dropped) {
            if (!Contains(loaded.log, cameraunlock::config::DescribeDroppedValue(dropped)))
                Fail(name + ": the log does not name the dropped " + dropped.key);
        }
    }

    // The migrated bytes as the canonical reader and the table read them, over the
    // built-in values Defaults.ini holds: nothing to report, and the settings the
    // session runs on.
    const cameraunlock::config::CanonicalIni doc = cameraunlock::config::ParseCanonicalIni(migrated);
    const auto table = preyht::ConfigTableFor();
    preyht::Config reread = table.defaults();
    if (doc.status != cameraunlock::config::CanonicalReadStatus::Readable || !doc.diagnostics.empty()
            || !cameraunlock::config::ApplyCanonical(doc, table, reread).diagnostics.empty())
        Fail(name + ": the migrated file draws a diagnostic");
    for (const std::string& field : ConfigDifferences(reread, loaded.config))
        Fail(name + ": the migrated file reads back a different " + field);

    // A second launch reads CameraUnlock.ini, does not import, and changes neither
    // file.
    const FILETIME migratedTime = WriteTime(file);
    ConfigOwner<preyht::Config> next(preyht::OwnerOptions(dir.wstring(), scratch.Defaults()));
    const auto again = next.Load();
    if (again.status != ConfigLoadStatus::Canonical || !ConfigDifferences(again.config, loaded.config).empty()
            || ReadBytes(file) != migrated || !SameTime(WriteTime(file), migratedTime)
            || (input.present && !Unchanged(legacy, before)) || Sorted(Listing(dir)) != expectedListing)
        Fail(name + ": a second load did not read CameraUnlock.ini as it was and leave both files alone");
    if (input.present && !Contains(again.log, "is left as it was and is not read."))
        Fail(name + ": a second load does not log that HeadTracking.ini is not read");
    return {loaded.config, migrated};
}

void Comparisons(Scratch& scratch, std::set<std::string>& distinct) {
    std::cout << "Comparison 1, published build against the import, and comparison 2, import against migration\n";
    const std::vector<Input> inputs = Inputs();
    int compared = 0;
    int droppedModifier = 0;
    int droppedOutOfRange = 0;
    int droppedShaping = 0;
    int droppedReticle = 0;
    int droppedPivot = 0;
    int touched = 0;
    int modeTouched = 0;
    int skewedMigrated = 0;
    using cameraunlock::config::DropRule;
    for (const Input& input : inputs) {
        const fs::path oracleDir = scratch.Folder("oracle");
        const fs::path importDir = scratch.Folder("import");
        const fs::path importFile = importDir / "HeadTracking.ini";
        if (input.present) {
            WriteBytes(oracleDir / "HeadTracking.ini", input.bytes);
            WriteBytes(importFile, input.bytes);
            SetFileAttributesW(importFile.c_str(), FILE_ATTRIBUTE_READONLY);
        }

        const prey_config_oracle::Result published =
            prey_config_oracle::Startup((oracleDir / "HeadTracking.ini").string());
        const Imported imported = RunImport(importFile);

        // N3 and N1: the published build bound a Ctrl, Shift or Alt code, or 0xFF,
        // as a key of its own. The import leaves that one binding out and keeps
        // the chord.
        prey_config_oracle::Result oracle = published;
        const auto without = [](std::vector<Binding>& list, const std::string& name) {
            const int code = preyht::legacy::ParseVk(name);
            if (!IsModifierCode(code) && code != 0xFF) return;
            list.erase(std::remove_if(list.begin(), list.end(),
                                      [code](const Binding& b) { return b.modifiers == 0 && b.vk == code; }),
                       list.end());
        };
        without(oracle.toggle, imported.frozen.toggle_key);
        without(oracle.yaw_mode, imported.frozen.yaw_mode_key);
        without(oracle.cycle_tracking_mode, imported.frozen.position_key);
        for (const std::string& field : Differences(oracle, FromImport(imported)))
            Fail(input.name + ": comparison 1: " + field + " differs from the published build");
        if (!SameBits(imported.config.limit_y_down, published.pos_limit_y))
            Fail(input.name + ": comparison 1: limit_y_down is not the published build's one vertical limit");

        const auto expectedStatus = input.present ? cameraunlock::config::ImportStatus::Imported
                                                  : cameraunlock::config::ImportStatus::Absent;
        if (imported.result.status != expectedStatus)
            Fail(input.name + ": the import's status is not " + (input.present ? "Imported" : "Absent"));

        // Every value the conversion drops, from what the frozen reader read.
        const preyht::legacy::Config& f = imported.frozen;
        const preyht::legacy::Config shipped;
        std::set<std::pair<DropRule, std::string>> expectedDrops;
        const auto hotkeyDrop = [&expectedDrops](const std::string& name, const char* key) {
            const int code = preyht::legacy::ParseVk(name);
            if (IsModifierCode(code)) expectedDrops.insert({DropRule::ModifierKey, key});
            if (code == 0xFF) expectedDrops.insert({DropRule::KeyCodeOutOfRange, key});
        };
        hotkeyDrop(f.toggle_key, "ToggleKey");
        hotkeyDrop(f.yaw_mode_key, "YawModeKey");
        hotkeyDrop(f.position_key, "PositionKey");
        const auto shaping = [&expectedDrops](bool changed, const char* key) {
            if (changed) expectedDrops.insert({DropRule::PoseShaping, key});
        };
        shaping(f.yaw_sens != shipped.yaw_sens, "YawSensitivity");
        shaping(f.pitch_sens != shipped.pitch_sens, "PitchSensitivity");
        shaping(f.roll_sens != shipped.roll_sens, "RollSensitivity");
        shaping(f.invert_yaw != shipped.invert_yaw, "InvertYaw");
        shaping(f.invert_pitch != shipped.invert_pitch, "InvertPitch");
        shaping(f.invert_roll != shipped.invert_roll, "InvertRoll");
        shaping(f.deadzone != shipped.deadzone, "Deadzone");
        shaping(f.pos_sens_x != shipped.pos_sens_x, "SensitivityX");
        shaping(f.pos_sens_y != shipped.pos_sens_y, "SensitivityY");
        shaping(f.pos_sens_z != shipped.pos_sens_z, "SensitivityZ");
        shaping(f.invert_pos_x != shipped.invert_pos_x, "InvertX");
        shaping(f.invert_pos_y != shipped.invert_pos_y, "InvertY");
        shaping(f.invert_pos_z != shipped.invert_pos_z, "InvertZ");
        if (!f.compensate_reticle) expectedDrops.insert({DropRule::Reticle, "CompensateReticle"});
        if (f.pivot_forward != shipped.pivot_forward) expectedDrops.insert({DropRule::TrackerPivot, "PivotForward"});
        if (f.pivot_up != shipped.pivot_up) expectedDrops.insert({DropRule::TrackerPivot, "PivotUp"});
        std::set<std::pair<DropRule, std::string>> drops;
        for (const auto& d : imported.result.dropped) drops.insert({d.rule, d.key});
        if (drops != expectedDrops || drops.size() != imported.result.dropped.size())
            Fail(input.name + ": the import's dropped values are not exactly the ones the conversion drops");
        for (const auto& d : drops) {
            if (d.first == DropRule::ModifierKey) ++droppedModifier;
            if (d.first == DropRule::KeyCodeOutOfRange) ++droppedOutOfRange;
            if (d.first == DropRule::PoseShaping) ++droppedShaping;
            if (d.first == DropRule::Reticle) ++droppedReticle;
            if (d.first == DropRule::TrackerPivot) ++droppedPivot;
        }
        if (imported.result.pose_shaping.size() != 13)
            Fail(input.name + ": the import does not report all thirteen pose-shaping settings");

        const std::set<Concept> follows(imported.result.follows_defaults_ini.begin(),
                                        imported.result.follows_defaults_ini.end());
        const std::set<Concept> untouched = UntouchedRows(imported.frozen);
        if (follows.size() != imported.result.follows_defaults_ini.size() || follows != untouched)
            Fail(input.name + ": the rows left to Defaults.ini are not exactly the ones the player never changed");
        if (!input.present && follows != kAllRows)
            Fail(input.name + ": with no file, not every row follows Defaults.ini");
        if (untouched != kAllRows) ++touched;
        if (!untouched.count(Concept::RotationEnabled)) ++modeTouched;

        if (input.present && (Listing(importDir) != std::vector<fs::path>{"HeadTracking.ini"}
                              || ReadBytes(importFile) != input.bytes))
            Fail(input.name + ": the import changed the folder of a read-only file");

        const Migrated migrated = Migrate(scratch, input, imported, false);
        for (const std::string& field : ConfigDifferences(imported.config, migrated.config)) {
            // The two defaults the conversion moves to the shipped file's values,
            // which only an install with no file sees.
            if (!input.present && (field == "compensate_markers" || field == "log_path")) continue;
            Fail(input.name + ": comparison 2: " + field + " differs between the import and the migration");
        }
        if (input.present) {
            const Migrated fromReadOnly = Migrate(scratch, input, imported, true);
            if (fromReadOnly.bytes != migrated.bytes
                    || !ConfigDifferences(fromReadOnly.config, migrated.config).empty())
                Fail(input.name + ": a read-only HeadTracking.ini migrates differently from a writable one");

            // Over a Defaults.ini that differs everywhere: an untouched row is
            // written default and takes its value, a changed row keeps the player's.
            const fs::path dir = scratch.Folder("skewed");
            WriteBytes(dir / "HeadTracking.ini", input.bytes);
            cameraunlock::config::ConfigOwner<preyht::Config> owner(
                preyht::OwnerOptions(dir.wstring(), scratch.Skewed()));
            const auto loaded = owner.Load();
            if (loaded.status != cameraunlock::config::ConfigLoadStatus::Migrated) {
                Fail(input.name + " (skewed Defaults.ini): the owner's load is not Migrated");
            } else {
                const preyht::Config want = OverDefaults(imported.config, follows, SkewedConfig());
                for (const std::string& field : ConfigDifferences(want, loaded.config))
                    Fail(input.name + " (skewed Defaults.ini): " + field +
                         " is not Defaults.ini's where untouched and the import's where changed");
                const std::string bytes = ReadBytes(dir / "CameraUnlock.ini");
                for (const Concept row : follows)
                    if (bytes.find("\r\n" + ConceptKey(row) + "=default\r\n") == std::string::npos)
                        Fail(input.name + " (skewed Defaults.ini): " + ConceptKey(row) + " is not written default");
                distinct.insert(bytes);
                ++skewedMigrated;
            }
        }
        distinct.insert(migrated.bytes);
        ++compared;
    }
    Check(compared > 1000, std::to_string(compared) + " inputs compared");
    Check(droppedModifier >= 27,
          std::to_string(droppedModifier) + " hotkeys on a Ctrl, Shift or Alt key alone dropped as ModifierKey");
    Check(droppedOutOfRange >= 3, std::to_string(droppedOutOfRange) + " hotkeys on 0xFF dropped as KeyCodeOutOfRange");
    Check(droppedShaping > 0 && droppedReticle > 0 && droppedPivot > 0,
          std::to_string(droppedShaping) + " pose-shaping, " + std::to_string(droppedReticle) + " reticle and " +
              std::to_string(droppedPivot) + " pivot values dropped");
    Check(touched > 0 && modeTouched > 0,
          std::to_string(touched) + " inputs change a row, " + std::to_string(modeTouched) +
              " of them the tracking mode, which then does not follow Defaults.ini");
    Check(skewedMigrated == compared - 1,
          std::to_string(skewedMigrated) + " present inputs migrated over a Defaults.ini that differs everywhere");
}

// The published build's shipped HeadTracking.ini, which is also what its players
// hold if they never changed a setting, imports into exactly the file a fresh
// install creates: with Defaults.ini at the built-in values every row it leaves
// at its default migrates as `default`. Its launcher seed is the same bytes.
void FreshEqualsUpgradeTests(Scratch& scratch) {
    std::cout << "Fresh install against upgrade\n";
    const std::string committed = ReadBytes(kRepo / "HeadTracking.ini");

    const fs::path upgraded = scratch.Folder("upgrade");
    WriteBytes(upgraded / "HeadTracking.ini", PublishedShipped());
    cameraunlock::config::ConfigOwner<preyht::Config> upgrade(
        preyht::OwnerOptions(upgraded.wstring(), scratch.Defaults()));
    const auto loaded = upgrade.Load();
    Check(loaded.status == cameraunlock::config::ConfigLoadStatus::Migrated
              && ReadBytes(upgraded / "CameraUnlock.ini") == committed,
          "the published shipped file imports into a CameraUnlock.ini equal to the committed file");
    Check(ReadBytes(upgraded / "HeadTracking.ini") == PublishedShipped(),
          "and HeadTracking.ini keeps the published build's bytes");

    const fs::path fresh = scratch.Folder("fresh");
    cameraunlock::config::ConfigOwner<preyht::Config> create(
        preyht::OwnerOptions(fresh.wstring(), scratch.Defaults()));
    Check(create.Load().status == cameraunlock::config::ConfigLoadStatus::Created
              && ReadBytes(fresh / "CameraUnlock.ini") == committed,
          "a fresh install creates the committed file byte for byte as CameraUnlock.ini");
}

// Writes the skewed Defaults.ini from the built-in one an earlier owner created,
// and checks it: a fresh install over it writes the committed file and runs on
// its values.
void SkewedDefaultsTests(Scratch& scratch) {
    std::cout << "Skewed Defaults.ini\n";
    std::string text = ReadBytes(scratch.BuiltinPath());
    for (const SkewedLine& line : kSkewedLines)
        text = Replaced(text, std::string("\r\n") + line.builtin + "\r\n",
                        std::string("\r\n") + line.skewed + "\r\n");
    fs::create_directories(scratch.SkewedPath().parent_path());
    WriteBytes(scratch.SkewedPath(), text);

    const fs::path fresh = scratch.Folder("fresh-skewed");
    cameraunlock::config::ConfigOwner<preyht::Config> create(
        preyht::OwnerOptions(fresh.wstring(), scratch.Skewed()));
    const auto loaded = create.Load();
    Check(loaded.status == cameraunlock::config::ConfigLoadStatus::Created
              && ConfigDifferences(loaded.config, SkewedConfig()).empty()
              && ReadBytes(fresh / "CameraUnlock.ini") == ReadBytes(kRepo / "HeadTracking.ini"),
          "a fresh install over the skewed Defaults.ini writes the committed file and runs on its values");
}

// Each distinct migrated file, under migrated\ beside this executable, replacing
// what an earlier run left there.
void WriteForLint(const std::set<std::string>& distinct) {
    wchar_t exe[MAX_PATH] = {};
    const DWORD length = GetModuleFileNameW(nullptr, exe, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) throw std::runtime_error("cannot read the test's own path");
    const fs::path dir = fs::path(std::wstring(exe, length)).parent_path() / "migrated";
    fs::remove_all(dir);
    fs::create_directories(dir);
    int n = 0;
    for (const std::string& bytes : distinct) WriteBytes(dir / (std::to_string(n++) + ".ini"), bytes);
    std::cout << "  wrote " << distinct.size() << " distinct migrated files to " << dir.string() << "\n";
}

}  // namespace

int main() {
    std::cout << "Prey config differential test\n";
    try {
        Scratch scratch;
        FrozenSourceTests();
        FreshEqualsUpgradeTests(scratch);
        SkewedDefaultsTests(scratch);
        std::set<std::string> distinct;
        Comparisons(scratch, distinct);
        WriteForLint(distinct);
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] threw: " << e.what() << "\n";
        ++g_failures;
    }
    if (g_failures == 0) {
        std::cout << "All differential tests passed\n";
        return 0;
    }
    std::cout << g_failures << " differential test(s) FAILED\n";
    return 1;
}
