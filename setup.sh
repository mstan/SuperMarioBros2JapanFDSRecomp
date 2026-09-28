#!/bin/sh
set -e
# nesrecomp/ and recomp-ui/ are git submodules pinned by this repo.
git submodule update --init nesrecomp recomp-ui
[ -f bios/disksys.rom ] || echo "Missing bios/disksys.rom - supply your own FDS BIOS dump (see bios/disksys.toml)."
[ -f "Super Mario Bros. 2 (Japan) (Debug Value 0).fds" ] || echo "Missing Super Mario Bros. 2 (Japan) (Debug Value 0).fds - supply your own disk image (see game.toml)."
echo "Ready - nesrecomp + recomp-ui checked out at their pinned commits."
echo "Build: cmake -S . -B build && cmake --build build --config Release"
