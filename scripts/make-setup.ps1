# make-setup.ps1 — prebuilt-mode packaging entry point of huxerui-installer.
#
# Turns a staged application payload plus a branding.json into a Burn bundle setup executable without
# compiling any C++:
#
#   1. Restore the pinned WiX v5.0.2 toolset (Restore-Wix.ps1, SHA256-verified).
#   2. Render the parameterized wix/Package.wxs.in and wix/Bundle.wxs.in templates.
#   3. wix build the MSI over the application payload.
#   4. Generate the HuxerUIInstallerPayloads fragment from the BA staging directory (branding.json, logo,
#      license, resources, mbanative.dll ride beside the BA executable).
#   5. wix build the bundle into <AppName>-<Version>-windows-x86_64-setup.exe.
#
# Typical use from an app repository CI:
#   ./make-setup.ps1 -AppName niki -AppId dev.niki -Version 1.2.0 `
#       -ApplicationDir ./dist/windows -AppExe niki.exe -Icon ./packaging/app.ico `
#       -BrandingJson ./packaging/branding.json -Publisher "niki authors"
#
# Upgrade codes derive deterministically from -AppId (RFC 4122 name-based SHA1 UUIDs, matching CMake's
# string(UUID ... TYPE SHA1)), so repeated runs produce stable upgrade identity without storing GUIDs.
# Pass -MsiUpgradeCode/-BundleUpgradeCode to pin explicit values instead.

[CmdletBinding()]
param(
    # Display name of the application (also the MSI file base name and install directory name).
    [Parameter(Mandatory = $true)][string]$AppName,
    # Stable reverse-domain application id; seeds the derived upgrade codes and the registry key.
    [Parameter(Mandatory = $true)][string]$AppId,
    # Semantic version without a leading v, for example 1.2.0.
    [Parameter(Mandatory = $true)][string]$Version,
    # Directory whose contents become the installed files (the application payload).
    [Parameter(Mandatory = $true)][string]$ApplicationDir,
    # Application executable file name inside ApplicationDir, used for shortcuts.
    [Parameter(Mandatory = $true)][string]$AppExe,
    # Path to the application .ico used by the bundle, the MSI, and the shortcuts.
    [Parameter(Mandatory = $true)][string]$Icon,
    # Path to branding.json; sibling files it references (logo, license) are picked up with their
    # relative directory structure preserved.
    [string]$BrandingJson,
    # Manufacturer shown by the bundle and MSI. Defaults to the branding publisher, then AppId.
    [string]$Publisher,
    # Directory with the prebuilt bootstrapper application: huxerui-installer-ba.exe, mbanative.dll and
    # the *.resources directory from the huxerui-installer release package. Defaults to ../ba relative
    # to this script.
    [string]$BaDir,
    # Explicit upgrade codes; both default to UUIDs derived from AppId.
    [string]$MsiUpgradeCode,
    [string]$BundleUpgradeCode,
    # Output path of the setup executable. Defaults to ./<AppName>-<Version>-windows-x86_64-setup.exe.
    [string]$Output,
    # WiX restore root forwarded to Restore-Wix.ps1.
    [string]$WixRoot,
    # Build scratch directory. Defaults to make-setup-work next to the output.
    [string]$WorkDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not ($Version -match '^\d+\.\d+\.\d+$')) {
    throw "make-setup: -Version must be MAJOR.MINOR.PATCH without a leading v, got '$Version'"
}

$ScriptDir = $PSScriptRoot
$PackageRoot = Split-Path -Parent $ScriptDir
if (-not $BaDir) {
    $BaDir = Join-Path $PackageRoot 'ba'
}
$BaExe = Join-Path $BaDir 'huxerui-installer-ba.exe'
foreach ($Required in @($BaExe, (Join-Path $BaDir 'mbanative.dll'))) {
    if (-not (Test-Path -LiteralPath $Required -PathType Leaf)) {
        throw "make-setup: prebuilt bootstrapper application is incomplete; missing $Required"
    }
}
if (-not (Test-Path -LiteralPath $ApplicationDir -PathType Container)) {
    throw "make-setup: -ApplicationDir does not exist: $ApplicationDir"
}
if (-not (Test-Path -LiteralPath (Join-Path $ApplicationDir $AppExe) -PathType Leaf)) {
    throw "make-setup: -AppExe '$AppExe' not found inside $ApplicationDir"
}
if (-not (Test-Path -LiteralPath $Icon -PathType Leaf)) {
    throw "make-setup: -Icon does not exist: $Icon"
}

