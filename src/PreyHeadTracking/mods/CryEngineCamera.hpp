#pragma once

#include "Mod.hpp"

namespace preyht {

class HeadTracking;
struct BuildProfile;

/// Turn carrying the first-person body with the head on or off, in game.
///
/// It is wanted in the space suit sections, where the suit fills the bottom of
/// the screen, and not wanted with a gun in hand, where Prey draws the weapon as
/// part of the same object and it would follow the head too. That is a per
/// section choice, so it is a key rather than a restart.
void ToggleBodyFollowsHead();

/// Injects the processed OpenTrack head pose into Prey's rendered view.
///
/// Prey runs on Arkane's CryEngine fork, which keeps one view camera (CCamera)
/// inside CSystem. The game writes it once per frame through SetViewCamera and
/// then reads it back through GetViewCamera for everything that needs to know
/// where the player is looking: interaction focus, weapon aim, raycasts, AI
/// sight. So the head pose cannot live in that camera for the whole frame.
///
/// Instead the mod hooks CSystem::Render - the one consumer that draws with the
/// camera - and rotates (and optionally translates) its Matrix34 only for the
/// duration of that call, restoring the clean matrix before returning. The
/// player sees the head-tracked view; every game-logic reader sees the
/// mouse-controlled one, so look and aim stay decoupled (CameraUnlock doctrine:
/// head tracking only changes what is rendered).
///
/// Engine resolution is PE-fingerprint gated: the mod matches the running
/// PreyDll.dll against a registry of build profiles and stays fully dormant when
/// none matches - no hook installed, game renders vanilla. Injection is further
/// gated on active gameplay (menus release the mouse cursor) and on the engine
/// not running a multiplayer session.
class CryEngineCamera final : public Mod {
public:
    explicit CryEngineCamera(HeadTracking& tracking) : m_tracking(tracking) {}

    std::string_view Name() const override { return "CryEngineCamera"; }

    std::optional<std::string> OnInitialize() override;
    void OnFrame() override;
    void OnShutdown() override;

private:
    /// Match PreyDll.dll against the build profiles, resolve the engine through
    /// the live ISystem vtable, and install the hooks. Retried each frame until
    /// the engine is reachable; one-shot after that, in either direction.
    bool InstallHook();

    /// The build profile for the running PreyDll.dll, or null. Null is either
    /// "not readable yet, retry next frame" or "dormant for this run", and the
    /// latter latches m_hookInstalled on its way out.
    const BuildProfile* ResolveProfile(void* preyDll);

    HeadTracking& m_tracking;
    bool          m_hookInstalled = false;
    bool          m_fpLogged      = false;
};

}  // namespace preyht
