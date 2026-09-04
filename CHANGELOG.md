# Changelog

## Unreleased

- Clarified user-facing terminology to distinguish Wayland-native operation from libcosmic/COSMIC toolkit integration.

## 0.1.0 - 2026-09-03

- Record physical keyboard input, absolute mouse movement, anchored clicks, drags, holds, and scroll on COSMIC Wayland, then play macros through uinput.
- Use trusted COSMIC cursor anchoring with safety checks that omit clicks and scrolls when no trustworthy absolute position is available.
- Save and load version 1 `.catmacro` files, loop playback, select playback speed, and optionally smooth mouse movement.
- Switch immediately between persistent Regular and Compact interfaces; Compact provides direct Record, Play, Stop, Open, Save, and Settings controls.
- Persist light and dark themes, interface mode, playback options, and configurable global shortcuts.
- Prevent accidental multiple independent application instances through per-user local IPC.
- Guide users through input-permission setup and provide safe diagnostics with version and build identity.
- Harden recovery from physical input-device loss and pathological stale COSMIC cursor metadata while keeping healthy inputs available.
