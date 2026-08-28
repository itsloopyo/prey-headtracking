#pragma once

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>

namespace preyht::log {

void Init(const std::string& path);
void Shutdown();

void Info(std::string_view msg);
void Warn(std::string_view msg);
void Error(std::string_view msg);

template <typename... Args>
std::string Format(const char* fmt, Args&&... args) {
    char buf[1024];
    int n = std::snprintf(buf, sizeof(buf), fmt, std::forward<Args>(args)...);
    if (n < 0) return {};
    // snprintf returns the length it WOULD have written had the buffer been
    // large enough, not the number of bytes actually stored. Constructing the
    // string with that raw count over-reads `buf` whenever a line exceeds its
    // size. Clamp to what actually landed in the buffer.
    size_t len = static_cast<size_t>(n);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    return std::string(buf, len);
}

}  // namespace preyht::log

#define PHT_LOG(level, ...) ::preyht::log::level(::preyht::log::Format(__VA_ARGS__))