# ---------------------------------------------------------------- branding ----
$Branding = @{}
if ($BrandingJson) {
    if (-not (Test-Path -LiteralPath $BrandingJson -PathType Leaf)) {
        throw "make-setup: -BrandingJson does not exist: $BrandingJson"
    }
    $Branding = Get-Content -LiteralPath $BrandingJson -Raw | ConvertFrom-Json -AsHashtable
}
if (-not $Publisher) {
    $Publisher = $Branding.publisher
}
if (-not $Publisher) {
    $Publisher = $AppId
}

# ------------------------------------------------------------- upgrade ids ----
function ConvertTo-NameGuid([string]$Name) {
    # RFC 4122 version 5 (SHA1) UUID over the ISO/IEC 11578 URL-adjacent namespace used by CMake's
    # string(UUID ... TYPE SHA1): 6ba7b810-9dad-11d1-80b4-00c04fd430c8.
    $Namespace = [byte[]](0x6b, 0xa7, 0xb8, 0x10, 0x9d, 0xad, 0x11, 0xd1,
                          0x80, 0xb4, 0x00, 0xc0, 0x4f, 0xd4, 0x30, 0xc8)
    $NameBytes = [Text.Encoding]::UTF8.GetBytes($Name)
    $Hash = [Security.Cryptography.SHA1]::Create().ComputeHash([byte[]]($Namespace + $NameBytes))
    $Guid = [byte[]]$Hash[0..15]
    $Guid[6] = ($Guid[6] -band 0x0F) -bor 0x50  # version 5
    $Guid[8] = ($Guid[8] -band 0x3F) -bor 0x80  # RFC 4122 variant
    # Format in RFC 4122 network byte order (NOT [Guid]::new([byte[]]), which is little-endian) so the
    # result matches CMake's string(UUID ... TYPE SHA1) for the same name.
    $Hex = -join ($Guid | ForEach-Object { $_.ToString('x2') })
    return ("{0}-{1}-{2}-{3}-{4}" -f $Hex.Substring(0, 8), $Hex.Substring(8, 4), $Hex.Substring(12, 4),
            $Hex.Substring(16, 4), $Hex.Substring(20, 12)).ToUpperInvariant()
}

if (-not $MsiUpgradeCode) {
    $MsiUpgradeCode = ConvertTo-NameGuid "$AppId.msi"
}
if (-not $BundleUpgradeCode) {
    $BundleUpgradeCode = ConvertTo-NameGuid "$AppId.bundle"
}

# ------------------------------------------------------------------ layout ----
if (-not $Output) {
    $Output = Join-Path (Get-Location) "$AppName-$Version-windows-x86_64-setup.exe"
}
$Output = [System.IO.Path]::GetFullPath($Output)
if (-not $WorkDir) {
    $WorkDir = Join-Path (Split-Path -Parent $Output) 'make-setup-work'
}
$ProjectDir = Join-Path $WorkDir 'project'    # !(bindpath.Project): icon and other project files
$InstallerStage = Join-Path $WorkDir 'installer'  # !(bindpath.Installer): BA executable + payloads
$WixSourceDir = Join-Path $WorkDir 'wxs'
$MsiPath = Join-Path $WorkDir "$AppName.msi"
$PayloadFragment = Join-Path $WorkDir 'installer-payloads.wxs'

if (Test-Path -LiteralPath $WorkDir) {
    Remove-Item -LiteralPath $WorkDir -Recurse -Force
}
New-Item -ItemType Directory -Path $ProjectDir, $InstallerStage, $WixSourceDir -Force | Out-Null

$IconFile = Split-Path -Leaf $Icon
Copy-Item -LiteralPath $Icon -Destination (Join-Path $ProjectDir $IconFile)

Copy-Item -Path (Join-Path $BaDir '*') -Destination $InstallerStage -Recurse
if ($BrandingJson) {
    # branding.json at the BA executable root; referenced payloads (logoPath, licenseFile) keep their
    # relative paths so LoadBranding() resolves them next to the executable.
    $BrandingDir = Split-Path -Parent (Resolve-Path -LiteralPath $BrandingJson)
    Copy-Item -LiteralPath $BrandingJson -Destination (Join-Path $InstallerStage 'branding.json')
    foreach ($Key in 'logoPath', 'licenseFile') {
        $Relative = $Branding[$Key]
        if ($Relative) {
            $Source = Join-Path $BrandingDir ($Relative -replace '\\', [IO.Path]::DirectorySeparatorChar)
            $Target = Join-Path $InstallerStage ($Relative -replace '\\', [IO.Path]::DirectorySeparatorChar)
            if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
                throw "make-setup: branding.$Key points at a missing file: $Source"
            }
            New-Item -ItemType Directory -Path (Split-Path -Parent $Target) -Force | Out-Null
            Copy-Item -LiteralPath $Source -Destination $Target
        }
    }
}

