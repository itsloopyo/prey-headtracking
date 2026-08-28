# Changelog

All notable changes to this project will be documented in this file.

## [Unreleased]

### Added

- Your own first-person body can now be carried with your head, on `Delete` or
  `Ctrl+Shift+J` (`[Camera] BodyFollowsHead`). The exterior sections put you in a
  space suit whose collar and shoulders fill the bottom of the screen, and they
  stayed facing where your character faces, so turning your head looked ACROSS
  the inside of the suit instead of turning inside it.

  Prey draws the suit as a camera-space render node and resolves it from two
  things: a Vec3 on the node, the body's offset from the eye in the camera's own
  axes, and the entity's world matrix, whose rotation the render keeps. Turning
  both by the head pose is a rigid rotation of the body about the head's own
  pivot, which is what a helmet does. Both halves are needed: with the offset
  pointing straight down and the head only yawing, turning the facing alone looks
  perfect right up until the first head pitch, where the body swings out of
  frame.

  The arms and the item in your hands are part of the same object, so they follow
  your head too. In the suit that reads correctly; on foot it would leave the
  barrel pointing away from the crosshair the shot goes to. So it applies only
  while the suit is on, which the mod reads off the body itself: its origin sits
  at chest height while you float in the suit (0.46m below the eye in the
  airlock, 0.69m outside) and at floor height on foot (1.00m crouched, 1.70m
  standing). `Delete` / `Ctrl+Shift+J` turns the whole thing off.

- The gun in your hands is now drawn through the same lens as the world, so its
  barrel points at the crosshair on a head turn instead of well past it. Prey
  renders held weapons and hands in a separate pass with its own field of view,
  `r_DrawNearFoV`, registered at 55 degrees vertical - the vertical equivalent of
  the default Field of View slider on a 16:9 display. Raise the slider and the
  world widens while the weapon keeps the narrow lens, which magnifies it by
  `tan(clFov/2) / tan(nearFov/2)`: 1.87x at the maximum slider on 16:9.

  Nothing shows that while the view and the gun turn together. Head tracking
  separates them - the gun stays on your body while the view turns - and the
  magnified gun then sweeps 1.87 times as far across the screen as the world
  behind it. The shot always landed on the crosshair; the gun just looked aimed
  somewhere else. `[Camera] MatchWeaponFieldOfView`, on by default, holds the
  weapon's field of view equal to the world's. At the default slider the two
  already match and it changes nothing; above it the gun is drawn smaller and
  with more perspective, which is what the barrel and the crosshair agreeing
  costs.

- `Ctrl+Shift+U` steps to the next app sending to the tracker port. The receiver
  follows one source and ignores the rest, and which one it follows is settled by
  whichever packet lands first after the game binds the port - a race measured in
  milliseconds, so starting your tracker first does not decide it. When a bridge
  left over from an earlier session wins, it streams a pose that never moves and
  never goes quiet, so nothing displaces it and the game looks like it has no
  head tracking at all. There was no way out of that from inside the game.

- The crosshair now holds its spot when you lean, crouch or rise with a weapon
  out. It used to be placed for the aim DIRECTION, which is exact while the head
  is only rotating - the frame is drawn from the eye the shot leaves. A lean
  moves the render eye up to 40cm off that one, so the point the round lands on
  stops being straight down the aim and slides out from under a crosshair that
  has not moved. The reported shape was the crosshair drifting one way while the
  round kept landing where it had been.

  Fixing it needs the distance to what you are pointing at, since the correction
  is lean/distance. The mod now casts the aim ray through CryEngine's own
  physics - `gEnv->pPhysicalWorld` at RVA 0x224D9C8, `RayWorldIntersection` at
  vtable slot +0x118, with the object filter and ray flags copied from the
  engine's own call at RVA 0x301932 - and projects the impact point through the
  head-tracked view. The cast happens on the frame that consumes it, with no
  smoothing and no rate limit: a smoothed or stale depth is what makes a
  crosshair agree at one range and drift at every other. A ray that hits nothing
  falls back to the aim direction, which is the right answer for a target at
  infinity, and rotation-only tracking never casts at all.

  Measured in game against actual bullet holes: at 4.7m the round lands on the
  crosshair leaning either way, and the hole stays under the crosshair across a
  0.6m swing between opposite leans. Across 3.2m to 18.6m the crosshair's offset
  times the distance is constant to three figures, which is the 1/d law the
  parallax has to follow.

