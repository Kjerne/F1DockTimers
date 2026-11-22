# Start one F1DockTimers build, time how long until its countdown window is on screen, then
# sample idle memory and CPU. Used for the numbers in PORT_NOTES.md.
#
#   powershell -ExecutionPolicy Bypass -File tools\measure.ps1 -Exe <path> -HomeDir <test home> [-Seconds 180] [-Quit stop]
#
# -HomeDir becomes NOCTALIA_F1_HOME, so the build reads <home>\config\settings.json and keeps its
# cache in <home>\cache, never touching the real %APPDATA% / %LOCALAPPDATA%.
param(
    [Parameter(Mandatory)] [string] $Exe,
    [Parameter(Mandatory)] [string] $HomeDir,
    [int] $Seconds = 180,
    [string] $Label = "",
    [ValidateSet("flag", "stop")] [string] $Quit = "flag"   # "flag": run `<exe> --quit`; "stop": end the started processes
)
$ErrorActionPreference = "Stop"

Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class Win {
    public delegate bool EnumProc(IntPtr h, IntPtr l);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    public static bool HasVisibleWindow(uint pid) {
        bool found = false;
        EnumWindows((h, l) => {
            uint p; GetWindowThreadProcessId(h, out p);
            if (p == pid && IsWindowVisible(h)) { found = true; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
"@

$env:NOCTALIA_F1_HOME = $HomeDir
$sw = [Diagnostics.Stopwatch]::StartNew()
$p = Start-Process -FilePath $Exe -PassThru
$visibleMs = $null
while ($sw.ElapsedMilliseconds -lt 30000) {
    # A onefile build runs a bootloader that starts the real app as a child process.
    $ids = @($p.Id) + @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$($p.Id)" | ForEach-Object { $_.ProcessId })
    if ($ids | Where-Object { [Win]::HasVisibleWindow([uint32]$_) }) { $visibleMs = $sw.ElapsedMilliseconds; break }
    Start-Sleep -Milliseconds 20
}

function Get-Tree([int] $root) {
    @($root) + @(Get-CimInstance Win32_Process -Filter "ParentProcessId=$root" | ForEach-Object { $_.ProcessId })
}

function Get-Sample([int[]] $ids) {
    $ws = 0; $priv = 0; $pws = 0; $cpu = 0.0
    foreach ($id in $ids) {
        $proc = Get-Process -Id $id -ErrorAction SilentlyContinue
        if (-not $proc) { continue }
        $ws += $proc.WorkingSet64; $priv += $proc.PrivateMemorySize64; $cpu += $proc.TotalProcessorTime.TotalSeconds
        $perf = Get-CimInstance Win32_PerfFormattedData_PerfProc_Process -Filter "IDProcess=$id" -ErrorAction SilentlyContinue
        if ($perf) { $pws += [int64]$perf.WorkingSetPrivate }
    }
    [pscustomobject]@{ WS = $ws / 1MB; Private = $priv / 1MB; PrivateWS = $pws / 1MB; Cpu = $cpu }
}

$ids = Get-Tree $p.Id
$samples = @()
$start = Get-Sample $ids
$t0 = $sw.Elapsed.TotalSeconds
for ($t = 30; $t -le $Seconds; $t += 30) {
    while ($sw.Elapsed.TotalSeconds - $t0 -lt $t) { Start-Sleep -Milliseconds 250 }
    $s = Get-Sample $ids
    $samples += [pscustomobject]@{ T = $t; PrivateWS = [math]::Round($s.PrivateWS, 1); WS = [math]::Round($s.WS, 1);
                                   Commit = [math]::Round($s.Private, 1); CpuS = [math]::Round($s.Cpu, 3) }
}
$end = $samples[-1]
$window = $Seconds - 30
$first = $samples[0]
$cpuPct = 100.0 * ($end.CpuS - $first.CpuS) / $window / [Environment]::ProcessorCount
"== $Label $Exe"
"window visible after: $visibleMs ms"
$samples | Format-Table -AutoSize | Out-String
"idle CPU ({0} s window, % of all {1} cores): {2:N3} %" -f $window, [Environment]::ProcessorCount, $cpuPct
"processes: $($ids -join ', ')"
if ($Quit -eq "flag") {
    Start-Process -FilePath $Exe -ArgumentList "--quit" -Wait      # the C++ build exits cleanly
} else {
    # Only the processes this script started, from this exe's folder.
    $dir = Split-Path $Exe -Parent
    foreach ($id in $ids) { Get-Process -Id $id -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$dir\*" } | Stop-Process }
}
