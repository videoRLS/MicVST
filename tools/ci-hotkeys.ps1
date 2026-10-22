# Windows CI: the system-wide shortcuts of the real MicVST.exe - keyboard (RegisterHotKey) and mouse
# (low-level mouse hook) - driven by injected input while MicVST runs in the background, observed in
# its log ("Ses kapatildi (sustur)" / "Ses acildi", "Ham ses: acik / kapali"). Also: bindings from
# config.xml, a switched-off shortcut does nothing, the plain wheel is never a shortcut, and the
# bindings survive a restart (written back to config.xml on quit).
param(
    [Parameter(Mandatory = $true)][string] $Exe,
    [Parameter(Mandatory = $true)][string] $Out
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$report = Join-Path (Resolve-Path $Out).Path "hotkeys.md"
"" | Set-Content -Encoding utf8 $report
$fails = 0
function W([string] $s) { $s | Add-Content -Encoding utf8 $report; Write-Output $s }
function Check([bool] $ok, [string] $what) { if ($ok) { W "- PASS: $what" } else { W "- FAIL: $what"; $script:fails++ } }
$Exe = (Resolve-Path $Exe).Path

Add-Type -TypeDefinition @"
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Inp {
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void mouse_event(uint flags, int dx, int dy, int data, UIntPtr extra);
    public static void Key(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(30); keybd_event(vk, 0, 2, UIntPtr.Zero); }
    public static void Down(byte vk) { keybd_event(vk, 0, 0, UIntPtr.Zero); }
    public static void Up(byte vk) { keybd_event(vk, 0, 2, UIntPtr.Zero); }
    public static void XButton(int which) { mouse_event(0x80, 0, 0, which, UIntPtr.Zero); System.Threading.Thread.Sleep(30); mouse_event(0x100, 0, 0, which, UIntPtr.Zero); }
    public static void Middle() { mouse_event(0x20, 0, 0, 0, UIntPtr.Zero); System.Threading.Thread.Sleep(30); mouse_event(0x40, 0, 0, 0, UIntPtr.Zero); }
    public static void Wheel(int delta) { mouse_event(0x800, 0, 0, delta, UIntPtr.Zero); }
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

$appData = Join-Path $env:APPDATA "MicVST"
New-Item -ItemType Directory -Force -Path $appData | Out-Null
$config = Join-Path $appData "config.xml"
$log = Join-Path $appData "log.txt"
$VK_F9 = 0x78; $VK_CONTROL = 0x11

function Write-Config([string] $mute, [string] $compare) {
    $attrs = 'inputDevice="" outputDevice="" language="tr"'
    if ($mute) { $attrs += " hotkeyMute=`"$mute`"" }
    if ($compare) { $attrs += " hotkeyCompare=`"$compare`"" }
    "<?xml version=`"1.0`" encoding=`"UTF-8`"?>`n<MicVST $attrs>`n  <plugins/>`n</MicVST>`n" | Set-Content -Encoding utf8 $config
}
function Start-App {
    Get-Process MicVST -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    $p = Start-Process -FilePath $Exe -ArgumentList "--tray" -PassThru   # in the background, like after Windows starts
    Start-Sleep -Seconds 8
    return $p
}
function LogLines { if (Test-Path $log) { @(Get-Content -Encoding UTF8 $log) } else { @() } }
# runs an action and returns the log lines it produced
function After([scriptblock] $action) {
    $n = (LogLines).Count
    & $action
    Start-Sleep -Milliseconds 1500
    $all = LogLines
    if ($all.Count -le $n) { return @() }
    return @($all[$n..($all.Count - 1)])
}
function Has($lines, [string] $pattern) { @($lines | Where-Object { $_ -match $pattern }).Count -gt 0 }
$muteOn = '\(sustur\)'; $muteOff = 'Ses a\S*ld'; $rawOn = 'Ham ses: a'; $rawOff = 'Ham ses: kapal'
$any = "$muteOn|$muteOff|$rawOn|$rawOff"

W "## 1. Defaults: Sustur = F9 (keyboard), Ham ses = mouse 4 (back) from config.xml"
Write-Config "" "1:0:0:2"
$p = Start-App
W ("MicVST.exe --tray running: " + (-not $p.HasExited))
$l = After { [Inp]::Key($VK_F9) };         Check (Has $l $muteOn) "F9 mutes (log: $($l -join ' | '))"
Check (Has $l 'bildirim sesi: Ah\S*ap - kapanma') "mute plays the default cue 'Ahsap - kapanma'"
$l = After { [Inp]::Key($VK_F9) };         Check (Has $l $muteOff) "F9 again unmutes"
# Bildirim sesleri (default Ahsap): the real exe plays the cue in the same call as the mute; the log line names it
Check (Has $l 'bildirim sesi: Ah\S*ap - a\S*lma') "unmute plays the default cue 'Ahsap - acilma' (log: $($l -join ' | '))"
$l = After { [Inp]::XButton(1) };          Check (Has $l $rawOn) "mouse 4 switches Ham ses on (log: $($l -join ' | '))"
$l = After { [Inp]::XButton(1) };          Check (Has $l $rawOff) "mouse 4 again switches it off"
$l = After { [Inp]::XButton(2) };          Check (-not (Has $l $any)) "mouse 5 (not bound) does nothing"
$l = After { [Inp]::Wheel(120) };          Check (-not (Has $l $any)) "the plain wheel does nothing"

W ""
W "## 2. Mouse for both: Sustur = middle button, Ham ses = Ctrl + wheel up"
Write-Config "1:0:0:1" "1:0:2:4"
$p = Start-App
$l = After { [Inp]::Middle() };            Check (Has $l $muteOn) "middle button mutes (log: $($l -join ' | '))"
$l = After { [Inp]::Middle() };            Check (Has $l $muteOff) "middle button again unmutes"
$l = After { [Inp]::Key($VK_F9) };         Check (-not (Has $l $any)) "F9 (no longer bound) does nothing"
$l = After { [Inp]::Wheel(120) };          Check (-not (Has $l $any)) "wheel up without Ctrl does nothing"
$l = After { [Inp]::Down($VK_CONTROL); Start-Sleep -Milliseconds 50; [Inp]::Wheel(120); Start-Sleep -Milliseconds 50; [Inp]::Up($VK_CONTROL) }
Check (Has $l $rawOn) "Ctrl + wheel up switches Ham ses on (log: $($l -join ' | '))"
$l = After { [Inp]::Down($VK_CONTROL); Start-Sleep -Milliseconds 50; [Inp]::Wheel(-120); Start-Sleep -Milliseconds 50; [Inp]::Up($VK_CONTROL) }
Check (-not (Has $l $any)) "Ctrl + wheel down (not bound) does nothing"
$l = After { [Inp]::Down($VK_CONTROL); Start-Sleep -Milliseconds 50; [Inp]::Wheel(120); Start-Sleep -Milliseconds 50; [Inp]::Up($VK_CONTROL) }
Check (Has $l $rawOff) "Ctrl + wheel up again switches it off"
# quit the way Windows does at log-off: MicVST saves its state
[void] [Inp]::AskToQuit([uint32] $p.Id)
$quit = $p.WaitForExit(15000)
if (-not $quit) { Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue }
$x = [xml] (Get-Content -Raw -Encoding UTF8 $config)
Check ($quit -and $x.MicVST.hotkeyMute -eq "1:0:0:1" -and $x.MicVST.hotkeyCompare -eq "1:0:2:4") ("bindings kept in config.xml after quitting: mute '" + $x.MicVST.hotkeyMute + "', compare '" + $x.MicVST.hotkeyCompare + "', quit by itself: $quit")

W ""
W "## 3. Restart with the saved bindings; a switched-off shortcut does nothing"
$p = Start-App
$l = After { [Inp]::Middle() };            Check (Has $l $muteOn) "after the restart the middle button still mutes"
$l = After { [Inp]::Middle() };            Check (Has $l $muteOff) "and unmutes"
Write-Config "0:0:0:1" "0:0:2:4"
$p = Start-App
$l = After { [Inp]::Middle() };            Check (-not (Has $l $any)) "switched off: the middle button does nothing"
$l = After { [Inp]::Down($VK_CONTROL); Start-Sleep -Milliseconds 50; [Inp]::Wheel(120); Start-Sleep -Milliseconds 50; [Inp]::Up($VK_CONTROL) }
Check (-not (Has $l $any)) "switched off: Ctrl + wheel does nothing"
Get-Process MicVST -ErrorAction SilentlyContinue | Stop-Process -Force

W ""
W ("Result: " + $(if ($fails -eq 0) { "all checks passed" } else { "$fails check(s) failed" }))
if ($fails -ne 0) { exit 1 }
