#!/usr/bin/env pwsh
#Requires -Version 5.1
<#
.SYNOPSIS
    Automated, unattended release workflow for Prey Head Tracking.

.DESCRIPTION
    Runs end to end with no operator interaction. The command-line
    invocation is the authorization; there is no confirmation gate. The
    canonical version lives in CMakeLists.txt (project() VERSION). This
    script bumps it, builds, regenerates the changelog, commits, tags, and
    pushes. Any failed precondition exits non-zero with a one-line reason.

.PARAMETER Version
    major | minor | patch, or a literal X.Y.Z. Omit to print the current
    version and usage, then exit 0.

.NOTES
    Run via: pixi run release <major|minor|patch|nightly|X.Y.Z>
#>
param(
    [Parameter(Position=0)]
    [string]$Version = "",
    # Ship a release even when there are no user-facing commits since the
    # last tag (writes a maintenance changelog entry instead of aborting).
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir
$cmakeListsPath = Join-Path $projectDir "CMakeLists.txt"
$installCmdPath = Join-Path $scriptDir "install.cmd"
$changelogPath = Join-Path $projectDir "CHANGELOG.md"

Import-Module (Join-Path $projectDir "cameraunlock-core\powershell\ReleaseWorkflow.psm1") -Force

# Mirrors New-ChangelogFromCommits' insertion so a -Force maintenance entry
# lands in the same place with the same shape.
function Add-MaintenanceChangelogEntry {
    param([string]$Path, [string]$NewVersion)
    $date = Get-Date -Format 'yyyy-MM-dd'
    $entry = "## [$NewVersion] - $date`n`n### Changed`n`n- Maintenance release (no user-facing changes).`n`n"
    $changelog = Get-Content $Path -Raw
    if ($changelog -match '(?s)(# Changelog.*?)(## \[)') {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n\n)', "`$1$entry"
    } else {
        $changelog = $changelog -replace '(?s)(# Changelog.*?\n)', "`$1$entry"
    }
    $changelog = $changelog.TrimEnd() + "`n"
    Set-Content $Path $changelog -NoNewline
}

$VersionRegex = 'project\(\s*PreyHeadTracking\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)'

function Get-CurrentVersion {
    $content = Get-Content $cmakeListsPath -Raw
    if ($content -notmatch $VersionRegex) {
        throw "Could not parse VERSION from CMakeLists.txt project() declaration."
    }
    return $matches[1]
}

function Set-CurrentVersion {
    param([string]$NewVersion)
    $content = Get-Content $cmakeListsPath -Raw
    $content = $content -replace '(project\(\s*PreyHeadTracking\s+VERSION\s+)[0-9]+\.[0-9]+\.[0-9]+', "`${1}$NewVersion"
    $content | Set-Content $cmakeListsPath -NoNewline
}

Write-Host "=== Prey Head Tracking Release ===" -ForegroundColor Cyan
Write-Host ""

$currentVersion = Get-CurrentVersion

if ([string]::IsNullOrWhiteSpace($Version)) {
    Write-Host "Current version: $currentVersion" -ForegroundColor White
    Write-Host "Usage: pixi run release <major|minor|patch|nightly|X.Y.Z>" -ForegroundColor Yellow
    exit 0
}

if ($Version -eq 'nightly') {
    & (Join-Path $PSScriptRoot 'release-nightly.ps1')
    exit $LASTEXITCODE
}

try {
    $Version = Resolve-ReleaseVersion -Argument $Version -CurrentVersion $currentVersion
} catch {
    Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
    exit 1
}

if (-not (Test-SemanticVersion $Version)) {
    Write-Host "Error: '$Version' is not a valid X.Y.Z version." -ForegroundColor Red
    exit 1
}

$tagName = "v$Version"

$currentBranch = git rev-parse --abbrev-ref HEAD
if ($currentBranch -ne "main") {
    Write-Host "Error: Must be on 'main' branch to release (currently on '$currentBranch')." -ForegroundColor Red
    exit 1
}

if (-not (Test-CleanGitStatus)) {
    Write-Host "Error: Working tree has uncommitted changes. Commit or stash first." -ForegroundColor Red
    exit 1
}

if (Test-GitTagExists $tagName) {
    Write-Host "Error: Tag '$tagName' already exists." -ForegroundColor Red
    exit 1
}

Write-Host "Current version: $currentVersion" -ForegroundColor Gray
Write-Host "New version:     $Version" -ForegroundColor Green
Write-Host ""

# Generate CHANGELOG from commits since last tag. This is the gate that
# aborts when there are no user-facing commits, so run it BEFORE mutating any
# version files or building - a failure here then leaves a clean tree instead
# of stranding a half-applied version bump with no tag.
Write-Host "Generating CHANGELOG from commits..." -ForegroundColor Cyan
$hasExistingTags = git tag -l 2>$null
if (-not $hasExistingTags) {
    $date = Get-Date -Format 'yyyy-MM-dd'
    $firstEntry = "# Changelog`n`n## [$Version] - $date`n`nFirst release.`n"
    Set-Content $changelogPath $firstEntry
    Write-Host "  First release - wrote initial CHANGELOG entry" -ForegroundColor Gray
} else {
    try {
        New-ChangelogFromCommits `
            -ChangelogPath $changelogPath `
            -Version $Version `
            -ArtifactPaths @("src/", "include/", "cameraunlock-core/", "scripts/install.cmd", "scripts/uninstall.cmd")
    } catch {
        if (-not $Force) {
            Write-Host "Error: $($_.Exception.Message)" -ForegroundColor Red
            Write-Host "No user-facing changes to release. Re-run with -Force for a maintenance release." -ForegroundColor Yellow
            exit 1
        }
        Write-Host "No user-facing commits since last tag - writing maintenance entry (-Force)." -ForegroundColor Yellow
        Add-MaintenanceChangelogEntry -Path $changelogPath -NewVersion $Version
    }
}

Write-Host "Updating CMakeLists.txt version to $Version..." -ForegroundColor Cyan
Set-CurrentVersion $Version

# Keep the installer's displayed version in lockstep with the canonical source.
(Get-Content $installCmdPath -Raw) -replace 'set "MOD_VERSION=.*?"', "set `"MOD_VERSION=$Version`"" |
    Set-Content $installCmdPath -NoNewline

Write-Host "Building Release configuration..." -ForegroundColor Cyan
& pixi run build
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: build failed (exit $LASTEXITCODE)." -ForegroundColor Red
    exit 1
}

Write-Host "Committing version change..." -ForegroundColor Cyan
git add $cmakeListsPath $installCmdPath $changelogPath
git commit -m "Release v$Version"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: commit failed (exit $LASTEXITCODE)." -ForegroundColor Red
    exit 1
}

Write-Host "Creating annotated tag $tagName..." -ForegroundColor Cyan
git tag -a $tagName -m "Release $tagName"
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: tag creation failed (exit $LASTEXITCODE)." -ForegroundColor Red
    exit 1
}

Write-Host "Pushing commits and tag..." -ForegroundColor Cyan
git push origin main
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: push of 'main' failed. Tag created locally; run: git push origin main $tagName" -ForegroundColor Red
    exit 1
}
git push origin $tagName
if ($LASTEXITCODE -ne 0) {
    Write-Host "Error: push of tag failed. Run: git push origin $tagName" -ForegroundColor Red
    exit 1
}

Write-Host ""
Write-Host "Release $tagName initiated. release.yml will build and publish." -ForegroundColor Green
