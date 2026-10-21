# Windows CI: the user's desktop helpers in tools/masaustu, run for real.
#   MicVST_Oncelik.vbs   - shortcut creation (3 .lnk with MicVST's icon) and the priority change of a
#                          running MicVST.exe (Normal / AboveNormal / High, read back from Windows),
#                          also through a created shortcut; silent on success; MicVST not running ->
#                          a warning box (the script waits on it, nothing is changed).
#   MicVST_Simgeleri.vbs - the icon of the user's four profile shortcuts becomes MicVST's; their targets
#                          and every other shortcut stay as they were.
# The scripts expect MicVST at C:\MicVST\MicVST.exe (the user's install), so the built exe is copied there.
param(
    [Parameter(Mandatory = $true)][string] $Exe,
    [Parameter(Mandatory = $true)][string] $Out
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$report = Join-Path (Resolve-Path $Out).Path "desktop-scripts.md"
"" | Set-Content -Encoding utf8 $report
$fails = 0
function W([string] $s) { $s | Add-Content -Encoding utf8 $report; Write-Output $s }
function Check([bool] $ok, [string] $what) {
    if ($ok) { W "- PASS: $what" } else { W "- FAIL: $what"; $script:fails++ }
}

$src = Join-Path $PSScriptRoot "masaustu"
New-Item -ItemType Directory -Force -Path "C:\MicVST" | Out-Null
Copy-Item -Force (Resolve-Path $Exe).Path "C:\MicVST\MicVST.exe"
Copy-Item -Force (Join-Path $src "MicVST_Oncelik.vbs") "C:\MicVST\MicVST_Oncelik.vbs"
Copy-Item -Force (Join-Path $src "MicVST_Simgeleri.vbs") "C:\MicVST\MicVST_Simgeleri.vbs"
$desktop = [Environment]::GetFolderPath("Desktop")
New-Item -ItemType Directory -Force -Path $desktop | Out-Null
$shell = New-Object -ComObject WScript.Shell
$wscript = Join-Path $env:SystemRoot "System32\wscript.exe"
$cscript = Join-Path $env:SystemRoot "System32\cscript.exe"

function Start-MicVST {
    Get-Process MicVST -ErrorAction SilentlyContinue | Stop-Process -Force
    $p = Start-Process -FilePath "C:\MicVST\MicVST.exe" -PassThru
    Start-Sleep -Seconds 8
    return $p
}
function Priority { (Get-Process MicVST -ErrorAction Stop | Select-Object -First 1).PriorityClass.ToString() }
# Runs a script; returns $true if it ended by itself within the time (false: it waits on a message box)
function Run-Script([string] $host_, [string[]] $args_, [int] $seconds = 20) {
    $p = Start-Process -FilePath $host_ -ArgumentList $args_ -PassThru
    if ($p.WaitForExit($seconds * 1000)) { return $true }
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
    return $false
}

W "## MicVST_Oncelik.vbs"
$names = @("Normal", ("Normalin " + [char]0x00DC + "st" + [char]0x00FC + "nde"), ("Y" + [char]0x00FC + "ksek"))
$lnks = $names | ForEach-Object { Join-Path $desktop ("MicVST " + [char]0x00D6 + "ncelik - " + $_ + ".lnk") }
$lnks | ForEach-Object { Remove-Item -LiteralPath $_ -ErrorAction SilentlyContinue }
# creation mode ends with an information box: the shortcuts are written before it
$p = Start-Process -FilePath $wscript -ArgumentList @('"C:\MicVST\MicVST_Oncelik.vbs"') -PassThru
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt 20 -and @($lnks | Where-Object { Test-Path -LiteralPath $_ }).Count -lt 3) { Start-Sleep -Milliseconds 500 }
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
for ($i = 0; $i -lt 3; $i++) {
    $ok = Test-Path -LiteralPath $lnks[$i]
    Check $ok ("shortcut created: " + (Split-Path -Leaf $lnks[$i]))
    if ($ok) {
        $l = $shell.CreateShortcut($lnks[$i])
        Check ($l.TargetPath -ieq $wscript) ("  target is wscript.exe (" + $l.TargetPath + ")")
        Check ($l.Arguments -eq ('"C:\MicVST\MicVST_Oncelik.vbs" ' + $i)) ("  arguments: " + $l.Arguments)
        Check ($l.IconLocation -ieq "C:\MicVST\MicVST.exe,0") ("  icon: " + $l.IconLocation)
    }
}

