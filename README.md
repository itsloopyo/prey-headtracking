# Prey Head Tracking

![Prey (2017) running with this mod](https://raw.githubusercontent.com/itsloopyo/prey-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Prey (2017) that moves the view with your head while your mouse or controller keeps aiming, driven by a webcam, phone, or any OpenTrack compatible tracker, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position

## Requirements

- [Prey (2017)](https://store.steampowered.com/app/480490/Prey/) by Arkane Studios, Steam edition, campaign build. Mooncrash and Typhon Hunter run from a separate `PreyDll.dll` that the mod does not recognize, so they render vanilla.
- A tracking source that sends OpenTrack UDP pose data: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam, phone app or VR headset.
- Windows 10 or 11, 64-bit.

## Installation

1. Download the installer ZIP from the [Releases](https://github.com/itsloopyo/prey-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`. It places the Ultimate ASI Loader (`dinput8.dll`), `PreyHeadTracking.asi` and a default `HeadTracking.ini` next to `Prey.exe` in `Binaries\Danielle\x64\Release\`.
4. Configure OpenTrack to output UDP to `127.0.0.1` port `4242` (see below).
5. Launch the game.

If the installer cannot find your copy of Prey, point it at the install folder yourself. Either set the environment variable:

```powershell
$env:PREY_PATH = "D:\Games\Prey"
.\install.cmd
```

or pass the path as an argument:

```powershell
.\install.cmd "D:\Games\Prey"
```

### Manual Installation

Use the Nexus ZIP, which contains the deploy subtree only and no loader.

1. Install the [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader): put its DLL in `...\steamapps\common\Prey\Binaries\Danielle\x64\Release\` renamed to `dinput8.dll`. The installer ZIP carries a copy under `vendor\ultimate-asi-loader\`.
2. Copy `PreyHeadTracking.asi` into the same folder, next to `Prey.exe`.
3. Copy `HeadTracking.ini` there as well if you want to change any settings. Without it the mod runs on the defaults that file ships with.

## Setting Up OpenTrack

Set OpenTrack's **Output** to **UDP over network**, with destination IP `127.0.0.1` and port `4242`. Any OpenTrack input works. Centering is done in your tracker, not in the game: use OpenTrack's Center bind, your phone app's CENTER button, or SteamVR's reset, and the mod applies whatever pose it receives.

### VR Headset Setup

1. Connect the headset to your PC over Air Link, Virtual Desktop or a link cable.
2. Start SteamVR.
3. Set OpenTrack's **Input** to the SteamVR tracker.
4. Leave **Output** on UDP `127.0.0.1` port `4242`.

### Webcam Setup

Set OpenTrack's **Input** to the **neuralnet tracker**, which tracks your face from a plain webcam with no markers and no IR hardware. Leave **Output** on UDP `127.0.0.1` port `4242`.

### Phone App Setup

The mod accepts one thing: OpenTrack UDP pose packets on port `4242`. A phone tracker is usable here if it sends that protocol itself, or ships a PC-side companion that does. Check your app against that before anything else.

For an app that does send it, what decides the wiring is how much filtering the app does before the packet leaves the phone. An app that filters on-device can point straight at your PC's LAN IP on port `4242`. A raw or lightly filtered feed sent direct will jitter, because the mod's smoothing is sized to take the edge off a clean signal rather than to rescue a noisy one, and that app should go through OpenTrack instead so its filters and curves can clean the feed up first. The test: try direct, hold your head still, and if the view drifts or shakes, route it through OpenTrack.

I made [Headcam](https://headcam.app) so decent tracking was free for anybody with a phone already in their pocket. It filters on-device, so it can send direct. Any app that filters enough noise works identically.

A phone on WiFi is a remote connection and gets `RemoteSmoothing`. So does a tracker on this same PC that sends to your LAN address instead of `127.0.0.1`: the mod classifies by transport, not by machine.

## Controls

Two equivalent binding sets - use whichever your keyboard has:

| Action | Nav-cluster | Chord |
|--------|-------------|-------|
| Toggle tracking | `End` | `Ctrl+Shift+Y` |
| Cycle tracking mode | `Page Up` | `Ctrl+Shift+G` |
| Toggle yaw mode (world / camera-local) | `Page Down` | `Ctrl+Shift+H` |
| Step to the next tracker source | - | `Ctrl+Shift+U` |
| Carry your body with your head (space suit) | `Delete` | `Ctrl+Shift+J` |

`Page Up` / `Ctrl+Shift+G` cycles through full 6DOF tracking, rotation only, position only, and back to 6DOF.

`Delete` / `Ctrl+Shift+J` turns off carrying your own body with your head. It is on by default and applies only while you are in the space suit: the suit's collar and shoulders turn with your head instead of staying where your character is facing, so you look around INSIDE the suit rather than across it. Prey draws the item in your hands as part of the same object, so in the suit that follows your head too; on foot the mod leaves the body - and the gun - alone, so the barrel keeps pointing at the crosshair.

`Ctrl+Shift+U` is for when more than one app is sending to the port. The mod follows one of them and ignores the rest, and which one it picks is decided by whichever packet arrives first after the game starts. Press it until the view answers your head.

## Configuration

Edit `HeadTracking.ini` next to `Prey.exe` in `Binaries\Danielle\x64\Release\`. Neither `install.cmd` nor the launcher overwrites an INI that already exists, and uninstalling leaves it alone. Values outside their documented range are clamped, and the log says which ones moved.

```ini
[Network]
; UDP port the OpenTrack-compatible tracker sends to. Must be 1024-65535.
UdpPort = 4242

[Tracking]
; Per-axis sensitivity. 1.0 = 1:1 with tracker.
YawSensitivity   = 1.0
PitchSensitivity = 1.0
RollSensitivity  = 1.0

; Per-axis inversion. Tune these in-game if an axis moves the wrong way.
InvertYaw   = false
InvertPitch = false
InvertRoll  = false

; Deadzone in degrees, applied to all axes. 0 = off.
Deadzone = 0.0

; Smoothing is picked per connection from the tracker's source address, and
; covers rotation and position. 0 = lightest, 1 = heavy.
; LocalSmoothing applies when the tracker sends from this machine (loopback).
LocalSmoothing  = 0.0
; RemoteSmoothing applies to a remote device on the network, such as a phone.
RemoteSmoothing = 0.15

[Hotkeys]
; Win32 VK names, or a numeric VK like 0x22. The Ctrl+Shift+Y/G/H chords are
; baked into the poller for keyboards without a nav cluster.
ToggleKey   = End
YawModeKey  = PageDown
PositionKey = PageUp

[Camera]
; true (default) = horizon-locked: head-yaw rotates around the world up-axis,
; so "up" stays constant. false = around the camera's own up-axis, which leans
; the view at extreme pitch. Toggle live with PageDown.
WorldSpaceYaw = true

; Move Prey's own crosshair to the spot in the head-tracked view your
; mouse-controlled aim points at, so shots land where the crosshair is drawn.
; Prey's interaction prompt is drawn at the crosshair and follows it.
CompensateReticle = true

; Project world-anchored HUD markers (objective markers, interactable diamonds)
; through the head-tracked view so they stay on their objects.
CompensateMarkers = true

; Horizontal field of view in degrees. 0 (default) leaves Prey's own Field of
; View slider in charge. Any other value in 25-170 is written straight into the
; engine every frame, which skips the game's 120-degree clamp and overrides the
; slider until you set this back to 0.
FieldOfView = 0

; Draw the gun in your hands through the same lens as the world. Prey renders
; held weapons in a separate pass with its own field of view, fixed at the
; default Field of View slider's value, so raising the slider magnifies the
; weapon. That is invisible until head tracking leaves the gun on your body
; while the view turns, at which point the gun sweeps further across the screen
; than the world and the barrel stops pointing at the crosshair. At the default
; slider this changes nothing; above it the gun is drawn smaller.
MatchWeaponFieldOfView = true

; Inject the head pose when the game sets the view camera rather than only for
; the render, so culling and the HUD see it too. Without this, geometry is
; culled out of the view when you turn your head.
EarlyInject = true

; Turn off Prey's software occlusion culling. It is built from a camera the mod
; cannot reach, so it culls the head-turned view against the un-turned one and
; geometry vanishes at the edge of a head turn.
DisableCoverageBuffer = true

; Keep the player's own first-person body on the body. Prey builds its render
; object from the view camera, so without this it swings with your head.
CompensateBody = true

; Carry your own first-person body with your head - the space suit fix. On by
; default, and only ever applied while the suit is on. Delete or Ctrl+Shift+J
; turns it off in game. See Controls.
BodyFollowsHead = true

; Turn the flashlight beam with your head. It turns further than the view does,
; because when you turn your head your eyes end up past the centre of the
; screen. 1.0 matches the view exactly, 0 leaves the beam where the game put it.
; Needs EarlyInject = true.
CompensateFlashlight = true
FlashlightScale = 1.5

[Position]
; 6DOF positional tracking. The head offset is added to the rendered camera
; position only. This key is the startup state; PageUp cycles it live.
Enabled = true

; Per-axis sensitivity. X = sway (left/right), Y = heave (up/down),
; Z = surge (forward/back).
SensitivityX = 1.0
SensitivityY = 1.0
SensitivityZ = 1.0

; Per-axis inversion.
InvertX = false
InvertY = false
InvertZ = false

; Travel limits in meters. Z is asymmetric: more range forward (LimitZ) than
; backward (LimitZBack) so leaning back does not clip through the player.
LimitX     = 0.30
LimitY     = 0.20
LimitZ     = 0.40
LimitZBack = 0.10

; Distance in meters from the pivot in your neck to the point your tracker
; watches: PivotForward towards your face, PivotUp above the pivot. 0 (default)
; leaves the correction off, which is right for a tracker that already does it.
; If pitching your head up walks the camera backwards out of your character,
; start at PivotForward = 0.10, PivotUp = 0.03 and raise PivotForward until the
; swing stops.
PivotForward = 0.0
PivotUp      = 0.0

[Logging]
; The log is written next to Prey.exe and starts empty on every launch. The
; previous session is kept alongside it as HeadTracking.prev.log, which is the
; one to send after a crash.
LogToFile = true
LogPath   = HeadTracking.log
```

The shipped INI also carries several diagnostic keys, each commented in place. Leave them off for normal play.

## Troubleshooting

**Mod not loading**

- Check for `HeadTracking.log` next to `Prey.exe`. No log file at all means the ASI loader never ran: confirm `dinput8.dll` and `PreyHeadTracking.asi` are both in `Binaries\Danielle\x64\Release\`.
- A log line saying the mod is staying dormant means your `PreyDll.dll` is not one of the builds this mod knows. The line says whether your game is newer or older than the newest build profile. Newer means the mod needs a new profile: open an issue with the fingerprint from that log line.
- Mooncrash and Typhon Hunter use their own `PreyDll.dll` and are refused by design.

**No tracking response**

- "Listening for OpenTrack" in the log with no pose following it means nothing is arriving. Check OpenTrack's output is UDP to `127.0.0.1` port `4242` and that its tracker is started.
- If the tracker is on another device, allow Prey through the Windows firewall on UDP `4242`.
- If the log says a second tracker source is being ignored, two apps are sending to the port and the mod is following the other one. Close the one you are not using, or press `Ctrl+Shift+U` in game to step to the next source. Starting your tracker before the game does not settle this - the two apps race by milliseconds.
- Tracking is suppressed whenever Prey releases the mouse cursor, so it stops in the main menu, the pause menu, the TranScribe, the inventory, and when you alt-tab away. Click back on the game window and it resumes.

**Jittery or unstable tracking**

- Raise `RemoteSmoothing` for a phone or WiFi tracker, or `LocalSmoothing` for a tracker on this PC.
- A phone app that does not filter on-device should be routed through OpenTrack rather than sent direct, so OpenTrack's filters can clean the feed up.

**Wrong rotation axis**

- Flip the matching `InvertYaw` / `InvertPitch` / `InvertRoll` in the INI, or `InvertX` / `InvertY` / `InvertZ` for position.
- If the view sits off to one side, center it in your tracker app.
- If yaw feels wrong when you look far up or down, toggle the yaw mode with `Page Down`.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs. The Ultimate ASI Loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

## Building from Source

Requires Visual Studio 2022 with the C++ toolchain, and [pixi](https://pixi.sh). The build is game-free: it links the `cameraunlock-core` and MinHook submodules, not any game DLL.

```powershell
git clone --recursive https://github.com/itsloopyo/prey-headtracking.git
cd prey-headtracking
pixi run build
pixi run package
```

`build` produces `build/src/PreyHeadTracking/Release/PreyHeadTracking.asi`; `package` writes the installer and Nexus ZIPs to `release/`.

## Community & Support

- [Discord](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch of head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your phone into a head tracker

## License

MIT License - see [LICENSE](LICENSE) for details.

## Credits

- **Prey (2017)** by Arkane Studios, published by Bethesda Softworks.
- [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) (MIT) for plugin loading.
- [MinHook](https://github.com/TsudaKageyu/minhook) (BSD-2-Clause) for function hooking.
- [OpenTrack](https://github.com/opentrack/opentrack) (ISC) for the UDP pose protocol.
- [CameraUnlock core](https://github.com/itsloopyo/cameraunlock-core) (MIT), the shared head-tracking library.

## Disclaimer

This mod is not affiliated with, endorsed by, or supported by Arkane Studios or Bethesda Softworks. Use at your own risk.
