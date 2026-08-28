#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace preyht {

class Framework;

/// Base class for all PHT mods. Mods are instantiated once and survive for
/// the lifetime of the host process. Lifecycle:
///
///   1. `OnInitialize()`: once, on the init thread, after Framework is up.
///      Return a non-empty string to abort load (and log the reason).
///   2. `OnFrame()`: every Present from the render thread.
///   3. `OnShutdown()`: once, on DLL detach (best-effort; may be skipped).
class Mod {
public:
    virtual ~Mod() = default;

    virtual std::string_view Name() const = 0;

    /// @return error message on failure, std::nullopt on success.
    virtual std::optional<std::string> OnInitialize() { return std::nullopt; }
    virtual void OnFrame() {}
    virtual void OnShutdown() {}
};

}  // namespace preyht
