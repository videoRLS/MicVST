# K14 + K22 (test task docs/TEST_PROMPTU_WIN11.md): the recorder on a real small disk and every
# recording setting the panel offers.
#   K14: a 260 MB NTFS VHD (diskpart, needs admin - the CI runner is): the recording runs until the
#        disk is nearly full and stops by itself; < 100 MB free -> no start; the disk taken away
#        while recording -> an error, no crash, no hang.
#   K22: MicVSTTests "Recorder K22" with MICVST_TEST_K22=1 (about 760 files of 10 s), table in k22.md.
param(
    [Parameter(Mandatory = $true)] [string] $Exe,   # MicVSTTests.exe
    [Parameter(Mandatory = $true)] [string] $Out
)
$ErrorActionPreference = "Continue"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$failures = @()

# --- K14 ---
$vhd = Join-Path $env:RUNNER_TEMP "k14.vhdx"
$letter = "Q"
$mk = Join-Path $env:RUNNER_TEMP "k14-create.txt"
$rm = Join-Path $env:RUNNER_TEMP "k14-detach.txt"
@("create vdisk file=`"$vhd`" maximum=260 type=expandable", "select vdisk file=`"$vhd`"", "attach vdisk",
  "create partition primary", "format fs=ntfs quick label=K14", "assign letter=$letter") | Set-Content -Encoding ascii $mk
@("select volume $letter", "remove all dismount noerr", "select vdisk file=`"$vhd`"", "detach vdisk noerr") | Set-Content -Encoding ascii $rm
$k14 = Join-Path $Out "k14.txt"
"# K14 - small disk (260 MB VHD, NTFS)" | Set-Content $k14
& diskpart /s $mk 2>&1 | Add-Content $k14
if (Test-Path "${letter}:\") {
    $env:MICVST_TEST_SMALLDISK = "${letter}:\kayit"
    $env:MICVST_TEST_SMALLDISK_DETACH = "diskpart /s $rm"
    & $Exe "Recorder K14" 2>&1 | Tee-Object -FilePath (Join-Path $Out "k14-unit.txt") | Out-Null
    $code = $LASTEXITCODE
    "exit code: $code" | Add-Content $k14
    if ($code -ne 0) { $failures += "K14 (exit $code)" }
    Remove-Item Env:MICVST_TEST_SMALLDISK, Env:MICVST_TEST_SMALLDISK_DETACH
    & diskpart /s $rm 2>&1 | Out-Null   # already detached by the test; harmless again
} else {
    "the VHD could not be created and mounted as ${letter}: - K14 not run" | Add-Content $k14
    $failures += "K14 (no VHD)"
}

# --- K22 ---
$env:MICVST_TEST_K22 = "1"
$env:MICVST_TEST_K22_REPORT = Join-Path $Out "k22.md"
$t0 = Get-Date
& $Exe "Recorder K22" 2>&1 | Tee-Object -FilePath (Join-Path $Out "k22-unit.txt") | Out-Null
$code = $LASTEXITCODE
"K22: exit code $code, $([Math]::Round(((Get-Date) - $t0).TotalMinutes, 1)) min" | Add-Content (Join-Path $Out "summary.txt")
if ($code -ne 0) { $failures += "K22 (exit $code)" }

if ($failures.Count) { "FAILED: $($failures -join ', ')" | Add-Content (Join-Path $Out "summary.txt"); exit 1 }
"all passed" | Add-Content (Join-Path $Out "summary.txt")
