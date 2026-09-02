# Development

Use Pop!_OS 24.04 or a compatible Ubuntu environment with CMake 3.24, C++20, Ninja, pkg-config, Qt 6.4 Core/Gui/QML/Quick/DBus/Test, PipeWire, SPA, libei, libevdev, Wayland headers, `wayland-scanner`, and wayland-protocols.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

Direct COSMIC support needs staging XML for ext image copy capture, ext image capture source, and ext foreign toplevel list. CMake prefers the verified copies under `third_party/wayland-protocols`, then checks normal system data locations. It does not download protocols.

```bash
cmake -S . -B build-asan -G Ninja -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined"
cmake --build build-asan -j4
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan --output-on-failure
cmake --install build --prefix /tmp/catclicker-install-test
```

See [DEBUGGING.md](DEBUGGING.md) for environment flags. Event-level cursor, recording, and uinput traces may contain sensitive input.
