#!/usr/bin/env pwsh
#Requires -Version 5.1
# Bump vendored Ultimate ASI Loader (dinput8.dll) to the latest upstream
# within the pinned range and rewrite vendor/ultimate-asi-loader/{LICENSE,README.md}.
# Manual: dev runs this when they want a fresh upstream bump, then commits the
# result. CI never refreshes.
# See ~/.claude/CLAUDE.md "Vendoring Third-Party Dependencies".
#
# Special case: Ultimate-ASI-Loader ships a DLL inside a release zip, not as a
# standalone asset, so this script extracts dinput8.dll rather than calling
# Update-VendoredLoader, which vendors the downloaded artifact whole.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'

$scriptDir  = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectDir = Split-Path -Parent $scriptDir

$module = Join-Path $projectDir 'cameraunlock-core/powershell/ModLoaderSetup.psm1'
if (-not (Test-Path $module)) {
    throw "ModLoaderSetup.psm1 not found at $module. Run 'pixi run sync' to update the cameraunlock-core submodule."
}
Import-Module $module -Force

$vendorAsiDir     = Join-Path $projectDir 'vendor/ultimate-asi-loader'
$vendorAsiDll     = Join-Path $vendorAsiDir 'dinput8.dll'
$vendorAsiLicense = Join-Path $vendorAsiDir 'LICENSE'
$vendorAsiReadme  = Join-Path $vendorAsiDir 'README.md'
if (-not (Test-Path $vendorAsiDir)) {
    New-Item -ItemType Directory -Path $vendorAsiDir -Force | Out-Null
}

$tempDir = Join-Path $env:TEMP ("asi-update-" + [IO.Path]::GetRandomFileName())
New-Item -ItemType Directory -Path $tempDir -Force | Out-Null
$tempZip     = Join-Path $tempDir 'upstream.zip'
$tempDll     = Join-Path $tempDir 'dinput8.dll'
$tempLicense = Join-Path $tempDir 'LICENSE'
try {
    Write-Host "Refreshing vendor/ultimate-asi-loader from upstream..." -ForegroundColor Cyan
    $meta = Invoke-FetchLatestLoader `
        -OutputPath $tempZip `
        -Owner 'ThirteenAG' -Repo 'Ultimate-ASI-Loader' `
        -VersionPrefix 'v9.' `
        -AssetPattern '^Ultimate-ASI-Loader_x64\.zip$'

    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $zip = [System.IO.Compression.ZipFile]::OpenRead($tempZip)
    try {
        $dllEntry = $zip.Entries | Where-Object { $_.Name -eq 'dinput8.dll' } | Select-Object -First 1
        if (-not $dllEntry) { throw "Upstream zip $($meta.AssetName) does not contain dinput8.dll." }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($dllEntry, $tempDll, $true)

        $licenseEntry = $zip.Entries | Where-Object { $_.Name -match '^(license|LICENSE)(\..+)?$' -and $_.FullName -notmatch '/.+/' } | Select-Object -First 1
        if ($licenseEntry) {
            [System.IO.Compression.ZipFileExtensions]::ExtractToFile($licenseEntry, $tempLicense, $true)
        }
    } finally { $zip.Dispose() }

    $dllSha = (Get-FileHash -LiteralPath $tempDll -Algorithm SHA256).Hash.ToLower()

    # Idempotency: an upstream that has not moved must leave the tree clean. Without
    # this the FetchedAt line rewrites README.md on every run, so `git status` after a
    # no-op refresh shows a timestamp-only diff with no artifact behind it.
    if ((Test-Path -LiteralPath $vendorAsiDll) -and (Test-Path -LiteralPath $vendorAsiLicense) -and (Test-Path -LiteralPath $vendorAsiReadme) -and
        ((Get-FileHash -LiteralPath $vendorAsiDll -Algorithm SHA256).Hash.ToLower() -eq $dllSha)) {
        Write-Host "  no change (tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))... matches on-disk vendor copy)" -ForegroundColor DarkGray
        Write-Host ""
        Write-Host "vendor/ultimate-asi-loader is already up to date." -ForegroundColor Green
        return
    }

    Move-Item -LiteralPath $tempDll -Destination $vendorAsiDll -Force

    if (Test-Path -LiteralPath $tempLicense) {
        Move-Item -LiteralPath $tempLicense -Destination $vendorAsiLicense -Force
    } else {
        $licenseUrl = "https://raw.githubusercontent.com/ThirteenAG/Ultimate-ASI-Loader/$($meta.Tag)/license"
        Invoke-WebRequest -Uri $licenseUrl -OutFile $vendorAsiLicense -UseBasicParsing -TimeoutSec 30 -Headers @{ "User-Agent" = "CameraUnlock-HeadTracking" }
    }

    $readme = @(
        '# Ultimate ASI Loader (vendored)',
        '',
        'Bundled copy of Ultimate ASI Loader, the install-time source of truth.',
        'install.cmd copies from here and never reaches out to the network.',
        'Refresh manually with `pixi run update-deps`, then commit.',
        '',
        '## Snapshot',
        '',
        '- Upstream: https://github.com/ThirteenAG/Ultimate-ASI-Loader',
        "- Tag: ``$($meta.Tag)``",
        "- Commit: ``$($meta.CommitSha)``",
        "- Asset: ``$($meta.AssetName)``",
        "- Upstream URL: $($meta.AssetUrl)",
        "- dinput8.dll SHA-256: ``$dllSha``",
        "- Fetched at: $($meta.FetchedAt)",
        '',
        '`dinput8.dll` is extracted from the upstream asset untouched. install.cmd copies it',
        'into the Prey exe dir under the name `ASI_LOADER_NAME` pins, which is `dinput8.dll`.'
    ) -join "`n"
    Set-Content -Path $vendorAsiReadme -Value $readme -Encoding UTF8

    Write-Host "  tag=$($meta.Tag) sha256=$($dllSha.Substring(0,12))..." -ForegroundColor DarkGray
} finally {
    Remove-Item $tempDir -Recurse -Force -ErrorAction SilentlyContinue
}

Write-Host ""
Write-Host "vendor/ultimate-asi-loader refreshed. Review and commit." -ForegroundColor Green
