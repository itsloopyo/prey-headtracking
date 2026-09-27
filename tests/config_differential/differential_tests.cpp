// The differential test for the config conversion.
//
// Oracle: the reader of the newest published build (the rolling dev pre-release,
// e87ed9e, core ee8cc72), vendored under oracle/published/ byte for byte, with the
// startup code that turned its output into the session's state.
// Import: the frozen reader in src/PreyHeadTracking/legacy_config/.
//
// Every input runs through both: the published build's shipped HeadTracking.ini
// (its ZIPs and its launcher seed hold the same bytes), no file, an empty file and
// the corpus core generates from the shipped file.
//
// Comparison 1, oracle against import, compares every field of the published
// Config after Sanitize, floats bit for bit, the notes it collected, the startup
// state and the registered hotkeys. No commit since e87ed9e changed how the file
// is read, so it finds no difference.
//
// What is recorded here, and checked by hash below:
//   - The published builds: only the dev pre-release, at e87ed9e. No v* tag and no
//     predecessor repo exists.
//   - oracle/published/src and include: e87ed9e:src/PreyHeadTracking/Config.cpp,
//     e87ed9e:include/preyht/Config.hpp and, for ParseVk, which oracle.cpp
//     transcribes, e87ed9e:src/PreyHeadTracking/mods/HeadTracking.cpp.
//   - oracle/published/core: the core sources those include, at ee8cc72, the pin
//     e87ed9e built against.
//   - The frozen import: src/PreyHeadTracking/legacy_config/. It compiles core's
//     IniReader, which hashes equal to ee8cc72's.
//   - inputs/: the published build's shipped HeadTracking.ini. The build writes no
//     file at first launch.

#include <windows.h>

#include <bcrypt.h>

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include <cameraunlock/config/testing/ini_mutations.h>

#include "legacy_config/legacy_config.h"
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

std::vector<Input> Inputs() {
    std::vector<Input> inputs;
    inputs.push_back({"shipped config of the published build (dev, e87ed9e)", true, PublishedShipped()});
    inputs.push_back({"no file", false, {}});
    inputs.push_back({"empty file", true, {}});
    for (IniMutation& m : GenerateIniMutations(PublishedShipped(), preyht::legacy::Keys(), Descriptors()))
        inputs.push_back({"corpus: " + m.name, true, std::move(m.bytes)});
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
        {kRepo / "src/PreyHeadTracking/legacy_config/legacy_config.h", "58202c0a046d838089e048df260cecec8b7f4b89828e2cc5b0b56f56727cd3a7"},
        {kRepo / "src/PreyHeadTracking/legacy_config/legacy_config.cpp", "b5dc59b727c4694b766fc6ed568f244804b47565994c85f076a8ae97b65c5255"},
    };
    for (const Recorded& r : recorded)
        Check(Sha256(ReadBytes(r.path)) == r.sha256, r.path.lexically_relative(kRepo).generic_string() +
                                                         " hashes as recorded");
}

void Comparisons(Scratch& scratch) {
    std::cout << "Comparison 1, published build against the frozen reader\n";
    const std::vector<Input> inputs = Inputs();
    int compared = 0;
    for (const Input& input : inputs) {
        const fs::path oracleDir = scratch.Folder("oracle");
        const fs::path importDir = scratch.Folder("import");
        if (input.present) {
            WriteBytes(oracleDir / "HeadTracking.ini", input.bytes);
            WriteBytes(importDir / "HeadTracking.ini", input.bytes);
        }
        const prey_config_oracle::Result published =
            prey_config_oracle::Startup((oracleDir / "HeadTracking.ini").string());
        preyht::legacy::Config frozen;
        if (!preyht::legacy::LoadConfig((importDir / "HeadTracking.ini").string(), frozen))
            Fail(input.name + ": the frozen reader could not open the file");
        std::vector<std::string> notes = preyht::legacy::Sanitize(frozen);
        for (const std::string& field : Differences(published, FromFrozen(frozen, std::move(notes))))
            Fail(input.name + ": comparison 1: " + field + " differs from the published build");
        ++compared;
    }
    Check(compared > 1000, std::to_string(compared) + " inputs compared");
}

}  // namespace

int main() {
    std::cout << "Prey config differential test\n";
    try {
        Scratch scratch;
        FrozenSourceTests();
        Comparisons(scratch);
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
