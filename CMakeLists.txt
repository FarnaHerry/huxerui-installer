# huxerui-installer — generic Windows installer for HuxerUI applications.
#
# Builds the precompiled bootstrapper application (BA) executable that ships in the
# huxerui-installer-<version>-windows-x86_64.zip release package. Applications either build their
# installer through this project (source mode, see cmake/HuxerUIInstaller.cmake) or download the
# prebuilt package and drive scripts/make-setup.ps1 with a branding.json (prebuilt mode).
#
# Requires Windows with the MSVC C++ x64 tools and HuxerUI — either an installed SDK
# (find_package via CMAKE_PREFIX_PATH) or a source checkout (-DHUXERUI_SOURCE_DIR=..., which is what
# the release CI uses to build against the latest HuxerUI sources). WiX v5.0.2 is restored with SHA256
# verification into HUXERUI_WIX_ROOT.

cmake_minimum_required(VERSION 3.24)

project(huxerui-installer VERSION 0.1.0 LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "huxerui-installer builds the Windows bootstrapper application; configure on Windows")
endif ()
if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "huxerui-installer currently supports x64 only")
endif ()

find_package(HuxerUI QUIET)
if (NOT TARGET HuxerUI::huxerui AND NOT TARGET HuxerUI::huxerui_static)
    # Source mode: build against a HuxerUI checkout (release CI clones the latest HuxerUI source).
    set(HUXERUI_SOURCE_DIR "" CACHE PATH "HuxerUI source checkout to build with instead of an installed SDK")
    if (NOT HUXERUI_SOURCE_DIR)
        message(FATAL_ERROR
                "HuxerUI not found: install the SDK and set CMAKE_PREFIX_PATH, or pass "
                "-DHUXERUI_SOURCE_DIR=<checkout> to build against the HuxerUI sources"
        )
    endif ()
    add_subdirectory("${HUXERUI_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/huxerui")
endif ()

if (NOT HUXERUI_WIX_ROOT)
    set(HUXERUI_WIX_ROOT "${CMAKE_BINARY_DIR}/.wix" CACHE PATH "WiX v5 toolset restore root")
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/HuxerUIInstaller.cmake")

# The generic BA executable shipped in the prebuilt package.
huxerui_installer_add(huxerui_installer_ba
        INTEGRATION_OUTPUT "${CMAKE_BINARY_DIR}/huxerui-installer/$<CONFIG>/installer.json"
)
set_target_properties(huxerui_installer_ba PROPERTIES OUTPUT_NAME "huxerui-installer-ba")

# make-setup — native prebuilt-mode packager. A plain Win32 console tool (no HuxerUI dependency) that
# wraps an already-staged application payload into a Burn bundle setup executable; the native equivalent
# of scripts/make-setup.ps1.
add_executable(make-setup src/pack/make_setup.cpp)
target_include_directories(make-setup PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/shared")
target_compile_features(make-setup PRIVATE cxx_std_17)
target_compile_definitions(make-setup PRIVATE UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(make-setup PRIVATE bcrypt)
install(TARGETS make-setup RUNTIME DESTINATION . COMPONENT HuxerUIInstaller_make_setup)
