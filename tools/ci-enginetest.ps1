# Runs MicVSTEngineTest (MicVST's real AudioEngine between virtual devices with WASAPI-like timing,
# real threads, MMCSS) on the Windows CI machine and writes every result to $Out:
#   <mode>.txt for edits, devices, hotplug, openfail, faults, latency, buffers, mono, flutter, quality, gain,
#   autogain, long, chaos (both with Otomatik Kazanç in the chain), vst3 (only when a Windows VST3 bundle could be downloaded), dump_*.json + integrity.txt.
# Each run has a time limit; a run that does not finish (deadlock, hang) is killed and reported.
# The tests' own verdicts are in the text files; this script fails only if a run crashed or hung.
param(
    [Parameter(Mandatory = $true)][string] $Exe,
    [Parameter(Mandatory = $true)][string] $Out,
    [double] $LongMinutes = 20
)
$ErrorActionPreference = "Stop"
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$Out = (Resolve-Path $Out).Path
$env:MICVST_TEST_OUT = Join-Path $Out "dumps"
New-Item -ItemType Directory -Force -Path $env:MICVST_TEST_OUT | Out-Null

# Optional real VST3 plug-ins (only the test downloads them - MicVST itself never connects anywhere):
# LSP Plugins (if a Windows VST3 build is published), otherwise Airwindows Consolidated (MIT).
$vst3 = $null
$headers = @{ "User-Agent" = "micvst-ci" }
if ($env:GITHUB_TOKEN) { $headers["Authorization"] = "Bearer $($env:GITHUB_TOKEN)" }
$sourceNote = Join-Path $Out "vst3-source.txt"
"" | Set-Content $sourceNote
function Find-Vst3([string] $dir) {
    $b = Get-ChildItem -Recurse -Directory $dir -Filter "*.vst3" -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $b) { $b = Get-ChildItem -Recurse -File $dir -Filter "*.vst3" -ErrorAction SilentlyContinue | Select-Object -First 1 }
    if ($b) { return $b.FullName } else { return $null }
}
try {
    $rel = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/lsp-plugins/lsp-plugins/releases/latest"
    $asset = $rel.assets | Where-Object { $_.name -match "vst3" -and $_.name -match "(?i)windows|win64" -and $_.name -match "\.zip$" } | Select-Object -First 1
    if ($asset) {
        $zip = Join-Path $env:RUNNER_TEMP $asset.name
        Invoke-WebRequest -Headers $headers -Uri $asset.browser_download_url -OutFile $zip
        $dst = Join-Path $env:RUNNER_TEMP "lsp-vst3"
        Expand-Archive -Force $zip $dst
        $vst3 = Find-Vst3 $dst
        if ($vst3) { "LSP Plugins $($rel.tag_name): $($asset.name)" | Add-Content $sourceNote }
    } else { "LSP Plugins $($rel.tag_name): no Windows VST3 asset" | Add-Content $sourceNote }
} catch { "LSP download failed: $_" | Add-Content $sourceNote }
if (-not $vst3) {
    try {
        $rel = Invoke-RestMethod -Headers $headers -Uri "https://api.github.com/repos/baconpaul/airwin2rack/releases/tags/DAWPlugin"
        # "Airwindows" itself contains "win": match the platform word, never Linux/macOS/ARM assets.
        # x64 zip first, then the other zips, then the x64 installer (Inno Setup, silent).
        $win = $rel.assets | Where-Object { $_.name -match "(?i)-windows|win64|-win-" -and $_.name -notmatch "(?i)linux|mac|arm64" }
        $zips = @($win | Where-Object { $_.name -match "(?i)-windows\.zip$" }) + @($win | Where-Object { $_.name -match "\.zip$" -and $_.name -notmatch "(?i)-windows\.zip$" })
        foreach ($zipAsset in $zips) {
            if ($vst3) { break }
            $zip = Join-Path $env:RUNNER_TEMP $zipAsset.name
            Invoke-WebRequest -Headers $headers -Uri $zipAsset.browser_download_url -OutFile $zip
            $dst = Join-Path $env:RUNNER_TEMP ("airwindows-" + [IO.Path]::GetFileNameWithoutExtension($zipAsset.name))
            Expand-Archive -Force $zip $dst
            $vst3 = Find-Vst3 $dst
            if ($vst3) { "Airwindows Consolidated: $($zipAsset.name)" | Add-Content $sourceNote }
            else { "$($zipAsset.name): no .vst3 inside; contents: $((Get-ChildItem -Recurse $dst | Select-Object -First 25 | ForEach-Object { $_.FullName.Substring($dst.Length) }) -join ', ')" | Add-Content $sourceNote }
        }
        if (-not $vst3) {
            $exeAsset = $win | Where-Object { $_.name -match "\.exe$" -and $_.name -notmatch "(?i)windows7" } | Select-Object -First 1
            if ($exeAsset) {
                $airExe = Join-Path $env:RUNNER_TEMP $exeAsset.name   # not $exe: that is $Exe, the test program
                Invoke-WebRequest -Headers $headers -Uri $exeAsset.browser_download_url -OutFile $airExe
                Start-Process -FilePath $airExe -ArgumentList @("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-") -Wait
                $vst3 = Find-Vst3 (Join-Path $env:CommonProgramFiles "VST3")
                if ($vst3) { "Airwindows Consolidated: $($exeAsset.name) (installer)" | Add-Content $sourceNote }
            }
        }
        if (-not $vst3) { "Airwindows: no usable Windows asset among: $(($rel.assets | ForEach-Object name) -join ', ')" | Add-Content $sourceNote }
    } catch { "Airwindows download failed: $_" | Add-Content $sourceNote }
}
if ($vst3) {
    # also into the standard VST3 folder, so the runtime check's MicVST.exe scans real plug-ins
    $common = Join-Path $env:CommonProgramFiles "VST3"
    New-Item -ItemType Directory -Force -Path $common | Out-Null
    if (-not $vst3.StartsWith($common, [StringComparison]::OrdinalIgnoreCase)) {
        Copy-Item -Recurse -Force $vst3 $common
        $vst3 = Join-Path $common (Split-Path -Leaf $vst3)
    }
    "VST3 bundle used: $vst3" | Add-Content $sourceNote
    $env:MICVST_TEST_VST3 = $vst3
}

