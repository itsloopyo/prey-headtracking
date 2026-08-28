#include "D3D11Hook.hpp"

#include "utility/Logging.hpp"

#include "cameraunlock/hooks/hook_manager.h"

#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <wrl/client.h>

#include <atomic>

namespace preyht::hooks {

namespace {

using Microsoft::WRL::ComPtr;

using PresentFn = HRESULT (STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);

/// IDXGISwapChain::Present. The vtable slot is fixed by the COM interface, so it
/// is the same on every driver and every Windows build.
constexpr int kPresentVtableSlot = 8;

PresentFn         g_origPresent = nullptr;
std::atomic<bool> g_inPresent{false};

D3D11Hook::PresentCallback g_callback;

HRESULT STDMETHODCALLTYPE PresentDetour(IDXGISwapChain* swap, UINT sync, UINT flags) {
    // Re-entrancy guard: many overlays call Present indirectly from their own
    // callbacks.
    if (!g_inPresent.exchange(true, std::memory_order_acq_rel)) {
        try {
            if (g_callback) g_callback();
        } catch (...) {
            // The mod must never take the game down with it, so the frame tick is
            // contained here. It is NOT silent: this target compiles with /EHa, so
            // this also catches access violations, and a fault that repeats every
            // frame would otherwise look exactly like head tracking quietly not
            // working. Logged once - after that the log would be the bigger
            // problem.
            static bool logged = false;
            if (!logged) {
                logged = true;
                PHT_LOG(Error, "Exception escaped the per-frame tick; head tracking is likely "
                               "dead for this session. Please report this log.");
            }
        }
        g_inPresent.store(false, std::memory_order_release);
    }
    return g_origPresent(swap, sync, flags);
}

/// Creates a 1x1 windowless swapchain so we can read the IDXGISwapChain vtable.
bool CreateDummySwapchain(ComPtr<IDXGISwapChain>& outSwap, ComPtr<ID3D11Device>& outDev) {
    WNDCLASSEXA wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DefWindowProcA;
    wc.hInstance     = GetModuleHandleA(nullptr);
    wc.lpszClassName = "PHT_DummyWnd";
    RegisterClassExA(&wc);
    HWND hwnd = CreateWindowExA(0, wc.lpszClassName, "preyht", 0, 0, 0, 1, 1,
                                nullptr, nullptr, wc.hInstance, nullptr);
    if (!hwnd) return false;

    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferCount       = 1;
    desc.BufferDesc.Width  = 1;
    desc.BufferDesc.Height = 1;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BufferDesc.RefreshRate.Numerator = 60;
    desc.BufferDesc.RefreshRate.Denominator = 1;
    desc.BufferUsage       = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.OutputWindow      = hwnd;
    desc.SampleDesc.Count  = 1;
    desc.Windowed          = TRUE;
    desc.SwapEffect        = DXGI_SWAP_EFFECT_DISCARD;

    D3D_FEATURE_LEVEL fl;
    const auto hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
        nullptr, 0, D3D11_SDK_VERSION, &desc,
        outSwap.GetAddressOf(), outDev.GetAddressOf(), &fl, nullptr);

    DestroyWindow(hwnd);
    UnregisterClassA(wc.lpszClassName, wc.hInstance);
    return SUCCEEDED(hr);
}

}  // namespace

D3D11Hook::D3D11Hook() = default;
D3D11Hook::~D3D11Hook() { Unhook(); }

bool D3D11Hook::Hook(PresentCallback on_present) {
    if (m_hooked) return true;

    ComPtr<IDXGISwapChain> swap;
    ComPtr<ID3D11Device>   dev;
    if (!CreateDummySwapchain(swap, dev)) {
        PHT_LOG(Warn, "D3D11Hook: failed to create dummy swapchain");
        return false;
    }

    auto** vtbl = *reinterpret_cast<void***>(swap.Get());
    void* present = vtbl[kPresentVtableSlot];

    g_callback = std::move(on_present);

    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    auto& mh = HookManager::Instance();

    if (mh.CreateHook(present, reinterpret_cast<void*>(&PresentDetour),
                      reinterpret_cast<void**>(&g_origPresent)) != HookStatus::Ok) {
        PHT_LOG(Error, "D3D11Hook: failed to create Present hook");
        return false;
    }
    if (mh.EnableHook(present) != HookStatus::Ok) {
        // Removed rather than left created, so nothing can arm a detour whose
        // callback we are about to drop.
        mh.RemoveHook(present);
        g_origPresent = nullptr;
        g_callback = nullptr;
        PHT_LOG(Error, "D3D11Hook: failed to enable Present hook");
        return false;
    }

    PHT_LOG(Info, "D3D11Hook: Present @ %p hooked", present);
    m_hooked = true;
    return true;
}

void D3D11Hook::Unhook() {
    if (!m_hooked) return;
    // Cleanup is centralized in HookManager::Shutdown when the framework
    // tears down; per-hook removal here would race the render thread.
    m_hooked = false;
    g_callback = nullptr;
}

}  // namespace preyht::hooks
