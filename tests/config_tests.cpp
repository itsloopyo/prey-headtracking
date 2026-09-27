// CameraUnlock.ini: the committed file the table renders, what the owner creates
// and reads, what it takes from Defaults.ini, what each hotkey saves and what it
// may not, how it imports HeadTracking.ini and leaves it alone, and the frozen
// legacy reader's own contract.

#include "preyht/Config.hpp"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "cameraunlock/config/config_owner.h"
#include "cameraunlock/config/defaults_file.h"
#include "cameraunlock/input/key_bindings.h"

#include "legacy_config/legacy_config.h"

namespace {

namespace fs = std::filesystem;
using cameraunlock::config::ConfigLoadStatus;
using cameraunlock::config::ConfigOwner;
using cameraunlock::config::ConfigSaveStatus;
using cameraunlock::config::DefaultsFile;

int g_failures = 0;

void Check(bool cond, const char* name) {
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << name << "\n";
    if (!cond) ++g_failures;
}

bool Near(float a, float b) { return std::fabs(a - b) < 1e-6f; }

const fs::path kCommitted = fs::path(PREY_REPO_DIR) / "HeadTracking.ini";

std::string ReadBytes(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot read " + path.string());
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

void WriteBytes(const fs::path& path, const std::string& bytes) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << bytes;
    if (!out) throw std::runtime_error("cannot write " + path.string());
}

FILETIME WriteTime(const fs::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data))
        throw std::runtime_error("cannot stat " + path.string());
    return data.ftLastWriteTime;
}

bool SameTime(const FILETIME& a, const FILETIME& b) {
    return a.dwLowDateTime == b.dwLowDateTime && a.dwHighDateTime == b.dwHighDateTime;
}

// A new, empty game folder, with a scratch Defaults.ini under the test's own root
// rather than the player's, so no test reaches %AppData%.
struct Folder {
    fs::path game;
    fs::path defaults;

    explicit Folder(const char* name) {
        const fs::path root = fs::temp_directory_path() / "prey-ht-config-tests" / name;
        std::error_code ignored;
        fs::remove_all(root, ignored);
        game = root / "game";
        fs::create_directories(game);
        defaults = root / "global" / "Defaults.ini";
    }

    fs::path Config() const { return game / "CameraUnlock.ini"; }
    fs::path Legacy() const { return game / "HeadTracking.ini"; }

    std::unique_ptr<ConfigOwner<preyht::Config>> Owner() const {
        return std::make_unique<ConfigOwner<preyht::Config>>(
            preyht::OwnerOptions(game.wstring(), DefaultsFile::At(defaults.wstring())));
    }

    std::vector<std::string> Listing() const {
        std::vector<std::string> names;
        for (const auto& entry : fs::directory_iterator(game)) names.push_back(entry.path().filename().string());
        std::sort(names.begin(), names.end());
        return names;
    }
};

bool Contains(const std::vector<std::string>& lines, const std::string& text) {
    for (const std::string& line : lines)
        if (line.find(text) != std::string::npos) return true;
    return false;
}

// The lines of @p b that differ from @p a, which must have as many.
std::vector<std::string> ChangedLines(const std::string& a, const std::string& b) {
    std::vector<std::string> la, lb, changed;
    std::istringstream sa(a), sb(b);
    for (std::string line; std::getline(sa, line);) la.push_back(line);
    for (std::string line; std::getline(sb, line);) lb.push_back(line);
    if (la.size() != lb.size()) return {"(line count differs)"};
    for (std::size_t i = 0; i < la.size(); ++i)
        if (la[i] != lb[i]) changed.push_back(lb[i]);
    return changed;
}

std::string Render() {
    cameraunlock::config::RenderHeader header;
    header.display_name = preyht::kGameDisplayName;
    return cameraunlock::config::RenderCanonicalFresh(preyht::ConfigTableFor(), header);
}

// The committed file with each `from` line replaced by `to`.
std::string CommittedWith(const std::vector<std::pair<std::string, std::string>>& lines) {
    std::string text = ReadBytes(kCommitted);
    for (const auto& [from, to] : lines) {
        const std::size_t at = text.find(from + "\r\n");
        if (at == std::string::npos) throw std::logic_error(from + " is not a line of the committed file");
        text.replace(at, from.size(), to);
    }
    return text;
}

