# Security Policy

## Supported versions

Only the **latest release** of MicVST receives fixes. Please update to the newest version
(see [Releases](https://github.com/philipz794/MicVST/releases)) before reporting an issue.

## Scope

MicVST is a small **local** Windows app: it processes audio between local devices and a virtual
audio cable. Since 1.2.0 it contains **no network code at all** (no update check, no telemetry, no
web links; the CI build fails if the exe imports a networking library). Third-party VST3 plugins,
however, run inside the MicVST process and can do whatever they want - `tools/firewall-block.ps1`
blocks the whole process in the Windows Firewall. The main areas of interest are those plugins and
the config/log files MicVST writes under `%APPDATA%\MicVST\`.

## Reporting a vulnerability

Please report security issues **privately** via GitHub - do not open a public issue.

Use **"Report a vulnerability"** in the repository's **Security** tab
(*Security → Advisories → Report a vulnerability*). This opens a private security advisory that is
visible only to the maintainer.

I'll acknowledge your report as soon as I can and work on a fix. Thanks for helping keep
MicVST and its users safe.
