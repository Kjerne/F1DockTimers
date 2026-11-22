# Tests the installer on this PC without touching a real F1DockTimers install: uses
# dist\F1DockTimersTest-setup.exe (packaging\build.ps1 -TestInstaller), which has its own app id,
# folder, shortcut, Run value and data folder, and never starts, quits or ends a running app.
#
#   powershell -ExecutionPolicy Bypass -File packaging\test-installer.ps1
#
# Covers: a silent upgrade over a Python-style install (the in-app updater's case), with the
# previous install's task list stored, removes the Python runtime and keeps settings; a second
# upgrade with nothing to clean; the Run value and Start-menu entry; a clean uninstall.
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$setup = Join-Path $root "dist\F1DockTimersTest-setup.exe"
if (-not (Test-Path $setup)) { throw "build it first: packaging\build.ps1 -TestInstaller" }

$name = "F1DockTimersTest"
$app = Join-Path $env:LOCALAPPDATA "Programs\$name"
$data = Join-Path $env:LOCALAPPDATA $name
$runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$shortcut = Join-Path ([Environment]::GetFolderPath("Programs")) "$name.lnk"
$log = Join-Path $env:TEMP "$name-setup.log"
$realRun = (Get-ItemProperty $runKey -ErrorAction SilentlyContinue).F1DockTimers
$failures = 0

function Check($ok, $what) {
    if ($ok) { Write-Host "  ok    $what" } else { Write-Host "  FAIL  $what"; $script:failures++ }
}
function Install([string]$extra = "") {
    $argv = @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/LOG=$log") + ($extra -split " " | Where-Object { $_ })
    $p = Start-Process $setup -ArgumentList $argv -Wait -PassThru
    if ($p.ExitCode -ne 0) { throw "setup exited with $($p.ExitCode); see $log" }
}
function Uninstall {
    $u = Join-Path $app "unins000.exe"
    if (Test-Path $u) { Start-Process $u -ArgumentList "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART" -Wait | Out-Null }
    Start-Sleep -Seconds 2   # the uninstaller finishes in a copy of itself
}

Write-Host "Clean start"
Uninstall
if (Test-Path $app) { Remove-Item $app -Recurse -Force }
if (Test-Path $data) { Remove-Item $data -Recurse -Force }

Write-Host "1. Earlier install without the Python files, tasks remembered: autostart only"
Install "/TASKS=autostart"
Check (Test-Path (Join-Path $app "F1DockTimers.exe")) "app installed"
Check (Test-Path (Join-Path $app "Qt6Core.dll")) "Qt next to the exe"
Check (Test-Path (Join-Path $app "vcruntime140.dll")) "C++ runtime next to the exe"
Check (Test-Path (Join-Path $app "licenses\LGPL-3.0.txt")) "licence texts"
Check (-not (Test-Path (Join-Path $app "tst_ui.exe"))) "no test programs"
Check ((Get-ItemProperty $runKey).$name -eq "`"$app\F1DockTimers.exe`" --minimized") "Run value as the app writes it"
Check (Test-Path $shortcut) "Start-menu entry"

Write-Host "2. Silent upgrade over a Python-style folder (what the in-app updater runs)"
New-Item -ItemType Directory -Force (Join-Path $app "_internal\PySide6") | Out-Null
Set-Content (Join-Path $app "_internal\python313.dll") "fake"
New-Item -ItemType Directory -Force $data | Out-Null
Set-Content (Join-Path $data "cache.json") "{}"
Set-Content (Join-Path $data "cache-cpp.json") "{}"
Set-Content (Join-Path $data "f1-cpp.log") "old"
Install
Check (-not (Test-Path (Join-Path $app "_internal"))) "Python runtime removed (checked by default, also silent)"
Check (Test-Path (Join-Path $app "F1DockTimers.exe")) "app still installed"
Check (Test-Path (Join-Path $data "cache.json")) "shared cache kept"
Check (-not (Test-Path (Join-Path $data "cache-cpp.json"))) "side-by-side cache removed"
Check (-not (Test-Path (Join-Path $data "f1-cpp.log"))) "side-by-side log removed"
Check ((Get-ItemProperty $runKey).$name) "Run value kept"

Write-Host "3. Upgrade with nothing to clean"
Install
Check (Test-Path (Join-Path $app "F1DockTimers.exe")) "app installed"

Write-Host "4. Uninstall"
Uninstall
Check (-not (Test-Path (Join-Path $app "F1DockTimers.exe"))) "app removed"
Check (-not (Test-Path $data)) "local data removed"
Check (-not (Get-ItemProperty $runKey).$name) "Run value removed"
Check (-not (Test-Path $shortcut)) "Start-menu entry removed"

Check ((Get-ItemProperty $runKey -ErrorAction SilentlyContinue).F1DockTimers -eq $realRun) "the real F1DockTimers Run value is untouched"
if ($failures) { Write-Host "$failures check(s) failed; setup log: $log"; exit 1 }
Write-Host "All installer checks passed."