void CommittedFileTests() {
    std::cout << "Committed file\n";
    Check(ReadBytes(kCommitted) == Render(),
          "HeadTracking.ini is the table's fresh render (pixi run render-config rewrites it)");

    const Folder folder("created");
    const auto owner = folder.Owner();
    const auto loaded = owner->Load();
    Check(loaded.status == ConfigLoadStatus::Created, "with no file of either name, CameraUnlock.ini is created");
    Check(ReadBytes(folder.Config()) == ReadBytes(kCommitted), "and holds the committed file byte for byte");
    Check(folder.Listing() == std::vector<std::string>{"CameraUnlock.ini"}, "and nothing else is written beside it");
    Check(fs::exists(folder.defaults), "a missing Defaults.ini is created with the built-in values");

    const preyht::Config& c = loaded.config;
    Check(c.udp_port == 4242 && c.enable_on_startup && c.world_space_yaw && c.rotation_enabled
              && c.position_enabled && Near(c.field_of_view, 0.0f),
          "defaults: port 4242, on at startup, world yaw, 6DOF, the game's own field of view");
    Check(Near(c.local_smoothing, 0.0f) && Near(c.remote_smoothing, 0.15f),
          "defaults: smoothing 0 local, 0.15 remote");
    Check(Near(c.limit_x, 0.30f) && Near(c.limit_y, 0.20f) && Near(c.limit_y_down, 0.20f)
              && Near(c.limit_z, 0.40f) && Near(c.limit_z_back, 0.10f),
          "defaults: the position limits the published build shipped, more room to lean in than back");
    Check(c.compensate_flashlight && Near(c.flashlight_scale, 1.5f), "defaults: the beam follows the head at 1.5x");
    Check(c.compensate_markers && c.match_weapon_fov && c.early_inject && c.disable_coverage_buffer
              && c.compensate_body && c.body_follows_head,
          "defaults: every camera feature the published build shipped on is on, the HUD markers included");
    Check(!c.dump_camera && !c.trace_body_nodes && !c.hide_body_nodes && !c.force_flashlight && !c.trace_lights
              && !c.trace_light_reader && !c.trace_camera_readers && c.clean_camera_for_reader == 0
              && c.clean_camera_reader_end == 0,
          "defaults: every diagnostic is off");
    Check(c.log_to_file && c.log_path == "HeadTracking.log", "defaults: the log is HeadTracking.log");
    Check(c.toggle_key == "End, Ctrl+Shift+Y" && c.cycle_tracking_mode_key == "PageUp, Ctrl+Shift+G"
              && c.yaw_mode_key == "PageDown, Ctrl+Shift+H" && c.cycle_tracker_source_key == "Ctrl+Shift+U"
              && c.body_follows_head_key == "Delete, Ctrl+Shift+J",
          "the hotkeys default to the nav keys and chords the published build bound");
}

void CanonicalReadTests() {
    std::cout << "Canonical read\n";
    const Folder folder("canonical");
    WriteBytes(folder.Config(), CommittedWith({
        {"UdpPort=default", "UdpPort=5252"},
        {"EnableOnStartup=default", "EnableOnStartup=false"},
        {"RotationEnabled=default", "RotationEnabled=false"},
        {"FieldOfView=0.0", "FieldOfView=110.0"},
        {"BodyFollowsHead=true", "BodyFollowsHead=false"},
        {"ToggleKey=default", "ToggleKey=F9"},
        {"BodyFollowsHeadKey=Delete, Ctrl+Shift+J", "BodyFollowsHeadKey=F10"},
    }));
    const auto loaded = folder.Owner()->Load();
    const preyht::Config& c = loaded.config;
    Check(loaded.status == ConfigLoadStatus::Canonical && loaded.diagnostics.empty(),
          "a stamped file is read as canonical with nothing to report");
    Check(c.udp_port == 5252 && !c.enable_on_startup && !c.rotation_enabled && c.position_enabled
              && Near(c.field_of_view, 110.0f) && !c.body_follows_head && c.toggle_key == "F9"
              && c.body_follows_head_key == "F10",
          "every edited row is read");
    Check(preyht::StartupMode(c) == cameraunlock::TrackingMode::PositionOnly,
          "RotationEnabled=false with PositionEnabled=true starts position only");

    const Folder narrow("field-of-view");
    WriteBytes(narrow.Config(), CommittedWith({{"FieldOfView=0.0", "FieldOfView=20.0"}}));
    const auto refused = narrow.Owner()->Load();
    Check(!refused.diagnostics.empty() && Near(refused.config.field_of_view, 0.0f),
          "FieldOfView=20 is refused with a diagnostic and the game's own field of view is kept");
}

