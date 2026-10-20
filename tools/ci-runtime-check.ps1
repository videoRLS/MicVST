# Closed-box check at run time (Windows CI): MicVST.exe is started in several scenarios and
# everything it does on the network and on disk is recorded.
#   * Windows Filtering Platform audit (Security log 5156/5157/5158/5159/5152): every connection,
#     every socket bind, blocked or allowed, with the process path. Proven to work by a control
#     process (curl.exe) that does connect.
#   * Polling of the TCP connections and UDP endpoints owned by MicVST processes (250 ms).
#   * Process Monitor (Sysinternals), if it can be downloaded: every file/registry write and every
#     TCP/UDP operation of MicVST.exe (including the --scan child processes).
#   * Loaded DLLs of the running process.
#   * New or changed files in %APPDATA%, %LOCALAPPDATA%, %TEMP%, Documents.
# Scenarios: first start (no settings), normal start, damaged settings file, damaged plug-in cache,
# settings that point to missing devices / plug-ins / folders (local paths), start in the tray
# (--tray). Not covered here: audio devices (the CI machine has none) - the audio engine with
# plug-ins runs in MicVSTEngineTest, whose network activity is checked by ci-enginetest.ps1's caller.
# MicVST.exe additionally gets an outbound+inbound firewall block rule for the whole check, so any
# attempt would show up as a blocked event (5157/5159) instead of silently succeeding.
param(
    [Parameter(Mandatory = $true)][string] $Exe,
    [Parameter(Mandatory = $true)][string] $Out,
    [int] $Seconds = 25
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$Exe = (Resolve-Path $Exe).Path
$report = Join-Path $Out "runtime-check.md"
function W([string] $s) { $s | Add-Content -Encoding utf8 $report; Write-Output $s }
"" | Set-Content -Encoding utf8 $report

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Win {
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc f, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h, uint msg, IntPtr w, IntPtr l, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    public static List<IntPtr> WindowsOf(uint pid) {
        var list = new List<IntPtr>();
        EnumWindows((h, p) => { uint q; GetWindowThreadProcessId(h, out q); if (q == pid) list.Add(h); return true; }, IntPtr.Zero);
        return list;
    }
    // Windows shutdown/log-off path: WM_QUERYENDSESSION makes a JUCE app call systemRequestedQuit().
    public static int AskToQuit(uint pid) {
        int n = 0;
        foreach (var h in WindowsOf(pid)) { IntPtr r; SendMessageTimeout(h, 0x0011, IntPtr.Zero, IntPtr.Zero, 0x0002, 3000, out r); ++n; }
        return n;
    }
}
"@

# --- audit + firewall ---
& auditpol /set /subcategory:"Filtering Platform Connection" /success:enable /failure:enable | Out-Null
& auditpol /set /subcategory:"Filtering Platform Packet Drop" /success:enable /failure:enable | Out-Null
New-NetFirewallRule -DisplayName "MicVST-closed-box-out" -Direction Outbound -Program $Exe -Action Block | Out-Null
New-NetFirewallRule -DisplayName "MicVST-closed-box-in" -Direction Inbound -Program $Exe -Action Block | Out-Null

function Get-WfpEvents([datetime] $since, [string] $pattern) {
    try {
        Get-WinEvent -FilterHashtable @{ LogName = "Security"; Id = 5152, 5156, 5157, 5158, 5159; StartTime = $since } -ErrorAction Stop |
            Where-Object { $_.Message -match $pattern }
    } catch { @() }   # "no events found" throws
}
function Describe-Wfp($e) {
    $m = $e.Message
    $app = if ($m -match "Application Name:\s*(\S+)") { $Matches[1] } else { "?" }
    $dst = if ($m -match "Destination Address:\s*(\S+)") { $Matches[1] } else { "" }
    $port = if ($m -match "Destination Port:\s*(\S+)") { $Matches[1] } else { "" }
    $src = if ($m -match "Source Address:\s*(\S+)") { $Matches[1] } else { "" }
    $sport = if ($m -match "Source Port:\s*(\S+)") { $Matches[1] } else { "" }
    "event $($e.Id) $app src $src`:$sport dst $dst`:$port"
}

# Control: the audit must see a process that really connects.
$tc = Get-Date
& curl.exe -s -o NUL --max-time 10 https://example.com 2>&1 | Out-Null
Start-Sleep -Seconds 2
$control = @(Get-WfpEvents $tc "curl\.exe")
W "# Closed box at run time (Windows CI, $(Get-Date -Format 'yyyy-MM-dd HH:mm') UTC)"
W ""
W "Control (curl.exe connects to example.com): $($control.Count) Windows Filtering Platform events recorded -> the audit $(if ($control.Count -gt 0) { 'works' } else { 'DID NOT RECORD ANYTHING (results below are not conclusive)' })."
W ""

# --- optional Process Monitor ---
$procmon = $null
try {
    $procmon = Join-Path $env:RUNNER_TEMP "Procmon64.exe"
    Invoke-WebRequest -Uri "https://live.sysinternals.com/Procmon64.exe" -OutFile $procmon
} catch { $procmon = $null; W "Process Monitor could not be downloaded: $_" }

$appData = Join-Path $env:APPDATA "MicVST"
$watchDirs = @($env:APPDATA, $env:LOCALAPPDATA, $env:TEMP, [Environment]::GetFolderPath("MyDocuments")) | Where-Object { $_ -and (Test-Path $_) }
$networkDlls = @("wininet.dll", "winhttp.dll", "ws2_32.dll", "wsock32.dll", "mswsock.dll", "urlmon.dll", "dnsapi.dll",
                 "iphlpapi.dll", "websocket.dll", "httpapi.dll", "webio.dll", "winnsi.dll", "nsi.dll", "netapi32.dll")
$totalWfp = 0; $totalSockets = 0; $totalProcmonNet = 0

function Run-Scenario([string] $title, [scriptblock] $prepare, [string[]] $arguments) {
    W "## $title"
    & $prepare
    $t0 = Get-Date
    $pml = Join-Path $Out ("procmon_" + ($title -replace "[^A-Za-z0-9]", "_") + ".pml")
    if ($procmon) {
        Start-Process -FilePath $procmon -ArgumentList @("/AcceptEula", "/Quiet", "/Minimized", "/BackingFile", $pml) | Out-Null
        Start-Sleep -Seconds 4
    }
    $before = @{}
    foreach ($d in $watchDirs) { Get-ChildItem -Recurse -Depth 4 -File -Force -ErrorAction SilentlyContinue $d | ForEach-Object { $before[$_.FullName] = $_.LastWriteTimeUtc } }

    $args2 = if ($arguments) { $arguments } else { @() }
    $p = if ($args2.Count -gt 0) { Start-Process -FilePath $Exe -ArgumentList $args2 -PassThru } else { Start-Process -FilePath $Exe -PassThru }
    $sockets = @{}
    $modules = @()
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline -and -not $p.HasExited) {
        $pids = @(Get-Process -Name "MicVST" -ErrorAction SilentlyContinue | ForEach-Object Id)
        foreach ($id in $pids) {
            Get-NetTCPConnection -OwningProcess $id -ErrorAction SilentlyContinue | ForEach-Object { $sockets["TCP $($_.LocalAddress):$($_.LocalPort) -> $($_.RemoteAddress):$($_.RemotePort) $($_.State)"] = 1 }
            Get-NetUDPEndpoint -OwningProcess $id -ErrorAction SilentlyContinue | ForEach-Object { $sockets["UDP $($_.LocalAddress):$($_.LocalPort)"] = 1 }
        }
        if ($modules.Count -eq 0 -and ((Get-Date) - $t0).TotalSeconds -gt 8) {
            try { $modules = @($p.Modules | ForEach-Object { $_.ModuleName.ToLowerInvariant() } | Sort-Object -Unique) } catch {}
        }
        Start-Sleep -Milliseconds 250
    }
    $running = -not $p.HasExited
    $exitHow = "exited by itself (code $(if ($p.HasExited) { $p.ExitCode }))"
    if ($running) {
        $n = [Win]::AskToQuit([uint32] $p.Id)
        if ($p.WaitForExit(15000)) { $exitHow = "quit normally after the Windows shutdown request ($n windows asked), exit code $($p.ExitCode)" }
        else { Stop-Process -Id $p.Id -Force; $exitHow = "DID NOT QUIT within 15 s of the shutdown request - killed" }
    }
    Get-Process -Name "MicVST" -ErrorAction SilentlyContinue | Stop-Process -Force   # left-over scan children
    Start-Sleep -Seconds 2

    $wfp = @(Get-WfpEvents $t0 "(?i)micvst\.exe")
    W "- ran $Seconds s, $exitHow"
    W "- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **$($wfp.Count)**"
    foreach ($e in ($wfp | Select-Object -First 20)) { W "  - $(Describe-Wfp $e)" }
    W "- TCP connections / UDP endpoints owned by MicVST processes: **$($sockets.Count)**"
    foreach ($s in ($sockets.Keys | Select-Object -First 20)) { W "  - $s" }
    $netLoaded = @($modules | Where-Object { $networkDlls -contains $_ })
    W "- loaded DLLs: $($modules.Count); networking DLLs among them: $(if ($netLoaded.Count) { $netLoaded -join ', ' } else { 'none' })"
    ($modules -join "`n") | Set-Content -Encoding utf8 (Join-Path $Out ("modules_" + ($title -replace "[^A-Za-z0-9]", "_") + ".txt"))
    $script:totalWfp += $wfp.Count; $script:totalSockets += $sockets.Count

    $changed = @()
    foreach ($d in $watchDirs) {
        Get-ChildItem -Recurse -Depth 4 -File -Force -ErrorAction SilentlyContinue $d | Where-Object { -not $before.ContainsKey($_.FullName) -or $before[$_.FullName] -ne $_.LastWriteTimeUtc } |
            ForEach-Object { $changed += $_.FullName }
    }
    $mine = @($changed | Where-Object { $_ -match "(?i)micvst" })
    W "- new/changed files with 'MicVST' in the path: $(if ($mine.Count) { ($mine | ForEach-Object { $_.Replace($env:USERPROFILE, '%USERPROFILE%') }) -join '; ' } else { 'none' })"
    W "- other new/changed files in the watched folders (any process on the CI machine): $($changed.Count - $mine.Count)"

    if ($procmon) {
        & $procmon /Terminate | Out-Null
        Start-Sleep -Seconds 3
        $csv = [IO.Path]::ChangeExtension($pml, ".csv")
        Start-Process -FilePath $procmon -ArgumentList @("/AcceptEula", "/Quiet", "/OpenLog", $pml, "/SaveAs", $csv) -Wait | Out-Null
        if (Test-Path $csv) {
            # only the lines of MicVST processes are parsed (the whole log of a CI machine is large)
            $header = Get-Content $csv -TotalCount 1
            $lines = @(Select-String -Path $csv -Pattern '"MicVST.exe"' -SimpleMatch | ForEach-Object Line)
            $rows = if ($lines.Count -gt 0) { @(ConvertFrom-Csv -InputObject (@($header) + $lines) | Where-Object { $_."Process Name" -eq "MicVST.exe" }) } else { @() }
            $net = @($rows | Where-Object { $_.Operation -match "^(TCP|UDP) " })
            $writes = @($rows | Where-Object { $_.Operation -match "WriteFile|SetDispositionInformation|SetRenameInformation|SetEndOfFile|RegSetValue|RegCreateKey|RegDeleteValue|RegDeleteKey" -and $_.Result -eq "SUCCESS" } |
                        ForEach-Object { "$($_.Operation) $($_.Path)".Replace($env:USERPROFILE, "%USERPROFILE%") } | Sort-Object -Unique)
            $reads = @($rows | Where-Object { $_.Operation -eq "CreateFile" -and $_.Path -notmatch "(?i)^C:\\Windows\\" -and $_.Path -notmatch [regex]::Escape((Split-Path $Exe)) } |
                       ForEach-Object { $_.Path.Replace($env:USERPROFILE, "%USERPROFILE%") } | Sort-Object -Unique)
            W "- Process Monitor: $($rows.Count) events of MicVST processes; TCP/UDP operations: **$($net.Count)**"
            foreach ($r in ($net | Select-Object -First 20)) { W "  - $($r.Operation) $($r.Path)" }
            W "- successful writes (files, registry):"
            foreach ($r in ($writes | Where-Object { $_ -notmatch "HKCU\\Software\\Microsoft\\Windows\\CurrentVersion\\Explorer|\\Microsoft\\Windows\\Shell|HKCU\\Software\\Classes\\Local Settings|MuiCache|\\WER\\" } | Select-Object -First 40)) { W "  - $r" }
            W "- files/folders opened outside C:\Windows and the program folder: $($reads.Count) (list in procmon_files_*.txt)"
            ($reads -join "`n") | Set-Content -Encoding utf8 (Join-Path $Out ("procmon_files_" + ($title -replace "[^A-Za-z0-9]", "_") + ".txt"))
            ($writes -join "`n") | Set-Content -Encoding utf8 (Join-Path $Out ("procmon_writes_" + ($title -replace "[^A-Za-z0-9]", "_") + ".txt"))
            $script:totalProcmonNet += $net.Count
            Remove-Item $csv
        } else { W "- Process Monitor: no log could be converted" }
        Remove-Item $pml -ErrorAction SilentlyContinue
    }
    W ""
}

