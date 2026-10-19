# Runs one test program with a time limit and writes its output to a file (used by the long runs).
# A run that does not finish in time (hang, deadlock) is killed and reported; exit code 1 then.
param(
    [Parameter(Mandatory = $true)][string] $Exe,
    [Parameter(Mandatory = $true)][string] $OutFile,
    [Parameter(Mandatory = $true)][int] $TimeoutSeconds,
    [string[]] $Arguments = @()
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $OutFile) | Out-Null
$err = [IO.Path]::ChangeExtension($OutFile, ".stderr.txt")
$t0 = Get-Date
$p = Start-Process -FilePath $Exe -ArgumentList $Arguments -NoNewWindow -PassThru -RedirectStandardOutput $OutFile -RedirectStandardError $err
$handle = $p.Handle
$peakMb = 0
while (-not $p.WaitForExit(5000)) {
    try { $p.Refresh(); $peakMb = [Math]::Max($peakMb, [Math]::Round($p.PeakWorkingSet64 / 1MB, 1)) } catch {}
    if (((Get-Date) - $t0).TotalSeconds -gt $TimeoutSeconds) {
        $p.Kill()
        "TIMEOUT after $TimeoutSeconds s (hang or deadlock) - process killed" | Add-Content $OutFile
        exit 1
    }
}
$secs = [Math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
"-- exit code $($p.ExitCode), $secs s, peak working set $peakMb MB" | Add-Content $OutFile
exit $p.ExitCode
