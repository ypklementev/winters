cmake_minimum_required(VERSION 3.21)
project(WinTers VERSION 0.1.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

option(WINTERS_BUILD_TESTS "Build sim unit tests" ON)

# Static CRT: a single self-contained exe, no VC++ redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/W4 /permissive- /utf-8 /Zc:__cplusplus /EHsc)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# sim: platform-independent simulation (builds on macOS/Linux too)
# ---------------------------------------------------------------------------
add_library(winters_sim STATIC
    src/sim/flakes.cpp
    src/sim/heightmap.cpp
    src/sim/surfaces.cpp
    src/sim/snowballs.cpp
    src/sim/wind.cpp
    src/sim/wiper.cpp
    src/sim/world.cpp
)
target_include_directories(winters_sim PUBLIC src)

# Procedural flake sprite atlas: pure C++ too, so it is tested on any host.
add_library(winters_atlas STATIC src/render/flake_atlas.cpp src/render/shapes.cpp)
target_link_libraries(winters_atlas PUBLIC winters_sim)

# ---------------------------------------------------------------------------
# WinTers.exe (Windows only: MSVC or MinGW cross-compile)
# ---------------------------------------------------------------------------
if(WIN32)
    enable_language(RC)
    add_executable(WinTers WIN32
        src/app/main.cpp
        src/app/app.cpp
        src/app/tray.cpp
        src/app/icon.cpp
        src/app/settings.cpp
        src/app/autostart.cpp
        src/app/power.cpp
        src/app/resources.rc
        src/input/input.cpp
        src/windows_tracker/tracker.cpp
        src/overlay/overlay_window.cpp
        src/overlay/displays.cpp
        src/render/device.cpp
        src/render/renderer.cpp
    )
    target_link_libraries(WinTers PRIVATE
        winters_sim
        winters_atlas
        d3d11 dxgi dcomp dwmapi shcore user32 gdi32 shell32 ole32 uuid advapi32 wtsapi32
    )
    target_compile_definitions(WinTers PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000007
    )
    if(MSVC)
        # The manifest comes from resources.rc; don't let link.exe add another.
        target_link_options(WinTers PRIVATE /MANIFEST:NO)
    else()
        target_link_options(WinTers PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(WINTERS_BUILD_TESTS)
    enable_testing()
    add_executable(sim_tests
        tests/test_main.cpp
        tests/test_flakes.cpp
        tests/test_heightmap.cpp
        tests/test_surfaces.cpp
        tests/test_world_piles.cpp
        tests/test_mitten_snowballs.cpp
        tests/test_settings.cpp
        tests/test_atlas.cpp
        tests/test_wiper_plow.cpp
    )
    target_link_libraries(sim_tests PRIVATE winters_atlas)
    target_include_directories(sim_tests PRIVATE tests)
    if(MINGW)
        target_link_options(sim_tests PRIVATE -static)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME sim_tests COMMAND sim_tests)
    endif()
endif()
