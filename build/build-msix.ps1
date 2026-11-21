# Builds the Microsoft Store package (dist\F1DockTimers-<version>.msix) from the PyInstaller
# folder build (dist\F1DockTimers, made by build.ps1). The package is unsigned on purpose:
# Partner Center signs it when you submit it to the Store.
#
#   .\build\build-msix.ps1 -IdentityName "12345Kjerne.F1DockTimers" -Publisher "CN=ABCD-1234-..." -PublisherDisplayName "Kjerne"
#
# The three values come from Partner Center > your app > Product management > Product identity.
param(
    [Parameter(Mandatory = $true)][string]$IdentityName,
    [Parameter(Mandatory = $true)][string]$Publisher,
    [string]$PublisherDisplayName = "Kjerne",
    [string]$Version = ""
)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not $Version) {
    $Version = (Select-String -Path "f1win\version.py" -Pattern 'VERSION = "([0-9.]+)"').Matches[0].Groups[1].Value
}
$msixVersion = "$Version.0"                     # the Store wants four parts, the last one 0

$app = Join-Path $root "dist\F1DockTimers"
if (-not (Test-Path "$app\F1DockTimers.exe")) { throw "Run build\build.ps1 first (dist\F1DockTimers is missing)." }

$sdk = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\makeappx.exe" -ErrorAction SilentlyContinue |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $sdk) { throw "makeappx.exe not found: install the Windows 10/11 SDK." }

$stage = Join-Path $root "build\msix-stage"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory $stage | Out-Null
Copy-Item "$app\*" $stage -Recurse

if (-not (Test-Path "build\store\Assets\StoreLogo.png")) { & python tools\make_store_assets.py }
Copy-Item "build\store\Assets" (Join-Path $stage "Assets") -Recurse

$manifest = Get-Content "build\store\AppxManifest.xml" -Raw
$manifest = $manifest.Replace('$(IdentityName)', $IdentityName).Replace('$(Publisher)', $Publisher)
$manifest = $manifest.Replace('$(PublisherDisplayName)', $PublisherDisplayName).Replace('$(Version)', $msixVersion)
Set-Content -Path (Join-Path $stage "AppxManifest.xml") -Value $manifest -Encoding utf8

$out = Join-Path $root "dist\F1DockTimers-$Version.msix"
& $sdk.FullName pack /o /d $stage /p $out
if ($LASTEXITCODE -ne 0) { throw "makeappx failed ($LASTEXITCODE)" }
Remove-Item $stage -Recurse -Force
Write-Host "Store package: $out (upload it in Partner Center; the Store signs it)"
