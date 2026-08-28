#!/usr/bin/env pwsh
#Requires -Version 5.1
# Dev deploy: copy the freshly built PreyHeadTracking.asi + HeadTracking.ini
# into a detected Prey install. Game-path detection (env var -> Steam ->
# games.json -> positional arg) matches install.cmd via GamePathDetection.psm1.
# No prompts; exits non-zero if detection or the build artifact is missing.

param(
    [Parameter(Position=0)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [Parameter(Position=1)]
    [string]$GivenPath,
    [Parameter(ValueFromRemainingArguments=$true)]
    [string[]]$RemainingArgs
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$ProgressPreference = 'SilentlyContinue'

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectRoot = Split-Path -Parent $scriptDir

Import-Module (Join-Path $projectRoot "cameraunlock-core\powershell\DevDeploy.psm1") -Force
Import-Module (Join-Path $projectRoot "cameraunlock-core\powershell\ModDeployment.psm1") -Force

$buildOutput  = Join-Path $projectRoot "build\src\PreyHeadTracking\$Configuration"
$configFile   = Join-Path $projectRoot 'HeadTracking.ini'
$vendorLoader = Join-Path $projectRoot 'vendor\ultimate-asi-loader\dinput8.dll'

$result = Invoke-DevDeployASILoader `
    -GameId 'prey' `
    -GameDisplayName 'Prey' `
    -BuildOutputPath $buildOutput `
    -ModDllName 'PreyHeadTracking.asi' `
    -ConfigFile $configFile `
    -VendorLoaderDll $vendorLoader `
    -AsiLoaderName 'dinput8.dll' `
    -ExtraDlls @() `
    -GivenPath $GivenPath

Write-DeploymentSuccess `
    -ModName "Prey Head Tracking" `
    -DeployPath $result.DeployedDllPath `
    -Controls @(
        "End       - Toggle head tracking on/off",
        "Page Up   - Cycle DOF mode (6DOF / rotation-only / position-only)",
        "Page Down - Toggle yaw mode (world / local)",
        "",
        "No nav cluster? Chords: Ctrl+Shift+ Y=Toggle G=DOF mode H=Yaw mode"
    )
