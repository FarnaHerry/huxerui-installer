# huxerui-installer

**简体中文** | [English](README.en.md) — 本页为英文版。The Simplified Chinese version is the default [README.md](README.md).

Generic Windows installer for [HuxerUI](https://github.com/HuxerUI/HuxerUI) applications.

- **Mature engine**: a WiX v5 Burn bundle owns detection, elevation, rollback, uninstall, and silent
  operation.
- **HuxerUI shell**: the bootstrapper application (BA) is a HuxerUI program — Burn drives the engine,
  the BA only renders the interface.
- **Silent and winget ready**: `setup.exe /quiet /norestart` with optional `InstallFolder=` and
  `CreateDesktopShortcut=` variables; no window is created, prompts take their recommended answer, and
  the process exits with `0`, `3010` (restart required), or `1602` (canceled).
- **Localized**: the installer interface follows the Windows display language. Simplified Chinese is the
  fallback default; English, Traditional Chinese (HK/TW), German, Spanish, French, Japanese, Korean, and
  Brazilian Portuguese catalogs ship in `resources/strings/`.

## Two ways to consume it

### 1. Source mode (deep customization)

Add this repository as a source dependency (git submodule, FetchContent, mcpp package) and call
`huxerui_installer_add()` from `cmake/HuxerUIInstaller.cmake`. Without `UI_SOURCES` you get the default
generic interface; with it you compile your own — the equivalent of today's per-app
`package/src/app.cpp` (for example a custom logo panel). See [examples/source-mode](examples/source-mode).

### 2. Prebuilt mode (zero build)

Download `huxerui-installer-<version>-windows-x86_64.zip` from a release, supply `branding.json`, an
icon, a version, and the staged application payload, then run `scripts/make-setup.ps1` to produce
`<app>-<version>-windows-x86_64-setup.exe`. No C++ compilation involved. See
[examples/prebuilt-mode](examples/prebuilt-mode) and the callable workflow
[.github/workflows/reusable-package.yml](.github/workflows/reusable-package.yml).

Per-application identity (product name, logo, accent color, license text) is injected at run time: the
bundle carries `branding.json` and the referenced files as payloads, and the generic BA reads them from
its own directory when it starts. Upgrade codes, the install location, and shortcuts live in the WiX
templates, never in the BA binary.

## Repository layout

```
src/engine/installer_engine.cpp  # Burn BA engine (vendored from the HuxerUI SDK) + silent-mode driving
src/ui/branding.{h,cpp}          # branding.json runtime loading
src/ui/default_app.cpp           # default generic interface
src/main.cpp                     # wWinMain entry point
resources/strings/               # UI string catalogs: default.properties = 简体中文, en + translations
wix/Package.wxs.in               # parameterized MSI template
wix/Bundle.wxs.in                # parameterized bundle template (InstallFolder/CreateDesktopShortcut contract)
cmake/HuxerUIInstaller.cmake     # huxerui_installer_add() — source-mode entry point
scripts/Restore-Wix.ps1          # pinned WiX 5.0.2 nupkg restore with SHA256 verification
scripts/make-setup.ps1           # one-shot packaging — prebuilt-mode entry point
.github/workflows/release.yml    # builds and publishes the prebuilt zip on v*.*.* tags
.github/workflows/reusable-package.yml  # workflow_call packaging job for app repositories
```

## Building the prebuilt package locally

Requires Windows with the MSVC C++ x64 tools, a HuxerUI source checkout (or an installed SDK), and a
.NET 6+ runtime for `wix.exe`:

```powershell
git clone --depth 1 https://github.com/HuxerUI/HuxerUI.git huxerui-src
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DHUXERUI_SOURCE_DIR="$PWD/huxerui-src" `
    -DCMAKE_CXX_COMPILER=cl
cmake --build build --config Release
cmake --install build --config Release --component HuxerUIInstaller_huxerui_installer_ba --prefix out/ba
```

`-DHUXERUI_SOURCE_DIR` builds HuxerUI from source (what the release CI does); to use an installed SDK
instead, drop it and pass `-DCMAKE_PREFIX_PATH <huxerui-sdk-prefix>`.

WiX 5.0.2 restores automatically into `HUXERUI_WIX_ROOT` (default `<build>/.wix`) with SHA256
verification; the pins are shared by `cmake/HuxerUIInstaller.cmake` and `scripts/Restore-Wix.ps1`.

## Release naming

Follows the HuxerUI release conventions: tag `v<MAJOR.MINOR.PATCH>`, release title
`huxerui-installer v<version>`, assets `<product>-<version>-windows-x86_64.zip` without a leading `v`.

## Notes

- Code signing is out of scope for now; sign `*-setup.exe` with `signtool` after `make-setup.ps1` (sign
  the BA executable before bundling for a fully signed bundle).
- Verification happens on Windows CI runners; the WiX build cannot run on Linux/macOS.
- The HuxerUI SDK's own `windows_installer.cpp` remains the default for SDK-generated projects; this
  repository's engine adds the silent-mode driving and is where installer work converges.

## License

MIT. See [LICENSE](LICENSE). The vendored engine derives from the HuxerUI SDK (Apache-2.0) and links the
WiX toolset's bootstrapper API (MS-RL).
