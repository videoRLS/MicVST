# Long run of the real MicVST.exe on the Windows CI machine: the app runs for -Minutes with its
# window open (UI timers, meters, the engine's watchdog and device logic), and every minute its
# memory, handles, GDI/USER objects, threads and CPU time are recorded. A slow leak shows up as a
# steady rise. The CI machine has no audio devices: the engine waits for the chosen devices (they
# are listed in config.xml but missing), so the reconnect logic runs the whole time.
# At the end MicVST is asked to quit like at a Windows shutdown; its log and settings are kept.
param(
    [Parameter(Mandatory = $true)][string] $Exe,
    [Parameter(Mandatory = $true)][string] $Out,
    [double] $Minutes = 300
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$Exe = (Resolve-Path $Exe).Path

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Soak {
    [DllImport("user32.dll")] public static extern uint GetGuiResources(IntPtr process, uint flags);   // 0 = GDI, 1 = USER
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
    public static int AskToQuit(uint pid) {
        var list = new List<IntPtr>();
        EnumWindows((h, p) => { uint q; GetWindowThreadProcessId(h, out q); if (q == pid) list.Add(h); return true; }, IntPtr.Zero);
        foreach (var h in list) { IntPtr r; SendMessageTimeout(h, 0x0011, IntPtr.Zero, IntPtr.Zero, 0x0002, 3000, out r); }
        return list.Count;
    }
}
"@

# settings: chosen devices that do not exist on this machine, Otomatik Kazanç + Kazanç in the chain
$appData = Join-Path $env:APPDATA "MicVST"
New-Item -ItemType Directory -Force -Path $appData | Out-Null
@"
<?xml version="1.0" encoding="UTF-8"?>
<MicVST inputDevice="Mikrofon (USB Audio Device)" outputDevice="CABLE Input (VB-Audio Virtual Cable)" sampleRate="48000.0"
        userBufferSize="0" safetyBufferMs="0" pluginFolders="" windowState="">
  <plugins>
    <plugin fileOrId="builtin:autogain" bypassed="0" state=""/>
    <plugin fileOrId="builtin:gain" bypassed="0" state=""/>
  </plugins>
</MicVST>
"@ | Set-Content -Encoding utf8 (Join-Path $appData "config.xml")

$csv = Join-Path $Out "app-soak.csv"
"minute,working_set_mb,private_mb,handles,gdi_objects,user_objects,threads,cpu_seconds,cpu_percent_last_minute,log_kb,config_writes" | Set-Content $csv
$p = Start-Process -FilePath $Exe -PassThru
$handle = $p.Handle
$t0 = Get-Date
$lastCpu = 0.0
$logFile = Join-Path $appData "log.txt"
$cfgFile = Join-Path $appData "config.xml"
$lastCfgWrite = (Get-Item $cfgFile).LastWriteTimeUtc
$cfgWrites = 0
$crashed = $false
for ($m = 0; $m -le [int] $Minutes; ++$m) {
    if ($p.HasExited) { $crashed = $true; break }
    $p.Refresh()
    $cpu = $p.TotalProcessorTime.TotalSeconds
    $gdi = [Soak]::GetGuiResources($p.Handle, 0)
    $usr = [Soak]::GetGuiResources($p.Handle, 1)
    $logKb = if (Test-Path $logFile) { [Math]::Round((Get-Item $logFile).Length / 1KB, 1) } else { 0 }
    $w = (Get-Item $cfgFile -ErrorAction SilentlyContinue)
    if ($w -and $w.LastWriteTimeUtc -ne $lastCfgWrite) { ++$cfgWrites; $lastCfgWrite = $w.LastWriteTimeUtc }
    $line = "{0},{1},{2},{3},{4},{5},{6},{7},{8},{9},{10}" -f $m, [Math]::Round($p.WorkingSet64 / 1MB, 2), [Math]::Round($p.PrivateMemorySize64 / 1MB, 2),
        $p.HandleCount, $gdi, $usr, $p.Threads.Count, [Math]::Round($cpu, 2), [Math]::Round(($cpu - $lastCpu) / 60.0 * 100.0, 3), $logKb, $cfgWrites
    $line | Add-Content $csv
    $lastCpu = $cpu
    if ($m -lt [int] $Minutes) { Start-Sleep -Seconds 60 }
}

$summary = Join-Path $Out "app-soak.md"
$rows = Import-Csv $csv
function Col([string] $n) { $rows | ForEach-Object { [double] $_.$n } }
function Stat([string] $n, [string] $label) {
    $v = @(Col $n)
    if ($v.Count -eq 0) { return }
    $first = $v[[Math]::Min(5, $v.Count - 1)]   # after the start-up (5 min)
    "| $label | $($v[0]) | $first | $($v[-1]) | $(($v | Measure-Object -Maximum).Maximum) |" | Add-Content $summary
}
"# MicVST.exe long run: $([Math]::Round(((Get-Date) - $t0).TotalMinutes, 1)) min" | Set-Content $summary
if ($crashed) { "**MicVST.exe ENDED BY ITSELF after $m min (exit code $($p.ExitCode))**" | Add-Content $summary }
"" | Add-Content $summary
"| Value | start | after 5 min | end | max |" | Add-Content $summary
"|---|---:|---:|---:|---:|" | Add-Content $summary
Stat "working_set_mb" "working set (MB)"
Stat "private_mb" "private bytes (MB)"
Stat "handles" "handles"
Stat "gdi_objects" "GDI objects"
Stat "user_objects" "USER objects"
Stat "threads" "threads"
Stat "cpu_percent_last_minute" "CPU of one core (%, per minute)"
Stat "log_kb" "log.txt (KB)"
Stat "config_writes" "config.xml writes (count)"
# linear trend of the private bytes after the start-up (MB per hour)
$after = @($rows | Where-Object { [int] $_.minute -ge 10 })
if ($after.Count -gt 10) {
    $n = $after.Count; $sx = 0.0; $sy = 0.0; $sxx = 0.0; $sxy = 0.0
    foreach ($r in $after) { $x = [double] $r.minute; $y = [double] $r.private_mb; $sx += $x; $sy += $y; $sxx += $x * $x; $sxy += $x * $y }
    $slope = ($n * $sxy - $sx * $sy) / ($n * $sxx - $sx * $sx) * 60.0
    "" | Add-Content $summary
    "private bytes trend after 10 min: $([Math]::Round($slope, 3)) MB per hour" | Add-Content $summary
}

if (-not $crashed) {
    $n = [Soak]::AskToQuit([uint32] $p.Id)
    if (-not $p.WaitForExit(15000)) { "did not quit within 15 s after WM_QUERYENDSESSION ($n windows) - killed" | Add-Content $summary; $p.Kill() }
    else { "quit cleanly after WM_QUERYENDSESSION, exit code $($p.ExitCode)" | Add-Content $summary }
}
if (Test-Path $logFile) { Copy-Item $logFile (Join-Path $Out "log.txt") }
if (Test-Path $cfgFile) { Copy-Item $cfgFile (Join-Path $Out "config.xml") }
Get-Content $summary