// A row holding `default` takes Defaults.ini's value; a value in the game's file
// wins over it.
void DefaultsIniTests() {
    std::cout << "Defaults.ini\n";
    const Folder folder("defaults-ini");
    fs::create_directories(folder.defaults.parent_path());
    WriteBytes(folder.defaults, "[General]\r\nWorldSpaceYaw=false\r\n[Hotkeys]\r\nToggleKey=F8\r\n"
                                "YawModeKey=F7\r\n[Network]\r\nUdpPort=5000\r\n");
    WriteBytes(folder.Config(), CommittedWith({{"YawModeKey=default", "YawModeKey=F6"}}));
    const std::string defaultsBefore = ReadBytes(folder.defaults);

    const auto owner = folder.Owner();
    const auto loaded = owner->Load();
    const preyht::Config& c = loaded.config;
    Check(loaded.status == ConfigLoadStatus::Canonical && !c.world_space_yaw && c.toggle_key == "F8"
              && c.udp_port == 5000,
          "rows holding default take Defaults.ini's values");
    Check(c.yaw_mode_key == "F6", "a value in CameraUnlock.ini wins over Defaults.ini");

    const std::string before = ReadBytes(folder.Config());
    const auto saved = owner->Save([](preyht::Config& config) { config.world_space_yaw = true; });
    Check(saved.status == ConfigSaveStatus::Saved
              && ChangedLines(before, ReadBytes(folder.Config())) == std::vector<std::string>{"WorldSpaceYaw=true\r"},
          "the yaw toggle writes its value over default and changes no other line");
    Check(ReadBytes(folder.defaults) == defaultsBefore, "the mod never writes Defaults.ini");
}

void SaveTests() {
    std::cout << "Saves\n";
    const Folder folder("save");
    const auto owner = folder.Owner();
    owner->Load();

    std::string before = ReadBytes(folder.Config());
    Check(owner->Save([](preyht::Config& c) { c.world_space_yaw = false; }).status == ConfigSaveStatus::Saved
              && ChangedLines(before, ReadBytes(folder.Config())) == std::vector<std::string>{"WorldSpaceYaw=false\r"},
          "the yaw mode saves, changing the WorldSpaceYaw line and no other byte");

    before = ReadBytes(folder.Config());
    const auto rotationOnly = cameraunlock::EncodeTrackingMode(cameraunlock::TrackingMode::RotationOnly);
    owner->Save([rotationOnly](preyht::Config& c) {
        c.rotation_enabled = rotationOnly.rotation_enabled;
        c.position_enabled = rotationOnly.position_enabled;
    });
    Check(ChangedLines(before, ReadBytes(folder.Config()))
              == std::vector<std::string>{"RotationEnabled=true\r", "PositionEnabled=false\r"},
          "a mode change writes both rows of the pair over default");

    before = ReadBytes(folder.Config());
    owner->Save([](preyht::Config& c) { c.body_follows_head = false; });
    Check(ChangedLines(before, ReadBytes(folder.Config())) == std::vector<std::string>{"BodyFollowsHead=false\r"},
          "the body key saves BodyFollowsHead and nothing else");

    bool refused = false;
    try {
        owner->Save([](preyht::Config& c) { c.enable_on_startup = false; });
    } catch (const std::logic_error&) {
        refused = true;
    }
    Check(refused, "EnableOnStartup is not Writable, so nothing End does can reach the file");

    const preyht::Config restarted = folder.Owner()->Load().config;
    Check(!restarted.world_space_yaw && restarted.rotation_enabled && !restarted.position_enabled
              && !restarted.body_follows_head && restarted.enable_on_startup,
          "every saved toggle comes back at the next start");
    Check(folder.Listing() == std::vector<std::string>{"CameraUnlock.ini"},
          "no save writes anything but CameraUnlock.ini");
}

// HeadTracking.ini is imported once, while CameraUnlock.ini is absent, and is
// never written.
void LegacyFileTests() {
    std::cout << "Legacy file\n";
    const Folder folder("legacy-file");
    const std::string legacy = "[Camera]\r\nWorldSpaceYaw=false\r\nCompensateReticle=false\r\n"
                               "[Position]\r\nLimitX=0.25\r\nSensitivityX=2.0\r\n";
    WriteBytes(folder.Legacy(), legacy);
    const FILETIME legacyTime = WriteTime(folder.Legacy());

    const auto first = folder.Owner()->Load();
    Check(first.status == ConfigLoadStatus::Migrated && !first.config.world_space_yaw
              && Near(first.config.limit_x, 0.25f),
          "with no CameraUnlock.ini, HeadTracking.ini is imported into a new one");
    Check(Contains(first.log, "CompensateReticle") && Contains(first.log, "SensitivityX"),
          "and the log names the reticle switch and the sensitivity it dropped");
    Check(folder.Listing() == std::vector<std::string>{"CameraUnlock.ini", "HeadTracking.ini"},
          "and the folder then holds the two files and nothing else");
    Check(ReadBytes(folder.Legacy()) == legacy && SameTime(WriteTime(folder.Legacy()), legacyTime),
          "HeadTracking.ini keeps its bytes and its write time");

    WriteBytes(folder.Legacy(), "[Camera]\r\nWorldSpaceYaw=true\r\n");
    const std::string migrated = ReadBytes(folder.Config());
    const auto second = folder.Owner()->Load();
    Check(second.status == ConfigLoadStatus::Canonical && !second.config.world_space_yaw
              && ReadBytes(folder.Config()) == migrated,
          "while CameraUnlock.ini exists, HeadTracking.ini is not read again");
    Check(Contains(second.log, "is left as it was and is not read."), "and the log says so");

    fs::remove(folder.Config());
    const auto third = folder.Owner()->Load();
    Check(third.status == ConfigLoadStatus::Migrated && third.config.world_space_yaw,
          "deleting only CameraUnlock.ini imports HeadTracking.ini again");
}