- Added a flashlight beam that turns with your head, further than the view does,
  so it lands on what you turned to look at rather than short of it.
  `[Camera] FlashlightScale` sets how much further (1.5 by default; 1.0 matches
  the view exactly), and `[Camera] CompensateFlashlight` turns it off.

  Two things had to be right. The beam is one of several lights riding the
  player, and the setter that hands them over only fires when a light's intensity
  CHANGES - during a level load, when there is no view camera yet to ask "is this
  light on my head". So every ArkLight is now recorded by class (its vtable) and
  the head-mounted ones are picked out later, once there is a camera to measure
  against. On a lit save that finds two, which is what the flashlight is.

  The second is why a whole day of writing the beam's rotation did nothing: Prey
  only re-syncs an entity's render node when the transform arrives through
  `IEntity::SetPosRotScale` (RVA 0x90AF90, vtable slot 39), which raises the dirty
  bits the sync keys on - 0x2 for position, 0x4 for rotation. A quaternion poked
  into `entity+0x44` behind its back reads back perfectly, survives the whole
  render untouched, and is rendered from stale state. Holding a 90 degree
  rotation on it for six seconds moved the beam by 0.03 degrees. Going through
  the engine's own setter moves it.

- Added head-following for world-anchored HUD markers, so they follow the head
  instead of sitting still on screen while the world slides under them.
  `[Camera] CompensateMarkers` turns it off.
- Added a clean-camera handback around Prey's HUD projection update, so the
  interaction ray keeps following the mouse while the rest of the frame is
  head-tracked. That update is not a HUD-only consumer: it also calls RVA
  0x1585320, which reads the view camera and the reticle offset at +0x17EC to
  resolve what the player is looking at. Anything that injects outside the render
  therefore aims the interaction ray with the head - look away from a prompt and
  it drops. That one call now gets the clean camera handed back for its duration,
  so markers project through the turned view while what the player can reach
  stays on the mouse.
- Added crosshair compensation, so the crosshair follows your aim into the
  head-tracked view. Shots go where the mouse points, so with the head turned the
  middle of the screen stopped being where they land and the crosshair stopped
  meaning anything. Prey draws its reticle at a normalized screen position it
  hands to Flash - and moves it itself every frame in controller free-aim - so
  the mod now hands it the position the aim direction projects to in the turned
  view. Prey's interaction prompt is drawn at the reticle and follows it.
  Measured in game against the world it points at: the reticle stays on its
  target to within a pixel across yaw, pitch, roll and combined pitch+roll.
  `[Camera] CompensateReticle` turns it off.
- Added suspension of head tracking outside gameplay: the main menu, the pause
  menu, the TranScribe, the inventory, and any time Prey is not the focused
  window. Detected from Prey releasing the mouse cursor, so it needs no
  per-build addresses.
- Added recognition of the Prey: Mooncrash / Typhon Hunter build of
  `PreyDll.dll`, which the mod refuses to engage on, so Typhon Hunter
  multiplayer renders vanilla. A second check suppresses head tracking whenever
  the engine reports a multiplayer session (`gEnv->bMultiplayer`); it is defence
  in depth, and because the mod never loads into the multiplayer build it has not
  been exercised in a match.
- Added `pixi run check-fingerprint`, which prints a paste-ready build profile
  stub for a `PreyDll.dll` on disk and recognises the Mooncrash binary rather
  than offering to profile it.
