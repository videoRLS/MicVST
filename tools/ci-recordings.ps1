# The recorder's test files (MicVSTTests, MICVST_RECORDER_KEEP) opened by other programs:
#  * ffprobe / ffmpeg: codec, sample format, rate, channels, length; a full decode with errors counted
#  * Windows itself (Shell property handlers, Media Foundation): length and bit rate as Explorer shows them
# Writes a Markdown table. A program that is not there is reported as skipped, never as passed.
param(
    [Parameter(Mandatory = $true)][string] $Dir,
    [Parameter(Mandatory = $true)][string] $Out
)
$ErrorActionPreference = "Continue"
$files = @(Get-ChildItem $Dir -File -ErrorAction SilentlyContinue | Sort-Object Name)
$lines = @("# Recordings opened by other programs", "", "Files: $($files.Count) (from the unit tests: every format, bit depth and rate)", "")

$ffprobe = Get-Command ffprobe -ErrorAction SilentlyContinue
if (-not $ffprobe) {
    choco install ffmpeg --no-progress -y *> $null
    $env:Path = [Environment]::GetEnvironmentVariable("Path", "Machine") + ";" + [Environment]::GetEnvironmentVariable("Path", "User")
    $ffprobe = Get-Command ffprobe -ErrorAction SilentlyContinue
}
$fail = 0
if ($ffprobe) {
    $lines += "## ffprobe / ffmpeg ($((& ffprobe -version | Select-Object -First 1)))", ""
    $lines += "| File | codec | sample format | rate | channels | bits | samples (decoded) | decode errors | result |"
    $lines += "|---|---|---|---:|---:|---:|---:|---:|---|"
    foreach ($f in $files) {
        $j = & ffprobe -v error -show_entries "stream=codec_name,sample_fmt,sample_rate,channels,bits_per_raw_sample,bits_per_sample" -of json $f.FullName | ConvertFrom-Json
        $s = $j.streams[0]
        $tmp = [IO.Path]::GetTempFileName()
        $err = & ffmpeg -v error -i $f.FullName -f f32le -ac 1 -y $tmp 2>&1
        $samples = [int64]((Get-Item $tmp).Length / 4)
        Remove-Item $tmp
        $errors = @($err | Where-Object { $_ }).Count
        # expected from the file name: <fmt>_<q>[_<level>]_<rate>_<ch>[_from<rate>]
        $parts = $f.BaseName -split "_"
        $rate = [int]($parts | Where-Object { $_ -match '^\d{5,6}$' } | Select-Object -First 1)
        $ch = [int]$parts[[array]::IndexOf($parts, "$rate") + 1]
        $ok = ($errors -eq 0) -and ([int]$s.sample_rate -eq $rate) -and ([int]$s.channels -eq $ch) -and ($samples -gt 0)
        if (-not $ok) { $fail++ }
        $bits = if ($s.bits_per_raw_sample) { $s.bits_per_raw_sample } else { $s.bits_per_sample }
        $lines += "| $($f.Name) | $($s.codec_name) | $($s.sample_fmt) | $($s.sample_rate) | $($s.channels) | $bits | $samples | $errors | $(if ($ok) { 'BAŞARILI' } else { 'BAŞARISIZ' }) |"
    }
} else {
    $lines += "## ffprobe / ffmpeg", "", "ATLANDI: ffmpeg could not be installed on this runner."
}

$lines += "", "## Windows (Shell property handlers)", ""
try {
    $shell = New-Object -ComObject Shell.Application
    $folder = $shell.Namespace((Resolve-Path $Dir).Path)
    $cols = @{}
    for ($i = 0; $i -lt 400; $i++) { $n = $folder.GetDetailsOf($null, $i); if ($n) { $cols[$n] = $i } }
    $lenCol = $cols["Length"]; $brCol = $cols["Bit rate"]
    $lines += "| File | length (Windows) | bit rate (Windows) |", "|---|---|---|"
    $shown = 0
    foreach ($f in $files) {
        $item = $folder.ParseName($f.Name)
        $len = if ($lenCol -ne $null) { $folder.GetDetailsOf($item, $lenCol) } else { "" }
        $br = if ($brCol -ne $null) { $folder.GetDetailsOf($item, $brCol) } else { "" }
        if ($len) { $shown++ }
        $lines += "| $($f.Name) | $len | $br |"
    }
    $lines += "", "Windows showed a length for $shown of $($files.Count) files (an empty cell: this Windows has no handler for that format - Server editions often lack Media Foundation)."
} catch {
    $lines += "ATLANDI: Shell.Application not available: $_"
}

$lines += "", "ffmpeg failures: $fail"
$lines | Set-Content $Out -Encoding utf8
$lines | Write-Output
if ($fail -gt 0) { exit 1 }
