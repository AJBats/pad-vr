# padVR

A SteamVR driver that exposes a conventional XInput gamepad as a synthesized
Index-style controller, so users without working motion controllers can still
navigate the SteamVR dashboard and play gamepad-friendly VR games.

Replaces SteamVR's built-in `driver_gamepad`.

## Build

Requires Windows, MSVC with C++17, CMake ≥ 3.20.

```sh
git clone https://github.com/AJBats/pad-vr padVR
cd padVR
git clone https://github.com/ValveSoftware/openvr third_party/openvr
cmake -S . -B build -A x64
cmake --build build --config Release
```

Outputs:
- `driver/padvr/bin/win64/driver_padvr.dll`
- `build/companion/Release/padvr_companion.exe`

## Install

Register the driver with SteamVR:

```sh
"C:/Program Files (x86)/Steam/steamapps/common/SteamVR/bin/win64/vrpathreg.exe" \
    adddriver "<absolute-path-to>/padVR/driver/padvr"
```

Disable SteamVR's built-in gamepad driver so it can't fight padVR. In
`C:\Program Files (x86)\Steam\config\steamvr.vrsettings`, find:

```json
"driver_gamepad" : { "enable" : true }
```

and change `true` to `false`.

Restart SteamVR, then run `build/companion/Release/padvr_companion.exe`.
Leave it running whenever you want padVR active - it stays fully passive
when SteamVR isn't running, so it's safe to leave on 24/7.

The companion is designed to run **silently** — no console window, no tray
icon, no UI at all. It just sits as a background process. To verify it's
running or to stop it, use **Task Manager** → find `padvr_companion.exe`
in the Details tab → End Task. Launching the .exe twice is a no-op (the
second instance detects the first via a named mutex and exits cleanly).

### Optional: auto-start the companion on login

1. Press `Win+R`, type `shell:startup`, hit Enter - opens
   `%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup`.
2. Right-click in the folder → **New** → **Shortcut** → browse to your
   `build\companion\Release\padvr_companion.exe` → finish.

To disable, just delete the shortcut.

### ⚠️ UEVR users - required per-game config change

If you play any game through [UEVR](https://github.com/praydog/UEVR) (or
UEVRDeluxe), you **must** disable UEVR's VR-controller mode for each game,
or padVR's synth controller will hijack input and the game will stop
responding to the gamepad after a button or two. UEVR auto-switches into
motion-controller mode the moment it sees a VR controller in SteamVR, and
padVR's synth controller looks like one.

For each UEVR game, edit `%APPDATA%\UnrealVRMod\<GameName>\config.txt` and
set:

```
VR_ControllersAllowed=false
```

(The line will already exist as `VR_ControllersAllowed=true` - just flip
the value.) Re-inject UEVR / restart the game. The gamepad will now drive
the game directly via UEVR's normal pass-through, and padVR continues to
handle SteamVR dashboard navigation.

## Known issues

- **padVR will intercept the Guide button.** If you leave the companion app
  running in the background, it will prevent the guide button from interacting 
  with Big Picture Mode.
- **Real motion controllers won't work alongside an active XInput pad.**
  While the companion sees a powered-on gamepad, padVR's synth controller
  is live and steals the right-hand role. Fully power down your gamepad
  (not just put it to sleep) to let real Sense / Touch / Index controllers
  take over.

## Uninstall

```sh
"C:/Program Files (x86)/Steam/steamapps/common/SteamVR/bin/win64/vrpathreg.exe" \
    removedriver "<absolute-path-to>/padVR/driver/padvr"
```

Re-enable `driver_gamepad` in `steamvr.vrsettings` if you want SteamVR's
default gamepad behavior back. Restart SteamVR.

## License

[Unlicense](LICENSE) - public domain.
