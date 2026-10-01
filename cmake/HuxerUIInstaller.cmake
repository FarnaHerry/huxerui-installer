# HuxerUIInstaller.cmake — source-mode entry point of huxerui-installer.
#
# Provides huxerui_installer_add(), which builds a Windows bootstrapper application (BA) for a WiX v5 Burn
# bundle out of the huxerui-installer engine plus either the default generic interface or the caller's own
# UI sources. WiX toolset restore logic mirrors HuxerUI/cmake/HuxerUIWindowsInstaller.cmake and
# scripts/Restore-Wix.ps1 — package ids, versions, and SHA256 hashes MUST stay in sync with those files.
#
# Usage:
#   find_package(HuxerUI REQUIRED)
#   include(<huxerui-installer>/cmake/HuxerUIInstaller.cmake)
#   huxerui_installer_add(myapp_installer
#       UI_SOURCES package/src/app.cpp        # optional; default interface when omitted
#       CONFIG package/branding.json          # optional; staged as BA payloads
#       INTEGRATION_OUTPUT <path>             # optional installer.json plan output
#   )

include_guard(GLOBAL)

get_filename_component(HUXERUI_INSTALLER_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

function(_huxerui_installer_restore_wix_package package_name archive_sha256 destination)
    set(HUXERUI_WIX_REQUIRED_FILES ${ARGN})
    set(HUXERUI_WIX_PACKAGE_COMPLETE TRUE)
    foreach (HUXERUI_WIX_REQUIRED_FILE IN LISTS HUXERUI_WIX_REQUIRED_FILES)
        if (NOT EXISTS "${destination}/${HUXERUI_WIX_REQUIRED_FILE}")
            set(HUXERUI_WIX_PACKAGE_COMPLETE FALSE)
            break()
        endif ()
    endforeach ()
    if (HUXERUI_WIX_PACKAGE_COMPLETE)
        return()
    endif ()

    file(MAKE_DIRECTORY "${HUXERUI_WIX_ROOT}/downloads")
    string(TOLOWER "${package_name}" HUXERUI_WIX_PACKAGE_ID)
    set(HUXERUI_WIX_ARCHIVE
            "${HUXERUI_WIX_ROOT}/downloads/${HUXERUI_WIX_PACKAGE_ID}.${HUXERUI_WIX_VERSION}.nupkg"
    )
    file(DOWNLOAD
            "https://api.nuget.org/v3-flatcontainer/${HUXERUI_WIX_PACKAGE_ID}/${HUXERUI_WIX_VERSION}/${HUXERUI_WIX_PACKAGE_ID}.${HUXERUI_WIX_VERSION}.nupkg"
            "${HUXERUI_WIX_ARCHIVE}"
            EXPECTED_HASH "SHA256=${archive_sha256}"
            TLS_VERIFY ON
            SHOW_PROGRESS
    )
    file(REMOVE_RECURSE "${destination}")
    file(MAKE_DIRECTORY "${destination}")
    file(ARCHIVE_EXTRACT INPUT "${HUXERUI_WIX_ARCHIVE}" DESTINATION "${destination}")
    foreach (HUXERUI_WIX_REQUIRED_FILE IN LISTS HUXERUI_WIX_REQUIRED_FILES)
        if (NOT EXISTS "${destination}/${HUXERUI_WIX_REQUIRED_FILE}")
            message(FATAL_ERROR "Restored ${package_name} package is incomplete")
        endif ()
    endforeach ()
endfunction()

function(_huxerui_installer_prepare_wix_dependencies)
    set(HUXERUI_WIX_VERSION "5.0.2")
    set(HUXERUI_WIX_TOOL_ARCHIVE_SHA256
            "f30ef0c74e2a986126539c5780be93ac24e8136eaf723b1937b26272703ae173"
    )
    set(HUXERUI_WIX_BOOTSTRAPPER_ARCHIVE_SHA256
            "6e0d3c68a68dcedde4a3a68de896f124a7b19c4a823fac49856e2ee77cb16256"
    )
    set(HUXERUI_WIX_DUTIL_ARCHIVE_SHA256
            "aa4f0668044318820e6c31ffef9f4141830c9fd8ebbe038281329423916547fe"
    )
    set(HUXERUI_WIX_TOOL_DIRECTORY "${HUXERUI_WIX_ROOT}/tool")
    set(HUXERUI_WIX_BOOTSTRAPPER_DIRECTORY "${HUXERUI_WIX_ROOT}/bootstrapper")
    set(HUXERUI_WIX_DUTIL_DIRECTORY "${HUXERUI_WIX_ROOT}/dutil")
    _huxerui_installer_restore_wix_package(
            "wix"
            "${HUXERUI_WIX_TOOL_ARCHIVE_SHA256}"
            "${HUXERUI_WIX_TOOL_DIRECTORY}"
            "tools/net6.0/any/wix.exe"
    )
    _huxerui_installer_restore_wix_package(
            "WixToolset.BootstrapperApplicationApi"
            "${HUXERUI_WIX_BOOTSTRAPPER_ARCHIVE_SHA256}"
            "${HUXERUI_WIX_BOOTSTRAPPER_DIRECTORY}"
            "build/native/include/BootstrapperApplication.h"
            "build/native/v14/x64/balutil.lib"
            "runtimes/win-x64/native/mbanative.dll"
    )
    _huxerui_installer_restore_wix_package(
            "WixToolset.DUtil"
            "${HUXERUI_WIX_DUTIL_ARCHIVE_SHA256}"
            "${HUXERUI_WIX_DUTIL_DIRECTORY}"
            "build/native/include/dutil.h"
            "build/native/v14/x64/dutil.lib"
    )

    set(HUXERUI_WIX_EXECUTABLE "${HUXERUI_WIX_TOOL_DIRECTORY}/tools/net6.0/any/wix.exe")
    cmake_path(CONVERT "${HUXERUI_WIX_EXECUTABLE}" TO_CMAKE_PATH_LIST HUXERUI_WIX_EXECUTABLE NORMALIZE)
    execute_process(
            COMMAND "${HUXERUI_WIX_EXECUTABLE}" --version
            RESULT_VARIABLE HUXERUI_WIX_PROBE_RESULT
            OUTPUT_VARIABLE HUXERUI_WIX_PROBE_OUTPUT
            ERROR_VARIABLE HUXERUI_WIX_PROBE_ERROR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_STRIP_TRAILING_WHITESPACE
    )
    if (NOT HUXERUI_WIX_PROBE_RESULT STREQUAL "0")
        if (NOT HUXERUI_WIX_PROBE_ERROR)
            set(HUXERUI_WIX_PROBE_ERROR "${HUXERUI_WIX_PROBE_OUTPUT}")
        endif ()
        message(FATAL_ERROR
                "HuxerUI installer packaging requires Microsoft.NETCore.App 6.0 or newer to run WiX: ${HUXERUI_WIX_PROBE_ERROR}"
        )
    endif ()

    set(HUXERUI_WIX_EXECUTABLE "${HUXERUI_WIX_EXECUTABLE}" PARENT_SCOPE)
    set(HUXERUI_WIX_BOOTSTRAPPER_INCLUDE
            "${HUXERUI_WIX_BOOTSTRAPPER_DIRECTORY}/build/native/include"
            PARENT_SCOPE
    )
    set(HUXERUI_WIX_BOOTSTRAPPER_LIBRARY
            "${HUXERUI_WIX_BOOTSTRAPPER_DIRECTORY}/build/native/v14/x64/balutil.lib"
            PARENT_SCOPE
    )
    set(HUXERUI_WIX_BOOTSTRAPPER_RUNTIME
            "${HUXERUI_WIX_BOOTSTRAPPER_DIRECTORY}/runtimes/win-x64/native/mbanative.dll"
            PARENT_SCOPE
    )
    set(HUXERUI_WIX_DUTIL_INCLUDE
            "${HUXERUI_WIX_DUTIL_DIRECTORY}/build/native/include"
            PARENT_SCOPE
    )
    set(HUXERUI_WIX_DUTIL_LIBRARY
            "${HUXERUI_WIX_DUTIL_DIRECTORY}/build/native/v14/x64/dutil.lib"
            PARENT_SCOPE
    )
endfunction()

# huxerui_installer_add(<target>
#     [UI_SOURCES <sources>...]       Custom installer UI sources. Defaults to the generic interface.
#     [UI_RESOURCES <directory>]      Resource root compiled with the UI sources. Defaults to the generic
#                                     interface's string catalogs when UI_SOURCES is omitted.
#     [RESOURCE_NAMESPACE <name>]     Resource namespace for UI_RESOURCES (default: installer).
#     [EXTRA_SOURCES <sources>...]    Extra target sources, for example a version/icon .rc file.
#     [CONFIG <branding.json>]        Branding configuration; its directory is installed into the
#                                     installer component so the bundle can carry it as BA payloads.
#     [INTEGRATION_OUTPUT <file>]     Writes the installer.json packaging plan (generator expressions OK).
# )
function(huxerui_installer_add target_name)
    if (NOT WIN32)
        message(FATAL_ERROR "huxerui_installer_add() is available only on Windows")
    endif ()
    if (NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "HuxerUI Windows installer packaging currently supports x64 only")
    endif ()
    if (NOT HuxerUI_DIR)
        message(FATAL_ERROR "huxerui_installer_add() requires find_package(HuxerUI) first")
    endif ()
    if (NOT HUXERUI_WIX_ROOT)
        get_filename_component(HUXERUI_WIX_ROOT "${CMAKE_BINARY_DIR}/.huxerui/wix" ABSOLUTE)
    endif ()

    cmake_parse_arguments(HUXERUI_INSTALLER
            ""
            "RESOURCE_NAMESPACE;CONFIG;INTEGRATION_OUTPUT"
            "UI_SOURCES;UI_RESOURCES;EXTRA_SOURCES"
            ${ARGN}
    )
    if (NOT HUXERUI_INSTALLER_RESOURCE_NAMESPACE)
        set(HUXERUI_INSTALLER_RESOURCE_NAMESPACE "installer")
    endif ()

    set(HUXERUI_INSTALLER_ALL_SOURCES
            "${HUXERUI_INSTALLER_ROOT}/src/engine/installer_engine.cpp"
            "${HUXERUI_INSTALLER_ROOT}/src/ui/branding.cpp"
    )
    if (HUXERUI_INSTALLER_UI_SOURCES)
        list(APPEND HUXERUI_INSTALLER_ALL_SOURCES ${HUXERUI_INSTALLER_UI_SOURCES})
    else ()
        # The default interface ships its own wWinMain entry point; custom UI_SOURCES must provide one
        # calling huxerui::windows::RunInstallerApplication().
        list(APPEND HUXERUI_INSTALLER_ALL_SOURCES
                "${HUXERUI_INSTALLER_ROOT}/src/main.cpp"
                "${HUXERUI_INSTALLER_ROOT}/src/ui/default_app.cpp"
        )
        if (NOT HUXERUI_INSTALLER_UI_RESOURCES)
            set(HUXERUI_INSTALLER_UI_RESOURCES "${HUXERUI_INSTALLER_ROOT}/resources")
        endif ()
    endif ()
    list(APPEND HUXERUI_INSTALLER_ALL_SOURCES ${HUXERUI_INSTALLER_EXTRA_SOURCES})

    _huxerui_installer_prepare_wix_dependencies()

    set(HUXERUI_INSTALLER_APP_ARGUMENTS
            SOURCES ${HUXERUI_INSTALLER_ALL_SOURCES}
    )
    if (HUXERUI_INSTALLER_UI_RESOURCES)
        list(APPEND HUXERUI_INSTALLER_APP_ARGUMENTS
                RESOURCES "${HUXERUI_INSTALLER_UI_RESOURCES}"
                RESOURCE_NAMESPACE "${HUXERUI_INSTALLER_RESOURCE_NAMESPACE}"
        )
    endif ()
    huxerui_add_app(${target_name} ${HUXERUI_INSTALLER_APP_ARGUMENTS})

    target_include_directories(${target_name} PRIVATE
            "${HUXERUI_INSTALLER_ROOT}/src/ui"
            "${HUXERUI_INSTALLER_ROOT}/src/engine"
            "${HuxerUI_DIR}"
            "${HUXERUI_WIX_BOOTSTRAPPER_INCLUDE}"
            "${HUXERUI_WIX_DUTIL_INCLUDE}"
    )
    target_link_libraries(${target_name} PRIVATE
            "${HUXERUI_WIX_BOOTSTRAPPER_LIBRARY}"
            "${HUXERUI_WIX_DUTIL_LIBRARY}"
            version
    )
    target_compile_definitions(${target_name} PRIVATE
            UNICODE
            _UNICODE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )
    set_property(TARGET ${target_name} PROPERTY HUXERUI_WINDOWS_CRT_ENTRY wWinMainCRTStartup)
    add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                    "${HUXERUI_WIX_BOOTSTRAPPER_RUNTIME}"
                    "$<TARGET_FILE_DIR:${target_name}>/mbanative.dll"
            VERBATIM
    )

    get_target_property(HUXERUI_INSTALLER_RESOURCE_PACKAGE
            ${target_name}
            HUXERUI_RESOURCE_PACKAGE
    )
    set(HUXERUI_INSTALLER_COMPONENT "HuxerUIInstaller_${target_name}")
    set_property(TARGET ${target_name} PROPERTY HUXERUI_APPLICATION_INSTALL_COMPONENT "${HUXERUI_INSTALLER_COMPONENT}")
    install(TARGETS ${target_name} RUNTIME DESTINATION . COMPONENT "${HUXERUI_INSTALLER_COMPONENT}")
    if (HUXERUI_INSTALLER_RESOURCE_PACKAGE
            AND NOT HUXERUI_INSTALLER_RESOURCE_PACKAGE MATCHES "-NOTFOUND$")
        install(DIRECTORY "${HUXERUI_INSTALLER_RESOURCE_PACKAGE}/"
                DESTINATION "$<TARGET_FILE_BASE_NAME:${target_name}>.resources"
                COMPONENT "${HUXERUI_INSTALLER_COMPONENT}"
        )
    endif ()
    if (HUXERUI_INSTALLER_CONFIG)
        if (NOT IS_ABSOLUTE "${HUXERUI_INSTALLER_CONFIG}")
            get_filename_component(HUXERUI_INSTALLER_CONFIG "${HUXERUI_INSTALLER_CONFIG}"
                    ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}"
            )
        endif ()
        if (NOT EXISTS "${HUXERUI_INSTALLER_CONFIG}")
            message(FATAL_ERROR "huxerui_installer_add() CONFIG does not exist: ${HUXERUI_INSTALLER_CONFIG}")
        endif ()
        # The branding directory ships as BA payloads: branding.json at the root, referenced logo and
        # license files in their relative subdirectories.
        get_filename_component(HUXERUI_INSTALLER_CONFIG_DIR "${HUXERUI_INSTALLER_CONFIG}" DIRECTORY)
        install(DIRECTORY "${HUXERUI_INSTALLER_CONFIG_DIR}/"
                DESTINATION .
                COMPONENT "${HUXERUI_INSTALLER_COMPONENT}"
        )
    endif ()
    huxerui_add_runtime_dependencies(${target_name} FILES "${HUXERUI_WIX_BOOTSTRAPPER_RUNTIME}")
    _huxerui_install_runtime_dependencies(${target_name} "${HUXERUI_INSTALLER_COMPONENT}"
            . "$<TARGET_FILE_NAME:${target_name}>"
    )

    if (HUXERUI_INSTALLER_INTEGRATION_OUTPUT)
        file(GENERATE
                OUTPUT "${HUXERUI_INSTALLER_INTEGRATION_OUTPUT}"
                CONTENT "{\n  \"schema\": 1,\n  \"wix\": \"${HUXERUI_WIX_EXECUTABLE}\",\n  \"installer\": \"$<TARGET_FILE_NAME:${target_name}>\",\n  \"installComponent\": \"${HUXERUI_INSTALLER_COMPONENT}\"\n}\n"
        )
    endif ()
endfunction()
