#include "PreyHud.hpp"

#include "SafeMemory.hpp"

#include <atomic>
#include <cmath>

namespace preyht::hud {

namespace {

// Prey draws its reticle from Flash, and the only way in is the same one the
// game uses on itself: fetch the "DanielleHUD" element and call the movie's
// reticlePosition(x, y) with normalized screen coordinates. The engine does
// exactly this every frame while the controller's free-aim reticle is active,
// so a per-frame call is a supported operation rather than something wedged in
// from outside.
using GetElementFn = void* (__fastcall*)();
using CallFn2fFn   = void  (__fastcall*)(void*, const char*, float, float);

Binding           g_bind;
std::atomic<bool> g_bound{false};

// Only ever touched from the render thread inside the camera detour.
float g_lastX = 0.0f;
float g_lastY = 0.0f;
bool  g_owned = false;

void* Element() {
    // Null through every menu and loading screen, which is why each of the
    // game's own call sites checks it too.
    return reinterpret_cast<GetElementFn>(g_bind.get_element)();
}

void Call(void* element, float x, float y) {
    reinterpret_cast<CallFn2fFn>(g_bind.call_fn_2f)(element, "reticlePosition", x, y);
}

float Clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

}  // namespace

void Bind(const Binding& b) {
    g_bind = b;
    g_bound.store(true, std::memory_order_release);
}

bool BasePosition(float& x, float& y) {
    if (!g_bound.load(std::memory_order_acquire)) return false;

    uintptr_t block = 0;
    if (!SafeRead(g_bind.config_ptr, block) || block == 0) return false;
    float yPercent = 0.0f;
    if (!SafeRead(block + g_bind.reticle_y_off, yPercent)) return false;
    if (!std::isfinite(yPercent)) return false;

    // The horizontal half is not configurable: the engine writes a literal 0.5
    // alongside the y setting every time the reticle preference changes.
    x = 0.5f;
    y = Clamp01(yPercent);
    return true;
}

void SetPosition(float x, float y) {
    if (!g_bound.load(std::memory_order_acquire)) return;

    x = Clamp01(x);
    y = Clamp01(y);
    // 1e-4 of a screen is well under a pixel at any resolution Prey runs at, so
    // anything finer than this would be an engine call the player cannot see.
    if (g_owned && std::fabs(x - g_lastX) < 1e-4f && std::fabs(y - g_lastY) < 1e-4f) return;

    void* element = Element();
    if (element == nullptr) return;
    Call(element, x, y);
    g_lastX = x;
    g_lastY = y;
    g_owned = true;
}

void Restore() {
    if (!g_owned) return;

    float x = 0.0f, y = 0.0f;
    if (!BasePosition(x, y)) return;
    void* element = Element();
    if (element == nullptr) return;
    Call(element, x, y);
    // Cleared only after the engine has actually taken the base position back,
    // so a restore attempted while the HUD is down is retried next frame rather
    // than leaving the reticle parked wherever the head last put it.
    g_owned = false;
}

}  // namespace preyht::hud
