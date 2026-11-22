# Builds the release files of the C++ build:
#   dist\F1DockTimers\                         the app folder (what the installer installs)
#   dist\F1DockTimers-<version>-portable.zip   the same folder zipped: unzip and run
#   dist\F1DockTimers-<version>-setup.exe      the installer (if Inno Setup 6 is installed)
#
#   powershell -ExecutionPolicy Bypass -File packaging\build.ps1 [-SkipTests] [-TestInstaller]
#
# -TestInstaller also builds dist\F1DockTimersTest-setup.exe: the same installer under another
# app id and name that never starts or ends a running F1DockTimers (for packaging\test-installer.ps1).
param([switch]$SkipTests, [switch]$TestInstaller)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

# Configure, build (with windeployqt) and test in one go; see CMakePresets.json.
if ($SkipTests) {
    cmake --preset msvc
    if ($LASTEXITCODE -ne 0) { throw "configure failed" }
    cmake --build --preset release
} else {
    cmake --workflow --preset release
}
if ($LASTEXITCODE -ne 0) { throw "build or tests failed" }

$version = (Select-String -Path CMakeLists.txt -Pattern 'project\(F1DockTimers VERSION ([0-9.]+)').Matches[0].Groups[1].Value
$out = Join-Path $root "build\msvc\Release"
$dist = Join-Path $root "dist"
$app = Join-Path $dist "F1DockTimers"

# Stage only what the app needs: no test programs, no debug or link files.
if (Test-Path $app) { Remove-Item $app -Recurse -Force }
New-Item -ItemType Directory -Force $app | Out-Null
Copy-Item (Join-Path $out "F1DockTimers.exe"), (Join-Path $out "Qt6*.dll") $app
foreach ($dir in "platforms", "imageformats", "tls", "assets", "lang") {
    Copy-Item (Join-Path $out $dir) (Join-Path $app $dir) -Recurse
}
Remove-Item (Join-Path $app "lang\_template.txt") -ErrorAction SilentlyContinue   # for translators, in the source
Copy-Item LICENSE, THIRD_PARTY_NOTICES.md $app
Copy-Item licenses (Join-Path $app "licenses") -Recurse

# The Microsoft C++ runtime next to the exe (app-local), so no separate redistributable install.
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt = Get-ChildItem (Join-Path $vs "VC\Redist\MSVC") -Directory | Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName "x64\Microsoft.VC143.CRT" } | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $crt) { throw "Visual C++ redistributable files not found" }
foreach ($dll in "vcruntime140.dll", "vcruntime140_1.dll", "msvcp140.dll", "msvcp140_1.dll", "msvcp140_2.dll") {
    Copy-Item (Join-Path $crt $dll) $app
}
Write-Host "App folder: dist\F1DockTimers\F1DockTimers.exe"

# Portable zip: the folder as is (settings still go to %APPDATA%\F1DockTimers).
$zip = Join-Path $dist "F1DockTimers-$version-portable.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path $app -DestinationPath $zip -CompressionLevel Optimal
Write-Host "Portable:   dist\F1DockTimers-$version-portable.zip"

$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
          "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($iscc) {
    & $iscc /Q "/DAppVersion=$version" packaging\installer.iss
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
    Write-Host "Installer:  dist\F1DockTimers-$version-setup.exe"
    if ($TestInstaller) {
        & $iscc /Q "/DAppVersion=$version" /DTestBuild "/DAppId={{0F1D0C4E-7E57-4B1D-9C0D-7E57F1D0C4E5}" /DName=F1DockTimersTest `
            "/FF1DockTimersTest-setup" packaging\installer.iss
        if ($LASTEXITCODE -ne 0) { throw "Inno Setup (test build) failed" }
        Write-Host "Test setup: dist\F1DockTimersTest-setup.exe"
    }
} else {
    Write-Host "Inno Setup 6 not found; skipped the installer (winget install JRSoftware.InnoSetup)."
}
