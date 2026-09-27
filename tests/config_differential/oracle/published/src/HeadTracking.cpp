#include "HeadTracking.hpp"

#include "CryEngineCamera.hpp"
#include "Framework.hpp"
#include "utility/Logging.hpp"

#include "cameraunlock/input/chord_hotkeys.h"
#include "cameraunlock/math/smoothing_utils.h"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include <windows.h>

namespace preyht {

namespace {
/// Map a friendly key name from HeadTracking.ini to a Win32 VK_ code.
/// Handles "F1".."F24", single A-Z/0-9, and a few common names.
int ParseVk(const std::string& name) {
    using namespace cameraunlock::input;
    if (name.empty()) return 0;

    std::string n;
    n.reserve(name.size());
    for (char c : name) n.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));

    // A lone digit is the key of that name, not a VK code: "5" must bind the 5
    // key (0x35), and reading it as VK 0x05 silently bound XBUTTON2 instead.
    if (n.size() == 1 && n[0] >= '0' && n[0] <= '9') return n[0];

    // Numeric VK literal, e.g. "0x22" or "34" (base 0 honours the 0x prefix).
    if (std::isdigit(static_cast<unsigned char>(n[0]))) {
        int v = static_cast<int>(std::strtol(n.c_str(), nullptr, 0));
        if (v > 0 && v <= 0xFF) return v;
    }
    if (n.size() >= 2 && n[0] == 'F' && std::isdigit(static_cast<unsigned char>(n[1]))) {
        int idx = std::atoi(n.c_str() + 1);
        if (idx >= 1 && idx <= 24) return 0x6F + idx;  // VK_F1 = 0x70
    }
    // Digits already returned above, so only letters are left to name themselves.
    if (n.size() == 1 && n[0] >= 'A' && n[0] <= 'Z') return n[0];
    if (n == "HOME")     return VK::Home;
    if (n == "END")      return VK::End;
    if (n == "INSERT")   return VK::Insert;
    if (n == "DELETE")   return VK::Delete;
    if (n == "SPACE")    return VK::Space;
    if (n == "PAGEUP")   return 0x21;  // VK_PRIOR
    if (n == "PAGEDOWN") return 0x22;  // VK_NEXT
    if (n == "ESCAPE" || n == "ESC") return VK::Escape;
    return 0;
}

void WarnUnboundKey(const char* setting, const std::string& name, const char* chord) {
    PHT_LOG(Warn, "[Hotkeys] %s = \"%s\" is not a key name this mod knows, so that action has "
                  "no nav-cluster binding this session. Use %s instead, or fix the name in "
                  "HeadTracking.ini.", setting, name.c_str(), chord);
}
}  // namespace

