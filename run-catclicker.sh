#!/usr/bin/env bash

cd "$HOME/CatClicker" || exit $?

echo "========== BUILD =========="
cmake --build build -j4 || exit $?

echo
echo "========== TEST =========="
ctest --test-dir build --output-on-failure || exit $?

echo
echo "========== LAUNCH =========="
./build/bin/CatClicker