void HotkeyImportTests() {
    std::cout << "Legacy hotkeys\n";
    const Folder folder("legacy-hotkeys");
    WriteBytes(folder.Legacy(), "[Hotkeys]\r\nToggleKey=F8\r\nYawModeKey=nonsense\r\nPositionKey=0x10\r\n");
    const auto loaded = folder.Owner()->Load();
    const preyht::Config& c = loaded.config;
    Check(c.toggle_key == "F8, Ctrl+Shift+Y", "a named key keeps its chord beside it");
    Check(c.yaw_mode_key == "Ctrl+Shift+H", "a name the old build could not place leaves the chord alone");
    Check(c.cycle_tracking_mode_key == "Ctrl+Shift+G" && Contains(loaded.log, "PositionKey"),
          "a Shift key alone is unbound and logged, and the chord stays");
}

// The frozen reader, as the published builds read HeadTracking.ini.
void LegacyReaderTests() {
    std::cout << "Frozen reader\n";
    const auto read = [](const char* name, const char* body) {
        const Folder folder(name);
        WriteBytes(folder.Legacy(), body);
        preyht::legacy::Config config;
        preyht::legacy::LoadConfig(folder.Legacy().string(), config);
        preyht::legacy::Sanitize(config);
        return config;
    };

    Check(read("legacy-port-high", "[Network]\nUdpPort=70000\n").udp_port == 4242,
          "legacy: a port above 65535 keeps the default");
    Check(read("legacy-port-low", "[Network]\nUdpPort=80\n").udp_port == 4242,
          "legacy: a privileged port keeps the default");
    Check(read("legacy-port-ok", "[Network]\nUdpPort=5000\n").udp_port == 5000, "legacy: a valid port is read");

    const auto bad = read("legacy-values", "[Tracking]\nLocalSmoothing=nan\nRemoteSmoothing=inf\n"
                                           "[Camera]\nFlashlightScale=9\n[Position]\nLimitZ=-3\n");
    Check(Near(bad.local_smoothing, 0.0f) && Near(bad.remote_smoothing, 0.15f),
          "legacy: a non-finite smoothing takes its default");
    Check(Near(bad.flashlight_scale, 5.0f) && Near(bad.pos_limit_z, 0.01f),
          "legacy: an out-of-range scale or limit is clamped");

    Check(Near(read("legacy-fov-zero", "[Camera]\nFieldOfView=0\n").field_of_view, 0.0f),
          "legacy: FieldOfView 0 survives the clamp");
    Check(Near(read("legacy-fov-low", "[Camera]\nFieldOfView=10\n").field_of_view, 25.0f),
          "legacy: a field of view below 25 is raised to 25");

    Check(preyht::legacy::ParseVk("5") == '5' && preyht::legacy::ParseVk("PageDown") == 0x22
              && preyht::legacy::ParseVk("0x22") == 0x22 && preyht::legacy::ParseVk("F24") == 0x87
              && preyht::legacy::ParseVk("bogus") == 0,
          "legacy: hotkey names read as the published build read them");
}

}  // namespace

int main(int argc, char** argv) {
    // `pixi run render-config`: rewrite the committed file from the config table
    // and run no tests.
    if (argc == 3 && std::strcmp(argv[1], "--render-config") == 0) {
        std::ofstream out(argv[2], std::ios::binary | std::ios::trunc);
        out << Render();
        if (!out) {
            std::cout << "could not write " << argv[2] << "\n";
            return 1;
        }
        std::cout << "wrote " << argv[2] << "\n";
        return 0;
    }

    std::cout << "Prey config tests\n";
    try {
        CommittedFileTests();
        CanonicalReadTests();
        DefaultsIniTests();
        SaveTests();
        LegacyFileTests();
        HotkeyImportTests();
        LegacyReaderTests();
    } catch (const std::exception& e) {
        std::cout << "  [FAIL] threw: " << e.what() << "\n";
        ++g_failures;
    }
    if (g_failures == 0) {
        std::cout << "All config tests passed\n";
        return 0;
    }
    std::cout << g_failures << " config test(s) FAILED\n";
    return 1;
}
