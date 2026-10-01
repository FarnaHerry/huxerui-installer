# huxerui-installer — generic Windows installer for HuxerUI applications.
#
# Builds the precompiled bootstrapper application (BA) executable that ships in the
# huxerui-installer-<version>-windows-x86_64.zip release package. Applications either build their
# installer through this project (source mode, see cmake/HuxerUIInstaller.cmake) or download the
# prebuilt package and drive scripts/make-setup.ps1 with a branding.json (prebuilt mode).
#
# Requires Windows with the MSVC C++ x64 tools and a HuxerUI SDK installation
# (find_package(HuxerUI); point CMAKE_PREFIX_PATH at the SDK prefix when it is not installed
# system-wide). WiX v5.0.2 is restored with SHA256 verification into HUXERUI_WIX_ROOT.

cmake_minimum_required(VERSION 3.24)

project(huxerui-installer VERSION 0.1.0 LANGUAGES CXX)

if (NOT WIN32)
    message(FATAL_ERROR "huxerui-installer builds the Windows bootstrapper application; configure on Windows")
endif ()
if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "huxerui-installer currently supports x64 only")
endif ()

find_package(HuxerUI REQUIRED)

if (NOT HUXERUI_WIX_ROOT)
    set(HUXERUI_WIX_ROOT "${CMAKE_BINARY_DIR}/.wix" CACHE PATH "WiX v5 toolset restore root")
endif ()

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/HuxerUIInstaller.cmake")

# The generic BA executable shipped in the prebuilt package.
huxerui_installer_add(huxerui_installer_ba
        INTEGRATION_OUTPUT "${CMAKE_BINARY_DIR}/huxerui-installer/$<CONFIG>/installer.json"
)
set_target_properties(huxerui_installer_ba PROPERTIES OUTPUT_NAME "huxerui-installer-ba")
