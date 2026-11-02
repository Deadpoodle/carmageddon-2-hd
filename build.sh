#!/bin/sh
# Build Carmageddon II HD (dinput.dll) with a 32-bit MinGW-w64 toolchain.
#   Windows: MSYS2 mingw32 (C:/msys64/mingw32/bin), or set CXX
#   Linux:   CXX=i686-w64-mingw32-g++ ./build.sh
set -e
cd "$(dirname "$0")"
CXX=${CXX:-/c/msys64/mingw32/bin/g++.exe}
PATH="$(dirname "$CXX"):$PATH"  # the compiler needs its own DLLs
mkdir -p build
"$CXX" -std=c++17 -O2 -msse2 -mfpmath=sse -Wall -Wextra -Wno-cast-function-type -shared -static -s -Wl,--no-insert-timestamp \
    -o build/dinput.dll src/main.cpp src/proxy.cpp src/loader.cpp src/headless.cpp src/input.cpp src/capture.cpp \
    src/settings.cpp src/dgvconf.cpp src/display.cpp src/wide.cpp src/frontend.cpp \
    src/dinput.def \
    -lole32 -ldxguid -luuid -luser32 -lkernel32
echo "built build/dinput.dll"
