# Contributing to CatClicker

CatClicker currently targets Pop!_OS 24.04, COSMIC, and Wayland. Work on other Wayland compositor backends is welcome, even before it is complete.

Install dependencies and follow [Development](docs/DEVELOPMENT.md), then run `cmake --build build -j4` and `ctest --test-dir build --output-on-failure`. Format changed C++ files with `clang-format -i path/to/changed.cpp path/to/changed.h`. The repository uses `.clang-format`; avoid formatting unrelated files.

## Repository map

- `src/app`: application state and QML-facing orchestration
- `src/input`: capture, trusted cursor, portal, and playback backends
- `src/macro`: macro model, recorder, and player
- `src/persistence`: macro files and settings
- `src/diagnostics`: safe reports
- `qml`: interface and theme
- `tests`: deterministic regression tests
- `tools`: developer utilities

Add capture behavior beside capture backends, absolute cursor providers beside the COSMIC provider, and virtual playback beside the uinput sender. New boundaries should solve a real ownership or backend problem.

## Safety invariants

Behavior changes need regression tests and existing tests must stay green. Mouse buttons and scroll require trusted absolute anchors. Unresolved mouse events are dropped. Playback uses exact saved anchors. Emergency release uses only an accepted trusted down anchor. `REL_X` and `REL_Y` are triggers, never coordinates. Smoothing is playback-only. External strings remain data and never pass through a shell.

Read [COSMIC_WAYLAND.md](docs/COSMIC_WAYLAND.md) before changing cursor sampling.

## Pull requests

Keep a patch focused and explain its user-visible reason, tests, and host desktop when relevant. Performance changes should include the scenario, command, duration, and before and after measurements. Small fixes need no extra ceremony.
