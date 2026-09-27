# Prey Head Tracking

![Prey (2017) running with this mod](https://raw.githubusercontent.com/itsloopyo/prey-headtracking/main/assets/readme-clip.gif)

An unofficial head tracking mod for Prey (2017) that moves the view with your head while your mouse or controller keeps aiming, driven by OpenTrack over UDP, with no VR headset required.

## Features

- **Decoupled look and aim** - head tracking moves the camera; aim stays on your mouse/controller
- **6DOF positional tracking** - lean and peek with head position
- **Works with any OpenTrack compatible tracker** - free options available for PC, iOS and Android

## Requirements

- [Prey (2017)](https://store.steampowered.com/app/480490/Prey/) by Arkane Studios, campaign build. The Steam and Xbox Game Pass editions are both supported; they are separate binaries and the mod carries a profile for each. Mooncrash and Typhon Hunter run from their own `PreyDll.dll` and are refused by design, so they render vanilla.
- A tracking source that sends OpenTrack UDP pose data: [OpenTrack](https://github.com/opentrack/opentrack) with a webcam, phone app or VR headset.
- Windows 10 or 11, 64-bit.

## Installation

### Lopari

Download [Lopari](https://lopari.app), choose **Prey**, and click
**Play with head tracking**.

### Standalone Installer

1. Download the installer ZIP from the [Releases](https://github.com/itsloopyo/prey-headtracking/releases) page.
2. Extract it anywhere.
3. Double-click `install.cmd`. It places the Ultimate ASI Loader (`dinput8.dll`) and `PreyHeadTracking.asi` next to `Prey.exe`. That folder differs by edition - see [Where the files go](#where-the-files-go). The mod creates `CameraUnlock.ini` beside them the first time the game starts.
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

### Where the files go

The mod's files and `CameraUnlock.ini` sit in the folder that holds `Prey.exe`,
which is not the same folder on both editions:

| Edition | Install root | Folder holding `Prey.exe` |
|---------|--------------|---------------------------|
| Steam | `...\steamapps\common\Prey` | `Binaries\Danielle\x64\Release\` |
| Xbox Game Pass | `...\XboxGames\Prey\Content` | `Binaries\Danielle\Gaming.Desktop.x64\Release\` |

`install.cmd` finds both and picks the right folder for whichever copy it is
installing into. If you own the game on both, run it once per copy and pass the
path you want each time.

### Manual Installation

Use the Nexus ZIP, which contains the deploy subtree only and no loader.

1. Install the [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader): put its DLL in the folder from the table above, renamed to `dinput8.dll`. The installer ZIP carries a copy under `vendor\ultimate-asi-loader\`.
2. Copy `PreyHeadTracking.asi` into the same folder, next to `Prey.exe`.
3. Start the game once. The mod creates `CameraUnlock.ini` in that folder, and that is the file to edit (see [Configuration](#configuration)).

## Setting Up OpenTrack

The mod listens for OpenTrack pose data on UDP port `4242`, on every network
interface. One datagram is six little-endian 64-bit floats in the order
`x, y, z, yaw, pitch, roll`: position in centimetres, rotation in degrees, 48
bytes in total. Anything that sends that to that port drives the view.
OpenTrack's **UDP over network** output sends exactly this, and the steps below
set it up.

1. Install [OpenTrack](https://github.com/opentrack/opentrack/releases).
2. Pick a tracker under **Input**, using the notes below.
3. Set **Output** to **UDP over network**, host `127.0.0.1`, port `4242`.
4. Press **Start**. Tracking and the game can start in either order.

### Webcam

OpenTrack ships a `neuralnet tracker` input that reads a plain webcam. Select it
under **Input**, pick your camera in its settings, and use the output settings
above. How well it tracks depends on your camera and your lighting, so try it
before buying anything.

### Phone

A phone app can reach the mod directly, with no OpenTrack on the PC, if it sends
the datagram described above. Point it at this PC's IP address (run `ipconfig`
to find it) on port `4242`. Not every phone tracker speaks this protocol, so
check yours for an OpenTrack or UDP output option first. [Headcam](https://headcam.app)
sends it, and I wrote it so decent tracking is free for anyone who already owns
a phone.

Sending direct works when the app filters its own signal on the device. The
mod's smoothing is sized to take the edge off a clean signal rather than to
rescue a noisy one, so a raw feed sent direct will jitter. If it does, point the
app at OpenTrack's **UDP over network** *input* on some other port, say 5252,
and let OpenTrack's filters and curves clean it up before its output forwards to
`127.0.0.1:4242`.

Anything arriving from outside `127.0.0.0/8` counts as a remote connection and
is smoothed with `RemoteSmoothing` rather than `LocalSmoothing`. That includes a
tracker on this very PC that sends to the machine's own LAN address, because the
mod reads the source address and not the machine.

### Headset or other hardware

If your device has an OpenTrack input driver, select it under **Input** and use
the same output settings. OpenTrack's own **Input** list is the authority on
what it can read; the mod only ever sees what OpenTrack sends.

### Centring

Centring belongs to your tracker. The mod subtracts no centre of its own: it
applies the pose it receives exactly as it arrives, so a stream of zeros holds
the view where the game itself puts it. Press the centre control in your tracker
(OpenTrack's **Center** bind, or the CENTER button in Headcam) and the tracker
zeroes its own output, which leaves the view centred with the mod doing nothing.

That is why there is no centre hotkey here and nothing to re-centre in game. Two
centres in series would drift apart, because each side re-centres at moments the
other cannot see, and you would end up pressing twice to centre once. If the
view sits off to one side, centre it in the tracker.

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

The tracking mode, the yaw mode and the body key save their new state to `CameraUnlock.ini` as soon as they change, so the next start comes back the same way. `End` does not: tracking starts on or off as `EnableOnStartup` says. Every key here can be rebound in `CameraUnlock.ini` (see [Configuration](#configuration)).

`Delete` / `Ctrl+Shift+J` turns carrying your own body with your head on or off. It is on by default and applies only while you are in the space suit: the suit's collar and shoulders turn with your head instead of staying where your character is facing, so you look around INSIDE the suit rather than across it. Prey draws the item in your hands as part of the same object, so in the suit that follows your head too; on foot the mod leaves the body - and the gun - alone, so the barrel keeps pointing at the crosshair.

`Ctrl+Shift+U` is for when more than one app is sending to the port. The mod follows one of them and ignores the rest, and which one it picks is decided by whichever packet arrives first after the game starts. Press it until the view answers your head.

## Configuration

<!-- cameraunlock:config -->
The mod reads its settings from `CameraUnlock.ini` in the game folder, at one of these paths depending on the store the game came from:

- `Binaries\Danielle\x64\Release\CameraUnlock.ini`
- `Binaries\Danielle\Gaming.Desktop.x64\Release\CameraUnlock.ini`

It creates the file when it starts and finds none. Edit it with any text editor.

A setting set to `default` takes its value from `Defaults.ini`, which every head tracking mod that keeps its settings in `CameraUnlock.ini` reads. Head tracking mods that keep their settings in another file do not read it. Writing a value in place of `default` changes that setting for this game only. When the mod saves a setting that a hotkey changed in game, it writes the new value in place of `default`, so that setting no longer follows `Defaults.ini` in this game until you set it to `default` again.

`Defaults.ini` is `%AppData%\CameraUnlock\Defaults.ini` on Windows; `$XDG_CONFIG_HOME/CameraUnlock/Defaults.ini` on Linux, or `~/.config/CameraUnlock/Defaults.ini` where `XDG_CONFIG_HOME` is not set, under Wine and Proton too; and `~/Library/Application Support/CameraUnlock/Defaults.ini` on macOS. The mod's log, where it writes one, names the file it read.

When the mod starts and finds no `Defaults.ini`, it creates one holding the built-in values, unless Windows runs the game as a packaged app. The mod never changes `Defaults.ini` after that. Edit it with any text editor.

The built-in value of each setting set to `default` below:

- `UdpPort=4242`
- `EnableOnStartup=true`
- `WorldSpaceYaw=true`
- `RotationEnabled=true`
- `LocalSmoothing=0.0`
- `RemoteSmoothing=0.15`
- `PositionEnabled=true`
- `PositionLimitX=0.3`
- `PositionLimitY=0.2`
- `PositionLimitYDown=0.2`
- `PositionLimitZ=0.4`
- `PositionLimitZBack=0.1`
- `ToggleKey=End, Ctrl+Shift+Y`
- `CycleTrackingModeKey=PageUp, Ctrl+Shift+G`
- `YawModeKey=PageDown, Ctrl+Shift+H`
- `LightFollowsHead=true`
- `LightMultiplier=1.5`

With every setting at its default, the file reads:

```ini
; Prey head tracking settings.
; Comments start with ; and go on their own line. Text after a value is part of the value.
; Hotkeys are key names such as End, PageUp or Ctrl+Shift+Y. Separate several with commas; leave empty for none.
; A setting set to default takes its value from Defaults.ini, which every head tracking mod
; that keeps its settings in CameraUnlock.ini reads: %AppData%\CameraUnlock\Defaults.ini on
; Windows, $XDG_CONFIG_HOME/CameraUnlock/Defaults.ini (normally ~/.config/CameraUnlock) on
; Linux, under Wine and Proton too, and ~/Library/Application Support/CameraUnlock/Defaults.ini
; on macOS. The log names the file it read. Write a value instead of default to change that
; setting for this game only.

[CameraUnlock]
; Written by the mod. Leave this section in place.
ConfigFormat=1

[Network]
; UDP port the mod receives tracker data on (OpenTrack protocol).
UdpPort=default

[General]
; true: head tracking is on when the game starts. ToggleKey turns it on and off.
EnableOnStartup=default
; true locks head yaw to the world's up axis, so the horizon stays level
; at any pitch. false turns about the camera's own up axis. In the space
; suit yaw is always the camera's own, since floating has no stable up.
WorldSpaceYaw=default
; true: turning your head turns the view.
; Tracking mode at startup, with PositionEnabled. The mode hotkey changes both.
RotationEnabled=default

[Smoothing]
; Smoothing when the tracker runs on this PC. 0 is the least, 1 the most.
LocalSmoothing=default
; Smoothing when the tracker is another device on the network, such as a phone.
; 0 is the least, 1 the most.
RemoteSmoothing=default

[Position]
; true: moving your head moves the view.
; Tracking mode at startup, with RotationEnabled. The mode hotkey changes both.
PositionEnabled=default
; How far, in metres, leaning left or right can move the view.
PositionLimitX=default
; How far, in metres, raising your head can move the view.
PositionLimitY=default
; How far, in metres, lowering your head can move the view.
PositionLimitYDown=default
; How far, in metres, leaning forward can move the view.
PositionLimitZ=default
; How far, in metres, leaning back can move the view.
PositionLimitZBack=default

[Hotkeys]
; Turns head tracking on and off.
ToggleKey=default
; Changes the tracking mode: rotation and position, rotation only, position only.
CycleTrackingModeKey=default
; Switches yaw between the world's up axis and the camera's own (WorldSpaceYaw).
YawModeKey=default
; Steps to the next app sending to the tracker port, when one you are not
; using got there first and holds it.
CycleTrackerSourceKey=Ctrl+Shift+U
; Turns BodyFollowsHead on or off, and saves it.
BodyFollowsHeadKey=Delete, Ctrl+Shift+J

[Light]
; true: a light you carry points where you look instead of where you aim.
LightFollowsHead=default
; How far the light turns for each degree your head turns.
; 1 matches the view, 0 keeps the light on your aim.
LightMultiplier=default

[Camera]
; Project the world-anchored HUD markers (objective markers, interactable
; diamonds) through the head-tracked view so they stay on their objects.
CompensateMarkers=true
; Horizontal field of view in degrees. 0 leaves Prey's own Field of View
; slider in charge. 25 to 170 is written straight into the engine, past the
; slider's 120 limit, and holds while it is set.
FieldOfView=0.0
; Draw the gun in your hands through the same lens as the world, so the
; barrel and the crosshair agree when the head turns. false leaves Prey's
; own weapon field of view alone.
MatchWeaponFieldOfView=true
; Apply the head pose when the game sets the view camera, so culling, the
; HUD and the flashlight see it too. false only turns the rendered image.
EarlyInject=true
; Turn off Prey's software occlusion culling, which culls a head-turned view
; against the un-turned one so geometry vanishes at the edge of a turn.
DisableCoverageBuffer=true
; Build the first-person body from the game's own camera, so it does not
; swing across the screen at twice the head's rotation.
CompensateBody=true
; Carry the space suit's collar and shoulders with your head while the suit
; is on. BodyFollowsHeadKey turns it on or off in game and saves it.
BodyFollowsHead=true
; Diagnostic: log the view camera and the crosshair projection twice a
; second. Off for play.
DumpCamera=false
; Diagnostic: sample the render node the body is drawn from. Off for play.
TraceBodyNodes=false
; Diagnostic: draw nothing for the body's render node. Off for play.
HideBodyNodes=false
; Diagnostic: force the flashlight on regardless of save progress.
ForceFlashlight=false
; Diagnostic: log each distinct dynamic light once. Off for play.
TraceLights=false
; Diagnostic: report every instruction that reads the flashlight's
; intensity. Off for play.
TraceLightReader=false
; Diagnostic: log every distinct caller of the engine's view-camera getter
; once, as an address. Off for play.
TraceCameraReaders=false
; Diagnostic: the address of one view-camera reader to hand the clean
; camera to. 0x0 hands it to none.
; CleanCameraForReader=0x0
; Diagnostic: the upper end of a range of reader addresses, for bisecting.
; 0x0 matches the one address above.
; CleanCameraReaderEnd=0x0

[Logging]
; Write HeadTracking.log next to Prey.exe. It starts empty at every launch,
; and the previous session is kept as HeadTracking.prev.log.
LogToFile=true
; The log file, relative to the folder Prey.exe is in, or a full path.
LogPath=HeadTracking.log
```
<!-- /cameraunlock:config -->

There is deliberately no sensitivity, deadzone or axis-inversion setting. Shape
the pose in your tracker app instead, so one profile behaves the same in every
game.

## Troubleshooting

**Mod not loading**

- Check for `HeadTracking.log` next to `Prey.exe`. No log file at all means the ASI loader never ran: confirm `dinput8.dll` and `PreyHeadTracking.asi` are both in the folder listed for your edition under [Where the files go](#where-the-files-go). On Xbox Game Pass that folder is `Gaming.Desktop.x64`, not `x64`.
- A log line saying the mod is staying dormant means your `PreyDll.dll` is not one of the builds this mod knows. The line says whether your game is newer or older than the newest build profile, and the working line names the profile that did match. Newer means the mod needs a new profile: open an issue with the fingerprint from that log line, and say which edition you are running.
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

- The mod has no inversion settings. If an axis moves the wrong way, invert it in your tracker app.
- If the view sits off to one side, center it in your tracker app.
- If yaw feels wrong when you look far up or down, toggle the yaw mode with `Page Down`.

## Updating

Download the new release and run `install.cmd` again. Your config is preserved.

## Uninstalling

Run `uninstall.cmd`. This removes the mod DLLs and leaves `CameraUnlock.ini` and any `HeadTracking.ini` in place. The Ultimate ASI Loader is only removed if the installer put it there. Use `uninstall.cmd /force` to remove it anyway.

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

- Discord: [Loop's Head Tracking Hangout](https://discord.com/invite/dxyZdyFNT9) - setup help, bug reports, and new-release announcements
- [Lopari](https://lopari.app) - free Windows launcher with one-click install and launch for the released head-tracking mods
- [Headcam](https://headcam.app) - free app that turns your iPhone or Android phone into the head tracker

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