- Added range clamping for config values, so one outside its documented range no
  longer reaches the engine; a non-finite sensitivity used to turn the whole view
  matrix into NaN. The log names anything that moved.
- Added a daily patch-watch workflow that opens an issue when Steam ships a new
  Prey build, so a profile can be prepared before players hit the dormant path.

### Changed

- Renamed the log file to `HeadTracking.log`, matching the INI it sits next to.
  The previous session is still kept as `HeadTracking.prev.log`. An existing
  `[Logging] LogPath` in your INI still wins, so update it if you want the new
  name; the uninstaller removes both names.
- Changed centring to be the tracker's job: the mod keeps no centre of its own
  and has no recentre control. Centre it in your tracker app (opentrack's Center
  bind, the CENTER button in Headcam, SteamVR's reset) and the mod applies the
  pose it receives as absolute. A second centre inside the mod could only drift
  out of step with the tracker's. The `Home` key, the `Ctrl+Shift+T` chord and
  the `[Hotkeys] RecenterKey` INI entry are gone.
- Changed smoothing to two keys: `[Tracking] LocalSmoothing` (default 0.0) and
  `[Tracking] RemoteSmoothing` (default 0.15), selected per connection from the
  tracker's source address. Both cover rotation and position; the old
  `[Tracking] Smoothing` and `[Position] Smoothing` keys are removed. The hidden
  0.15 baseline floor is gone, so a tracker on this machine now gets the lightest
  setting by default. Frame interpolation still runs at 0.0 - that is what keeps
  motion smooth on a high-refresh display - so it is the lightest setting rather
  than no filtering at all.

### Fixed

- Fixed the player's own first-person body swinging with the head. Prey builds
  the body's render object from the view camera, so it was welded to the view and
  rotated with it on every axis, sweeping across the screen - worst with the
  mouse pitched down, where the body already lies across the view. The mod now
  hands that one call site a snapshot of the camera taken before the pose went
  in, so the body stays on the body. It is still drawn: look down with your head
  level and it is exactly as the game intends.

  The camera was never the problem. At the worst case the mod's offset was
  0.117m and the game's own camera did not move at all. `TraceCameraReaders` and
  `CleanCameraForReader` are kept as diagnostics because they are what found the
  call site, and are how to re-derive it when a patch moves it.

- Fixed geometry being culled out of the head-tracked view when the head turned
  off aim. Confining the pose to CSystem::Render was too late: something
  snapshots the camera for culling before the render, and rebuilding the cached
  frustum on the engine's own camera did nothing for it because that is not the
  camera being read. The pose now goes in at SetViewCamera, so every consumer in
  the frame sees it. Verified in game.

  Two things were tried first and did NOT fix it, so do not spend the time again:
  rebuilding CCamera's cached frustum through UpdateFrustum (RVA 0x121D70), and
  turning off the software coverage buffer. The frustum rebuild is kept because
  the engine culls against the cache rather than the matrix and writing one
  without the other is wrong on its own terms. `e_CoverageBuffer` is at CVars
  +0x314 and the mod can set it directly; note that Prey does NOT read user.cfg,
  which invalidated an earlier attempt to test this through a config file.

  No widening knob: a frustum grows in tangent, so widening is both enormous and
  insufficient on a super ultrawide, which is how it went in
  titanfall-2-headtracking.

- Fixed head yaw, head roll and sideways lean all going the wrong way. The
  tracker protocol never states which direction its positives mean, so the
  mapping to CryEngine cannot be derived and has to be settled in game; all three
  came back mirrored, which is what most of the fleet reports. They are negated
  once at the engine boundary rather than through the INI's inversion switches,
  so the directional position limits still apply to the axis they were meant for.

- Fixed a crash on level load with a tracker that is not centred, seen as an
  access violation on a JobSystem worker reading `0xFFFFFFFFFFFFFFFF`. Prey
  treats a view camera whose translation is within 0.05m of the origin as one it
  has not positioned yet, and skips drawing the world through it. The positional
  offset is up to 0.40m, so adding it to a camera parked at the origin during a
  load made an unset camera look set; the engine rendered the world through it
  and its async visibility jobs walked an octree that was not built. The mod now
  leaves a camera inside that epsilon completely alone.
- Fixed a freeze on startup with reticle compensation on. The crosshair was being
  placed before the game was in a level, which meant calling into a Flash HUD
  that does not exist yet. Same fix: nothing is touched until the engine has a
  real camera.
- Fixed `PreyHeadTracking.log` being opened in append mode, so it grew across
  every session the mod had ever run and buried the startup chain a user is asked
  to read. It now starts fresh each launch, keeping the previous session as
  `PreyHeadTracking.prev.log` so a crash report survives the relaunch.

- Fixed look and aim so they are decoupled for real. The head pose was written
  into the engine's view camera and left there, and the engine hands that same
  camera to the code that decides what the player is looking at, so turning your
  head moved Prey's interaction focus onto whatever your head pointed at. The
  pose is now injected inside `CSystem::Render` only and taken back out before
  the call returns, so every gameplay reader stays on the mouse-controlled view.
- Fixed head roll going the wrong way: tilting your head right rolled the view
  left.
- Fixed leaning going the wrong way on the forward/back axis. The pipeline's z
  runs out the back of the head, so a forward lean is negative z; it was being
  added along the camera's forward axis without the flip, which pushed the view
  backwards when you leaned in.
- Fixed losing the tracker for more than half a second swinging the view to
  centre and back again when it returned. It now holds the last pose, as it
  should.
- Fixed leaning using the camera's own axes, so leaning in while looking down
  drove the view into the floor. Lean is now horizon-locked: forward is forward
  through the room whatever your eyes are pointed at.
- Fixed `install.cmd` overwriting `HeadTracking.ini` on every re-install and
  `uninstall.cmd` deleting it, so updating the mod silently reset tuned settings.
  Neither script touches the file now.
- Fixed a single-digit hotkey binding such as `PositionKey = 5` binding a mouse
  button instead of the 5 key.
- Fixed the log landing in the game's working directory instead of next to
  `Prey.exe`, where the documentation always said it was.
- Fixed warnings about retired config keys reaching only a debugger, because the
  config is read before the log file is open.
- Fixed an exception escaping the per-frame tick being swallowed without a word;
  it is now reported once.
- Fixed the engine's view camera not being restored when the renderer unwinds, so
  a fault inside the engine cannot leave the head rotation in the camera the next
  game update reads.
- Fixed quitting the game joining the mod's own threads from inside the loader
  lock.
- Fixed `install.cmd` overwriting an edited `HeadTracking.ini` with the shipped
  defaults every time it ran. The file was listed alongside the .asi, which put
  it through the same unconditional copy, so re-running the installer to update
  the mod reset every key. It is now written only when it is absent, which is
  what `launcher-manifest.json` already did through its `loader.seed` block, so
  the installer and the launcher deliver it the same way.

### Removed

- Removed `[Camera] SetViewCameraRva` and `[Camera] MatrixOffset`. Both were
  per-build values that belong in the build profile, and a wrong `MatrixOffset`
  wrote into live engine memory. The mod now warns if it finds either key in your
  INI.

## [0.0.0] - 2026-06-29

### Added

- C++ ASI mod for Prey (2017) built on the CameraUnlock shared core: OpenTrack
  UDP receiver, interpolation, processing pipeline, hotkeys, and D3D11 present
  hook.
- `CryEngineCamera` mod that hooks `CSystem::SetViewCamera` and injects the head
  pose (rotation + optional 6DOF position) into the rendered view only, leaving
  the game's player view decoupled. PE-fingerprint gated with a `HeadTracking.ini`
  RVA override; dormant until a build profile or override is provided.
- Ultimate ASI Loader vendored as `dinput8.dll` (loaded as a static import of
  `PreyDll.dll`).
- install/uninstall scripts, launcher manifest, build/release CI.
