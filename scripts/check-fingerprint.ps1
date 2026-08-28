#!/usr/bin/env pwsh
#Requires -Version 5.1
# Print the PE fingerprint of a PreyDll.dll as a paste-ready build-profile stub.
# First thing to run when a user reports the "staying dormant" log line, and the
# first step of a re-derive after a Prey patch.
#
#   pixi run check-fingerprint                 # auto-detect the Steam install
#   pixi run check-fingerprint <path-to-dll>

param(
    [Parameter(Position = 0)]
    [string]$DllPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not $DllPath) {
    $projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
    Import-Module (Join-Path $projectRoot 'cameraunlock-core\powershell\GamePathDetection.psm1') -Force
    $gamePath = Find-GamePath -GameId 'prey'
    if (-not $gamePath) {
        throw "Prey not found. Pass the path to PreyDll.dll explicitly."
    }
    $DllPath = Join-Path $gamePath 'Binaries\Danielle\x64\Release\PreyDll.dll'
}

if (-not (Test-Path $DllPath)) { throw "Not found: $DllPath" }

$bytes = [System.IO.File]::ReadAllBytes($DllPath)
$peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
if ([System.Text.Encoding]::ASCII.GetString($bytes, $peOffset, 4) -ne "PE`0`0") {
    throw "Not a PE image: $DllPath"
}
# COFF header: TimeDateStamp at +8. Optional header starts at +24; SizeOfImage
# and CheckSum sit at +56 and +64 within it (same offsets for PE32 and PE32+).
$timeDateStamp = [BitConverter]::ToUInt32($bytes, $peOffset + 8)
$optional      = $peOffset + 24
$sizeOfImage   = [BitConverter]::ToUInt32($bytes, $optional + 56)
$checkSum      = [BitConverter]::ToUInt32($bytes, $optional + 64)

$built = [DateTimeOffset]::FromUnixTimeSeconds($timeDateStamp).UtcDateTime
$stamp = $built.ToString('yyyyMMdd')

# Prey: Mooncrash / Typhon Hunter ships its own PreyDll.dll. Typhon Hunter is
# multiplayer, so the mod refuses that binary on purpose - printing a profile
# stub for it would invite exactly the change the refusal exists to prevent.
if ($timeDateStamp -eq 0x5D2352B3 -and $sizeOfImage -eq 0x02FB9000) {
    Write-Host ""
    Write-Host "This is the Prey: Mooncrash / Typhon Hunter PreyDll.dll." -ForegroundColor Yellow
    Write-Host "Typhon Hunter is multiplayer. The mod refuses this build deliberately;"
    Write-Host "see kRefusedBuilds in src/PreyHeadTracking/mods/BuildProfile.cpp."
    Write-Host "No profile stub printed."
    Write-Host ""
    exit 0
}

Write-Host ""
Write-Host "PreyDll.dll: $DllPath"
Write-Host ("  built (TimeDateStamp): {0:yyyy-MM-dd HH:mm:ss} UTC" -f $built)
Write-Host ""
Write-Host "Paste into kKnownProfiles in src/PreyHeadTracking/mods/BuildProfile.cpp,"
Write-Host "at the TOP of the array, leaving every existing profile in place. The RVAs"
Write-Host "below are placeholders and must be re-derived for this build:"
Write-Host ""
Write-Host ("    {{ `"steam-win64-{0}`", {{ 0x{1:X8}u, 0x{2:X8}u, 0x{3:X8}u }}," -f `
    $stamp, $timeDateStamp, $sizeOfImage, $checkSum)
Write-Host "      0x0u /* gEnv->pSystem */, 0x0u /* gEnv->bMultiplayer */,"
Write-Host "      0x048u /* ISystem::Render */, 0x388u /* ISystem::GetViewCamera */, 0x0u },"
Write-Host ""
Write-Host "The zeroed entries keep the profile INCOMPLETE, so the mod stays dormant until"
Write-Host "they are re-derived. Fill them in, then verify in game before shipping."
Write-Host ""
