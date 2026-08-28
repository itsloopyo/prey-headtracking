// Behaviour lock for the INI boundary.
//
// HeadTracking.ini is a system boundary in the sense AGENTS.md means: the values
// in it reach the live engine camera, the engine's own CVars and a socket bind.
// strtod accepts "inf" and "nan", ReadInt hands back a full int that a uint16_t
// port silently truncates, and neither failure looks like anything in game - the
// view turns to NaN, or the tracker talks to a port nobody is listening on.
//
// Config::Sanitize and the port check in Config::LoadFromFile are what stop that,
// and both are plain data-in/data-out, so they are checkable without launching
// Prey.

#include "preyht/Config.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>

namespace {

int g_failures = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        std::cout << "  [PASS] " << name << "\n";
    } else {
        std::cout << "  [FAIL] " << name << "\n";
        ++g_failures;
    }
}

/// Writes an INI and hands back its ABSOLUTE path. Absolute matters: Windows
/// resolves a relative profile path against the Windows directory, not the
/// working directory, so a relative one silently reads nothing. Each case gets
/// its own filename because the profile API caches by path.
class TempIni {
public:
    TempIni(const char* stem, const std::string& body) {
        m_path = std::filesystem::temp_directory_path() /
                 (std::string("preyht_") + stem + ".ini");
        std::ofstream out(m_path, std::ios::out | std::ios::trunc);
        out << body;
    }
    ~TempIni() {
        std::error_code ec;
        std::filesystem::remove(m_path, ec);
    }

    TempIni(const TempIni&) = delete;
    TempIni& operator=(const TempIni&) = delete;

    std::string Path() const { return m_path.string(); }

private:
    std::filesystem::path m_path;
};

bool MentionsUdpPort(const preyht::Config& cfg) {
    for (const auto& note : cfg.load_notes) {
        if (note.find("UdpPort") != std::string::npos) return true;
    }
    return false;
}

// ---- the socket bind ---------------------------------------------------------

void TestPortOutsideTheRangeIsRefused() {
    std::cout << "udp port:\n";
    // 70000 fits an int and does not fit a uint16_t: narrowing it binds 4464,
    // and the player is told nothing.
    const TempIni ini("port_high", "[Network]\nUdpPort = 70000\n");
    preyht::Config cfg;
    Check(preyht::Config::LoadFromFile(ini.Path(), cfg), "an INI with a bad port still parses");
    Check(cfg.udp_port == 4242,
          "a port past 65535 is refused rather than truncated into a different port");
    Check(MentionsUdpPort(cfg), "and the player is told which port is actually being listened on");
}

void TestPortThatWrapsToZeroIsRefused() {
    // 65536 narrows to 0, which binds whatever ephemeral port the OS hands out -
    // a socket no tracker can find.
    const TempIni ini("port_wrap", "[Network]\nUdpPort = 65536\n");
    preyht::Config cfg;
    preyht::Config::LoadFromFile(ini.Path(), cfg);
    Check(cfg.udp_port == 4242, "a port that would narrow to zero is refused");
}

void TestNegativePortIsRefused() {
    const TempIni ini("port_neg", "[Network]\nUdpPort = -1\n");
    preyht::Config cfg;
    preyht::Config::LoadFromFile(ini.Path(), cfg);
    Check(cfg.udp_port == 4242, "a negative port is refused");
}

void TestPrivilegedPortIsRefused() {
    const TempIni ini("port_low", "[Network]\nUdpPort = 80\n");
    preyht::Config cfg;
    preyht::Config::LoadFromFile(ini.Path(), cfg);
    Check(cfg.udp_port == 4242, "a port below 1024 is refused");
}

void TestValidPortIsAccepted() {
    const TempIni ini("port_ok", "[Network]\nUdpPort = 4250\n");
    preyht::Config cfg;
    preyht::Config::LoadFromFile(ini.Path(), cfg);
    Check(cfg.udp_port == 4250, "a port inside the documented range is used as written");
    Check(cfg.load_notes.empty(), "and a well-formed INI produces no complaint");
}

