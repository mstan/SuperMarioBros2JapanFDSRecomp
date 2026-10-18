cmake_minimum_required(VERSION 3.20)
project(SuperMarioBros2JapanFDSRecomp C CXX)

set(NESRECOMP_ROOT "${CMAKE_SOURCE_DIR}/nesrecomp" CACHE PATH "NESRecomp engine checkout")

# nesrecomp/ and recomp-ui/ are git submodules (see .gitmodules). Run
# setup.bat / setup.sh after cloning, and supply the disk image and
# bios/disksys.rom yourself (both untracked).
#
# FDS titles build on the cycle backend only (nesrecomp runner/cyc): the BIOS
# runs LLE and disk files compile at their load addresses as RAM views. A local
# capture file (NESRECOMP_CYCLE_CAPTURES, from routes/) can add RAM code the
# game copies at run time; it holds game code bytes and is never committed.
# tools/coverage_sites.json contains only observed addresses. The default
# build derives guarded code views from the supplied disk image and keeps
# rewritten operands live; additional private captures remain optional.
#
# The game's own additions (HOST_EXTRAS, src/): the widescreen mod
# (docs/WIDESCREEN.md), an opt-in package of the mod runtime (MODS, the
# launcher's Mods screen; mods/preloaded). It hooks the game.toml
# [[mod_function_hook]] sites; with the mod off the program runs exactly as
# without it.
option(NESRECOMP_HEADLESS "Build without the SDL window and audio" OFF)
set(NESRECOMP_CYCLE_CAPTURES "" CACHE FILEPATH "Local RAM capture file (optional; holds game code, never committed)")
include("${NESRECOMP_ROOT}/runner/cyc/project.cmake")
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
set(_smb2j_views "${CMAKE_CURRENT_BINARY_DIR}/smb2j-derived-views.txt")
set(_smb2j_view_options "")
if(NESRECOMP_CYCLE_CAPTURES)
    list(APPEND _smb2j_view_options --base-captures "${NESRECOMP_CYCLE_CAPTURES}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${NESRECOMP_CYCLE_CAPTURES}")
endif()
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_ram_views.py"
    --rom "${CMAKE_CURRENT_SOURCE_DIR}/Super Mario Bros. 2 (Japan) (Debug Value 0).fds"
    --out "${_smb2j_views}" ${_smb2j_view_options}
    RESULT_VARIABLE _smb2j_views_result)
if(NOT _smb2j_views_result EQUAL 0)
    message(FATAL_ERROR "Could not derive original FDS code views")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/prepare_ram_views.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/coverage_sites.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/Super Mario Bros. 2 (Japan) (Debug Value 0).fds")
set(cycle_options "")
if(NESRECOMP_HEADLESS)
    list(APPEND cycle_options HEADLESS)
endif()
nesrecomp_add_cycle_game(SuperMarioBros2JapanFDSRecomp
    ROM "${CMAKE_SOURCE_DIR}/Super Mario Bros. 2 (Japan) (Debug Value 0).fds"
    BIOS "${CMAKE_SOURCE_DIR}/bios/disksys.rom"
    GAME_CONFIG "${CMAKE_SOURCE_DIR}/game.toml"
    BOXART "${CMAKE_SOURCE_DIR}/boxart.tga"
    CAPTURE_FILE "${_smb2j_views}"
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
