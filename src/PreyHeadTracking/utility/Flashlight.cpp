#include "Flashlight.hpp"

#include "Logging.hpp"
#include "SafeMemory.hpp"
#include "Watchpoint.hpp"

#include "cameraunlock/hooks/hook_manager.h"

#include <atomic>
#include <cmath>
#include <map>
#include <mutex>

namespace preyht::flashlight {

namespace {

/// ArkLight::SetValue(this, float, bool) - walks child entities' ArkLight
/// components, so it is the one call that hands over live pointers to them.
using ArkLightSetFn  = void(__fastcall*)(void*, float, char);
using AddDynLightFn  = void(__fastcall*)(void*, void*, void*, void*, float, uintptr_t);
using SetTransformFn = void(__fastcall*)(void*, const V3*, const Quat4*, const V3*, int);

/// The beam entity's transform, confirmed by content rather than by assuming a
/// layout: rotating (0,1,0) by the quaternion at +0x44 reproduces the camera's
/// forward vector to three decimals.
constexpr uintptr_t kEntityPositionOff = 0x38;
constexpr uintptr_t kEntityRotationOff = 0x44;
constexpr uintptr_t kEntityScaleOff    = 0x54;

/// The smallest plausible object address, used to tell a real pointer from a
/// small integer that happens to sit where one is expected.
constexpr uintptr_t kMinPlausiblePointer = 0x10000;

/// What counts as "riding the head": within 1.5m of the eye (squared, to keep a
/// square root off the per-frame path) AND pointing where the un-tracked view
/// points. Sitting on the eye alone is not enough - other lights ride the player
/// too, and one with a near-identity rotation was picked up that way.
constexpr float kOnEyeDistSquared = 2.25f;
constexpr float kFacingViewDot    = 0.9f;

/// Prey rides more than one ArkLight on the head - a build with the flashlight on
/// carried two, at intensity 1.000 and 0.409 - and turning only one left the beam
/// where it was. Every head-mounted light gets turned, so the slots are an array.
constexpr int kMaxBeams = 4;

/// Every ArkLight the setter has ever handed us. The setter early-outs on an
/// unchanged value, so it fires while the level loads and then goes quiet - and
/// during a load the view camera is not set up yet, so "is this light on the eye"
/// cannot be answered at the one moment we hear about it. Keep them all, then ask
/// that question later, once there is a camera.
constexpr int kMaxKnownLights = 1024;

/// Re-picking the head-mounted lights out of a thousand pointers is paid for on
/// the render thread, and the set only changes when the player switches a light
/// on or walks into a level. Twice a second is far more often than that.
constexpr unsigned kBeamRefreshFrames = 30;

/// Two diagnostics that log at most every N frames. Deliberately coprime with
/// kBeamRefreshFrames so a periodic dump does not only ever sample the frame the
/// slots were refreshed on.
constexpr unsigned kTraceEveryFrames = 121;
constexpr unsigned kReadbackEveryFrames = 120;

std::atomic<ArkLightSetFn> g_origArkLightSet{nullptr};
std::atomic<AddDynLightFn> g_origAddDynLight{nullptr};

std::atomic<uint32_t>  g_entityOff{0};
std::atomic<uintptr_t> g_arkLightVtable{0};
std::atomic<uintptr_t> g_setTransformAddr{0};
std::atomic<int>       g_setTransformSlot{-1};
std::atomic<float>     g_scale{1.5f};
std::atomic<bool>      g_traceLights{false};
std::atomic<bool>      g_traceLightReader{false};
std::atomic<bool>      g_watchArmed{false};

std::atomic<void*> g_knownLights[kMaxKnownLights]{};
std::atomic<int>   g_knownLightCount{0};

/// Lights the setter reported while they were sitting on the eye and pointing
/// where the view points - the beam itself, in other words, recognised at the
/// moment we are told about it rather than looked for later.
///
/// The class list above cannot be relied on for this. It is capped, and a level
/// with a thousand lights fills it during the load; the flashlight is heard from
/// LATER, because its intensity only changes as the battery drains, and by then
/// there is no room. That is exactly how the beam stopped being turned: the log
/// read "0 of 1024 known lights are riding the head" while the setter was
/// reporting the beam itself, 0.04m off the eye and dead on the view, several
/// times a second.
std::atomic<void*> g_headLights[kMaxBeams]{};
std::atomic<int>   g_headLightNext{0};
std::atomic<void*> g_arkLights[kMaxBeams]{};

std::atomic<uintptr_t> g_beamEntity[kMaxBeams]{};
std::atomic<bool>      g_beamHeld[kMaxBeams]{};
Quat4                  g_beamSaved[kMaxBeams]{};
Quat4                  g_beamWrote[kMaxBeams]{};
V3                     g_beamSavedScale[kMaxBeams]{};

// Written and read only from the game's update, which is one function on one
// thread, so the readers nested inside it see a consistent pair.
Mat34 g_clean{};
Mat34 g_modified{};

/// The beam's entity, its position and its rotation, or false if this component
/// is not one (or not one any more - the engine recycles these objects).
bool ReadLightEntity(void* arkLight, uintptr_t& outEnt, V3& outPos, Quat4& outRot) {
    const uint32_t entOff = g_entityOff.load(std::memory_order_relaxed);
    if (arkLight == nullptr || entOff == 0) return false;
    const uintptr_t self = reinterpret_cast<uintptr_t>(arkLight);
    return SafeRead(self + entOff, outEnt) && outEnt > kMinPlausiblePointer &&
           SafeRead(outEnt + kEntityPositionOff, outPos) &&
           SafeRead(outEnt + kEntityRotationOff, outRot);
}

/// Is this light sitting on the eye and pointing where the un-tracked view
/// points? That is what a flashlight beam looks like and nothing else does.
bool IsRidingTheHead(const V3& pos, const Quat4& rot) {
    const V3 toEye = Sub(pos, g_clean.Trans());
    if (Dot(toEye, toEye) > kOnEyeDistSquared) return false;
    return Dot(RotateY(rot), g_clean.Forward()) >= kFacingViewDot;
}

/// Move an entity the way the engine does it.
///
/// Writing the quaternion by hand is what a whole day went into, and it never
/// moved the beam: the engine only re-syncs an entity's render node when the
/// transform arrives through IEntity::SetPosRotScale, which raises the dirty bits
/// (0x2 for position, 0x4 for rotation) that the sync keys on. A field poked
/// behind its back is read back perfectly and rendered from stale state.
///
/// Flags are passed as 0 - the function accumulates its own dirty bits, and
/// non-zero bits 5 and 13 take early-out paths.
bool SetEntityTransform(uintptr_t ent, const V3& pos, const Quat4& rot, const V3& scale) {
    const uintptr_t want = g_setTransformAddr.load(std::memory_order_acquire);
    if (want == 0) return false;

    uintptr_t vt = 0;
    if (!SafeRead(ent, vt) || vt < kMinPlausiblePointer) return false;

    int idx = g_setTransformSlot.load(std::memory_order_acquire);
    if (idx < 0) {
        constexpr int kMaxVtableSlots = 256;
        for (int i = 0; i < kMaxVtableSlots; ++i) {
            uintptr_t fn = 0;
            if (!SafeRead(vt + static_cast<uintptr_t>(i) * sizeof(uintptr_t), fn)) break;
            if (fn == want) { idx = i; break; }
        }
        if (idx < 0) return false;
        g_setTransformSlot.store(idx, std::memory_order_release);
        PHT_LOG(Info, "IEntity::SetPosRotScale is vtable slot %d.", idx);
    }

    uintptr_t fn = 0;
    if (!SafeRead(vt + static_cast<uintptr_t>(idx) * sizeof(uintptr_t), fn) || fn != want) {
        return false;
    }
    reinterpret_cast<SetTransformFn>(fn)(reinterpret_cast<void*>(ent), &pos, &rot, &scale, 0);
    return true;
}

/// Turn one head-mounted light by `delta`, saving what the game had so it can go
/// straight back after the frame is drawn.
void TurnBeam(int slot, const Quat4& delta) {
    void* obj = g_arkLights[slot].load(std::memory_order_acquire);
    if (obj == nullptr) return;

    // The engine recycles these objects - the same address was seen carrying two
    // different vtables minutes apart - so a pointer captured on a state change can
    // belong to something else by the time the frame is drawn, and writing into it
    // would corrupt whatever took its place. Re-establish the light every frame by
    // what it is. Failing that drops the slot, which also makes a missed restore
    // self-healing.
    uintptr_t ent = 0;
    V3 pos{};
    Quat4 rot{};
    if (!ReadLightEntity(obj, ent, pos, rot)) return;
    if (!IsRidingTheHead(pos, rot)) {
        g_arkLights[slot].store(nullptr, std::memory_order_release);
        return;
    }

    Quat4 turned{};
    if (!ComposeRotation(delta, rot, turned)) return;

    g_beamEntity[slot].store(ent, std::memory_order_release);
    g_beamSaved[slot] = rot;
    g_beamWrote[slot] = turned;

    // While hunting the game's own writer, stay off the field entirely - the page
    // watch fires on the first write from anywhere and ours would always win.
    // The slot is only marked held when a write actually happened, otherwise the
    // restore puts our value back and trips the watch instead.
    if (!g_traceLightReader.load(std::memory_order_relaxed)) {
        V3 scale{1.0f, 1.0f, 1.0f};
        SafeRead(ent + kEntityScaleOff, scale);
        if (SetEntityTransform(ent, pos, turned, scale)) {
            g_beamSavedScale[slot] = scale;
            g_beamHeld[slot].store(true, std::memory_order_release);
        }
    }

    if (g_traceLights.load(std::memory_order_relaxed)) {
        static unsigned s_n = 0;
        if ((s_n++ % kTraceEveryFrames) == 0) {
            PHT_LOG(Info, "beam[%d]: ent=%p q=(%.3f %.3f %.3f %.3f) -> (%.3f %.3f %.3f %.3f)",
                    slot, reinterpret_cast<void*>(ent), rot.x, rot.y, rot.z, rot.w,
                    turned.x, turned.y, turned.z, turned.w);
        }
    }
}

/// Pick the head-mounted lights out of everything the setter has shown us.
void RefreshBeamSlots() {
    const uintptr_t wantVt = g_arkLightVtable.load(std::memory_order_relaxed);

    int slot = 0;
    // Whatever the setter caught on the eye comes first: in a level whose lights
    // filled the class list, that is the only place the beam appears at all.
    auto consider = [&](void* obj) {
        if (obj == nullptr || slot >= kMaxBeams) return;
        for (int i = 0; i < slot; ++i) {
            if (g_arkLights[i].load(std::memory_order_relaxed) == obj) return;
        }
        // These objects get recycled, so the class check is what keeps a freed
        // slot from being treated as a light.
        uintptr_t vt = 0;
        if (wantVt != 0 &&
            (!SafeRead(reinterpret_cast<uintptr_t>(obj), vt) || vt != wantVt)) return;
        uintptr_t ent = 0;
        V3 pos{};
        Quat4 rot{};
        if (!ReadLightEntity(obj, ent, pos, rot)) return;
        if (!IsRidingTheHead(pos, rot)) return;

        // The first head-mounted light found is the one to watch: whatever writes
        // its rotation each frame is the flashlight's own aiming code.
        if (slot == 0 && g_traceLightReader.load(std::memory_order_relaxed) &&
            !g_watchArmed.exchange(true, std::memory_order_acq_rel)) {
            watch::ArmPageWrite(ent + kEntityRotationOff);
        }
        g_arkLights[slot++].store(obj, std::memory_order_release);
    };

    for (int i = 0; i < kMaxBeams; ++i) consider(g_headLights[i].load(std::memory_order_acquire));
    const int known = g_knownLightCount.load(std::memory_order_acquire);
    for (int i = 0; i < known && slot < kMaxBeams; ++i) {
        consider(g_knownLights[i].load(std::memory_order_relaxed));
    }
    for (int i = slot; i < kMaxBeams; ++i) {
        g_arkLights[i].store(nullptr, std::memory_order_release);
    }

    if (g_traceLights.load(std::memory_order_relaxed)) {
        static int s_last = -1;
        if (slot != s_last) {
            s_last = slot;
            PHT_LOG(Info, "flashlight: %d lights are riding the head (%d caught on the eye by "
                          "the setter, %d known by class)",
                    slot, kMaxBeams, known);
        }
    }
}

/// Record a light that is riding the head right now. Round-robin rather than
/// first-come: these objects are recycled, and a slot holding a stale pointer
/// must not lock out the live beam. Every reader re-checks class and geometry
/// before writing anything, so a wrong entry costs a skipped frame, not a write.
void PromoteHeadLight(void* arkLight) {
    for (int i = 0; i < kMaxBeams; ++i) {
        if (g_headLights[i].load(std::memory_order_relaxed) == arkLight) return;
    }
    const int slot = g_headLightNext.fetch_add(1, std::memory_order_relaxed) % kMaxBeams;
    g_headLights[slot].store(arkLight, std::memory_order_release);
}

void RememberLight(void* arkLight) {
    // Identify by class, not by where it happens to be sitting: geometry alone
    // picked the wrong object more than once, and at load time there is no camera
    // to compare against anyway.
    const uintptr_t wantVt = g_arkLightVtable.load(std::memory_order_relaxed);
    if (wantVt != 0) {
        uintptr_t vt = 0;
        if (!SafeRead(reinterpret_cast<uintptr_t>(arkLight), vt) || vt != wantVt) return;
    }

    const int known = g_knownLightCount.load(std::memory_order_acquire);
    for (int i = 0; i < known; ++i) {
        if (g_knownLights[i].load(std::memory_order_relaxed) == arkLight) return;
    }
    if (known >= kMaxKnownLights) return;
    g_knownLights[known].store(arkLight, std::memory_order_release);
    g_knownLightCount.store(known + 1, std::memory_order_release);
}

/// A light that switches on when the player presses the flashlight key is the
/// flashlight. Geometry alone cannot say that - several lights ride the player -
/// so log the value transition per light and let the key press name it.
void TraceLightValue(void* arkLight, uintptr_t ent, float value, const V3& pos,
                     const Quat4& rot) {
    static std::mutex             seenMx;
    static std::map<void*, float> seen;
    bool  report = false;
    float prev   = 0.0f;
    {
        std::lock_guard<std::mutex> lk(seenMx);
        auto it = seen.find(arkLight);
        if (it == seen.end()) {
            seen.emplace(arkLight, value);
            report = true;
        } else if (std::fabs(it->second - value) > 0.001f) {
            prev = it->second;
            it->second = value;
            report = true;
        }
    }
    if (!report) return;

    const V3 toEye = Sub(pos, g_clean.Trans());
    const float dot = Dot(RotateY(rot), g_clean.Forward());
    PHT_LOG(Info, "arklight: ark=%p ent=%p value=%.3f (was %.3f) dist=%.2f dot=%.3f%s",
            arkLight, reinterpret_cast<void*>(ent), value, prev, Length(toEye), dot,
            IsRidingTheHead(pos, rot) ? "  <- ON EYE, FACING VIEW" : "");
}

/// Capture an ArkLight component the moment the beam is switched.
void __fastcall ArkLightSetDetour(void* self, float value, char force) {
    uintptr_t ent = 0;
    V3 pos{};
    Quat4 rot{};
    if (ReadLightEntity(self, ent, pos, rot)) {
        RememberLight(self);
        if (IsRidingTheHead(pos, rot)) PromoteHeadLight(self);
        if (g_traceLights.load(std::memory_order_relaxed)) {
            TraceLightValue(self, ent, value, pos, rot);
        }
    }
    g_origArkLightSet.load(std::memory_order_relaxed)(self, value, force);
}

void __fastcall AddDynLightDetour(void* self, void* light, void* a, void* b, float c,
                                  uintptr_t d) {
    if (light != nullptr) {
        constexpr int kMaxTracedLights = 64;
        static void* s_seen[kMaxTracedLights] = {};
        static int   s_count = 0;
        bool known = false;
        for (int i = 0; i < s_count; ++i) {
            if (s_seen[i] == light) { known = true; break; }
        }
        if (!known && s_count < kMaxTracedLights) {
            s_seen[s_count++] = light;
            PHT_LOG(Info, "LIGHT #%d ptr=%p", s_count, light);
        }
    }
    g_origAddDynLight.load(std::memory_order_relaxed)(self, light, a, b, c, d);
}

}  // namespace

bool Install(const Binding& b) {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    auto& mh = HookManager::Instance();

    void* target = reinterpret_cast<void*>(b.arklight_set_addr);
    ArkLightSetFn orig = nullptr;
    if (mh.CreateHook(target, reinterpret_cast<void*>(&ArkLightSetDetour),
                      reinterpret_cast<void**>(&orig)) != HookStatus::Ok) {
        return false;
    }
    g_origArkLightSet.store(orig, std::memory_order_release);
    if (mh.EnableHook(target) != HookStatus::Ok) {
        g_origArkLightSet.store(nullptr, std::memory_order_release);
        mh.RemoveHook(target);
        return false;
    }

    g_arkLightVtable.store(b.arklight_vtable, std::memory_order_relaxed);
    g_setTransformAddr.store(b.entity_set_transform_addr, std::memory_order_release);
    g_scale.store(b.scale, std::memory_order_relaxed);
    g_traceLights.store(b.trace_lights, std::memory_order_relaxed);
    g_traceLightReader.store(b.trace_light_reader, std::memory_order_relaxed);
    // Last: every reader gates on the entity offset, so nothing pairs with it
    // until the rest is visible.
    g_entityOff.store(b.arklight_entity_off, std::memory_order_release);
    return true;
}

bool InstallDynamicLightTrace(uintptr_t addDynLightAddr) {
    using cameraunlock::hooks::HookManager;
    using cameraunlock::hooks::HookStatus;
    auto& mh = HookManager::Instance();

    void* target = reinterpret_cast<void*>(addDynLightAddr);
    AddDynLightFn orig = nullptr;
    if (mh.CreateHook(target, reinterpret_cast<void*>(&AddDynLightDetour),
                      reinterpret_cast<void**>(&orig)) != HookStatus::Ok) {
        return false;
    }
    g_origAddDynLight.store(orig, std::memory_order_release);
    if (mh.EnableHook(target) != HookStatus::Ok) {
        g_origAddDynLight.store(nullptr, std::memory_order_release);
        mh.RemoveHook(target);
        return false;
    }
    return true;
}

void PublishViewBasis(const Mat34& clean, const Mat34& modified) {
    g_clean    = clean;
    g_modified = modified;
}

void ApplyTracking() {
    if (g_entityOff.load(std::memory_order_acquire) == 0) return;

    static unsigned s_frame = 0;
    if ((s_frame++ % kBeamRefreshFrames) == 0) RefreshBeamSlots();

    const AxisAngle head = RotationBetween(g_clean, g_modified);
    if (!head.valid) return;   // head is centred; leave the beam alone

    const float scaled = head.angle * g_scale.load(std::memory_order_relaxed);
    const float hs = std::sin(scaled * 0.5f);
    const Quat4 delta{head.axis.x * hs, head.axis.y * hs, head.axis.z * hs,
                      std::cos(scaled * 0.5f)};
    for (int i = 0; i < kMaxBeams; ++i) TurnBeam(i, delta);
}

void RestoreTracking() {
    for (int i = 0; i < kMaxBeams; ++i) {
        if (!g_beamHeld[i].exchange(false, std::memory_order_acq_rel)) continue;
        const uintptr_t ent = g_beamEntity[i].load(std::memory_order_acquire);
        if (ent == 0) continue;

        // The pool on the wall follows the beam as the GAME left it, not as this
        // wrote it, so read back what survived the render before putting the
        // original value back. If it is not what went in, something inside the
        // render recomputed the attachment from the skeleton and this write is
        // being made too early to matter.
        if (g_traceLights.load(std::memory_order_relaxed)) {
            Quat4 now{};
            if (SafeRead(ent + kEntityRotationOff, now)) {
                const Quat4& w = g_beamWrote[i];
                const float d = std::fabs(now.x - w.x) + std::fabs(now.y - w.y) +
                                std::fabs(now.z - w.z) + std::fabs(now.w - w.w);
                static unsigned s_n = 0;
                if ((s_n++ % kReadbackEveryFrames) == 0) {
                    PHT_LOG(Info, "readback[%d]: wrote=(%.3f %.3f %.3f %.3f) survived=(%.3f %.3f "
                                  "%.3f %.3f) delta=%.4f %s",
                            i, w.x, w.y, w.z, w.w, now.x, now.y, now.z, now.w, d,
                            d > 0.001f ? "<- OVERWRITTEN DURING RENDER" : "<- intact");
                }
            }
        }

        V3 pos{};
        if (SafeRead(ent + kEntityPositionOff, pos)) {
            SetEntityTransform(ent, pos, g_beamSaved[i], g_beamSavedScale[i]);
        }
    }
}

}  // namespace preyht::flashlight
