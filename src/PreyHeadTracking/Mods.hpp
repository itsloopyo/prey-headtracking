#pragma once

#include <memory>
#include <vector>

#include "Mod.hpp"

namespace preyht {

/// Owns the list of mods. Lifecycle is driven by `Framework`.
class Mods {
public:
    Mods();
    ~Mods();

    /// Calls `OnInitialize` on each mod, in order. First failure aborts.
    /// @return error message on failure, std::nullopt on success.
    std::optional<std::string> Initialize();

    /// Per-frame tick, fired from the Present hook.
    void OnFrame();

    void Shutdown();

private:
    std::vector<std::unique_ptr<Mod>> m_mods;
};

}  // namespace preyht
