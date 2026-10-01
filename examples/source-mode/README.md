# Source mode example

Deep customization: consume huxerui-installer as a source dependency and compile your own installer UI
(or reuse the default one) into `<app>-Installer.exe` inside your existing CMake build.

```cmake
# CMakeLists.txt of the application
find_package(HuxerUI REQUIRED)

# Any source form works: git submodule, FetchContent, or a checked-out sibling directory.
add_subdirectory(third_party/huxerui-installer EXCLUDE_FROM_ALL)
# (add_subdirectory of the project root only defines targets when built on Windows; including the
# function file directly is enough for the packaging helper:)
include("${CMAKE_CURRENT_SOURCE_DIR}/third_party/huxerui-installer/cmake/HuxerUIInstaller.cmake")

# Default generic interface — no UI_SOURCES needed:
huxerui_installer_add(myapp_installer
        CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/package/windows/branding.json"
        INTEGRATION_OUTPUT "${CMAKE_BINARY_DIR}/package/windows/$<CONFIG>/installer.json"
)
set_target_properties(myapp_installer PROPERTIES OUTPUT_NAME "myapp-Installer")

# Or a custom interface (equivalent to the per-app package/src/app.cpp you may have today):
huxerui_installer_add(myapp_installer_custom
        UI_SOURCES
            "${CMAKE_CURRENT_SOURCE_DIR}/package/windows/src/main.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/package/windows/src/app.cpp"
        UI_RESOURCES "${CMAKE_CURRENT_SOURCE_DIR}/package/windows/resources"
        RESOURCE_NAMESPACE installer
        CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/package/windows/branding.json"
        INTEGRATION_OUTPUT "${CMAKE_BINARY_DIR}/package/windows-custom/$<CONFIG>/installer.json"
)
```

Contract notes:

- `UI_SOURCES` replaces the default interface entirely, including the `wWinMain` entry point — provide a
  `main.cpp` that calls `huxerui::windows::RunInstallerApplication()` (see `src/main.cpp`).
- The engine (`src/engine/installer_engine.cpp`) and runtime branding (`src/ui/branding.cpp`) are always
  compiled in; custom UIs use the SDK's `<huxerui/windows/installer.h>` API and may read
  `huxerui_installer::GetBranding()`.
- `CONFIG` points at a `branding.json`; its whole directory is staged as BA payloads so relative logo and
  license references resolve next to the BA executable.
- The written `installer.json` matches the HuxerUI SDK schema (`wix`, `installer`, `installComponent`),
  so existing `huxerui package windows`-style flows can consume it.
- Silent mode (`setup.exe /quiet /norestart`, optional `InstallFolder=`) is handled by the engine; a
  custom UI only runs when Burn shows a window.
