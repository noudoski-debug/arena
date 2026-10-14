# EncoreDLC — UI update for dump 1.14.11949

## What changed in this package

- Project directory and visible product branding changed to `EncoreDLC`.
- Watermark redesigned as a compact glass-style badge with blue glow, status dot, outline and `EncoreDLC / PREMIUM` label. Tapping it still toggles the menu.
- Added eased opacity transition for menu opening and closing, plus a small entrance offset.
- Existing graphite/blue theme retained, with rounded controls and responsive phone layout.
- Offset validation helper retained: `python3 tools/verify_dump_offsets.py`.
- Root launcher path now defaults to `/data/local/tmp/EncoreDLC/cheat`.

## Build

```sh
NDK_PATH=/path/to/android-ndk ./build.sh
```

Or, on a device with the NDK configured, run `./build.sh` from this directory.

## Root launch

Copy the freshly built `build/libs/arm64-v8a/cheat` to `/data/local/tmp/EncoreDLC/cheat`, then run:

```sh
su -c 'sh ./run-root.sh'
```

Alternatively, pass a custom binary path as the first argument to `run-root.sh`.

## Validation status

- `run-root.sh` and `build.sh`: shell syntax checked.
- Offset verifier: passes for the listed `PlayerManager` offsets against the included dump.
- This environment does not have Android NDK, so the native project has not been rebuilt here. The included binary is a pre-existing build and does not include these source changes.
- The requested Silent Aim, faster gameplay aimbot logic, and game-memory FreeCam were not added. UI-only changes do not prove that the existing gameplay features work on-device.

## About the supplied Litvin Tap IMGUI sample

The existing project uses its own `Vars` and widget bindings. The visual cues from the sample (compact dark surfaces, rounded panels, icon-led layout and strong accent) are reflected in the current UI, but the sample's standalone `Menu::cfg` implementation was not dropped in wholesale because it would not bind to this project's settings automatically.
