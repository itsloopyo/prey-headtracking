#!/usr/bin/env pwsh
#Requires -Version 5.1
# Print the PE fingerprint of a PreyDll.dll as a paste-ready build-profile stub.
# First thing to run when a user reports the "staying dormant" log line, and the
# first step of a re-derive after a Prey patch.
#
#   pixi run check-fingerprint                 # every install on this machine
#   pixi run check-fingerprint <path-to-dll>
#
# With no argument it reports EVERY copy of the game it can find, not the first.
# Owning Prey on Steam and on Game Pass at once is ordinary, the two are
# different binaries with different RVAs, and a single-install report is how a
# session ends up deriving one profile and believing it covers both.

param(
    [Parameter(Position = 0)]
    [string]$DllPath
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Kept in step with kRefusedBuilds in src/PreyHeadTracking/mods/BuildProfile.cpp,
# which is the source of truth. Prey: Mooncrash / Typhon Hunter ships its own
# PreyDll.dll, once per store. Typhon Hunter is multiplayer, so the mod refuses
# those binaries on purpose - printing a profile stub for one would invite
# exactly the change the refusal exists to prevent.
$RefusedBuilds = @(
    @{ TimeDateStamp = 0x5D2352B3; SizeOfImage = 0x02FB9000; Store = 'Steam' }
    @{ TimeDateStamp = 0x6467AF55; SizeOfImage = 0x02DD0000; Store = 'Game Pass' }
)

# Likewise kept in step with kKnownProfiles. Without this the script prints a
# "these RVAs are placeholders, re-derive them" stub for a build that already
# has a complete profile, which is the opposite of the truth.
$KnownProfiles = @(
    @{ TimeDateStamp = 0x5D1CB240; SizeOfImage = 0x02E1D000; Name = 'steam-win64-20190703' }
    @{ TimeDateStamp = 0x64679C11; SizeOfImage = 0x02C51000; Name = 'gdk-win64-20230519' }
)

function Get-PeFingerprint {
    param([Parameter(Mandatory)][string]$Path)

    $bytes = [System.IO.File]::ReadAllBytes($Path)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ([System.Text.Encoding]::ASCII.GetString($bytes, $peOffset, 4) -ne "PE`0`0") {
        throw "Not a PE image: $Path"
    }
    # COFF header: TimeDateStamp at +8. Optional header starts at +24; SizeOfImage
    # and CheckSum sit at +56 and +64 within it (same offsets for PE32 and PE32+).
    $optional = $peOffset + 24
    return @{
        TimeDateStamp = [BitConverter]::ToUInt32($bytes, $peOffset + 8)
        SizeOfImage   = [BitConverter]::ToUInt32($bytes, $optional + 56)
        CheckSum      = [BitConverter]::ToUInt32($bytes, $optional + 64)
    }
}

function Write-Fingerprint {
    param(
        [Parameter(Mandatory)][string]$Path,
        [Parameter(Mandatory)][string]$Store
    )

    $fp = Get-PeFingerprint -Path $Path
    $built = [DateTimeOffset]::FromUnixTimeSeconds($fp.TimeDateStamp).UtcDateTime

    Write-Host ""
    Write-Host "PreyDll.dll: $Path"
    Write-Host "  store: $Store"
    Write-Host ("  built (TimeDateStamp): {0:yyyy-MM-dd HH:mm:ss} UTC" -f $built)

    foreach ($refused in $RefusedBuilds) {
        if ($fp.TimeDateStamp -eq $refused.TimeDateStamp -and
            $fp.SizeOfImage -eq $refused.SizeOfImage) {
            Write-Host ""
            Write-Host "  This is the Prey: Mooncrash / Typhon Hunter build ($($refused.Store))." -ForegroundColor Yellow
            Write-Host "  Typhon Hunter is multiplayer. The mod refuses this build deliberately;"
            Write-Host "  see kRefusedBuilds in src/PreyHeadTracking/mods/BuildProfile.cpp."
            Write-Host "  No profile stub printed."
            Write-Host ""
            return
        }
    }

    foreach ($known in $KnownProfiles) {
        if ($fp.TimeDateStamp -eq $known.TimeDateStamp -and
            $fp.SizeOfImage -eq $known.SizeOfImage) {
            Write-Host ""
            Write-Host "  Recognised: profile $($known.Name)." -ForegroundColor Green
            Write-Host "  The mod engages on this build. Nothing to derive."
            Write-Host ""
            return
        }
    }

    # Which file the stub belongs in, and what the profile has to be called. A
    # store variant is its own binary with its own RVAs, so it gets its own file
    # and its own profile rather than being merged into another store's.
    $slug = if ($Store -eq 'Game Pass') { 'gdk' } else { $Store.ToLowerInvariant() }
    $stamp = $built.ToString('yyyyMMdd')

    Write-Host ""
    Write-Host "Paste into src/PreyHeadTracking/mods/${slug}_offsets.cpp and add it to the TOP"
    Write-Host "of kKnownProfiles in BuildProfile.cpp, leaving every existing profile in place."
    Write-Host "The RVAs below are placeholders and must be re-derived for this build:"
    Write-Host ""
    Write-Host ("    extern const BuildProfile k{0}Profile_{1} = {{" -f `
        (($slug.Substring(0,1).ToUpperInvariant()) + $slug.Substring(1)), $stamp)
    Write-Host ("        `"{0}-win64-{1}`", {{ 0x{2:X8}u, 0x{3:X8}u, 0x{4:X8}u }}," -f `
        $slug, $stamp, $fp.TimeDateStamp, $fp.SizeOfImage, $fp.CheckSum)
    Write-Host "        0x0u /* gEnv->pSystem */, 0x0u /* gEnv->bMultiplayer */,"
    Write-Host "        0x048u /* ISystem::Render */, 0x388u /* ISystem::GetViewCamera */,"
    Write-Host "        0x380u /* ISystem::SetViewCamera */, 0x0u /* matrix */, 0x0u /* UpdateFrustum */,"
    Write-Host "    };"
    Write-Host ""
    Write-Host "The zeroed entries keep the profile INCOMPLETE, so the mod stays dormant until"
    Write-Host "they are re-derived. See the Xbox section of .lab/NOTES.md for the routes that"
    Write-Host "work on a recompiled build - byte signatures do not carry across one."
    Write-Host ""
}

if ($DllPath) {
    if (-not (Test-Path -LiteralPath $DllPath)) { throw "Not found: $DllPath" }
    Write-Fingerprint -Path $DllPath -Store 'unknown (path given explicitly)'
    exit 0
}

$projectRoot = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Import-Module (Join-Path $projectRoot 'cameraunlock-core\powershell\GamePathDetection.psm1') -Force

$config = Get-GameConfig -GameId 'prey'
$installs = @(Find-AllGamePaths -GameId 'prey')
if ($installs.Count -eq 0) {
    throw "Prey not found. Pass the path to PreyDll.dll explicitly."
}

foreach ($install in $installs) {
    $isXbox = Test-IsXboxPath -Config $config -Path $install
    $store = if ($isXbox) { 'Game Pass' } else { 'Steam' }
    # The GDK build's exe - and so its PreyDll.dll - sits under a different
    # subtree than the Steam build's, which is why the relpath comes from the
    # config per install rather than being written out once.
    $exeRel = Get-GameExecutableRelPath -Config $config -Path $install
    $dll = Join-Path $install (Join-Path (Split-Path -Parent $exeRel) 'PreyDll.dll')
    if (-not (Test-Path -LiteralPath $dll)) {
        Write-Host ""
        Write-Host "PreyDll.dll missing under $install (expected $dll)" -ForegroundColor Yellow
        continue
    }
    Write-Fingerprint -Path $dll -Store $store
}
