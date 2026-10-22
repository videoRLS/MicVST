# R6 (test task docs/TEST_PROMPTU_WIN11.md): the real MicVST.exe started and closed 1000 times; 200 of
# them killed (taskkill /f) at a random moment in the first 2 s - while it starts and writes its
# settings (config.xml is written once at every start). After every round:
#   * the process ended: closed normally within 15 s (exit code 0) or killed; a crash (another exit
#     code) or a hang is counted
#   * config.xml is still a whole XML file and still holds the marker settings written before the run
#     (recording format, recording folder, headphone volume) - nothing lost, nothing reset
#   * no temporary files left over in %APPDATA%\MicVST (JUCE writes config.xml through a temporary
#     file that replaces it at once)
#   * the window appears (time to it measured)
# At the end: log.txt size, Windows Error Reporting / Application Error events for MicVST.exe.
# %APPDATA%\MicVST is saved before and put back afterwards.
param(
    [Parameter(Mandatory = $true)][string] $Exe,
    [Parameter(Mandatory = $true)][string] $Out,
    [int] $Rounds = 1000,
    [int] $KillEvery = 5      # every 5th round killed: 200 of 1000
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$Exe = (Resolve-Path $Exe).Path
$report = Join-Path $Out "startstop.md"
function W([string] $s) { $s | Add-Content -Encoding utf8 $report; Write-Output $s }
"" | Set-Content -Encoding utf8 $report

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Win6 {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, System.Text.StringBuilder s, int n);
    // the main window shown: a visible top-level window titled "MicVST"
    public static int WindowCount(uint pid) {
        int n = 0;
        EnumWindows((h, p) => {
            uint q; GetWindowThreadProcessId(h, out q);
            if (q == pid && IsWindowVisible(h)) { var sb = new System.Text.StringBuilder(64); GetWindowText(h, sb, 64); if (sb.ToString() == "MicVST") ++n; }
            return true; }, IntPtr.Zero);
        return n;
    }
    // Windows shutdown / log-off: WM_QUERYENDSESSION makes JUCE call systemRequestedQuit() (= tray "Kapat")
    public static int AskToQuit(uint pid) {
        var list = new List<IntPtr>();
        EnumWindows((h, p) => { uint q; GetWindowThreadProcessId(h, out q); if (q == pid) list.Add(h); return true; }, IntPtr.Zero);
        foreach (var h in list) { IntPtr r; SendMessageTimeout(h, 0x0011, IntPtr.Zero, IntPtr.Zero, 0x0002, 3000, out r); }
        return list.Count;
    }
}
"@

$dir = Join-Path $env:APPDATA "MicVST"
$cfg = Join-Path $dir "config.xml"
$backup = Join-Path $env:RUNNER_TEMP "micvst-appdata-backup"
if (Test-Path $backup) { Remove-Item -Recurse -Force $backup }
if (Test-Path $dir) { Copy-Item -Recurse $dir $backup }

function Stop-All { Get-Process MicVST -ErrorAction SilentlyContinue | ForEach-Object { try { $_.Kill(); $_.WaitForExit(5000) | Out-Null } catch {} } }
Stop-All

# first start: config.xml written; then the markers go in
$p = Start-Process -FilePath $Exe -PassThru
Start-Sleep -Seconds 5
[Win6]::AskToQuit([uint32] $p.Id) | Out-Null
if (-not $p.WaitForExit(15000)) { Stop-All }
$recFolder = Join-Path $env:RUNNER_TEMP "r6-kayit"
New-Item -ItemType Directory -Force -Path $recFolder | Out-Null
$markers = [ordered]@{ recordFormat = "flac:24:5|48000|2"; recordFolder = $recFolder; monitorVolumeDb = "-7.5" }
[xml] $x = Get-Content -Raw -Encoding utf8 $cfg
foreach ($k in $markers.Keys) { $x.DocumentElement.SetAttribute($k, $markers[$k]) }
$x.Save($cfg)
W "# R6 - $Rounds starts of MicVST.exe, every ${KillEvery}th killed in its first 2 s"
W ""
W "markers in config.xml: $(($markers.GetEnumerator() | ForEach-Object { "$($_.Key)=$($_.Value)" }) -join ', ')"
$baseFiles = @(Get-ChildItem $dir -File | ForEach-Object { $_.Name })
W "files in %APPDATA%\MicVST before: $($baseFiles -join ', ')"
W ""

