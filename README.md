# CatClicker

CatClicker is a native Linux Wayland macro recorder/player focused on accurate keyboard + mouse automation instead of X11 compatibility shims. The target stack is C++20, Qt 6.4+, Qt Quick/QML, QtDBus, PipeWire/SPA, libei, libevdev, and direct Linux `uinput`.

This repository currently contains a COSMIC-focused playback milestone:

- a runnable Qt Quick application shell
- explicit Wayland/portal/uinput diagnostics
- a versioned `.catmacro` JSON format
- settings persistence and shortcut configuration UI
- a real runtime-selected playback abstraction
- a real `/dev/uinput` absolute-pointer + keyboard playback backend
- a threaded playback scheduler with anchor enforcement and emergency release
- core macro domain classes, scheduler math, and unit tests

Current backend policy:

- prefer `RemoteDesktop + libei` when it is truly usable
- fall back to `/dev/uinput` on current COSMIC where the portal path is not reliable
- keep `ScreenCast + PipeWire` and evdev architecture in place for the next recording milestone where the compositor actually advertises ScreenCast cursor `Metadata` mode

## Building

On Pop!_OS / Ubuntu-family systems, install the development dependencies first:

```bash
sudo apt update
sudo apt install \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  qt6-base-dev \
  qt6-declarative-dev \
  libpipewire-0.3-dev \
  libspa-0.2-dev \
  libei-dev \
  libevdev-dev
```

Then configure and build:

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

## Running

Launch the application from the build directory:

```bash
./build/bin/CatClicker
```

The GUI is expected to run on Wayland. On this sandbox host, the live session variables indicate `XDG_SESSION_TYPE=wayland` and `XDG_CURRENT_DESKTOP=COSMIC`.

## Input Permissions

CatClicker is designed to read physical keyboard/mouse input only while recording, and to inject playback events through `/dev/uinput` without running the GUI as root.

Run the setup helper once:

```bash
./scripts/setup-input-permissions.sh
```

The setup script now installs `TAG+="uaccess"` rules so systemd-logind grants ACLs to the active local session for:

- `/dev/uinput`
- physical keyboard event nodes
- physical mouse event nodes
- touchpad event nodes

If your host does not apply `uaccess` ACLs as expected, the README and diagnostics will make that visible; CatClicker itself should still not be run as root.

## Wayland Permissions

CatClicker uses or plans to use XDG Desktop Portal APIs for:

- `FileChooser` for native Wayland save/load dialogs instead of the Qt Quick fallback dialog path
- `RemoteDesktop` for preferred future keyboard/pointer injection when truly usable
- `ScreenCast` for cursor metadata from PipeWire/SPA on desktops that advertise cursor `Metadata` mode
- `GlobalShortcuts` for future system-wide Record / Play / Stop hotkeys on desktops that expose it

When these backends are implemented, the compositor or portal service will display permission dialogs to authorize screen selection, remote input, and shortcut registration.

## Architecture

- `src/macro/`: versioned macro data model, event timeline ordering, and scheduler timestamp scaling.
- `src/input/`: backend boundaries for passive evdev capture, PipeWire cursor metadata, portal session control, libei injection, and direct `uinput` playback.
- `src/persistence/`: `.catmacro` JSON serialization and user settings via XDG paths.
- `src/app/`: explicit application state machine and GUI-facing controller.
- `src/diagnostics/`: environment and backend capability reporting for debugging compositor-specific issues.
- `qml/`: the CatClicker Qt Quick interface and central visual theme.

Current playback flow on COSMIC:

1. Load or generate a `.catmacro`.
2. `ApplicationController` selects the best playback backend at runtime.
3. On current COSMIC, `UinputInputSender` creates persistent virtual keyboard and absolute-pointer devices.
4. `MacroPlayer` replays the unified event timeline on a worker thread using `std::chrono::steady_clock`.
5. Button-down anchor coordinates are enforced immediately before the click.
6. Stop, error, completion, and shutdown all trigger real key/button release events before internal held-state cleanup.

Planned recording flow for the next milestone:

1. If the compositor advertises ScreenCast cursor `Metadata` mode, `ScreenCast` provides the selected monitor stream.
2. PipeWire/SPA cursor metadata yields absolute logical cursor coordinates on those desktops.
3. Passive evdev capture records physical keys, mouse buttons, and wheel events.
4. evdev-based CatClicker shortcuts will be filtered so CatClicker does not record itself.

On current COSMIC, the reported ScreenCast cursor bitmask `3` means `Hidden + Embedded` only, not `Metadata`. For COSMIC-specific absolute cursor recording, the next investigation target is the newer image-copy-capture path, especially `ext_image_copy_capture_cursor_session_v1.position`, if the compositor advertises:

- `ext_image_copy_capture_manager_v1`
- `ext_output_image_capture_source_manager_v1`
- `ext_image_capture_source_v1`
- related COSMIC image-capture protocols such as `zcosmic_workspace_image_capture_source_manager_v1`

## Troubleshooting

Check the current session:

```bash
echo $XDG_SESSION_TYPE
echo $XDG_CURRENT_DESKTOP
echo $WAYLAND_DISPLAY
```

Check that the desktop portal is on the session bus:

```bash
busctl --user list | grep org.freedesktop.portal
```

Run the host probe script for a fuller report:

```bash
./scripts/probe-host.sh
```

If CatClicker reports missing capabilities:

- confirm you are in a native Wayland session, not X11
- confirm `xdg-desktop-portal` is running for your desktop
- confirm the development packages above were installed before building
- confirm `/dev/uinput` exists and the current user has an ACL granting access
- confirm the selected compositor exposes `ScreenCast`, and do not assume `RemoteDesktop` or `GlobalShortcuts` exist on COSMIC yet

## Current Status

As of September 1, 2026, the repository has a real `/dev/uinput` playback path for current COSMIC, but recording and global shortcut capture are still intentionally deferred. The next vertical slices are:

1. real `ScreenCast` + PipeWire cursor metadata capture
2. passive evdev capture without `EVIOCGRAB`
3. evdev-based CatClicker Record / Play / Stop shortcuts with self-device filtering
4. `RemoteDesktop.ConnectToEIS()` plus libei absolute pointer + keyboard injection where the portal is truly usable
