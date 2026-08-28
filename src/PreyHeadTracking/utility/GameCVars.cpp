#include "GameCVars.hpp"

#include "CameraMath.hpp"
#include "Logging.hpp"
#include "SafeMemory.hpp"

#include <atomic>
#include <cmath>

namespace preyht::cvars {

namespace {

std::atomic<uintptr_t> g_gameBlockPtr{0};
std::atomic<uintptr_t> g_engineBlockPtr{0};
std::atomic<uint32_t>  g_coverageBufferOff{0};
std::atomic<uint32_t>  g_forceFlashlightOff{0};
std::atomic<uint32_t>  g_clFovOff{0};
std::atomic<uint32_t>  g_clHfovOff{0};
std::atomic<uintptr_t> g_drawNearFovAddr{0};
std::atomic<float>     g_fieldOfView{0.0f};

/// Resolve one CVar's storage. The block pointer is a global the engine fills in
/// during startup, so both indirections have to be checked every time.
bool ValueAddress(const std::atomic<uintptr_t>& blockPtr, uint32_t offset, uintptr_t& out) {
    if (offset == 0) return false;
    const uintptr_t ptrAddr = blockPtr.load(std::memory_order_relaxed);
    if (ptrAddr == 0) return false;
    uintptr_t block = 0;
    if (!SafeRead(ptrAddr, block) || block == 0) return false;
    out = block + offset;
    return true;
}

/// A thousandth of a degree is far below anything the player can see, and the
/// compare is what keeps a settled override down to reads alone.
constexpr float kFovEpsilonDegrees = 1e-3f;

}  // namespace

void Bind(const Binding& b) {
    g_coverageBufferOff.store(b.coverage_buffer_off, std::memory_order_relaxed);
    g_forceFlashlightOff.store(b.force_flashlight_off, std::memory_order_relaxed);
    g_clFovOff.store(b.cl_fov_off, std::memory_order_relaxed);
    g_clHfovOff.store(b.cl_hfov_off, std::memory_order_relaxed);
    g_drawNearFovAddr.store(b.draw_near_fov_addr, std::memory_order_relaxed);
    g_fieldOfView.store(b.field_of_view, std::memory_order_relaxed);
    // Last: every reader gates on a block pointer, so the offsets it will pair
    // with one must already be visible.
    g_gameBlockPtr.store(b.game_block_ptr, std::memory_order_release);
    g_engineBlockPtr.store(b.engine_block_ptr, std::memory_order_release);
}

void SuppressCoverageBuffer() {
    uintptr_t addr = 0;
    if (!ValueAddress(g_engineBlockPtr, g_coverageBufferOff.load(std::memory_order_relaxed),
                      addr)) {
        return;
    }
    int value = 0;
    if (!SafeRead(addr, value) || value == 0) return;
    SafeWrite(addr, 0);
    // Called every frame, and the engine re-applies its config on a graphics
    // settings change, so the suppression can fire more than once a session.
    // The line is worth one appearance, not one per re-application.
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        PHT_LOG(Info, "Software occlusion culling (e_CoverageBuffer) was %d; set to 0. It is "
                      "built from a camera the mod cannot reach, so it culls a head-turned "
                      "view against the un-turned one.", value);
    }
}

void ForceFlashlight() {
    uintptr_t addr = 0;
    if (!ValueAddress(g_gameBlockPtr, g_forceFlashlightOff.load(std::memory_order_relaxed),
                      addr)) {
        return;
    }
    int value = 0;
    if (!SafeRead(addr, value) || value != 0) return;
    SafeWrite(addr, 1);
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        PHT_LOG(Info, "ForceFlashlight on: pl_forceFlashlight set to 1 so the beam is "
                      "available for testing regardless of save progress.");
    }
}

bool FieldOfViewRequested() {
    return g_fieldOfView.load(std::memory_order_relaxed) > 0.0f &&
           g_clHfovOff.load(std::memory_order_relaxed) != 0 &&
           g_gameBlockPtr.load(std::memory_order_relaxed) != 0;
}

void ApplyFieldOfView(float projRatio) {
    const float horizontal = g_fieldOfView.load(std::memory_order_relaxed);
    uintptr_t hAddr = 0, vAddr = 0;
    if (horizontal <= 0.0f || !(projRatio > 0.0f) ||
        !ValueAddress(g_gameBlockPtr, g_clHfovOff.load(std::memory_order_relaxed), hAddr) ||
        !ValueAddress(g_gameBlockPtr, g_clFovOff.load(std::memory_order_relaxed), vAddr)) {
        return;
    }

    // The engine's own relation between the two, straight out of the cl_hfov
    // change handler: tan(hFov/2) = projectionRatio * tan(vFov/2).
    const float vertical =
        2.0f * std::atan(std::tan(horizontal * kDegToRad * 0.5f) / projRatio) / kDegToRad;

    float haveH = 0.0f, haveV = 0.0f;
    if (!SafeRead(hAddr, haveH) || !SafeRead(vAddr, haveV)) return;
    if (std::fabs(haveH - horizontal) < kFovEpsilonDegrees &&
        std::fabs(haveV - vertical) < kFovEpsilonDegrees) {
        return;
    }
    if (!SafeWrite(hAddr, horizontal) || !SafeWrite(vAddr, vertical)) return;

    static bool s_logged = false;
    if (!s_logged) {
        PHT_LOG(Info, "FieldOfView %.1f: cl_hfov set to %.1f degrees horizontal and cl_fov to "
                      "%.1f vertical at a projection ratio of %.4f. This is held every frame, "
                      "so Prey's own Field of View slider no longer has the last word - set "
                      "FieldOfView back to 0 to hand it back.",
                horizontal, horizontal, vertical, projRatio);
        s_logged = true;
    }
}

void MatchWeaponFieldOfView() {
    const uintptr_t nearAddr = g_drawNearFovAddr.load(std::memory_order_relaxed);
    uintptr_t vAddr = 0;
    if (nearAddr == 0 ||
        !ValueAddress(g_gameBlockPtr, g_clFovOff.load(std::memory_order_relaxed), vAddr)) {
        return;
    }

    float clFov = 0.0f, nearFov = 0.0f;
    if (!SafeRead(vAddr, clFov) || !SafeRead(nearAddr, nearFov)) return;
    // The CVar block is zeroed until the client registers its variables, and a
    // near lens of zero degrees would collapse the weapon to a point.
    if (!(clFov > 0.0f) || std::fabs(nearFov - clFov) < kFovEpsilonDegrees) return;
    if (!SafeWrite(nearAddr, clFov)) return;

    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        PHT_LOG(Info, "MatchWeaponFieldOfView: r_DrawNearFoV was %.2f degrees against a world "
                      "field of view of %.2f, so the first-person weapon was drawn magnified "
                      "%.2fx and swept that much further than the world on a head turn. It now "
                      "follows cl_fov.",
                nearFov, clFov,
                std::tan(clFov * kDegToRad * 0.5f) / std::tan(nearFov * kDegToRad * 0.5f));
    }
}

}  // namespace preyht::cvars
