cmake_minimum_required(VERSION 3.21)
# macOS: SMAppService (Open at Login) needs 13. Must be set before project().
if(CMAKE_HOST_APPLE AND NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
endif()
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

# Platform-independent half of rendering: the procedural flake sprite atlas,
# vector shapes and per-frame GPU instance data. Shared by the Direct3D and
# Metal renderers and tested on any host.
add_library(winters_render STATIC src/render/flake_atlas.cpp src/render/shapes.cpp src/render/instances.cpp)
target_link_libraries(winters_render PUBLIC winters_sim)

# The version lives only in project(): it goes into VERSIONINFO (version.h),
# the MSIX manifest (X.Y.Z.0: the Store wants the 4th part to be 0), the macOS
# Info.plist and version.txt, which the build scripts use to name dist/ files.
if(PROJECT_VERSION_PATCH STREQUAL "" OR NOT PROJECT_VERSION_TWEAK STREQUAL "")
    message(FATAL_ERROR "project(VERSION) must be X.Y.Z, got ${PROJECT_VERSION}")
endif()
file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/version.txt "${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# WinTers.exe (Windows only: MSVC or MinGW cross-compile)
# ---------------------------------------------------------------------------
if(WIN32)
    enable_language(RC)

    set(WINTERS_GEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated)
    configure_file(src/app/version.h.in ${WINTERS_GEN_DIR}/version.h @ONLY)

    # MaxVersionTested = the SDK this build uses (build.ps1 picks it via CMAKE_SYSTEM_VERSION).
    if(CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION)
        set(WINTERS_SDK_VERSION ${CMAKE_VS_WINDOWS_TARGET_PLATFORM_VERSION})
    elseif(CMAKE_SYSTEM_VERSION MATCHES "^10\\.0\\.[0-9]+\\.[0-9]+$")
        set(WINTERS_SDK_VERSION ${CMAKE_SYSTEM_VERSION})
    else()
        set(WINTERS_SDK_VERSION 10.0.18362.0)
    endif()
    set(WINTERS_MSIX_VERSION ${PROJECT_VERSION}.0)
    configure_file(packaging/AppxManifest.xml ${CMAKE_CURRENT_BINARY_DIR}/msix/AppxManifest.xml @ONLY)
    add_executable(WinTers WIN32
        src/app/main.cpp
        src/app/app.cpp
        src/app/tray.cpp
        src/app/icon.cpp
        src/app/settings.cpp
        src/app/autostart.cpp
        src/app/package.cpp
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
        winters_render
        d3d11 dxgi dcomp dwmapi shcore user32 gdi32 shell32 ole32 uuid advapi32 wtsapi32
    )
    target_include_directories(WinTers PRIVATE ${WINTERS_GEN_DIR})
    target_compile_definitions(WinTers PRIVATE
        UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX
        _WIN32_WINNT=0x0A00 WINVER=0x0A00 NTDDI_VERSION=0x0A000007
    )
    if(MSVC)
        # The manifest comes from resources.rc; don't let link.exe add another.
        target_link_options(WinTers PRIVATE /MANIFEST:NO)
        # C++/WinRT (StartupTask in MSIX mode). VS normally has Include\<sdk>\cppwinrt
        # on the include path already; add it explicitly in case it does not.
        target_link_libraries(WinTers PRIVATE windowsapp)
        get_filename_component(WINTERS_KITS_ROOT
            "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]" ABSOLUTE)
        set(WINTERS_CPPWINRT_DIR "${WINTERS_KITS_ROOT}/Include/${WINTERS_SDK_VERSION}/cppwinrt")
        if(EXISTS "${WINTERS_CPPWINRT_DIR}/winrt/base.h")
            target_include_directories(WinTers SYSTEM PRIVATE "${WINTERS_CPPWINRT_DIR}")
        endif()
    else()
        target_link_options(WinTers PRIVATE -municode -static -static-libgcc -static-libstdc++)
    endif()

# ---------------------------------------------------------------------------
# WinTers.app (macOS: AppKit + Metal, Objective-C++). Builds with the Command
# Line Tools alone: shaders are compiled at runtime, no Xcode project.
# ---------------------------------------------------------------------------
elseif(APPLE)
    enable_language(OBJCXX)
    set(WINTERS_MAC_BUNDLE_ID "ru.ypklementev.WinTers" CACHE STRING "macOS bundle identifier")
    option(WINTERS_MAC_ADHOC_SIGN "Ad-hoc sign WinTers.app after every build (Open at Login needs a signed bundle)" ON)

    # Metal renderer: shared by the app and the offscreen preview tool.
    add_library(winters_metal STATIC src/mac/metal_renderer.mm)
    target_link_libraries(winters_metal PUBLIC winters_render "-framework Metal" "-framework Foundation")
    target_compile_options(winters_metal PUBLIC $<$<COMPILE_LANGUAGE:OBJCXX>:-fobjc-arc>)

    set(WINTERS_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/WinTers.icns)
    set_source_files_properties(${WINTERS_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    add_executable(WinTers MACOSX_BUNDLE
        src/mac/main.mm
        src/mac/controller.mm
        src/mac/overlay.mm
        src/mac/screens.mm
        src/mac/window_list.mm
        src/mac/input.mm
        src/mac/prefs.mm
        ${WINTERS_ICNS}
    )
    set_target_properties(WinTers PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/packaging/macos/Info.plist.in
    )
    target_link_libraries(WinTers PRIVATE
        winters_metal
        "-framework AppKit"
        "-framework QuartzCore"
        "-framework CoreGraphics"
        "-framework Carbon"
        "-framework ServiceManagement"
    )
    if(WINTERS_MAC_ADHOC_SIGN)
        add_custom_command(TARGET WinTers POST_BUILD
            COMMAND codesign --force --sign - --timestamp=none $<TARGET_BUNDLE_DIR:WinTers>
            COMMENT "Ad-hoc signing WinTers.app"
            VERBATIM)
    endif()

    # Renders a simulated desktop to a PNG through the Metal renderer: a quick
    # visual check of the shaders without running the overlay.
    add_executable(winters_preview tools/mac_preview.mm)
    target_link_libraries(winters_preview PRIVATE winters_metal "-framework ImageIO" "-framework CoreGraphics")
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
        tests/test_instances.cpp
    )
    target_link_libraries(sim_tests PRIVATE winters_render)
    target_include_directories(sim_tests PRIVATE tests)
    if(MINGW)
        target_link_options(sim_tests PRIVATE -static)
    endif()
    if(NOT CMAKE_CROSSCOMPILING)
        add_test(NAME sim_tests COMMAND sim_tests)
    endif()
endif()
