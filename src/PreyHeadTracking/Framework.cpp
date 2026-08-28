#include "Framework.hpp"

#include "Mods.hpp"
#include "hooks/D3D11Hook.hpp"
#include "utility/Logging.hpp"

#include "preyht/Version.hpp"
#include "cameraunlock/hooks/hook_manager.h"

namespace preyht {

Framework& Framework::Get() {
    static Framework s;
    return s;
}

bool Framework::Initialize() {
    if (m_ready.load(std::memory_order_acquire))    return true;
    if (m_initFailed.load(std::memory_order_acquire)) return false;

    bool ok = false;
    std::call_once(m_initOnce, [&]{ ok = DoInitialize(); });
    // A second caller that merely waited on the once_flag never ran the lambda,
    // so its `ok` is false even though initialization succeeded.
    if (m_ready.load(std::memory_order_acquire)) return true;

    if (!ok) {
        m_initFailed.store(true, std::memory_order_release);
        return false;
    }
    m_ready.store(true, std::memory_order_release);
    return true;
}

bool Framework::DoInitialize() {
    // Config + logging come up first so everything else has a place to talk.
    Config::LoadFromFile(Config::DefaultIniPathNextToHostExe(), m_config);
    const auto clamped = m_config.Sanitize();
    if (m_config.log_to_file) {
        log::Init(Config::ResolveLogPath(m_config.log_path));
    }
    PHT_LOG(Info, "%s %s starting up", kProductName, kVersion);
    // Reported here rather than from the loader: the config is read before the
    // log file exists, so anything logged in there would reach a debugger only.
    for (const auto& msg : m_config.load_notes) PHT_LOG(Warn, "%s", msg.c_str());
    for (const auto& msg : clamped)             PHT_LOG(Warn, "%s", msg.c_str());

    // MinHook is shared between cameraunlock_hooks and our D3D11 present hook.
    using cameraunlock::hooks::HookManager;
    auto& mh = HookManager::Instance();
    const auto status = mh.Initialize();
    if (status != cameraunlock::hooks::HookStatus::Ok &&
        status != cameraunlock::hooks::HookStatus::ErrorAlreadyInitialized) {
        PHT_LOG(Error, "MinHook initialize failed: %s",
                 cameraunlock::hooks::HookStatusToString(status));
        return false;
    }

    m_mods = std::make_unique<Mods>();
    if (auto err = m_mods->Initialize(); err.has_value()) {
        PHT_LOG(Error, "Mod initialization failed: %s", err->c_str());
        return false;
    }

    // Prey renders on D3D11 (PreyDll.dll). The present hook is the per-frame
    // tick that drives the mod pipeline.
    m_d3d11 = std::make_unique<hooks::D3D11Hook>();
    if (!m_d3d11->Hook([this]{ OnFrame(); })) {
        PHT_LOG(Warn, "D3D11 Present could not be hooked yet. "
                       "Will retry on first device creation.");
    }

    PHT_LOG(Info, "PreyHeadTracking initialized.");
    return true;
}

void Framework::Shutdown() {
    // Closes the frame tick first: OnFrame gates on m_ready, so from here on it
    // stops touching mods that are about to be destroyed.
    m_ready.store(false, std::memory_order_release);
    PHT_LOG(Info, "PreyHeadTracking shutting down");
    if (m_d3d11) m_d3d11->Unhook();
    if (m_mods)  m_mods->Shutdown();
    cameraunlock::hooks::HookManager::Instance().Shutdown();
    log::Shutdown();
}

void Framework::OnFrame() {
    if (!m_ready.load(std::memory_order_acquire)) return;
    m_mods->OnFrame();
}

}  // namespace preyht
