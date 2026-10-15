# Optional "closed box" hardening: blocks ALL network traffic of MicVST.exe with a Windows Firewall
# rule (inbound + outbound). Needs an elevated PowerShell.
#
# MicVST itself contains no network code since 1.2.0. This rule additionally covers the third-party
# VST3 plug-ins, which run INSIDE the MicVST process and could go online on their own (licence or
# update checks). Plug-ins that need online activation must be activated beforehand (e.g. with
# their own installer / licence manager - those run as separate programs and are not affected).
# The rule is bound to the exe path: run the script again after moving MicVST.exe.
#
# Usage (PowerShell as administrator):
#   .\tools\firewall-block.ps1 -Exe "C:\Tools\MicVST\MicVST.exe"    # add (or refresh) the rule
#   .\tools\firewall-block.ps1 -Remove                              # remove the rule again
param(
    [string] $Exe = "",
    [switch] $Remove
)
$ErrorActionPreference = "Stop"
$name = "MicVST - block all network access"

$principal = New-Object Security.Principal.WindowsPrincipal([Security.Principal.WindowsIdentity]::GetCurrent())
if (-not $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Please run PowerShell as administrator."
}

Get-NetFirewallRule -DisplayName $name -ErrorAction SilentlyContinue | Remove-NetFirewallRule
if ($Remove) { Write-Output "Firewall rule '$name' removed."; return }

if (-not $Exe) { throw "Pass the full path of MicVST.exe with -Exe" }
$Exe = (Resolve-Path -LiteralPath $Exe).Path
foreach ($direction in "Outbound", "Inbound") {
    New-NetFirewallRule -DisplayName $name -Direction $direction -Program $Exe -Action Block -Profile Any | Out-Null
}
Write-Output "MicVST is now blocked from all network access (inbound + outbound): $Exe"
