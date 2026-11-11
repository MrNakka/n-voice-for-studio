<#
.SYNOPSIS
    Installs or updates N Voice for Studio for OBS Studio 33 and later.

.DESCRIPTION
    Copies the plugin into C:\ProgramData\obs-studio\plugins\n-voice-for-studio
    using the OBS 33 layout (the .dll next to the data folder).

    The existing plugin folder is replaced as a whole, so files dropped in a
    newer version and the old "bin\64bit" (Legacy) layout do not linger.
    User settings live in %APPDATA%\obs-studio\plugin_config and are kept.

.PARAMETER Source
    The plugin folder (containing n-voice-for-studio.dll and data\) or a
    release .zip downloaded from GitHub. Defaults to the local build output
    release\RelWithDebInfo\n-voice-for-studio.

.EXAMPLE
    .\tools\install-windows.ps1
    Installs the plugin built locally with "cmake --install".

.EXAMPLE
    .\tools\install-windows.ps1 -Source $HOME\Downloads\n-voice-for-studio-0.1.1-windows-x64.zip
    Installs a release zip.
#>
param(
    [string] $Source
)

$ErrorActionPreference = 'Stop'
$PluginName = 'n-voice-for-studio'
$ProjectRoot = Split-Path -Parent $PSScriptRoot

if (-not $Source) {
    $Source = Join-Path $ProjectRoot "release\RelWithDebInfo\$PluginName"
}

if (Get-Process -Name 'obs64' -ErrorAction SilentlyContinue) {
    throw 'OBS Studio is running. Close OBS and run this script again.'
}

$TempDir = $null
try {
    if ($Source -like '*.zip') {
        $TempDir = Join-Path ([IO.Path]::GetTempPath()) "nvoice-install-$([Guid]::NewGuid())"
        Expand-Archive -Path $Source -DestinationPath $TempDir
        $Source = Join-Path $TempDir $PluginName
    }

    if (-not (Test-Path (Join-Path $Source "$PluginName.dll"))) {
        throw "$PluginName.dll was not found in '$Source'."
    }

    $Destination = Join-Path $env:ProgramData "obs-studio\plugins\$PluginName"

    if (Test-Path $Destination) {
        Remove-Item -Path $Destination -Recurse -Force
    }
    New-Item -ItemType Directory -Path $Destination -Force | Out-Null
    Copy-Item -Path (Join-Path $Source '*') -Destination $Destination -Recurse -Force

    $Manifest = Join-Path $Destination 'data\manifest.json'
    $Version = if (Test-Path $Manifest) { (Get-Content $Manifest -Raw | ConvertFrom-Json).version } else { 'unknown' }
    Write-Host "Installed N Voice for Studio $Version to $Destination"
} finally {
    if ($TempDir -and (Test-Path $TempDir)) {
        Remove-Item -Path $TempDir -Recurse -Force
    }
}
