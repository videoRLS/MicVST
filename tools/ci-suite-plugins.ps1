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
        $suiteZip = Join-Path $env:RUNNER_TEMP ("suite-" + [IO.Path]::GetRandomFileName() + ".zip")
        Get-Download $src[1] $suiteZip
        $suiteDst = Join-Path $env:RUNNER_TEMP ("suite-x-" + [IO.Path]::GetRandomFileName())
        Expand-Archive -Force $suiteZip $suiteDst
        # nested archives (some vendors zip the platform folders separately)
        Get-ChildItem -Recurse -File $suiteDst -Filter "*.zip" | Where-Object { $_.Name -match "(?i)win|x64|vst3" } | ForEach-Object { Expand-Archive -Force $_.FullName (Join-Path $suiteDst $_.BaseName) }
        Add-SuiteBundles $suiteDst $src[0]
    } catch { "$($src[0]): download/extract failed: $_" | Add-Content $suiteNote }
}
$msiSources = @(
    @("ZL Equalizer 2 1.4.1 (dynamic EQ, AGPL)", "https://github.com/ZL-Audio/ZLEqualizer/releases/download/1.4.1/ZL.Equalizer.2-1.4.1-Windows-x86-64.msi"),
    @("ZL Compressor 0.6.1 (AGPL)", "https://github.com/ZL-Audio/ZLCompressor/releases/download/0.6.1/ZL.Compressor-0.6.1-Windows-x86-64.msi")
)
foreach ($src in $msiSources) {
    try {
        $suiteMsi = Join-Path $env:RUNNER_TEMP ("suite-" + [IO.Path]::GetRandomFileName() + ".msi")
        Get-Download $src[1] $suiteMsi
        $suiteDst = Join-Path $env:RUNNER_TEMP ("suite-m-" + [IO.Path]::GetRandomFileName())
        # administrative install = unpack only, nothing is installed
        $msiProc = Start-Process msiexec.exe -ArgumentList @("/a", "`"$suiteMsi`"", "/qn", "TARGETDIR=`"$suiteDst`"") -Wait -PassThru
        if ($msiProc.ExitCode -ne 0) { "$($src[0]): msiexec /a exit $($msiProc.ExitCode)" | Add-Content $suiteNote }
        Add-SuiteBundles $suiteDst $src[0]
    } catch { "$($src[0]): download/unpack failed: $_" | Add-Content $suiteNote }
}
# Klanghelm DC1A3 (free; the user's own plug-in): a zip with an installer (or the installer itself) -
# silent install with a time limit. (Variable names here must not clash with the caller's: $Exe!)
function Install-Silently([string] $installer) {
    foreach ($instArgs in @(@("/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/SP-"), @("/S"), @("/quiet"))) {
        $proc = Start-Process -FilePath $installer -ArgumentList $instArgs -PassThru
        if (-not $proc.WaitForExit(180000)) { $proc.Kill(); "DC1A3: installer $($instArgs -join ' ') timed out" | Add-Content $suiteNote; continue }
        $found = @(Get-ChildItem -Directory (Join-Path $env:CommonProgramFiles "VST3") -ErrorAction SilentlyContinue | Where-Object { $_.Name -match "(?i)DC1A" })
        $found += @(Get-ChildItem -File (Join-Path $env:CommonProgramFiles "VST3") -ErrorAction SilentlyContinue | Where-Object { $_.Name -match "(?i)DC1A.*\.vst3$" })
        if ($found.Count -gt 0) {
            foreach ($b in $found) { Copy-Item -Recurse -Force $b.FullName $suiteDir; "Klanghelm DC1A3: $($b.Name) (installer $($instArgs -join ' '), exit $($proc.ExitCode))" | Add-Content $suiteNote }
            return $true
        }
        "DC1A3: installer $($instArgs -join ' ') exit $($proc.ExitCode), no DC1A3 in the VST3 folder" | Add-Content $suiteNote
    }
    return $false
}
try {
    $dcDownload = Join-Path $env:RUNNER_TEMP "dc1a3-download.bin"
    Get-Download "https://klanghelm.com/free/DC1A3dl.php?id=1" $dcDownload
    $dcHead = [IO.File]::ReadAllBytes($dcDownload)[0..1]
    $dcInstallers = @()
    if ($dcHead[0] -eq 0x50 -and $dcHead[1] -eq 0x4B) {   # zip
        $dcDir = Join-Path $env:RUNNER_TEMP "dc1a3"
        Copy-Item $dcDownload "$dcDownload.zip"; Expand-Archive -Force "$dcDownload.zip" $dcDir
        Add-SuiteBundles $dcDir "Klanghelm DC1A3 (in the archive)"
        $dcInstallers = @(Get-ChildItem -Recurse -File $dcDir -Include "*.exe" | Where-Object { $_.Name -notmatch "(?i)mac|uninst" } | ForEach-Object FullName)
    } elseif ($dcHead[0] -eq 0x4D -and $dcHead[1] -eq 0x5A) {
        $dcExe = Join-Path $env:RUNNER_TEMP "dc1a3-setup.exe"; Copy-Item $dcDownload $dcExe; $dcInstallers = @($dcExe)
    } else { "DC1A3: the download is neither a zip nor an installer ($((Get-Item $dcDownload).Length) bytes)" | Add-Content $suiteNote }
    foreach ($inst in $dcInstallers) { "DC1A3: installer $(Split-Path -Leaf $inst)" | Add-Content $suiteNote; if (Install-Silently $inst) { break } }
} catch { "DC1A3: download/install failed: $_" | Add-Content $suiteNote }
if ($vst3) { Copy-Item -Recurse -Force $vst3 $suiteDir; "Airwindows Consolidated: $(Split-Path -Leaf $vst3)" | Add-Content $suiteNote }
$suiteBundles = @(Get-ChildItem $suiteDir -Filter "*.vst3" -ErrorAction SilentlyContinue)
"suite folder: $($suiteBundles.Count) bundles: $(($suiteBundles | ForEach-Object Name) -join ', ')" | Add-Content $suiteNote
if ($suiteBundles.Count -gt 0) { $env:MICVST_TEST_VST3_DIR = $suiteDir }

