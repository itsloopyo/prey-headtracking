#include "Mods.hpp"

#include "mods/HeadTracking.hpp"
#include "mods/CryEngineCamera.hpp"
#include "utility/Logging.hpp"

namespace preyht {

Mods::Mods() {
    // HeadTracking goes first so the camera has a pose source to hold on to.
    auto tracking = std::make_unique<HeadTracking>();
    auto camera   = std::make_unique<CryEngineCamera>(*tracking);
    m_mods.push_back(std::move(tracking));
    m_mods.push_back(std::move(camera));
}

Mods::~Mods() = default;

std::optional<std::string> Mods::Initialize() {
    for (auto& m : m_mods) {
        PHT_LOG(Info, "Initializing mod: %.*s",
                 static_cast<int>(m->Name().size()), m->Name().data());
        if (auto err = m->OnInitialize(); err.has_value()) {
            return std::string(m->Name()) + ": " + *err;
        }
    }
    return std::nullopt;
}

void Mods::OnFrame() {
    for (auto& m : m_mods) m->OnFrame();
}

void Mods::Shutdown() {
    // Reverse order so the camera lets go of its hook before HeadTracking
    // stops the receiver thread.
    for (auto it = m_mods.rbegin(); it != m_mods.rend(); ++it) {
        (*it)->OnShutdown();
    }
}

}  // namespace preyht