Remove-Item -Recurse -Force $appData -ErrorAction SilentlyContinue
Run-Scenario "First start (no settings, plug-in scan)" {} @()
Run-Scenario "Normal start (saved settings)" {} @()
Run-Scenario "Start in the tray (--tray, autostart)" {} @("--tray")
Run-Scenario "Damaged settings file" { New-Item -ItemType Directory -Force $appData | Out-Null; [IO.File]::WriteAllBytes((Join-Path $appData "config.xml"), [byte[]] (1..255)) } @()
Run-Scenario "Damaged plug-in cache" { [IO.File]::WriteAllText((Join-Path $appData "plugin_cache.xml"), "<KnownPlugins><PLUGIN name=") } @()
Run-Scenario "Settings pointing to missing devices, plug-ins and folders" {
    @"
<?xml version="1.0" encoding="UTF-8"?>
<MicVST inputDevice="Olmayan Mikrofon" outputDevice="CABLE Input (VB-Audio Virtual Cable)" sampleRate="48000" userBufferSize="480" safetyBufferMs="10" pluginFolders="C:\olmayan\klasor">
  <plugins>
    <plugin fileOrId="C:\Program Files\Common Files\VST3\Olmayan.vst3" pluginName="Olmayan" bypassed="0" state=""/>
  </plugins>
</MicVST>
"@ | Set-Content -Encoding utf8 (Join-Path $appData "config.xml")
} @()