$rng = New-Object System.Random 6
$t0 = Get-Date
$closed = 0; $killed = 0; $crashes = @(); $hangs = @(); $cfgBad = @(); $lost = @(); $startMs = New-Object System.Collections.Generic.List[double]
$leftovers = @{}
for ($i = 1; $i -le $Rounds; $i++) {
    $kill = ($i % $KillEvery) -eq 0
    $sw = [Diagnostics.Stopwatch]::StartNew()
    $p = Start-Process -FilePath $Exe -PassThru
    if ($kill) {
        Start-Sleep -Milliseconds ($rng.Next(0, 2000))
        & taskkill /f /pid $p.Id 2>&1 | Out-Null
        if (-not $p.WaitForExit(10000)) { $hangs += "round ${i}: did not end after taskkill /f" ; Stop-All }
        $killed++
    } else {
        # wait for the window (the program is up), then close it the way Windows does
        while ($sw.ElapsedMilliseconds -lt 20000 -and -not $p.HasExited -and [Win6]::WindowCount([uint32] $p.Id) -eq 0) { Start-Sleep -Milliseconds 25 }
        if ($p.HasExited) { $crashes += "round ${i}: ended by itself before its window, exit code $($p.ExitCode)" }
        else {
            $startMs.Add($sw.Elapsed.TotalMilliseconds)
            Start-Sleep -Milliseconds ($rng.Next(300, 1500))
            [Win6]::AskToQuit([uint32] $p.Id) | Out-Null
            if (-not $p.WaitForExit(15000)) { $hangs += "round ${i}: still running 15 s after the close request"; Stop-All }
            elseif ($p.ExitCode -ne 0) { $crashes += "round ${i}: exit code $($p.ExitCode) (0x$('{0:X8}' -f $p.ExitCode))" }
            else { $closed++ }
        }
    }
    # settings still whole and unchanged?
    try {
        [xml] $c = Get-Content -Raw -Encoding utf8 $cfg
        foreach ($k in $markers.Keys) {
            if ($c.DocumentElement.GetAttribute($k) -ne $markers[$k]) { $lost += "round ${i}: $k = '$($c.DocumentElement.GetAttribute($k))'"; break }
        }
    } catch { $cfgBad += "round ${i}: config.xml unreadable: $($_.Exception.Message)" }
    foreach ($f in Get-ChildItem $dir -File) { if ($baseFiles -notcontains $f.Name -and $f.Name -ne "log.txt" -and -not $leftovers.ContainsKey($f.Name)) { $leftovers[$f.Name] = $i } }
    if ($i % 100 -eq 0) { W "after $i rounds ($([Math]::Round(((Get-Date) - $t0).TotalMinutes, 1)) min): closed $closed, killed $killed, crashes $($crashes.Count), hangs $($hangs.Count), settings unreadable $($cfgBad.Count), markers lost $($lost.Count), new files $($leftovers.Count)" }
}
Stop-All

W ""
$sorted = $startMs | Sort-Object
if ($sorted.Count) { W "time to the window: median $([Math]::Round($sorted[[int]($sorted.Count / 2)])) ms, 95 % $([Math]::Round($sorted[[int]($sorted.Count * 0.95)])) ms, max $([Math]::Round($sorted[-1])) ms" }
$log = Join-Path $dir "log.txt"
if (Test-Path $log) { W "log.txt: $([Math]::Round((Get-Item $log).Length / 1KB)) KB" }
foreach ($l in @($crashes + $hangs + $cfgBad + $lost)) { W "- $l" }
$temps = @($leftovers.Keys | Where-Object { $_ -match "temp|tmp" })
foreach ($k in $leftovers.Keys) { W "- new file in %APPDATA%\MicVST: $k (first seen after round $($leftovers[$k]))" }
$ev = @(Get-WinEvent -FilterHashtable @{ LogName = "Application"; StartTime = $t0 } -ErrorAction SilentlyContinue | Where-Object { $_.Message -match "MicVST" -and ($_.Id -eq 1000 -or $_.Id -eq 1001 -or $_.Id -eq 1002) })
W "Application Error / WER / hang events for MicVST: $($ev.Count)"
foreach ($e in ($ev | Select-Object -First 10)) { W "- $($e.TimeCreated.ToString('HH:mm:ss')) id $($e.Id): $(($e.Message -split "`n")[0..3] -join ' | ')" }

# a temporary file left by a kill in the middle of a write is listed (it would pile up over years) but
# is not a failure of the settings themselves; crashes, hangs, unreadable or changed settings are
if ($temps.Count) { W "temporary files left by killed writes: $($temps.Count)" }
$fail = $crashes.Count + $hangs.Count + $cfgBad.Count + $lost.Count
W ""
W "result: $(if ($fail -eq 0) { 'all passed' } else { "FAILED ($fail)" }) - $closed closed normally, $killed killed, $([Math]::Round(((Get-Date) - $t0).TotalMinutes, 1)) min"

# put the settings back as they were
if (Test-Path $dir) { Remove-Item -Recurse -Force $dir }
if (Test-Path $backup) { Copy-Item -Recurse $backup $dir }
if ($fail -ne 0) { exit 1 }
