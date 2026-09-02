#!/usr/bin/env bash

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR" || exit $?

echo "========== BUILD =========="
cmake --build build -j4 || exit $?

echo
echo "========== TEST =========="
ctest --test-dir build --output-on-failure || exit $?

echo
echo "========== LAUNCH =========="
./build/bin/CatClicker
