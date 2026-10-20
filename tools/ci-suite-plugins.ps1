# Downloads the free VST3 effects of the engine test's vst3suite (and the long runs with a real
# plug-in chain) into one folder and sets $env:MICVST_TEST_VST3_DIR to it (dot-source this script).
# They stand in for the user's commercial plug-ins (same kinds of processing); the downloads are the
# test's own - MicVST itself never connects anywhere. Each source is optional: what cannot be
# fetched is noted in $NoteFile and simply not tested.
param(
    [Parameter(Mandatory = $true)][string] $NoteFile,
    [string] $Extra = ""   # another bundle to add (Airwindows from ci-enginetest.ps1)
)
$suiteNote = $NoteFile
$vst3 = $Extra
$suiteDir = Join-Path $env:RUNNER_TEMP "suite-vst3"
New-Item -ItemType Directory -Force -Path $suiteDir | Out-Null
"" | Set-Content $suiteNote
function Add-SuiteBundles([string] $from, [string] $label) {
    $found = @(Get-ChildItem -Recurse -Directory $from -Filter "*.vst3" -ErrorAction SilentlyContinue | Where-Object { $_.Parent.Name -notmatch "\.vst3$" -and $_.FullName -notmatch "\\Contents\\" })
    if ($found.Count -eq 0) { $found = @(Get-ChildItem -Recurse -File $from -Filter "*.vst3" -ErrorAction SilentlyContinue) }
    if ($found.Count -eq 0) { "${label}: no .vst3 found" | Add-Content $suiteNote; return }
    foreach ($b in $found) {
        Copy-Item -Recurse -Force $b.FullName $suiteDir
        "${label}: $($b.Name)" | Add-Content $suiteNote
    }
}
function Get-Download([string] $url, [string] $file) {
    Invoke-WebRequest -Headers @{ "User-Agent" = "Mozilla/5.0 micvst-ci" } -Uri $url -OutFile $file -MaximumRedirection 10
}
$zipSources = @(
    @("RNNoise 1.10 (werman, GPL)", "https://github.com/werman/noise-suppression-for-voice/releases/download/v1.10/win-rnnoise.zip"),
    @("Fire 2.0.0 (multiband saturation, AGPL)", "https://github.com/jerryuhoo/Fire/releases/download/v2.0.0/Fire-2.0.0-Windows.zip"),
    @("TDR VOS SlickEQ 1.3.9 (free edition)", "https://www.tokyodawn.net/labs/SlickEQ/1.3.9/TDR%20VOS%20SlickEQ%20(no%20installer).zip")
)
foreach ($src in $zipSources) {
    try {
        $zip = Join-Path $env:RUNNER_TEMP ("suite-" + [IO.Path]::GetRandomFileName() + ".zip")
        Get-Download $src[1] $zip
        $dst = Join-Path $env:RUNNER_TEMP ("suite-x-" + [IO.Path]::GetRandomFileName())
        Expand-Archive -Force $zip $dst
        # nested archives (some vendors zip the platform folders separately)
        Get-ChildItem -Recurse -File $dst -Filter "*.zip" | Where-Object { $_.Name -match "(?i)win|x64|vst3" } | ForEach-Object { Expand-Archive -Force $_.FullName (Join-Path $dst $_.BaseName) }
        Add-SuiteBundles $dst $src[0]
    } catch { "$($src[0]): download/extract failed: $_" | Add-Content $suiteNote }
}
$msiSources = @(
    @("ZL Equalizer 2 1.4.1 (dynamic EQ, AGPL)", "https://github.com/ZL-Audio/ZLEqualizer/releases/download/1.4.1/ZL.Equalizer.2-1.4.1-Windows-x86-64.msi"),
    @("ZL Compressor 0.6.1 (AGPL)", "https://github.com/ZL-Audio/ZLCompressor/releases/download/0.6.1/ZL.Compressor-0.6.1-Windows-x86-64.msi")
)
foreach ($src in $msiSources) {
    try {
        $msi = Join-Path $env:RUNNER_TEMP ("suite-" + [IO.Path]::GetRandomFileName() + ".msi")
        Get-Download $src[1] $msi
        $dst = Join-Path $env:RUNNER_TEMP ("suite-m-" + [IO.Path]::GetRandomFileName())
        # administrative install = unpack only, nothing is installed
        $p = Start-Process msiexec.exe -ArgumentList @("/a", "`"$msi`"", "/qn", "TARGETDIR=`"$dst`"") -Wait -PassThru
        if ($p.ExitCode -ne 0) { "$($src[0]): msiexec /a exit $($p.ExitCode)" | Add-Content $suiteNote }
        Add-SuiteBundles $dst $src[0]
    } catch { "$($src[0]): download/unpack failed: $_" | Add-Content $suiteNote }
}
# Klanghelm DC1A3 (free; the user's own plug-in): installer only - silent install with a time limit
try {
    $exe = Join-Path $env:RUNNER_TEMP "dc1a3-setup.exe"
    Get-Download "https://klanghelm.com/free/DC1A3dl.php?id=1" $exe
    $head = [IO.File]::ReadAllBytes($exe)[0..1]
    if ($head[0] -eq 0x50 -and $head[1] -eq 0x4B) {   # a zip after all
        $dst = Join-Path $env:RUNNER_TEMP "dc1a3"; Expand-Archive -Force $exe $dst
        Get-ChildItem -Recurse -File $dst -Include "*.exe","*.msi" | ForEach-Object { "DC1A3: archive contains $($_.Name)" | Add-Content $suiteNote }
        Add-SuiteBundles $dst "Klanghelm DC1A3"
    } elseif ($head[0] -eq 0x4D -and $head[1] -eq 0x5A) {   # an .exe installer
        $before = @(Get-ChildItem -Directory (Join-Path $env:CommonProgramFiles "VST3") -ErrorAction SilentlyContinue | ForEach-Object Name)
        foreach ($instArgs in @(@("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-"), @("/S"))) {
            $p = Start-Process -FilePath $exe -ArgumentList $instArgs -PassThru
            if (-not $p.WaitForExit(180000)) { $p.Kill(); "DC1A3: installer $($instArgs -join ' ') timed out" | Add-Content $suiteNote; continue }
            $new = @(Get-ChildItem -Directory (Join-Path $env:CommonProgramFiles "VST3") -ErrorAction SilentlyContinue | Where-Object { $_.Name -match "(?i)DC1A" })
            if ($new.Count -gt 0) { foreach ($b in $new) { Copy-Item -Recurse -Force $b.FullName $suiteDir; "Klanghelm DC1A3: $($b.Name) (installer $($instArgs -join ' '))" | Add-Content $suiteNote }; break }
        }
    } else { "DC1A3: the download is no installer ($((Get-Item $exe).Length) bytes)" | Add-Content $suiteNote }
} catch { "DC1A3: download/install failed: $_" | Add-Content $suiteNote }
if ($vst3) { Copy-Item -Recurse -Force $vst3 $suiteDir; "Airwindows Consolidated: $(Split-Path -Leaf $vst3)" | Add-Content $suiteNote }
$suiteBundles = @(Get-ChildItem $suiteDir -Filter "*.vst3" -ErrorAction SilentlyContinue)
"suite folder: $($suiteBundles.Count) bundles: $(($suiteBundles | ForEach-Object Name) -join ', ')" | Add-Content $suiteNote
if ($suiteBundles.Count -gt 0) { $env:MICVST_TEST_VST3_DIR = $suiteDir }