# Free VST3 effects for the vst3suite run (tools/ci-suite-plugins.ps1 sets $env:MICVST_TEST_VST3_DIR)
. (Join-Path $PSScriptRoot "ci-suite-plugins.ps1") -NoteFile (Join-Path $Out "vst3-suite-source.txt") -Extra ($(if ($vst3) { $vst3 } else { "" }))

# Network audit for the test process itself (same method as ci-runtime-check.ps1)
& auditpol /set /subcategory:"Filtering Platform Connection" /success:enable /failure:enable | Out-Null
$auditStart = Get-Date

$failures = @()
function Run-Test([string] $name, [string[]] $arguments, [int] $timeoutSeconds) {
    $file = Join-Path $Out "$name.txt"
    $err = Join-Path $Out "$name.stderr.txt"
    $t0 = Get-Date
    $p = Start-Process -FilePath $Exe -ArgumentList $arguments -NoNewWindow -PassThru -RedirectStandardOutput $file -RedirectStandardError $err
    $handle = $p.Handle   # keeps the exit code available after the process ends
    $peakMb = 0
    while (-not $p.WaitForExit(1000)) {
        try { $p.Refresh(); $peakMb = [Math]::Max($peakMb, [Math]::Round($p.PeakWorkingSet64 / 1MB, 1)) } catch {}
        if (((Get-Date) - $t0).TotalSeconds -gt $timeoutSeconds) {
            $p.Kill()
            "TIMEOUT after $timeoutSeconds s (hang or deadlock) - process killed" | Add-Content $file
            $script:failures += "$name (timeout)"
            return
        }
    }
    $code = $p.ExitCode
    $secs = [Math]::Round(((Get-Date) - $t0).TotalSeconds, 1)
    "-- exit code $code, $secs s, peak working set $peakMb MB" | Add-Content $file
    if ($code -ne 0) { $script:failures += "$name (exit code $code)" }
    Write-Output "$name : exit $code, $secs s"
}

