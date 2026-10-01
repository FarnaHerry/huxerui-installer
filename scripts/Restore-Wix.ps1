# Restore-Wix.ps1 — restore the pinned WiX v5.0.2 toolset from nuget.org with
# SHA256 verification. PowerShell port of the restore logic in
# HuxerUI/cmake/HuxerUIWindowsInstaller.cmake (lines 3-114); package ids,
# versions and hashes MUST stay in sync with that file.
#
# Layout produced under -OutDir:
#   tool/tools/net6.0/any/wix.exe
#   bootstrapper/build/native/include/BootstrapperApplication.h
#   bootstrapper/build/native/v14/x64/balutil.lib
#   bootstrapper/runtimes/win-x64/native/mbanative.dll
#   dutil/build/native/include/dutil.h
#   dutil/build/native/v14/x64/dutil.lib
#   downloads/<package>.5.0.2.nupkg        (download cache)
#
# The script emits a single PSCustomObject with the resolved paths so callers
# can do:  $wix = & ./Restore-Wix.ps1 -OutDir .wix; & $wix.WixExe ...

[CmdletBinding()]
param(
    # Restore root. Defaults to $env:HUXERUI_INSTALLER_WIX_ROOT, then to a
    # ".wix" directory next to the repository root (i.e. ../.wix relative to
    # this script).
    [string]$OutDir
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $OutDir) {
    $OutDir = $env:HUXERUI_INSTALLER_WIX_ROOT
}
if (-not $OutDir) {
    $OutDir = Join-Path (Split-Path -Parent $PSScriptRoot) '.wix'
}
$OutDir = [System.IO.Path]::GetFullPath($OutDir)

$WixVersion = '5.0.2'
$Packages = @(
    @{
        Name     = 'wix'
        Sha256   = 'f30ef0c74e2a986126539c5780be93ac24e8136eaf723b1937b26272703ae173'
        Dest     = 'tool'
        Required = @('tools/net6.0/any/wix.exe')
    },
    @{
        Name     = 'WixToolset.BootstrapperApplicationApi'
        Sha256   = '6e0d3c68a68dcedde4a3a68de896f124a7b19c4a823fac49856e2ee77cb16256'
        Dest     = 'bootstrapper'
        Required = @(
            'build/native/include/BootstrapperApplication.h',
            'build/native/v14/x64/balutil.lib',
            'runtimes/win-x64/native/mbanative.dll'
        )
    },
    @{
        Name     = 'WixToolset.DUtil'
        Sha256   = 'aa4f0668044318820e6c31ffef9f4141830c9fd8ebbe038281329423916547fe'
        Dest     = 'dutil'
        Required = @(
            'build/native/include/dutil.h',
            'build/native/v14/x64/dutil.lib'
        )
    }
)

function Test-PackageComplete([string]$Destination, [string[]]$Required) {
    foreach ($File in $Required) {
        if (-not (Test-Path -LiteralPath (Join-Path $Destination ($File -replace '/', [IO.Path]::DirectorySeparatorChar)) -PathType Leaf)) {
            return $false
        }
    }
    return $true
}

function Restore-WixPackage([hashtable]$Package) {
    $Destination = Join-Path $OutDir $Package.Dest
    if (Test-PackageComplete $Destination $Package.Required) {
        Write-Host "WiX package '$($Package.Name)' already restored at $Destination"
        return
    }

    $PackageId = $Package.Name.ToLowerInvariant()
    $DownloadDir = Join-Path $OutDir 'downloads'
    New-Item -ItemType Directory -Path $DownloadDir -Force | Out-Null
    $Archive = Join-Path $DownloadDir "$PackageId.$WixVersion.nupkg"
    $Url = "https://api.nuget.org/v3-flatcontainer/$PackageId/$WixVersion/$PackageId.$WixVersion.nupkg"

    if (-not (Test-Path -LiteralPath $Archive -PathType Leaf) -or
        (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash -ne $Package.Sha256) {
        Write-Host "Downloading $Url"
        Invoke-WebRequest -Uri $Url -OutFile $Archive -UseBasicParsing
    }
    $ActualHash = (Get-FileHash -LiteralPath $Archive -Algorithm SHA256).Hash
    if ($ActualHash -ne $Package.Sha256) {
        Remove-Item -LiteralPath $Archive -Force
        throw "Restore-Wix: SHA256 mismatch for $($Package.Name) $WixVersion (expected $($Package.Sha256), got $ActualHash)"
    }

    if (Test-Path -LiteralPath $Destination) {
        Remove-Item -LiteralPath $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($Archive, $Destination)

    if (-not (Test-PackageComplete $Destination $Package.Required)) {
        throw "Restore-Wix: restored $($Package.Name) package is incomplete at $Destination"
    }
}

foreach ($Package in $Packages) {
    Restore-WixPackage $Package
}

$WixExe = Join-Path $OutDir 'tool/tools/net6.0/any/wix.exe'
try {
    $WixVersionOutput = & $WixExe --version 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "wix.exe --version exited with $LASTEXITCODE`: $WixVersionOutput"
    }
} catch {
    throw "Restore-Wix: cannot run wix.exe. WiX v5 requires the Microsoft.NETCore.App 6.0 or newer runtime: $_"
}

[PSCustomObject]@{
    Root         = $OutDir
    WixExe       = $WixExe
    BalInclude   = Join-Path $OutDir 'bootstrapper/build/native/include'
    BalUtilLib   = Join-Path $OutDir 'bootstrapper/build/native/v14/x64/balutil.lib'
    MbaNativeDll = Join-Path $OutDir 'bootstrapper/runtimes/win-x64/native/mbanative.dll'
    DUtilInclude = Join-Path $OutDir 'dutil/build/native/include'
    DUtilLib     = Join-Path $OutDir 'dutil/build/native/v14/x64/dutil.lib'
}
