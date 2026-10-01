# Prebuilt mode example

Zero-build packaging: download a `huxerui-installer-<version>-windows-x86_64.zip` release, stage your
application payload, and run `make-setup.exe`. Nothing is compiled and no PowerShell is involved — the
WiX toolset ships inside the package (a .NET 6+ runtime is required for `wix.exe`).

```powershell
# 1. Unpack the release package; it contains make-setup.exe, ba/, wix/, wix-toolset/, and scripts/.
Expand-Archive huxerui-installer-0.1.0-windows-x86_64.zip -DestinationPath .

# 2. Stage the application payload — exactly the files that should be installed.
#    dist/windows/myapp.exe, dist/windows/*.dll, dist/windows/myapp.resources/...

# 3. Build the setup executable.
./huxerui-installer-0.1.0-windows-x86_64/make-setup.exe `
    --app-name "My App" --app-id "com.example.myapp" --version 1.2.0 `
    --app-dir ./dist/windows --app-exe myapp.exe `
    --icon ./app.ico --branding ./branding.json

# Produces ./My App-1.2.0-windows-x86_64-setup.exe
```

`scripts/make-setup.ps1` remains available with the same behavior (it restores WiX from nuget.org on
first use); CI pipelines that already drive PowerShell can keep using it — both entry points derive the
same AppId-based upgrade codes and produce equivalent bundles.

This directory holds a sample `branding.json` plus the `branding/` payload directory it references:

- `branding.json` — every key is optional; delete keys rather than leaving placeholders.
- `branding/logo.png` — referenced by `logoPath`; shown on the brand panel (PNG or JPEG).
- `branding/license.txt` — referenced by `licenseFile`; shown on a license step with an accept checkbox.

Paths in `branding.json` are relative to the bootstrapper executable's directory at install time; keep
them under a subdirectory such as `branding\` so they do not collide with the BA's own files. Windows
path separators in the JSON values are conventional (`branding\\logo.png`).

The produced bundle already supports unattended installs:

```
setup.exe /quiet /norestart InstallFolder=D:\Tools\MyApp CreateDesktopShortcut=1
```

Exit codes: `0` success, `3010` restart required, `1602` canceled — ready for a winget manifest whose
`InstallSwitches` are `/quiet /norestart`.
