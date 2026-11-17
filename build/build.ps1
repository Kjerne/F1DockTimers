# Builds the portable app, the release ZIP and (if Inno Setup 6 is installed) the installer.
#   powershell -ExecutionPolicy Bypass -File build\build.ps1 [-SkipTests]
param([switch]$SkipTests)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
Set-Location $root

if (-not (Test-Path ".venv\Scripts\python.exe")) {
    python -m venv .venv
}
$py = Join-Path $root ".venv\Scripts\python.exe"
& $py -m pip install --quiet --upgrade pip
& $py -m pip install --quiet -r requirements.txt -r requirements-dev.txt
if ($LASTEXITCODE -ne 0) { throw "pip install failed" }

if (-not $SkipTests) {
    $env:QT_QPA_PLATFORM = "offscreen"
    & $py -m pytest -q
    if ($LASTEXITCODE -ne 0) { throw "tests failed" }
    Remove-Item Env:QT_QPA_PLATFORM
}

$version = (& $py -c "from f1win.version import VERSION; print(VERSION)").Trim()
$env:F1DT_VERSION = $version
& $py tools\make_icon.py

# One-folder build: what the installer installs (fastest start).
& $py -m PyInstaller --noconfirm --clean --distpath dist --workpath build\work build\F1DockTimers.spec
if ($LASTEXITCODE -ne 0) { throw "PyInstaller (folder) failed" }

# Single-file portable exe: nothing to install, unpacks itself on each start.
& $py -m PyInstaller --noconfirm --clean --distpath dist --workpath build\work-portable build\F1DockTimers-portable.spec
if ($LASTEXITCODE -ne 0) { throw "PyInstaller (portable) failed" }
Get-ChildItem dist -Filter "*-win64-portable.zip" | Remove-Item   # older release format
Write-Host "App folder: dist\F1DockTimers\F1DockTimers.exe"
Write-Host "Portable:   dist\F1DockTimers-$version-portable.exe"

$iscc = @("${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
          "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe") | Where-Object { Test-Path $_ } | Select-Object -First 1
if ($iscc) {
    & $iscc "/DAppVersion=$version" build\installer.iss
    if ($LASTEXITCODE -ne 0) { throw "Inno Setup failed" }
    Write-Host "Installer: dist\F1DockTimers-$version-setup.exe"
} else {
    Write-Host "Inno Setup 6 not found; skipped the installer (winget install JRSoftware.InnoSetup)."
}