std::optional<std::string> HeadTracking::OnInitialize() {
    const auto& cfg = Framework::Get().Cfg();

    m_worldSpaceYaw.store(cfg.world_space_yaw, std::memory_order_release);
    m_dofMode.store(cfg.position_enabled ? DofMode::SixDof : DofMode::RotationOnly,
                    std::memory_order_release);

    m_processor.SetSensitivity(cfg.AsSensitivity());
    m_processor.SetDeadzone(cfg.AsDeadzone());
    m_processor.SetLocalSmoothing(cfg.local_smoothing);
    m_processor.SetRemoteSmoothing(cfg.remote_smoothing);

    m_posProcessor.SetSettings(cfg.AsPositionSettings());
    m_pivotForward = cfg.pivot_forward;
    m_pivotUp      = cfg.pivot_up;
    m_localSmoothing = cfg.local_smoothing;
    m_remoteSmoothing = cfg.remote_smoothing;

    m_receiver = std::make_unique<cameraunlock::UdpReceiver>();
    m_receiver->SetLog([](const std::string& m){ PHT_LOG(Info, "[udp] %s", m.c_str()); });
    if (!m_receiver->Start(cfg.udp_port)) {
        // Non-fatal: UdpReceiver schedules its own retry loop when the port
        // is held. We log and continue; pose simply stays zero until it binds.
        PHT_LOG(Warn, "OpenTrack UDP %u not bound yet; receiver will retry.", cfg.udp_port);
    } else {
        PHT_LOG(Info, "Listening for OpenTrack on UDP %u", cfg.udp_port);
    }
    // Seed the processors so the first frame already uses the right value.
    // Nothing is logged here: no packet has arrived, and the locality line
    // asserts a connection.
    m_isRemoteConnection = m_receiver->IsRemoteConnection();
    m_processor.SetIsRemoteConnection(m_isRemoteConnection);
    m_posProcessor.SetIsRemoteConnection(m_isRemoteConnection);

    m_hotkeys = std::make_unique<cameraunlock::input::HotkeyPoller>();
    using cameraunlock::input::ChordGuarded;
    using cameraunlock::input::NavGuarded;
    // Nav-cluster bindings from config. NavGuarded suppresses them while
    // Ctrl+Shift is held so the chord path is the sole trigger for a
    // Ctrl+Shift+<nav> combo and one keypress never fires an action twice.
    // A name ParseVk cannot place leaves that action on its chord alone, which
    // reads in game exactly like a broken hotkey. Say so rather than binding
    // nothing in silence.
    if (int vk = ParseVk(cfg.toggle_key); vk != 0) {
        m_hotkeys->SetToggleKey(vk, NavGuarded([this]{ SetEnabled(!Enabled()); }));
    } else {
        WarnUnboundKey("ToggleKey", cfg.toggle_key, "Ctrl+Shift+Y");
    }
    if (int vk = ParseVk(cfg.yaw_mode_key); vk != 0) {
        m_hotkeys->AddHotkey(vk, NavGuarded([this]{ ToggleYawMode(); }));
    } else {
        WarnUnboundKey("YawModeKey", cfg.yaw_mode_key, "Ctrl+Shift+H");
    }
    if (int vk = ParseVk(cfg.position_key); vk != 0) {
        m_hotkeys->AddHotkey(vk, NavGuarded([this]{ CycleDofMode(); }));
    } else {
        WarnUnboundKey("PositionKey", cfg.position_key, "Ctrl+Shift+G");
    }
    // Chord equivalents (Ctrl+Shift+Y toggle, Ctrl+Shift+G DOF-mode cycle,
    // Ctrl+Shift+H yaw mode) for keyboards without a nav cluster.
    // The poller edge-detects the letter; ChordGuarded gates it on the modifiers.
    m_hotkeys->AddHotkey('Y', ChordGuarded([this]{ SetEnabled(!Enabled()); }));
    m_hotkeys->AddHotkey('G', ChordGuarded([this]{ CycleDofMode(); }));
    m_hotkeys->AddHotkey('H', ChordGuarded([this]{ ToggleYawMode(); }));
    // The receiver locks onto whichever app's packet lands first after the bind
    // and ignores every other one, so an app the player is not using - a bridge
    // left running from a previous session, a vendor tool that streams a pose
    // whether or not it sees a face - can win the port and hold it for the whole
    // session. From the game that reads as tracking simply not working, and
    // starting the real tracker afterwards does not displace the incumbent
    // because it never goes silent. This steps to the next source.
    m_hotkeys->AddHotkey('U', ChordGuarded([this]{ CycleTrackerSource(); }));
    // Delete / Ctrl+Shift+J carry the first-person body with the head. Wanted in
    // the space suit, not wanted with a gun in hand - Prey draws the held item as
    // part of the same object - so it is a key rather than a restart.
    m_hotkeys->AddHotkey(cameraunlock::input::VK::Delete, NavGuarded([]{ ToggleBodyFollowsHead(); }));
    m_hotkeys->AddHotkey('J', ChordGuarded([]{ ToggleBodyFollowsHead(); }));
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
                    Framework::Get().Cfg().udp_port);
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

    // The PHYSICAL head rotation: the smoothed pose before per-axis
    // sensitivity and inversion. The pivot artifact is a property of where the
    // tracked point sits on a real head, so scaling that angle by a sensitivity
    // factor over-corrects it and inverting an axis drives the correction
    // backwards.
    const float rawX = px, rawY = py, rawZ = pz;

    float physYaw{}, physPitch{}, physRoll{};
    m_processor.GetSmoothedRotation(physYaw, physPitch, physRoll);
    const auto rotQ = cameraunlock::math::Quat4::FromYawPitchRoll(physYaw, physPitch, physRoll);

    // Take out the part of the reported position that is only the head turning.
    //
    // A tracker watches a point on the face, and the head swings that point about
    // a pivot down in the neck. Pitching up therefore reports several centimetres
    // of translation the player never made, and because the lever is longest on
    // pitch that axis is much the worst - yaw and roll swing the tracked point far
    // less. Left uncorrected in Prey it walks the camera out of the player's head
    // and into their own body.
    //
    // Modelled as a rigid lever from the pivot to the tracked point in head-local
    // axes (x right, y up, z out the BACK of the head, matching the rest of the
    // pipeline). At the tracker's own neutral pose the rotation is identity, so
    // the correction is zero there.
    if (m_pivotForward != 0.0f || m_pivotUp != 0.0f) {
        const cameraunlock::math::Vec3 lever(0.0f, m_pivotUp, -m_pivotForward);
        const cameraunlock::math::Vec3 artifact = rotQ.Rotate(lever) - lever;
        px -= artifact.x;
        py -= artifact.y;
        pz -= artifact.z;
    }

    // The raw tracker position next to what the pivot correction made of it. A
    // camera that moves when the head only turned is either the tracker reporting
    // the swing or this correction inventing one, and the two are impossible to
    // tell apart from the processed value alone.
    if (Framework::Get().Cfg().dump_camera) {
        constexpr unsigned kDumpEveryFrames = 30;   // about twice a second
        static unsigned s_n = 0;
        if ((s_n++ % kDumpEveryFrames) == 0) {
            PHT_LOG(Info, "tracker raw: pitch=%.2f pos_in=(%.4f %.4f %.4f) "
                          "after_pivot=(%.4f %.4f %.4f) lever=(%.2f %.2f)",
                    physPitch, rawX, rawY, rawZ, px, py, pz, m_pivotForward, m_pivotUp);
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

void HeadTracking::CycleDofMode() {
    DofMode next;
    const char* label;
    switch (GetDofMode()) {
        case DofMode::SixDof:       next = DofMode::RotationOnly; label = "3DOF rotation only"; break;
        case DofMode::RotationOnly: next = DofMode::PositionOnly; label = "3DOF position only"; break;
        default:                    next = DofMode::SixDof;       label = "6DOF (rotation + position)"; break;
    }
    m_dofMode.store(next, std::memory_order_release);
    PHT_LOG(Info, "DOF mode: %s", label);
}

void HeadTracking::CycleTrackerSource() {
    m_receiver->CycleSource();
    PHT_LOG(Info, "Stepping to the next app sending to UDP %u; the next [udp] line names the "
                  "source now driving the view.", Framework::Get().Cfg().udp_port);
}

void HeadTracking::ToggleYawMode() {
    const bool world = !m_worldSpaceYaw.load(std::memory_order_acquire);
    m_worldSpaceYaw.store(world, std::memory_order_release);
    PHT_LOG(Info, "Yaw mode: %s", world ? "world-space (horizon-locked)" : "camera-local");
}

}  // namespace preyht
