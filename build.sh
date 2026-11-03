#!/bin/sh
# Build Carmageddon II HD (dinput.dll) with a 32-bit MinGW-w64 toolchain.
#   Windows: MSYS2 mingw32 (C:/msys64/mingw32/bin), or set CXX
#   Linux:   CXX=i686-w64-mingw32-g++ ./build.sh
set -e
cd "$(dirname "$0")"
CXX=${CXX:-/c/msys64/mingw32/bin/g++.exe}
PATH="$(dirname "$CXX"):$PATH"  # the compiler needs its own DLLs
mkdir -p build
# -fno-indirect-inlining: when GCC inlines a helper that takes a function
# pointer and the caller passes a game address (a constant integer, not a
# function it knows), it turns the call into __builtin_unreachable(): the
# call and everything after it vanish and the code runs on into whatever
# follows. That broke menukeys.cpp's Activate (Change Car's Left/Right pressed
# CANCEL; Load Game's Esc sometimes did nothing).
"$CXX" -std=c++17 -O2 -msse2 -mfpmath=sse -Wall -Wextra -Wno-cast-function-type -fno-indirect-inlining -shared -static -s -Wl,--no-insert-timestamp \
    -o build/dinput.dll src/main.cpp src/proxy.cpp src/loader.cpp src/headless.cpp src/input.cpp src/capture.cpp \
    src/settings.cpp src/dgvconf.cpp src/display.cpp src/wide.cpp src/frontend.cpp src/hdtab.cpp src/controller.cpp src/options.cpp src/driver.cpp src/movie.cpp src/music.cpp src/netshim.cpp src/netmenu.cpp src/quit.cpp src/audio.cpp src/keysets.cpp src/sharptext.cpp src/icons.cpp src/wrecks.cpp src/menukeys.cpp src/peds.cpp src/debug.cpp src/fixes.cpp src/publicaddr.cpp \
    src/dinput.def \
    -lole32 -lws2_32 -lwinhttp -lwinmm -ldxguid -luuid -luser32 -lkernel32
echo "built build/dinput.dll"