# ---------------------------------------------------------------- templates ---
function ConvertTo-WixXml([string]$Value) {
    return $Value -replace '&', '&amp;' -replace '<', '&lt;' -replace '>', '&gt;' -replace '"', '&quot;'
}

$Tokens = @{
    '@@APP_NAME@@'            = ConvertTo-WixXml $AppName
    '@@MANUFACTURER@@'        = ConvertTo-WixXml $Publisher
    '@@APP_VERSION@@'         = $Version
    '@@APP_ID@@'              = ConvertTo-WixXml $AppId
    '@@APP_EXE@@'             = ConvertTo-WixXml $AppExe
    '@@MSI_UPGRADE_CODE@@'    = $MsiUpgradeCode
    '@@BUNDLE_UPGRADE_CODE@@' = $BundleUpgradeCode
    '@@ICON_FILE@@'           = ConvertTo-WixXml $IconFile
    '@@INSTALL_DIR_NAME@@'    = ConvertTo-WixXml $AppName
    '@@BA_EXE@@'              = Split-Path -Leaf $BaExe
}
foreach ($Template in 'Package.wxs.in', 'Bundle.wxs.in') {
    $Content = Get-Content -LiteralPath (Join-Path $PackageRoot "wix/$Template") -Raw
    foreach ($Token in $Tokens.GetEnumerator()) {
        $Content = $Content.Replace($Token.Key, $Token.Value)
    }
    if ($Content -match '@@') {
        throw "make-setup: unresolved @@TOKEN@@ left in $Template"
    }
    [IO.File]::WriteAllText((Join-Path $WixSourceDir ($Template -replace '\.in$', '')), $Content)
}

# -------------------------------------------------------------------- WiX -----
$RestoreArgs = @{}
if ($WixRoot) {
    $RestoreArgs.OutDir = $WixRoot
}
$Wix = & (Join-Path $ScriptDir 'Restore-Wix.ps1') @RestoreArgs

Write-Host "Building $AppName.msi"
& $Wix.WixExe build (Join-Path $WixSourceDir 'Package.wxs') -arch x64 `
    -bindpath "Application=$ApplicationDir" -bindpath "Project=$ProjectDir" `
    -out $MsiPath
if ($LASTEXITCODE -ne 0) {
    throw "make-setup: wix build Package.wxs failed with exit code $LASTEXITCODE"
}

# Payload fragment: every BA staging file except the BA executable itself rides as a bundle payload
# (mirrors HuxerUI's cmake/HuxerUIGenerateWixPayloads.cmake; the PayloadGroup id is a contract with
# Bundle.wxs.in).
$PayloadLines = [System.Collections.Generic.List[string]]@()
$PayloadLines.Add('<Wix xmlns="http://wixtoolset.org/schemas/v4/wxs">')
$PayloadLines.Add('  <Fragment>')
$PayloadLines.Add('    <PayloadGroup Id="HuxerUIInstallerPayloads">')
$BaExeStaged = [System.IO.Path]::GetFullPath((Join-Path $InstallerStage (Split-Path -Leaf $BaExe)))
$PayloadFiles = Get-ChildItem -LiteralPath $InstallerStage -Recurse -File |
    Where-Object { $_.FullName -ne $BaExeStaged } |
    Sort-Object { $_.FullName.Substring($InstallerStage.Length + 1) }
foreach ($File in $PayloadFiles) {
    $Relative = $File.FullName.Substring($InstallerStage.Length + 1)
    $Source = ConvertTo-WixXml $File.FullName
    $Name = ConvertTo-WixXml $Relative
    $PayloadLines.Add("      <Payload SourceFile=`"$Source`" Name=`"$Name`" />")
}
$PayloadLines.Add('    </PayloadGroup>')
$PayloadLines.Add('  </Fragment>')
$PayloadLines.Add('</Wix>')
[IO.File]::WriteAllLines($PayloadFragment, $PayloadLines)

Write-Host "Building $(Split-Path -Leaf $Output)"
& $Wix.WixExe build (Join-Path $WixSourceDir 'Bundle.wxs') $PayloadFragment -arch x64 `
    -bindpath "Installer=$InstallerStage" -bindpath "Package=$WorkDir" -bindpath "Project=$ProjectDir" `
    -out $Output
if ($LASTEXITCODE -ne 0) {
    throw "make-setup: wix build Bundle.wxs failed with exit code $LASTEXITCODE"
}
if (-not (Test-Path -LiteralPath $Output -PathType Leaf)) {
    throw "make-setup: wix reported success but the bundle is missing: $Output"
}

[PSCustomObject]@{
    Setup             = $Output
    Msi               = $MsiPath
    MsiUpgradeCode    = $MsiUpgradeCode
    BundleUpgradeCode = $BundleUpgradeCode
}
