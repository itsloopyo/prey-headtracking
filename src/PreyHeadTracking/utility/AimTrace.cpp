#include "AimTrace.hpp"

#include "Logging.hpp"
#include "SafeMemory.hpp"

#include <atomic>
#include <cmath>

namespace preyht::aim {

namespace {

/// CryEngine's SRWIParams, laid out from the engine's own two call sites: the
/// 3D engine's ray at RVA 0x19BE99 and the entity ray at RVA 0x301932. Both
/// build the struct field by field and both agree, which is what pins the
/// offsets - `org` at +0x18, `dir` at +0x24, then objtypes, flags, the hit
/// buffer and its count.
///
/// Neither call site writes +0x10, so it is left zeroed here rather than
/// guessed at; zeroing is strictly safer than what the engine passes.
struct SRWIParams {
    void*    pSkipEnts;     // 0x00
    int32_t  nSkipEnts;     // 0x08
    int32_t  pad0c;
    uint64_t unused10;      // 0x10 - untouched by both engine call sites
    float    org[3];        // 0x18
    float    dir[3];        // 0x24 - NOT normalized: its length is the ray's range
    int32_t  objtypes;      // 0x30
    int32_t  flags;         // 0x34
    void*    hits;          // 0x38
    int32_t  nMaxHits;      // 0x40
    int32_t  pad44;
    void*    pForeignData;  // 0x48
    int32_t  iForeignData;  // 0x50
    int32_t  pad54;
    void*    onEvent;       // 0x58
    void*    hitsNoAlloc;   // 0x60
    uint8_t  bThreadSafe;   // 0x68
    uint8_t  pad69[7];
};
static_assert(sizeof(SRWIParams) == 0x70, "SRWIParams must match the engine's layout");

/// ray_hit, confirmed by the 3D engine copying exactly 0x50 bytes out of its hit
/// buffer and reading the surface normal at +0x30. The distance along the ray is
/// the first field.
struct RayHit {
    float    dist;          // 0x00
    uint8_t  rest[0x4c];
};
static_assert(sizeof(RayHit) == 0x50, "ray_hit must match the engine's layout");

using RwiFn = int(__fastcall*)(void* world, const SRWIParams* rp, const char* nameTag, int iCaller);

/// The two filters, read off the engine's own ray at RVA 0x301932 rather than
/// assembled from remembered enum values. 7 is the solid world -
/// ent_static, ent_sleeping_rigid, ent_rigid - and 0x40F stops the ray at the
/// first surface a bullet would not pierce.
constexpr int32_t kObjTypes = 7;
constexpr int32_t kFlags    = 0x40F;

/// 150 m. Past that the parallax term is under a tenth of a pixel at any lean
/// the mod allows, so a longer ray would buy nothing and cost the traversal.
constexpr float kRangeMetres = 150.0f;

/// What the engine's own game-thread call sites pass for iCaller: physics
/// worker threads carry their index in TLS, and everything else is
/// MAX_PHYS_THREADS, which is the externally-locked path.
constexpr int kExternalCaller = 4;

Binding           g_bind;
std::atomic<bool> g_bound{false};
std::atomic<bool> g_faulted{false};

/// SEH around the engine call, in its own function with nothing to unwind.
bool CallRwi(void* world, RwiFn fn, const SRWIParams* rp, int& outHits) {
    __try {
        outHits = fn(world, rp, "PreyHeadTracking", kExternalCaller);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

void Bind(const Binding& b) {
    g_bind = b;
    g_bound.store(true, std::memory_order_release);
}

bool Distance(const float org[3], const float dir[3], float& outMetres) {
    if (!g_bound.load(std::memory_order_acquire)) return false;
    if (g_faulted.load(std::memory_order_relaxed)) return false;

    uintptr_t world = 0;
    if (!SafeRead(g_bind.physical_world_ptr, world) || world == 0) return false;
    uintptr_t vtbl = 0;
    if (!SafeRead(world, vtbl) || vtbl == 0) return false;
    uintptr_t rwi = 0;
    if (!SafeRead(vtbl + g_bind.rwi_vtable_off, rwi) || rwi == 0) return false;

    RayHit hit{};
    SRWIParams rp{};
    rp.org[0] = org[0];
    rp.org[1] = org[1];
    rp.org[2] = org[2];
    rp.dir[0] = dir[0] * kRangeMetres;
    rp.dir[1] = dir[1] * kRangeMetres;
    rp.dir[2] = dir[2] * kRangeMetres;
    rp.objtypes = kObjTypes;
    rp.flags    = kFlags;
    rp.hits     = &hit;
    rp.nMaxHits = 1;

    int hits = 0;
    if (!CallRwi(reinterpret_cast<void*>(world), reinterpret_cast<RwiFn>(rwi), &rp, hits)) {
        // A boundary failure, not a missing target: say so once and leave the
        // reticle on the aim direction for the rest of the session rather than
        // faulting the engine every frame.
        g_faulted.store(true, std::memory_order_relaxed);
        PHT_LOG(Warn, "Aim trace faulted; the crosshair no longer corrects for leaning this "
                      "session. Rotation compensation is unaffected.");
        return false;
    }
    if (hits <= 0) return false;
    if (!std::isfinite(hit.dist) || hit.dist <= 0.0f || hit.dist > kRangeMetres) return false;

    outMetres = hit.dist;
    return true;
}

}  // namespace preyht::aim
