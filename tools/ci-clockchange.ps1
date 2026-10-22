# R5 (test task docs/TEST_PROMPTU_WIN11.md): Windows' clock changed while MicVST runs and records.
# MicVSTEngineTest "recordlong 7" (the real engine: virtual microphone and cable, a WAV 24 recording,
# clicks on the cable counted, every sample of the file checked) runs while this script moves the
# clock: +1 h, back, +1 day, back, another time zone, back. The test measures with the
# performance counter, not the clock: the file's length must still equal the time it ran, the
# audio must go on without a click, nothing may hang. (The file name keeps the start time; a name
# already there is never overwritten - unit test "Recorder" uniqueFile.) The clock and the time zone
# are put back at the end.
param(
    [Parameter(Mandatory = $true)][string] $Exe,   # MicVSTEngineTest.exe
    [Parameter(Mandatory = $true)][string] $Out
)
$ErrorActionPreference = "Continue"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$report = Join-Path $Out "clockchange.md"
function W([string] $s) { $s | Add-Content -Encoding utf8 $report; Write-Output $s }
"# R5 - the clock changed during a recording" | Set-Content -Encoding utf8 $report

$sw = [Diagnostics.Stopwatch]::StartNew()
$orig = Get-Date
$tz = (& tzutil /g).Trim()
W "start: $($orig.ToString('o')), time zone $tz"
$folder = Join-Path $env:RUNNER_TEMP "r5-kayit"
$stdout = Join-Path $Out "recordlong.txt"
$p = Start-Process -FilePath $Exe -ArgumentList @("recordlong", "7", "wav:24|48000|2", $folder) -NoNewWindow -PassThru -RedirectStandardOutput $stdout -RedirectStandardError (Join-Path $Out "recordlong.stderr.txt")

function At([double] $s) { while ($sw.Elapsed.TotalSeconds -lt $s -and -not $p.HasExited) { Start-Sleep -Milliseconds 100 } }
$steps = @(
    @{ at = 70;  what = "+1 hour";  do = { Set-Date -Adjust ([TimeSpan]::FromHours(1)) | Out-Null } },
    @{ at = 130; what = "-1 hour";  do = { Set-Date -Adjust ([TimeSpan]::FromHours(-1)) | Out-Null } },
    @{ at = 190; what = "+1 day";   do = { Set-Date -Adjust ([TimeSpan]::FromDays(1)) | Out-Null } },
    @{ at = 250; what = "-1 day";   do = { Set-Date -Adjust ([TimeSpan]::FromDays(-1)) | Out-Null } },
    @{ at = 310; what = "time zone Tokyo Standard Time"; do = { & tzutil /s "Tokyo Standard Time" } },
    @{ at = 370; what = "time zone back to $tz"; do = { & tzutil /s $tz } }
)
foreach ($st in $steps) {
    At $st.at
    if ($p.HasExited) { W "the test ended before '$($st.what)'"; break }
    & $st.do
    W ("{0,6:F1} s: clock {1} -> now {2}" -f $sw.Elapsed.TotalSeconds, $st.what, (Get-Date).ToString('yyyy-MM-dd HH:mm:ss zzz'))
}
if (-not $p.WaitForExit(15 * 60 * 1000)) { $p.Kill(); W "TIMEOUT: the test did not end (hang?)" }

# put the clock and the time zone back
& tzutil /s $tz
Set-Date -Date ($orig + $sw.Elapsed) | Out-Null
W "clock put back: $((Get-Date).ToString('o')), time zone $((& tzutil /g).Trim())"
W ""
$lines = Get-Content -Encoding utf8 $stdout | Where-Object { $_ -notmatch '^\s*\[log' }
$lines | Where-Object { $_ -match 'PASS|FAIL|^cable:|^file:|^content:|^\|' } | ForEach-Object { W $_ }
$fails = @($lines | Where-Object { $_ -match '^\s*FAIL' }).Count
W ""
W "exit code $($p.ExitCode); failed checks $fails"
if (Test-Path $folder) { Remove-Item -Recurse -Force $folder }
if ($fails -gt 0 -or $p.ExitCode -ne 0) { exit 1 }