void TestMissingFileKeepsDefaults() {
    preyht::Config cfg;
    const std::string absent =
        (std::filesystem::temp_directory_path() / "preyht_does_not_exist.ini").string();
    Check(preyht::Config::LoadFromFile(absent, cfg), "a missing INI is not an error");
    Check(cfg.udp_port == 4242 && cfg.local_smoothing == 0.0f && cfg.remote_smoothing == 0.15f,
          "and leaves every default in place");
}

// ---- the numbers that reach the engine ---------------------------------------

void TestNonFiniteValuesGoToTheDefault() {
    std::cout << "sanitize:\n";
    preyht::Config cfg;
    cfg.yaw_sens = std::numeric_limits<float>::infinity();
    cfg.pitch_sens = std::numeric_limits<float>::quiet_NaN();
    cfg.pos_limit_z = std::numeric_limits<float>::quiet_NaN();
    cfg.flashlight_scale = std::numeric_limits<float>::infinity();

    const auto notes = cfg.Sanitize();
    Check(cfg.yaw_sens == 1.0f && cfg.pitch_sens == 1.0f,
          "a non-finite sensitivity goes to the default instead of into the view matrix");
    Check(cfg.pos_limit_z == 0.40f, "and so does a non-finite travel limit");
    Check(cfg.flashlight_scale == 1.5f, "and a non-finite beam scale");
    Check(notes.size() == 4, "each substitution is reported to the player");
}

void TestOutOfRangeValuesAreClamped() {
    preyht::Config cfg;
    cfg.yaw_sens = 99.0f;
    cfg.roll_sens = -5.0f;
    cfg.remote_smoothing = 4.0f;
    cfg.pos_limit_x = 10.0f;
    cfg.deadzone = -1.0f;

    cfg.Sanitize();
    Check(cfg.yaw_sens == 3.0f && cfg.roll_sens == 0.1f, "sensitivity is held to 0.1..3.0");
    Check(cfg.remote_smoothing == 1.0f, "smoothing is held to 0..1");
    Check(cfg.pos_limit_x == 0.5f, "travel limits are held to 0.01..0.5");
    Check(cfg.deadzone == 0.0f, "and a negative deadzone becomes none");
}

void TestZeroFieldOfViewSurvivesSanitize() {
    // 0 is the "leave Prey's own slider in charge" value, so it has to survive a
    // clamp whose floor is 25. Clamping it to 25 would hold every player at a
    // 25 degree horizontal FOV without them asking for anything.
    preyht::Config cfg;
    cfg.field_of_view = 0.0f;
    const auto notes = cfg.Sanitize();
    Check(cfg.field_of_view == 0.0f, "a FieldOfView of 0 is left alone, not clamped up to 25");
    Check(notes.empty(), "and a config that is already in range reports nothing");
}

void TestRequestedFieldOfViewIsClamped() {
    preyht::Config cfg;
    cfg.field_of_view = 400.0f;
    cfg.Sanitize();
    Check(cfg.field_of_view == 170.0f,
          "a FieldOfView past anything playable is clamped rather than handed to the frustum");
}

}  // namespace

int main() {
    TestPortOutsideTheRangeIsRefused();
    TestPortThatWrapsToZeroIsRefused();
    TestNegativePortIsRefused();
    TestPrivilegedPortIsRefused();
    TestValidPortIsAccepted();
    TestMissingFileKeepsDefaults();

    TestNonFiniteValuesGoToTheDefault();
    TestOutOfRangeValuesAreClamped();
    TestZeroFieldOfViewSurvivesSanitize();
    TestRequestedFieldOfViewIsClamped();

    if (g_failures == 0) {
        std::cout << "\nAll config checks passed.\n";
        return 0;
    }
    std::cout << "\n" << g_failures << " check(s) FAILED.\n";
    return 1;
}