$mic = Start-MicVST
W ("MicVST.exe started (pid " + $mic.Id + "), priority at start: " + (Priority))
$expect = @("Normal", "AboveNormal", "High")
foreach ($n in 2, 1, 0, 2) {
    $ended = Run-Script $cscript @("//nologo", '"C:\MicVST\MicVST_Oncelik.vbs"', "$n")
    $now = Priority
    Check ($ended -and $now -eq $expect[$n]) ("argument $n -> " + $expect[$n] + ": got $now, ended silently: $ended")
}
# through a created shortcut (as the user clicks it): Normal
if (Test-Path -LiteralPath $lnks[0]) {
    Start-Process -FilePath $lnks[0]
    $t0 = Get-Date
    while (((Get-Date) - $t0).TotalSeconds -lt 15 -and (Priority) -ne "Normal") { Start-Sleep -Milliseconds 500 }
    Check ((Priority) -eq "Normal") ("shortcut 'Normal' clicked: " + (Priority))
    Start-Sleep -Seconds 2
    Check (@(Get-Process wscript -ErrorAction SilentlyContinue).Count -eq 0) "no message box left open after the shortcut"
}
Get-Process MicVST -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Seconds 1
$ended = Run-Script $cscript @("//nologo", '"C:\MicVST\MicVST_Oncelik.vbs"', "2") 8
Check (-not $ended) "MicVST not running: the script shows its warning (waits on the box) instead of ending silently"
Check (@(Get-Process MicVST -ErrorAction SilentlyContinue).Count -eq 0) "MicVST not running: nothing was started"

W ""
W "## MicVST_Simgeleri.vbs"
$docs = Join-Path $env:TEMP "masaustu-docs"
New-Item -ItemType Directory -Force -Path $docs | Out-Null
$profiles = @("Renk_Ince.vbs", "Renk_Tok.vbs", "Ince VST.vbs", "Tok VST.vbs")
$made = @()
foreach ($pf in $profiles) {
    $target = Join-Path $docs $pf
    "' test" | Set-Content -Encoding ascii $target
    $lp = Join-Path $desktop ("Test " + [IO.Path]::GetFileNameWithoutExtension($pf) + ".lnk")
    $l = $shell.CreateShortcut($lp); $l.TargetPath = $target; $l.IconLocation = "C:\Windows\System32\notepad.exe,0"; $l.Save()
    $made += $lp
}
$otherTarget = Join-Path $docs "Baska.vbs"; "' test" | Set-Content -Encoding ascii $otherTarget
$other = Join-Path $desktop "Test Baska.lnk"
$l = $shell.CreateShortcut($other); $l.TargetPath = $otherTarget; $l.IconLocation = "C:\Windows\System32\notepad.exe,0"; $l.Save()
$p = Start-Process -FilePath $wscript -ArgumentList @('"C:\MicVST\MicVST_Simgeleri.vbs"') -PassThru
$t0 = Get-Date
while (((Get-Date) - $t0).TotalSeconds -lt 20 -and @($made | Where-Object { $shell.CreateShortcut($_).IconLocation -ieq "C:\MicVST\MicVST.exe,0" }).Count -lt 4) { Start-Sleep -Milliseconds 500 }
Start-Sleep -Seconds 1
Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
for ($i = 0; $i -lt 4; $i++) {
    $l = $shell.CreateShortcut($made[$i])
    Check ($l.IconLocation -ieq "C:\MicVST\MicVST.exe,0") ((Split-Path -Leaf $made[$i]) + ": icon " + $l.IconLocation)
    Check ($l.TargetPath -ieq (Join-Path $docs $profiles[$i])) ("  target unchanged: " + $l.TargetPath)
}
$l = $shell.CreateShortcut($other)
Check ($l.IconLocation -ieq "C:\Windows\System32\notepad.exe,0") ("another shortcut is left alone: " + $l.IconLocation)
($made + $other + $lnks) | ForEach-Object { Remove-Item -LiteralPath $_ -ErrorAction SilentlyContinue }

W ""
W ("Result: " + $(if ($fails -eq 0) { "all checks passed" } else { "$fails check(s) failed" }))
if ($fails -ne 0) { exit 1 }
