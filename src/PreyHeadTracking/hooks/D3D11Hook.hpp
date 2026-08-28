#pragma once

#include <functional>

namespace preyht::hooks {

/// Hooks IDXGISwapChain::Present by spinning up a throwaway swapchain on a
/// hidden window, reading the vtable, and routing the Present slot through
/// MinHook (via cameraunlock's HookManager).
class D3D11Hook {
public:
    using PresentCallback = std::function<void()>;

    D3D11Hook();
    ~D3D11Hook();

    bool Hook(PresentCallback on_present);
    void Unhook();

private:
    bool m_hooked = false;
};

}  // namespace preyht::hooks
