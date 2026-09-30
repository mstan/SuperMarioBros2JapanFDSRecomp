cmake_minimum_required(VERSION 3.20)
project(SuperMarioBros2JapanFDSRecomp C CXX)

set(NESRECOMP_ROOT ${CMAKE_SOURCE_DIR}/nesrecomp)

# nesrecomp/ and recomp-ui/ are git submodules (see .gitmodules). Run
# setup.bat / setup.sh after cloning, and supply the disk image and
# bios/disksys.rom yourself (both untracked).
#
# FDS titles build on the cycle backend only (nesrecomp runner/cyc): the BIOS
# runs LLE and disk files compile at their load addresses as RAM views. A local
# capture file (NESRECOMP_CYCLE_CAPTURES, from routes/) can add RAM code the
# game copies at run time; it holds game code bytes and is never committed.
#
# The game's own additions (HOST_EXTRAS, src/): the widescreen mod
# (docs/WIDESCREEN.md), an opt-in package of the mod runtime (MODS, the
# launcher's Mods screen; mods/preloaded). It hooks the game.toml
# [[mod_function_hook]] sites; with the mod off the program runs exactly as
# without it.
option(NESRECOMP_HEADLESS "Build without the SDL window and audio" OFF)
set(NESRECOMP_CYCLE_CAPTURES "" CACHE FILEPATH "Local RAM capture file (optional; holds game code, never committed)")
include("${NESRECOMP_ROOT}/runner/cyc/project.cmake")
set(cycle_options "")
if(NESRECOMP_HEADLESS)
    list(APPEND cycle_options HEADLESS)
endif()
nesrecomp_add_cycle_game(SuperMarioBros2JapanFDSRecomp
    ROM "${CMAKE_SOURCE_DIR}/Super Mario Bros. 2 (Japan) (Debug Value 0).fds"
    BIOS "${CMAKE_SOURCE_DIR}/bios/disksys.rom"
    GAME_CONFIG "${CMAKE_SOURCE_DIR}/game.toml"
    BOXART "${CMAKE_SOURCE_DIR}/boxart.tga"
    CAPTURE_FILE "${NESRECOMP_CYCLE_CAPTURES}"
    MODS GAME_ID "super-mario-bros-2-japan-fds"
    HOST_EXTRAS
        "${CMAKE_SOURCE_DIR}/src/smb2j_ws.c"
        "${CMAKE_SOURCE_DIR}/src/smb2j_ws_world.c"
        "${CMAKE_SOURCE_DIR}/src/smb2j_ws_actors.c"
        "${CMAKE_SOURCE_DIR}/mods/widescreen_plugin.c"
    ${cycle_options})
target_include_directories(SuperMarioBros2JapanFDSRecomp PRIVATE "${CMAKE_SOURCE_DIR}/src")

# The preloaded mod catalog beside the executable (packages only: the player's
# selection, state.toml, is written there by the launcher).
add_custom_command(TARGET SuperMarioBros2JapanFDSRecomp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/mods/preloaded" "$<TARGET_FILE_DIR:SuperMarioBros2JapanFDSRecomp>/mods"
    COMMENT "Staging the preloaded mod packages")
