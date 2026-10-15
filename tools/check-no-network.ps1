# Fails if the given executable imports any networking DLL ("closed box" check for MicVST).
# Usage: ./tools/check-no-network.ps1 <path-to-exe> [<imports-output-file>]
param(
    [Parameter(Mandatory = $true)][string] $Exe,
    [string] $ImportsOut = ""
)
$ErrorActionPreference = "Stop"

$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
$dumpbin = & $vswhere -latest -products * -find "VC\Tools\MSVC\*\bin\Hostx64\x64\dumpbin.exe" | Select-Object -First 1
if (-not $dumpbin) { throw "dumpbin.exe not found" }

$text = & $dumpbin /nologo /dependents $Exe | Out-String
$dlls = [regex]::Matches($text, "(?im)^\s+([A-Za-z0-9_\-\.]+\.dll)\s*$") | ForEach-Object { $_.Groups[1].Value.ToUpperInvariant() } | Sort-Object -Unique
if ($ImportsOut) { $dlls | Set-Content -Encoding utf8 $ImportsOut }

$network = @("WININET.DLL", "WINHTTP.DLL", "WS2_32.DLL", "WSOCK32.DLL", "MSWSOCK.DLL", "URLMON.DLL",
             "IPHLPAPI.DLL", "DNSAPI.DLL", "WEBSOCKET.DLL", "NETAPI32.DLL", "HTTPAPI.DLL", "WINHTTPCOM.DLL")
$found = $dlls | Where-Object { $network -contains $_ }

Write-Output "Imported DLLs of $(Split-Path -Leaf $Exe): $($dlls -join ', ')"
if ($found) {
    Write-Output "::error::network libraries imported: $($found -join ', ')"
    exit 1
}
Write-Output "OK: no networking library is imported (WinINet/WinHTTP/Winsock/URLMon/...)."
