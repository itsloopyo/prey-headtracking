#include "HeadTracking.hpp"

#include "CryEngineCamera.hpp"
#include "Framework.hpp"
#include "utility/Logging.hpp"

#include "cameraunlock/input/key_binding_registration.h"
#include "cameraunlock/input/key_bindings.h"
#include "cameraunlock/math/smoothing_utils.h"

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include <windows.h>

namespace preyht {

namespace {
/// A hotkey list from CameraUnlock.ini. The table holds only lists its hotkey
/// codec wrote, so one that does not parse is a bug, not a player's typo.
std::vector<cameraunlock::input::KeyBinding> Bindings(const char* key, const std::string& list) {
    const auto parsed = cameraunlock::input::ParseKeyBindings(list);
    if (!parsed.ok())
        throw std::logic_error(std::string(key) + "=" + list + " does not parse: " + parsed.error);
    return parsed.bindings;
}

const char* ModeName(cameraunlock::TrackingMode mode) {
    switch (mode) {
        case cameraunlock::TrackingMode::RotationAndPosition: return "6DOF (rotation + position)";
        case cameraunlock::TrackingMode::RotationOnly:        return "3DOF rotation only";
        case cameraunlock::TrackingMode::PositionOnly:        return "3DOF position only";
    }
    return "unknown";
}
}  // namespace

std::optional<std::string> HeadTracking::OnInitialize() {
    const auto& cfg = Framework::Get().Cfg();

    m_enabled.store(cfg.enable_on_startup, std::memory_order_release);
    m_worldSpaceYaw.store(cfg.world_space_yaw, std::memory_order_release);
    m_mode.store(StartupMode(cfg), std::memory_order_release);
    PHT_LOG(Info, "Tracking %s at startup, mode %s, yaw %s",
            cfg.enable_on_startup ? "on" : "off", ModeName(StartupMode(cfg)),
            cfg.world_space_yaw ? "world-space" : "camera-local");

    m_processor.SetLocalSmoothing(cfg.local_smoothing);
    m_processor.SetRemoteSmoothing(cfg.remote_smoothing);

    m_posProcessor.SetSettings(cfg.AsPositionSettings());
    m_localSmoothing = cfg.local_smoothing;
    m_remoteSmoothing = cfg.remote_smoothing;

    m_receiver = std::make_unique<cameraunlock::UdpReceiver>();
    m_receiver->SetLog([](const std::string& m){ PHT_LOG(Info, "[udp] %s", m.c_str()); });
    if (!m_receiver->Start(static_cast<uint16_t>(cfg.udp_port))) {
        // Non-fatal: UdpReceiver schedules its own retry loop when the port
        // is held. We log and continue; pose simply stays zero until it binds.
        PHT_LOG(Warn, "OpenTrack UDP %u not bound yet; receiver will retry.", static_cast<unsigned>(cfg.udp_port));
    } else {
        PHT_LOG(Info, "Listening for OpenTrack on UDP %u", static_cast<unsigned>(cfg.udp_port));
    }
    // Seed the processors so the first frame already uses the right value.
    // Nothing is logged here: no packet has arrived, and the locality line
    // asserts a connection.
    m_isRemoteConnection = m_receiver->IsRemoteConnection();
    m_processor.SetIsRemoteConnection(m_isRemoteConnection);
    m_posProcessor.SetIsRemoteConnection(m_isRemoteConnection);

    m_hotkeys = std::make_unique<cameraunlock::input::HotkeyPoller>();
    using cameraunlock::input::RegisterKeyBindings;
    RegisterKeyBindings(*m_hotkeys, Bindings("ToggleKey", cfg.toggle_key), [this]{ SetEnabled(!Enabled()); });
    RegisterKeyBindings(*m_hotkeys, Bindings("CycleTrackingModeKey", cfg.cycle_tracking_mode_key),
                        [this]{ CycleTrackingMode(); });
    RegisterKeyBindings(*m_hotkeys, Bindings("YawModeKey", cfg.yaw_mode_key), [this]{ ToggleYawMode(); });
    // The receiver locks onto whichever app's packet lands first after the bind
    // and ignores every other one, so an app the player is not using - a bridge
    // left running from a previous session, a vendor tool that streams a pose
    // whether or not it sees a face - can win the port and hold it for the whole
    // session. From the game that reads as tracking simply not working, and
    // starting the real tracker afterwards does not displace the incumbent
    // because it never goes silent. This steps to the next source.
    RegisterKeyBindings(*m_hotkeys, Bindings("CycleTrackerSourceKey", cfg.cycle_tracker_source_key),
                        [this]{ CycleTrackerSource(); });
    // Carries the first-person body with the head. Wanted in the space suit, not
    // wanted with a gun in hand - Prey draws the held item as part of the same
    // object - so it is a key rather than a restart.
    RegisterKeyBindings(*m_hotkeys, Bindings("BodyFollowsHeadKey", cfg.body_follows_head_key), []{
        const bool follows = ToggleBodyFollowsHead();
        Framework::Get().Persist([follows](Config& c) { c.body_follows_head = follows; });
    });
    m_hotkeys->Start();

    m_lastFrame = std::chrono::steady_clock::now();
    return std::nullopt;
}

void HeadTracking::SetEnabled(bool e) {
    if (m_enabled.exchange(e, std::memory_order_release) != e) {
        PHT_LOG(Info, "Tracking %s", e ? "enabled" : "disabled");
    }
}

void HeadTracking::SyncConnectionLocality() {
    const bool isRemote = m_receiver->IsRemoteConnection();
    const bool changed = (isRemote != m_isRemoteConnection);
    if (changed) {
        m_isRemoteConnection = isRemote;
        m_processor.SetIsRemoteConnection(isRemote);
        m_posProcessor.SetIsRemoteConnection(isRemote);
    }

    // Only called from the receiving path, so a packet has arrived and the line
    // names a real connection. Emitted on the first packet and on every
    // locality change after that.
    if (!changed && m_loggedLocality) return;
    m_loggedLocality = true;

    const double effective = cameraunlock::math::GetEffectiveSmoothing(
        m_localSmoothing, m_remoteSmoothing, isRemote);
    PHT_LOG(Info, "Tracker connection is %s; smoothing=%.3f",
            isRemote ? "remote" : "local", effective);
}

void HeadTracking::OnFrame() {
    const auto now = std::chrono::steady_clock::now();
    const float dt = std::chrono::duration<float>(now - m_lastFrame).count();
    m_lastFrame = now;

    // Reported ahead of the enabled gate: a user who pressed End is one of the
    // likeliest authors of a "no head tracking" report, and hiding the packet
    // evidence there is the one case the line exists for.
    if (m_receiver && m_receiver->IsReceiving()) {
        SyncConnectionLocality();
    }

    if (!Enabled()) {
        // Toggled off is the one case that must return the view to the game's
        // own camera, so publish a zero pose rather than holding the last one.
        m_outYaw.store(0.0f, std::memory_order_relaxed);
        m_outPitch.store(0.0f, std::memory_order_relaxed);
        m_outRoll.store(0.0f, std::memory_order_relaxed);
        m_outPosValid.store(false, std::memory_order_release);
        m_wasReceiving = false;
        return;
    }
    if (!m_receiver || !m_receiver->IsReceiving()) {
        // Tracking loss HOLDS the last pose (doctrine). Zeroing here would swing
        // the view to centre every time a phone tracker stalls for half a second
        // and swing it back when the packets resume.
        m_wasReceiving = false;  // resume eases in from a clean interpolator segment
        return;
    }

    // New-sample edge: the receiver's packet timestamp changes only when fresh
    // data arrives. Held frames between packets report the same stamp, which is
    // exactly what lets the interpolators bridge the gap instead of flat-spotting.
    const int64_t sampleTs = m_receiver->GetLastReceiveTimestamp();
    const bool isNew = (sampleTs != m_lastSampleTs);
    m_lastSampleTs = sampleTs;

    const bool resume = !m_wasReceiving;
    m_wasReceiving = true;

    if (resume) {
        m_poseInterp.Reset();
        m_posInterp.Reset();
        m_posProcessor.ResetSmoothing();
    }

    float yaw{}, pitch{}, roll{};
    if (!m_receiver->GetRotation(yaw, pitch, roll)) return;

    static bool s_loggedFirst = false;
    if (!s_loggedFirst) {
        PHT_LOG(Info, "HeadTracking: first OpenTrack sample yaw=%.2f pitch=%.2f roll=%.2f", yaw, pitch, roll);
        s_loggedFirst = true;
    }

    // Receiver -> interpolator -> processor.
    const auto interp = m_poseInterp.Update(yaw, pitch, roll, isNew, dt);
    const auto processed = m_processor.Process(interp.yaw, interp.pitch, interp.roll, dt);

    // The pose arrives on a socket bound to every interface, so this is an
    // untrusted boundary, and the failure it lets through does not clear itself:
    // the smoother reads its own previous output, so one sample that resolves to
    // no rotation leaves every later frame there too, long after the sender has
    // stopped. The interpolator is where a merely enormous angle becomes one,
    // because it takes a difference between consecutive samples.
    //
    // So drop the frame, put the pipeline back to a state the next good sample
    // can start from, and hold the last pose the player actually saw.
    if (!std::isfinite(processed.yaw) || !std::isfinite(processed.pitch) ||
        !std::isfinite(processed.roll)) {
        m_poseInterp.Reset();
        m_posInterp.Reset();
        m_processor.ResetSmoothing();
        m_posProcessor.ResetSmoothing();
        static bool s_loggedBadSample = false;
        if (!s_loggedBadSample) {
            s_loggedBadSample = true;
            PHT_LOG(Warn, "A tracker sample did not resolve to a rotation and was dropped; the "
                          "view holds its last pose and the pipeline restarts on the next good "
                          "sample. Check what is sending to UDP %u.",
                    static_cast<unsigned>(Framework::Get().Cfg().udp_port));
        }
        return;
    }

    // In position-only mode the head must not rotate the view, so publish a zero
    // delta (the processor still runs to keep its smoothing state warm for the
    // next mode switch). pose stays "valid" via the timestamp; a zero delta is a
    // no-op in both the world-yaw add and the camera-local compose paths.
    const bool rotOn = RotationEnabled();
    m_outYaw  .store(rotOn ? processed.yaw   : 0.0f, std::memory_order_relaxed);
    m_outPitch.store(rotOn ? processed.pitch : 0.0f, std::memory_order_relaxed);
    m_outRoll .store(rotOn ? processed.roll  : 0.0f, std::memory_order_relaxed);
    m_outTs   .store(processed.timestamp_us, std::memory_order_release);

    // --- Positional tracking (6DOF) ---------------------------------------
    float px{}, py{}, pz{};
    if (!PositionEnabled() || !m_receiver->GetPosition(px, py, pz)) {
        m_outPosValid.store(false, std::memory_order_release);
        return;
    }

    float physYaw{}, physPitch{}, physRoll{};
    m_processor.GetSmoothedRotation(physYaw, physPitch, physRoll);
    const auto rotQ = cameraunlock::math::Quat4::FromYawPitchRoll(physYaw, physPitch, physRoll);

    if (Framework::Get().Cfg().dump_camera) {
        constexpr unsigned kDumpEveryFrames = 30;   // about twice a second
        static unsigned s_n = 0;
        if ((s_n++ % kDumpEveryFrames) == 0) {
            PHT_LOG(Info, "tracker raw: pitch=%.2f pos_in=(%.4f %.4f %.4f)", physPitch, px, py, pz);
        }
    }

    // Tag with the receiver stamp so the position interpolator shares the same
    // new-sample detection as the pose interpolator.
    const cameraunlock::PositionData raw(px, py, pz, sampleTs);

    const cameraunlock::PositionData interpPos = m_posInterp.Update(raw, dt);
    const cameraunlock::math::Vec3 offset = m_posProcessor.Process(interpPos, rotQ, dt);

    m_outPosX.store(offset.x, std::memory_order_relaxed);
    m_outPosY.store(offset.y, std::memory_order_relaxed);
    m_outPosZ.store(offset.z, std::memory_order_relaxed);
    m_outPosValid.store(true, std::memory_order_release);
}

void HeadTracking::OnShutdown() {
    if (m_hotkeys)  { m_hotkeys->Stop();  m_hotkeys.reset(); }
    if (m_receiver) { m_receiver->Stop(); m_receiver.reset(); }
}

cameraunlock::TrackingPose HeadTracking::CurrentPose() const {
    cameraunlock::TrackingPose p;
    // Timestamp first: OnFrame publishes it last, with release ordering, so
    // acquiring it here is what makes the three angles beneath it visible.
    // Reading them before the acquire pairs the fence with nothing.
    p.timestamp_us = m_outTs   .load(std::memory_order_acquire);
    p.yaw          = m_outYaw  .load(std::memory_order_relaxed);
    p.pitch        = m_outPitch.load(std::memory_order_relaxed);
    p.roll         = m_outRoll .load(std::memory_order_relaxed);
    return p;
}

HeadPosition HeadTracking::CurrentPosition() const {
    HeadPosition p;
    p.valid = m_outPosValid.load(std::memory_order_acquire);
    p.x = m_outPosX.load(std::memory_order_relaxed);
    p.y = m_outPosY.load(std::memory_order_relaxed);
    p.z = m_outPosZ.load(std::memory_order_relaxed);
    return p;
}

void HeadTracking::CycleTrackingMode() {
    using cameraunlock::TrackingMode;
    TrackingMode next;
    switch (GetTrackingMode()) {
        case TrackingMode::RotationAndPosition: next = TrackingMode::RotationOnly; break;
        case TrackingMode::RotationOnly:        next = TrackingMode::PositionOnly; break;
        default:                                next = TrackingMode::RotationAndPosition; break;
    }
    m_mode.store(next, std::memory_order_release);
    PHT_LOG(Info, "Tracking mode: %s", ModeName(next));
    const cameraunlock::TrackingModeChannels channels = cameraunlock::EncodeTrackingMode(next);
    Framework::Get().Persist([channels](Config& c) {
        c.rotation_enabled = channels.rotation_enabled;
        c.position_enabled = channels.position_enabled;
    });
}

void HeadTracking::CycleTrackerSource() {
    m_receiver->CycleSource();
    PHT_LOG(Info, "Stepping to the next app sending to UDP %u; the next [udp] line names the "
                  "source now driving the view.", static_cast<unsigned>(Framework::Get().Cfg().udp_port));
}

void HeadTracking::ToggleYawMode() {
    const bool world = !m_worldSpaceYaw.load(std::memory_order_acquire);
    m_worldSpaceYaw.store(world, std::memory_order_release);
    PHT_LOG(Info, "Yaw mode: %s", world ? "world-space (horizon-locked)" : "camera-local");
    Framework::Get().Persist([world](Config& c) { c.world_space_yaw = world; });
}

}  // namespace preyht
