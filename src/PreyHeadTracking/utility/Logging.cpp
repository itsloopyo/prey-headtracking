#include "Logging.hpp"

#include <windows.h>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>

namespace preyht::log {

namespace {
std::mutex g_mutex;
std::ofstream g_file;
bool g_init = false;

std::string TimestampNow() {
    using namespace std::chrono;
    const auto now  = system_clock::now();
    const auto t    = system_clock::to_time_t(now);
    const auto ms   = duration_cast<milliseconds>(now.time_since_epoch()) % 1000;
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[16];
    const int n = std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
                                tm.tm_hour, tm.tm_min, tm.tm_sec,
                                static_cast<int>(ms.count()));
    return std::string(buf, n < 0 ? 0 : static_cast<size_t>(n));
}

void Write(const char* level, std::string_view msg) {
    std::lock_guard lk(g_mutex);
    std::string line;
    line.reserve(msg.size() + 32);
    line += '[';
    line += TimestampNow();
    line += "][";
    line += level;
    line += "] ";
    line.append(msg.data(), msg.size());
    line += '\n';
    if (g_file.is_open()) {
        g_file << line;
        g_file.flush();
    }
    OutputDebugStringA(line.c_str());
}
}  // namespace

void Init(const std::string& path) {
    DWORD rotateError = 0;
    {
        std::lock_guard lk(g_mutex);
        if (g_init) return;
        if (!path.empty()) {
            // Start fresh every launch so what a user sends is one session and not
            // the history of the install. One generation is kept because a camera
            // hook fault ends the session and they relaunch before sending it.
            const std::string prev =
                std::filesystem::path(path).replace_extension(".prev.log").string();
            if (!MoveFileExA(path.c_str(), prev.c_str(), MOVEFILE_REPLACE_EXISTING)) {
                rotateError = GetLastError();
            }
            g_file.open(path, std::ios::out | std::ios::trunc);
        }
        g_init = true;
    }
    // The truncating open above has already destroyed the generation the
    // rotation existed to keep. A missing source is the ordinary first run.
    if (rotateError != 0 && rotateError != ERROR_FILE_NOT_FOUND) {
        Warn("Could not rotate the previous log (error " + std::to_string(rotateError) +
             ") - the previous session log was overwritten");
    }
}

void Shutdown() {
    std::lock_guard lk(g_mutex);
    if (g_file.is_open()) g_file.close();
    g_init = false;
}

void Info(std::string_view msg)  { Write("INFO",  msg); }
void Warn(std::string_view msg)  { Write("WARN",  msg); }
void Error(std::string_view msg) { Write("ERROR", msg); }

}  // namespace preyht::log