$env:ETEST_LOG = "1"   # MicVST's log lines on stdout once: shows whether the plug-in thread got MMCSS
Run-Test "edits"   @("edits")   300
Remove-Item Env:ETEST_LOG
Run-Test "devices" @("devices") 180
Run-Test "startxrun" @("startxrun") 120   # "kesinti": Windows' start-of-stream discontinuity flag is not a dropout, a later one is
Run-Test "hotplug" @("hotplug") 300
Run-Test "openfail" @("openfail") 300   # a listed device that cannot be opened: retries, log lines, recovery
Run-Test "faults"  @("faults")  300
Run-Test "msgblock" @("msgblock", "1", "3000", "6") 300   # UI thread blocked 3 s right after each device change
Run-Test "latency" @("latency") 300
Run-Test "buffers" @("buffers") 600
Run-Test "mono"    @("mono")    120
Run-Test "flutter" @("flutter") 300
Run-Test "quality" @("quality") 900
Run-Test "gain"    @("gain", "60") 900   # built-in Kazanç: level, channels, slider moved 60 s, edits, restore
Run-Test "autogain" @("autogain") 900    # built-in Otomatik Kazanç: speech levels, shouting, silence, clipping, position, on/off
Run-Test "priority" @("priority") 1200   # Islem onceligi: each class applied/read back, audio under full CPU load, effect on other programs, switching
Run-Test "monochain" @("monochain") 300  # mono microphone into stereo plug-ins: both cable channels
Run-Test "cues"    @("cues")    300     # Bildirim sesleri: in sync with Sustur, once per change, never into the cable
Run-Test "record"  @("record")  600     # Kayit: the cable unaffected, the file exact, 30 start/stop in every format, CPU, memory
Run-Test "effects" @("effects") 1200     # built-in effects: each alone, chain edits, all 24 at once, live parameters, save/restore, mono
if ($vst3) { Run-Test "vst3" @("vst3") 900 }
if ($env:MICVST_TEST_VST3_DIR) {
    # what every plug-in offers (buses, latency, parameters with their value texts)
    $infoFile = Join-Path $Out "vst3info.txt"
    "" | Set-Content $infoFile
    foreach ($b in $suiteBundles) {
        $tmp = Join-Path $Out "vst3info-one.txt"
        $p = Start-Process -FilePath $Exe -ArgumentList @("vst3info", "`"$($b.FullName)`"") -NoNewWindow -PassThru -RedirectStandardOutput $tmp -RedirectStandardError (Join-Path $Out "vst3info.stderr.txt")
        if (-not $p.WaitForExit(120000)) { $p.Kill(); "TIMEOUT: $($b.Name)" | Add-Content $infoFile }
        Get-Content $tmp | Add-Content $infoFile
        Remove-Item $tmp -ErrorAction SilentlyContinue
    }
    Run-Test "vst3suite" @("vst3suite") 3000   # real plug-ins one by one (with their windows) and together
}

# Fidelity: exact input vs. recorded cable output (analysed below)
$dumps = @(
    @("i_noise_m12",  "noise",     "-12",  "20", "empty"),
    @("i_noise_m60",  "noise",     "-60",  "20", "empty"),
    @("i_mt_m3",      "multitone", "-3",   "20", "empty"),
    @("i_mt_m40",     "multitone", "-40",  "20", "empty"),
    @("i_sine_m01",   "sine",      "-0.1", "20", "empty"),
    @("i_voice",      "voice",     "-6",   "20", "empty"),
    @("i_music",      "music",     "-6",   "20", "empty"),
    @("i_silence",    "silence",   "0",    "12", "empty"),
    @("i_mono_noise", "noise",     "-12",  "20", "empty", "1"),
    @("i_noise_b128", "noise",     "-12",  "20", "empty", "2", "128"),
    @("i_noise_b2048","noise",     "-12",  "20", "empty", "2", "2048"),
    @("i_mt_44to48",  "multitone", "-3",   "20", "empty", "2", "441", "44100", "48000"),
    @("i_mt_48to441", "multitone", "-3",   "20", "empty", "2", "480", "48000", "44100")
)
$dumps += ,@("i_noise_testdelay", "noise", "-12", "20", "testdelay240")   # reported latency 240 samples
$dumps += ,@("i_voice_cablestall", "voice", "-6", "20", "cablestall")     # cable driver stalls 60 ms every 3 s -> trims
$dumps += ,@("i_music_cablestall", "music", "-6", "20", "cablestall")
$dumps += ,@("i_voice_pluginstall", "voice", "-6", "20", "pluginstall")   # plug-in stalls 200 ms every 3 s -> underrun + skip
if ($vst3) { $dumps += ,@("i_mt_vst3", "multitone", "-3", "20", "vst3"); $dumps += ,@("i_mt_vst3byp", "multitone", "-3", "20", "vst3-bypassed") }
$dumps += ,@("i_voice_m40", "voice", "-40", "20", "empty")   # quiet and normal voice through the empty chain
$dumps += ,@("i_voice_m20", "voice", "-20", "20", "empty")
if ($env:MICVST_TEST_VST3_DIR) {
    # the cable must equal the suite's plug-ins run offline in the order MicVST shows them; the
    # negative control compares it with the reverse order (must be far off)
    $dumps += ,@("s_suite_voice", "voice", "-6", "20", "suite")
    $dumps += ,@("s_suite_music", "music", "-6", "20", "suite")
    $dumps += ,@("s_suite_wrongref", "voice", "-6", "20", "suite-wrongref")
}
$names = @()
foreach ($d in $dumps) { Run-Test ("dump_" + $d[0]) (@("dump") + $d) 180; $names += $d[0] }

$env:ETEST_LONG_AUTOGAIN = "1"   # long runs with Otomatik Kazanç at the end of the chain
Run-Test "long" @("long", "$LongMinutes") ([int] ($LongMinutes * 60 + 600))
Run-Test "chaos" @("chaos", "10") 1500   # changes during playback every 20 s, device buffer change every 5 min
Remove-Item Env:ETEST_LONG_AUTOGAIN

try {
    python -m pip install --quiet numpy scipy 2>&1 | Out-Null
    python (Join-Path $PSScriptRoot "..\tests\enginetest\analyse_dump.py") @names 2>&1 | Set-Content -Encoding utf8 (Join-Path $Out "integrity.txt")
} catch { "analysis failed: $_" | Set-Content (Join-Path $Out "integrity.txt") }

try {
    $ev = @(Get-WinEvent -FilterHashtable @{ LogName = "Security"; Id = 5152, 5156, 5157, 5158, 5159; StartTime = $auditStart } -ErrorAction Stop |
            Where-Object { $_.Message -match "(?i)micvstenginetest\.exe" })
} catch { $ev = @() }
"Windows Filtering Platform events (connect/bind) of MicVSTEngineTest.exe during all runs: $($ev.Count)" | Set-Content (Join-Path $Out "network.txt")
$ev | Select-Object -First 20 | ForEach-Object { $_.Message -replace "\s+", " " } | Add-Content (Join-Path $Out "network.txt")

# the raw dumps are large: keep only the analysis and the metadata
Get-ChildItem $env:MICVST_TEST_OUT -Filter "*.f32" | Remove-Item

if ($failures.Count -gt 0) {
    "FAILED: $($failures -join ', ')" | Tee-Object -FilePath (Join-Path $Out "summary.txt")
    exit 1
}
"all engine-test runs finished (verdicts in the text files)" | Tee-Object -FilePath (Join-Path $Out "summary.txt")