# --- Islem onceligi (process priority) of the real MicVST.exe: older config.xml without the setting ->
# Normal; each saved choice applied at start; the choice survives the restart (config.xml) ---
$priorityFails = 0
$cfgPath = Join-Path $appData "config.xml"
W "## Process priority of the running MicVST.exe"
foreach ($case in @(@("(no setting: older config.xml)", $null, "Normal"), @("0 Normal", "0", "Normal"), @("1 Normalin Ustunde", "1", "AboveNormal"), @("2 Yuksek", "2", "High"), @("2 again (restart)", "keep", "High"))) {
    try {
        if (Test-Path $cfgPath) {
            [xml] $x = Get-Content -Raw -Encoding utf8 $cfgPath
            if ($null -eq $case[1]) { $x.MicVST.RemoveAttribute("processPriority") }
            elseif ($case[1] -ne "keep") { $x.MicVST.SetAttribute("processPriority", $case[1]) }
            $x.Save($cfgPath)
        }
        $p = Start-Process -FilePath $Exe -PassThru
        Start-Sleep -Seconds 10
        $p.Refresh()
        $cls = if ($p.HasExited) { "exited" } else { [string] $p.PriorityClass }
        $n = [Win]::AskToQuit([uint32] $p.Id)
        if (-not $p.WaitForExit(15000)) { Stop-Process -Id $p.Id -Force }
        Get-Process -Name "MicVST" -ErrorAction SilentlyContinue | Stop-Process -Force
        Start-Sleep -Seconds 1
        $saved = if (Test-Path $cfgPath) { ([xml] (Get-Content -Raw -Encoding utf8 $cfgPath)).MicVST.GetAttribute("processPriority") } else { "?" }
        $ok = $cls -eq $case[2]
        if (-not $ok) { $priorityFails++ }
        W "- $($case[0]): Windows reports **$cls** (expected $($case[2])) - $(if ($ok) { 'OK' } else { 'WRONG' }); config.xml after exit: processPriority=`"$saved`""
    } catch { $priorityFails++; W "- $($case[0]): failed: $_" }
}
$logNow = Join-Path $appData "log.txt"
if (Test-Path $logNow) { Get-Content -Encoding utf8 $logNow | Select-String -Pattern "nceli" | Select-Object -Last 8 | ForEach-Object { W "  log: $($_.Line)" } }
W "- process priority checks failed: $priorityFails"

$log = Join-Path $appData "log.txt"
if (Test-Path $log) { Copy-Item $log (Join-Path $Out "micvst-log.txt") }

Remove-NetFirewallRule -DisplayName "MicVST-closed-box-out" -ErrorAction SilentlyContinue
Remove-NetFirewallRule -DisplayName "MicVST-closed-box-in" -ErrorAction SilentlyContinue

W "## Result"
W "Filtering Platform events of MicVST: $totalWfp; sockets seen: $totalSockets; Process Monitor TCP/UDP operations: $totalProcmonNet."
if ($control.Count -eq 0) { W "Not conclusive: the control process was not recorded."; exit 2 }
if ($totalWfp -gt 0 -or $totalSockets -gt 0 -or $totalProcmonNet -gt 0) { W "::error::MicVST used the network"; exit 1 }
W "OK: no network activity of MicVST in any scenario."
