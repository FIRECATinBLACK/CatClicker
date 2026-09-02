# CatClicker

<p align="center"><img src="assets/branding/source/icon.png" width="160" alt="CatClicker logo"></p>

CatClicker is a native autoclicker and macro recorder built for Pop!_OS and the COSMIC desktop on Wayland.

## Status and platform support

CatClicker is pre-release software. Its primary and currently supported environment is Pop!_OS 24.04 with the COSMIC desktop in a native Wayland session. Other Wayland compositors are not supported yet. Contributions for trustworthy absolute cursor backends on other compositors are welcome. X11 is not a current target.

The current COSMIC implementation has been host tested for keyboard and mouse recording, anchored clicks and scroll, drags, long holds, repeated sessions, global hotkeys, save and load, looping, and optional smooth playback.

## Features

- Passive physical input capture through evdev while recording
- Exact mouse button and scroll anchors from COSMIC cursor metadata
- Keyboard and pointer playback through Linux uinput
- Versioned local `.catmacro` files
- Global record, play, and stop shortcuts
- Looping and selectable playback speed
- Optional cosmetic smoothing between recorded mouse positions
- Light and dark CatClicker themes

## Important limitations

- COSMIC Wayland is the only supported compositor environment today.
- Input capture and uinput need explicit device permissions.
- A macro replays input into whichever application receives it. Review macros from other people before playback.
- CatClicker never derives absolute coordinates by adding relative mouse deltas. A click or scroll without a trusted absolute position is dropped.

## Build requirements

On Pop!_OS 24.04:

```bash
sudo apt update
sudo apt install build-essential cmake ninja-build pkg-config \
  qt6-base-dev qt6-declarative-dev libpipewire-0.3-dev libspa-0.2-dev \
  libei-dev libevdev-dev libwayland-dev wayland-protocols
```

CatClicker needs Qt 6.4 or newer. Direct COSMIC cursor support also needs `wayland-scanner` and the ext image capture protocol XML files described in [Development](docs/DEVELOPMENT.md).

## Build, install, and run

```bash
cmake -S . -B build -G Ninja
cmake --build build -j4
ctest --test-dir build --output-on-failure
./build/bin/CatClicker
cmake --install build --prefix "$HOME/.local"
```

After installation, launch `CatClicker` from the desktop menu or run `$HOME/.local/bin/CatClicker`.

## Input permissions

Do not run the GUI as root and do not make input devices globally writable. Run `./scripts/setup-input-permissions.sh` once. The helper installs udev rules using `uaccess` for `/dev/uinput` and eligible physical input devices. CatClicker can request the helper through Polkit or show a manual command. It never collects a password and does not prompt when access is already correct.

## Record and play

1. Choose Record or use the record shortcut.
2. Perform the actions to capture, then choose Stop.
3. Save the macro if wanted, then choose Play.
4. Stop playback at any time with the stop shortcut.

Mouse actions whose positions cannot be trusted are omitted. Smooth playback adds cosmetic intermediate positions only. It does not change the macro file, and saved click and scroll anchors remain exact.

## Privacy and safety

CatClicker has no telemetry, analytics, update checker, or network client. Recordings remain local unless you share them. The GitHub button only asks your desktop browser to open the fixed project URL after you click it. See [Privacy](docs/PRIVACY.md) and [Macro format](docs/MACRO_FORMAT.md).

Macro text is parsed as data and is never passed to a shell. Replayed keyboard input can still be interpreted by the target application. Text deliberately replayed into a terminal may be executed by that terminal. CatClicker itself does not execute macro text.

## Project documentation

- [Architecture](docs/ARCHITECTURE.md)
- [COSMIC Wayland cursor design](docs/COSMIC_WAYLAND.md)
- [Development](docs/DEVELOPMENT.md)
- [Debugging](docs/DEBUGGING.md)
- [Performance](docs/PERFORMANCE.md)
- [Contributing](CONTRIBUTING.md)
- [Security policy](SECURITY.md)

Support for additional compositor backends, tests, documentation fixes, and focused code improvements is welcome.

## AI assistance

CatClicker has been developed with substantial assistance from AI coding tools. Changes are reviewed, tested, and validated on the target environment before they are accepted.

## License

CatClicker source code is licensed under GPL-3.0-or-later. See [LICENSE](LICENSE).
