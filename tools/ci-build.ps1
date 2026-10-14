# Configures + builds MicVST (Release, x64) and records metrics for the CI comparison.
# Usage: ./tools/ci-build.ps1 -Source <dir> -Build <dir> -Targets "MicVST MicVSTTests" -Out <results-dir>
param(
    [Parameter(Mandatory = $true)][string] $Source,
    [Parameter(Mandatory = $true)][string] $Build,
    [Parameter(Mandatory = $true)][string] $Targets,
    [Parameter(Mandatory = $true)][string] $Out
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force $Out | Out-Null

$cfg = Measure-Command { cmake -S $Source -B $Build -A x64 2>&1 | Tee-Object -FilePath "$Out/configure.log" | Out-Host }
if ($LASTEXITCODE -ne 0) { throw "configure failed" }

$targetArgs = @()
foreach ($t in $Targets.Split(" ", [StringSplitOptions]::RemoveEmptyEntries)) { $targetArgs += @("--target", $t) }
$bld = Measure-Command { cmake --build $Build --config Release @targetArgs -- /m /v:minimal 2>&1 | Tee-Object -FilePath "$Out/build.log" | Out-Host }
$buildOk = ($LASTEXITCODE -eq 0)

$log = Get-Content "$Out/build.log"
$warnings = $log | Select-String -Pattern "warning [A-Z]+[0-9]+" | ForEach-Object { ($_.Line -replace "\s*\[[^\]]*\]\s*$", "").Trim() } | Sort-Object -Unique
$own   = @($warnings | Where-Object { $_ -notmatch "[\\/]_deps[\\/]" })
$juce  = @($warnings | Where-Object { $_ -match "[\\/]_deps[\\/]" })
$errors = @($log | Select-String -Pattern "error [A-Z]+[0-9]+" | ForEach-Object { $_.Line.Trim() } | Sort-Object -Unique)
$own   | Set-Content -Encoding utf8 "$Out/warnings-micvst.txt"
$juce  | Set-Content -Encoding utf8 "$Out/warnings-juce.txt"
$errors | Set-Content -Encoding utf8 "$Out/errors.txt"

$exe = Join-Path $Build "MicVST_artefacts/Release/MicVST.exe"
$m = [ordered]@{
    configure_seconds = [math]::Round($cfg.TotalSeconds, 1)
    build_seconds     = [math]::Round($bld.TotalSeconds, 1)
    build_ok          = $buildOk
    warnings_micvst   = $own.Count
    warnings_juce     = $juce.Count
    errors            = $errors.Count
    exe_bytes         = if (Test-Path $exe) { (Get-Item $exe).Length } else { 0 }
    exe_sha256        = if (Test-Path $exe) { (Get-FileHash $exe -Algorithm SHA256).Hash } else { "" }
}
$m | ConvertTo-Json | Set-Content -Encoding utf8 "$Out/metrics.json"
if (Test-Path $exe) { Copy-Item $exe "$Out/MicVST.exe" }
if (-not $buildOk) { Write-Output "::error::build failed"; $errors | Select-Object -First 40 | ForEach-Object { Write-Output $_ }; exit 1 }
