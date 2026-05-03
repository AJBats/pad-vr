# padVR

A SteamVR driver that emulates a single Index-style controller driven by a
conventional gamepad (XInput). Built so an Xbox / PSVR2 user without working
motion controllers can still navigate the SteamVR dashboard and play
gamepad-friendly VR games.

Replaces SteamVR's built-in `driver_gamepad` (which is buggy, especially
around quitting games).

## What it does

| Gamepad input            | Synth controller output                    | Effect                            |
| ------------------------ | ------------------------------------------ | --------------------------------- |
| Pad plugged in           | `deviceIsConnected = true`                 | Synth controller appears          |
| Pad unplugged            | `deviceIsConnected = false`                | Synth controller disappears       |
| LT or RT (max of)        | `/input/trigger/value` + `/input/trigger/click` | Click UI elements           |
| Left thumbstick          | `/input/thumbstick/{x,y}` + `/input/joystick/{x,y}` | Menu navigation, scrolling |
| Left thumbstick press    | `/input/thumbstick/click`                  | (free for app to bind)            |
| Guide / Xbox button tap  | `vrmonitor://...system_dashboard_toggle`   | Open / close SteamVR dashboard    |
| Guide button held ≥500ms | `vrmonitor://...seated_..._reset`          | Recenter view                     |

Pose: synth controller is positioned 0.5 m below the HMD in world space and
oriented to match HMD rotation with a +27° local pitch-up. Visually the laser
appears to emerge from the user's chest at an upward angle, landing roughly
center-of-panel on a typical SteamVR dashboard distance. Tunable via two
constants near the top of [`src/PadVRController.cpp`](src/PadVRController.cpp).

## How it works

```
┌──────────────────────────┐         ┌────────────────────────────┐
│  padvr_companion.exe     │ shared  │  driver_padvr.dll          │
│  (user-mode process)     │ memory  │  (loaded inside vrserver)  │
│                          │ ◀──────▶│                            │
│  - polls XInput @ 120Hz  │  named  │  - registers a fake        │
│  - reads triggers, stick │ mapping │    Knuckles-style          │
│  - reads Guide button    │  v4     │    controller w/ SteamVR   │
│  - state machine for     │         │  - mirrors HMD pose +      │
│    tap-vs-hold dispatch  │         │    chest-mount offset      │
│  - shells out to         │         │  - reads IPC each frame    │
│    vrmonitor:// URIs     │         │    and pushes input        │
└──────────────────────────┘         └────────────────────────────┘
```

Two processes by design — XInput is messy to poll from inside `vrserver.exe`
(no message pump, async nature), and `vrmonitor://` URI dispatch wants a
normal user-mode context. Lock-free shared memory keeps them decoupled and
either side can be restarted without dragging the other down.

The synth controller registers as `controller_type = "knuckles"` so SteamVR's
existing Index controller bindings (`vrcompositor_bindings_knuckles.json`)
apply automatically — no per-controller binding files to ship.

## Layout

```
padVR/
├── CMakeLists.txt
├── cmake/
├── src/                         # driver_padvr.dll
│   ├── DriverFactory.cpp        # exports HmdDriverFactory
│   ├── PadVRProvider.{h,cpp}    # SteamVR driver provider
│   ├── PadVRController.{h,cpp}  # ITrackedDeviceServerDriver
│   ├── TriggerIPC.{h,cpp}       # shared-memory channel
│   └── MathUtil.h               # matrix → quat helper
├── companion/                   # padvr_companion.exe
│   └── main.cpp                 # XInput poller + URI dispatcher
├── driver/padvr/                # SteamVR driver install layout
│   ├── driver.vrdrivermanifest
│   ├── bin/win64/               # build output goes here
│   └── resources/input/         # input profile + bindings
└── third_party/openvr/          # cloned separately, not in repo
```

## Build

Requires Windows, Visual Studio 2022 (or any MSVC with C++17), CMake ≥ 3.20.

```sh
git clone https://github.com/<you>/padVR
cd padVR
git clone https://github.com/ValveSoftware/openvr third_party/openvr
cmake -S . -B build -A x64
cmake --build build --config Release
```

Outputs:
- `driver/padvr/bin/win64/driver_padvr.dll`
- `build/companion/Release/padvr_companion.exe`

## Install

Register the driver folder with SteamVR:

```sh
"C:/Program Files (x86)/Steam/steamapps/common/SteamVR/bin/win64/vrpathreg.exe" \
    adddriver "<absolute-path-to>/padVR/driver/padvr"
```

Disable SteamVR's built-in (buggy) gamepad driver so it can't fight padVR.
Edit `C:\Program Files (x86)\Steam\config\steamvr.vrsettings` and find:

```json
"driver_gamepad" : {
   "enable" : true
}
```

Change `true` to `false`. Restart SteamVR.

## Run

1. Launch SteamVR.
2. Run the companion: `build/companion/Release/padvr_companion.exe`.
   Leave it running. Console will show XInput connection state and any
   Guide-button events.
3. Plug in your gamepad. The synth controller appears in SteamVR's device
   list. Unplug and it disappears.

Verify by tailing `C:\Program Files (x86)\Steam\logs\vrserver.txt` —
look for `[padVR]` lines.

## Tunables

Source-level constants you might want to adjust:

| Constant                 | File                              | Default | What it controls                                                          |
| ------------------------ | --------------------------------- | ------- | ------------------------------------------------------------------------- |
| `kDownOffsetM`           | `src/PadVRController.cpp`         | `0.50`  | How far below the HMD (in metres) the controller "lives"                  |
| `kPitchUpDeg`            | `src/PadVRController.cpp`         | `27.0`  | Local pitch-up angle of the laser                                         |
| `kHoldThresholdMs`       | `companion/main.cpp`              | `500`   | Guide-button hold duration before recenter fires                          |
| `kClickThreshold`        | `companion/main.cpp`              | `200`   | XInput trigger value (0–255) at which `/input/trigger/click` flips        |
| `XINPUT_GAMEPAD_GUIDE`   | `companion/main.cpp`              | `0x400` | Swap with `XINPUT_GAMEPAD_START` if Steam's overlay swallows Guide presses |

## Known caveats

- **Guide button race with Steam.** Our companion polling drains Guide events
  before Steam's Big Picture launcher can claim them — but only while the
  companion is running. With the companion stopped, pressing Guide on the
  desktop opens Big Picture as normal. (You may consider this a feature.)
- **`driver_gamepad` must be disabled.** Otherwise SteamVR's built-in
  gamepad-as-VR-controller driver synthesizes a competing fake controller
  and the two fight.
- **Single hand only.** padVR registers one right-hand controller. Two-hand
  games where the missing left hand matters won't work.
- **No haptics, no render model.** Not implemented — controller is invisible
  in-headset (only the laser shows). Render model could be added under
  `driver/padvr/resources/rendermodels/`.

## Uninstall

```sh
"C:/Program Files (x86)/Steam/steamapps/common/SteamVR/bin/win64/vrpathreg.exe" \
    removedriver "<absolute-path-to>/padVR/driver/padvr"
```

Re-enable `driver_gamepad` in `steamvr.vrsettings` if you want SteamVR's
default behavior back. Restart SteamVR.

## License

MIT.
