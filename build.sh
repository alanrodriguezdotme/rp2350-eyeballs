#!/bin/sh
# Build all firmware into ./build and copy the UF2 files to ./dist.
# Needs cmake, ninja and arm-none-eabi-gcc. If PICO_SDK_PATH is not set,
# CMake downloads the Pico SDK on the first run.
set -e
cd "$(dirname "$0")"
cmake -S . -B build -G Ninja
cmake --build build
mkdir -p dist
cp build/firmware/bringup/bringup.uf2 build/firmware/eyetest/eyetest.uf2 build/firmware/pendant/pendant.uf2 dist/
echo "Built dist/pendant.uf2, dist/eyetest.uf2 and dist/bringup.uf2"
