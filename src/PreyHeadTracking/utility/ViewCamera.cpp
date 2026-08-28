#include "ViewCamera.hpp"

#include "Logging.hpp"
#include "SafeMemory.hpp"

#include <atomic>

namespace preyht::camera {

namespace {

// Written by the camera hook's install pass on the frame-tick thread, read from
// whichever thread the engine renders on. Atomic because nothing in the code
// establishes that those are the same thread.
std::atomic<GetViewCameraFn> g_getViewCamera{nullptr};
std::atomic<UpdateFrustumFn> g_updateFrustum{nullptr};
std::atomic<uint32_t>        g_matrixOffset{0};
std::atomic<uint32_t>        g_fovOffset{0};
std::atomic<uint32_t>        g_projRatioOffset{0};

/// Past any FOV the engine will accept, so a frustum it has not set up yet - all
/// zeroes - is rejected rather than divided by.
constexpr float kMaxPlausibleFovRadians = 3.0f;

void RebuildFrustum(void* camera) {
    const auto update = g_updateFrustum.load(std::memory_order_relaxed);
    if (update != nullptr) update(camera);
}

}  // namespace

void Bind(const Binding& b) {
    g_getViewCamera.store(b.get_view_camera, std::memory_order_relaxed);
    g_matrixOffset.store(b.matrix_offset, std::memory_order_relaxed);
    g_fovOffset.store(b.fov_offset, std::memory_order_relaxed);
    g_projRatioOffset.store(b.proj_ratio_offset, std::memory_order_relaxed);
    // Last, and with release ordering: the detours gate on the getter, so
    // everything they read alongside it must already be in place.
    g_updateFrustum.store(b.update_frustum, std::memory_order_release);
}

uintptr_t MatrixAddress(void* pSystem, void** outCamera) {
    const auto getCam = g_getViewCamera.load(std::memory_order_relaxed);
    if (getCam == nullptr || pSystem == nullptr) return 0;
    void* camera = getCam(pSystem);
    if (camera == nullptr) return 0;
    if (outCamera != nullptr) *outCamera = camera;
    return reinterpret_cast<uintptr_t>(camera) +
           g_matrixOffset.load(std::memory_order_relaxed);
}

bool ReadProjectionRatio(void* camera, float& projRatio) {
    const uintptr_t addr = reinterpret_cast<uintptr_t>(camera) +
                           g_projRatioOffset.load(std::memory_order_relaxed);
    float ratio = 0.0f;
    // Engine memory is a boundary.
    if (!SafeRead(addr, ratio) || !(ratio > 0.0f)) return false;
    projRatio = ratio;
    return true;
}

bool ReadFrustum(void* camera, float& fovRadians, float& projRatio) {
    float ratio = 0.0f;
    if (!ReadProjectionRatio(camera, ratio)) return false;
    float fov = 0.0f;
    const uintptr_t fovAddr = reinterpret_cast<uintptr_t>(camera) +
                              g_fovOffset.load(std::memory_order_relaxed);
    if (!SafeRead(fovAddr, fov) || !(fov > 0.0f) || !(fov < kMaxPlausibleFovRadians)) {
        return false;
    }
    fovRadians = fov;
    projRatio  = ratio;
    return true;
}

bool WriteMatrix(uintptr_t mtxAddr, void* camera, const Mat34& m) {
    // Last gate before the engine's own camera. Every write of the matrix comes
    // through here, so this is the one place that can promise the engine never
    // culls or draws from a NaN basis - see IsFinite for where one comes from.
    if (!IsFinite(m)) {
        static bool s_logged = false;
        if (!s_logged) {
            s_logged = true;
            PHT_LOG(Error, "Refused to write a non-finite camera basis into the engine; the "
                           "frame renders from the game's own camera instead. The pose source "
                           "sent a value the pipeline could not turn into a rotation.");
        }
        return false;
    }
    if (!SafeWrite(mtxAddr, m)) return false;
    RebuildFrustum(camera);
    return true;
}

// Its own function with no unwindable objects in scope, which is what __finally
// requires (MSVC C2712 forbids it alongside C++ object destruction).
void CallThenRestore(MemberVoidFn orig, void* self, uintptr_t mtxAddr, void* camera,
                     const Mat34& restore) {
    __try {
        orig(self);
    } __finally {
        SafeWrite(mtxAddr, restore);
        RebuildFrustum(camera);
    }
}

}  // namespace preyht::camera
