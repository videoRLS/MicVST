# Changelog

All notable changes to MicVST are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/), and this
project follows [Semantic Versioning](https://semver.org/).

## [1.2.1] - Unreleased

Focus: sound quality under heavy background load (e.g. a GPU/AI video render), no clicks when
the chain or the devices change, and a completely Turkish interface. Measurements and what could
not be tested: [docs/RAPOR.md](docs/RAPOR.md) (Turkish).

### Changed
- The plugin chain runs on its own thread (MMCSS "Pro Audio") behind a lock-free input queue; the
  microphone's capture thread only copies the audio (microseconds). In 1.2.0 the chain ran on the
  capture thread: whenever plugin blocks took longer than ~20 ms in total, Windows' capture buffer
  overflowed and the audio was lost ("microphone driver gaps"). If the chain stays slower than
  real time for more than 120 ms, the oldest audio is skipped with a crossfade at a
  waveform-matched point, so the latency stays bounded.
- Clock-drift and buffer-level control: beyond the learned clock drift the playback speed now
  changes by at most +-0.05 % (0.9 cent); 1.2.0 allowed +-0.5 % (8.6 cent) for seconds after every
  disturbance. Excess latency above 20 ms (e.g. after the cable stalled) is trimmed at once with a
  10 ms crossfade at the point where the waveforms match. The level estimate now accounts for the
  plugin thread's delay and the correction is smoothed in two stages: a constant clock drift is
  followed with < 2 ppm error (1.2.0: a slow wander of up to ~400 ppm) and the block-to-block
  speed jitter (phase noise, -84 dB at 1 kHz) is gone. Clock differences up to +-1000 ppm are
  learned completely. While blocks arrive late (the plugins fall behind, e.g. under a video
  render's CPU load) the drift is not learned: in the simulated render periods the learned value
  had wandered by up to +-170 ppm and then needed minutes to settle again; now it stays put.
- A larger safety buffer chosen while running takes effect at once (short fade-out, refill,
  fade-in) instead of over minutes.
- All texts shown to the user are Turkish: window, menus, dialogs, tooltips, tray, the usage
  screen, the log file (now with timestamps) and JUCE's own buttons and text-field menu.
- Displayed latency: the buffer part is now the mean time audio spends in the buffer plus the
  plugin thread's delay (1.2.0 counted about half a block too much).
- Unplugged devices: MicVST no longer falls back to another device. 1.2.0 (via JUCE's device
  manager) switched to the next available microphone or speaker when the chosen one disappeared,
  so a built-in microphone could go out on the stream, or the processed microphone on the
  speakers, and it stayed there after the device came back. Now the chosen devices stay selected
  (shown as "... (bağlı değil)"), the output is silent meanwhile, and they reopen by themselves
  within about a second when they are plugged in again.
- The log file shows the user folder as %USERPROFILE% (it contains the Windows user name and the
  log is often passed on for troubleshooting).
- JUCE patches (cmake/PatchJuce.cmake), now five: besides the three WASAPI fixes, the audio
  graph no longer allocates memory on the audio thread when a plugin reports a larger block
  (audio-thread probe: 0 allocations, 0 lock waits in all scenarios), and the device manager no
  longer opens a different device when the device list changes.

### Fixed
- A plugin that outputs NaN, infinite or absurd values (> +24 dBFS) no longer reaches the cable
  (or crashes the next plugin): the whole block is silenced and the sound fades in again (2 ms)
  once the plugin recovers; counted in the dropout tooltip and the log. A plugin that throws an
  exception costs one silent block.
- Clicks when the chain is edited during playback (add, remove, move, bypass): the output fades
  out (5 ms), the graph switches while muted, then fades in. Measured on virtual devices: 1.2.0
  clicked on 11 of 13 edits, 1.2.1 on none.
- Clicks on the cable when MicVST quits or a device/buffer setting is changed: the output now
  fades out and drains before the device stops (1.2.0: two clicked samples per change).
- VST3 files that contain several plugins (Waves WaveShell, LSP, ...): every entry came back as
  the first plugin of the file after a restart (since 1.1.1). The plugin's identity is now stored;
  old configs still load.
- Non-ASCII text in the log file was double-encoded (UTF-8 read as Latin-1).
- The outer labels of the meter's dB scale were clipped.

## [1.2.0] - Unreleased

Focus: no more crackles under heavy load, and a fully offline app. Full analysis, measurements
and what could not be tested: [docs/RAPOR.md](docs/RAPOR.md) (Turkish).

### Changed
- New audio engine: microphone and cable run on two separate realtime threads (input-only and
  output-only WASAPI devices) instead of one shared device. Between them a lock-free safety buffer
  with a high-quality resampler absorbs processing spikes and continuously compensates the clock
  drift of the two devices. 1.1.1 filled every input shortfall with silence and never compensated
  drift, which produced periodic clicks and crackles under load.
- New "Safety" row: "Auto (recommended)" starts at 10 ms and adds 5 ms whenever the output runs
  dry (max. 80 ms); fixed values 5-80 ms are available. Costs roughly 10-20 ms of latency in Auto.
- Realtime priority: the audio threads use MMCSS "Pro Audio" priority HIGH (was NORMAL) and opt
  out of Windows power throttling; the process runs "above normal" and opts out of EcoQoS/timer
  throttling. Plugin scan processes run "below normal".
- JUCE 8.0.13 -> 9.0.4 (WASAPI start/stop race fix, VST3 hosting fixes) plus a small patch for
  JUCE's WASAPI backend (input-only devices drain a backlog immediately and survive timeouts, no
  busy spinning while a session restarts).
- Second status line: CPU load of the plugin chain, safety buffer, clock drift and a dropout
  counter (orange with a cause breakdown in the tooltip).

### Removed
- All network code: the opt-in update check (WinINet request to GitHub), the consent dialog, the
  update tray bubble and all clickable web links. Old config files load fine; the update keys are
  ignored. `tools/firewall-block.ps1` optionally blocks the whole process (incl. plugins) in the
  Windows Firewall.

### Fixed
- Saving the state held every plugin's audio lock while the plugin serialised itself - the audio
  thread could stall behind the UI thread (priority inversion).
- No denormal protection on the audio thread (decaying plugin tails could cause CPU spikes).
- Changing the chain rebuilt the audio graph once per connection; the audio thread could hit
  intermediate states without connections (short silence/clicks). Now one rebuild per change.
- Bypassed plugins were still processed every block (CPU); they are now suspended and start with
  a clean state when switched back on.
- A chain that ends mono was sent to the left cable channel only.
- Microphone and cable at different sample rates (e.g. 44.1 kHz mic, 48 kHz cable) now work.
- Plugin editor changes are saved when the editor closes; the autostart toggle no longer reads
  the registry 30 times per second.

## [1.1.1] - 2026-07-27

### Added
- "Reset app (clear all data)..." in the Manage VST3 Folders menu: deletes all settings
  and the plugin cache, then closes MicVST for a fresh first-run setup.
- Scan diagnostics: skip reasons now include the scanner's exit code where relevant
  (e.g. "failed (exit 0xC0000005)"), and the scan log records why a file is rescanned.

### Fixed
- Plugins shipped as bundle folders (Acustica, Minimal Audio, UADx, ...) were rescanned
  on every start when vendor background services wrote into their bundles - the rescan
  check now tracks the plugin binary inside the bundle instead of the folder itself.
- A plugin that crashes the scanner mid-enumeration (e.g. Waves WaveShell) no longer
  loses everything: plugins found before the crash are kept and usable, and the file is
  marked "crashed (N plugin(s) rescued)" - use "Retry skipped plugins" to try again.

## [1.1.0] - 2026-07-22

### Added
- Searchable plugin picker: "+ Plugin" now opens a search box with live filtering
  (grouped by manufacturer when the search is empty). Built-ins are included.
- "Skip" button while scanning: skip the plugin that is currently being scanned.
- "Retry skipped plugins" (Manage VST3 Folders menu): rescans skipped plugins with a
  generous 10-minute timeout - lets huge shell plugins like Waves WaveShell finish.
- Buffer size dropdown (low-latency mode): when the selected devices support multiple
  buffer sizes, a "Buffer" row appears in the device panel ("Auto" = device default).

### Changed
- Scan timeout raised from 30 s to 120 s, so large shell plugins (e.g. Waves WaveShell)
  usually finish on the initial scan already.
- All standard VST3 locations are scanned (Program Files, %LOCALAPPDATA%, VST3_PATH).
- Audio devices now open in WASAPI low-latency mode when available (shared as fallback).

### Fixed
- Plugins that write into their own bundle during load (UAD, Acustica, ...) were
  rescanned on every start; their cache timestamp is now taken after the scan finishes.
- Plugin updates that change the internal plugin ID no longer cause endless rescans.
- Removing a custom folder like "C:\Plugins" no longer prunes plugins from sibling
  folders like "C:\Plugins2".
- Editing the plugin chain or the custom VST3 folders no longer resets the update-check
  consent and the saved window position in the config file.

## [1.0.3] - 2026-07-21

### Changed
- Window and tray icon now appear immediately; the VST3 scan runs in the background with
  visible progress (count + plugin name).
- "Manage custom VST3 Folders" is now "Manage VST3 Folders" and its menu also offers
  "Rescan all plugins" (previously only in the "+ Plugin" menu).

### Fixed
- A hanging or crashing VST3 no longer blocks startup invisibly: each plugin is scanned in an
  isolated helper process with a 30 s timeout and gets skipped (with a hint in the UI) instead.
- Scan results are cached (`plugin_cache.xml`) - startup no longer rescans every plugin.

## [1.0.2] - 2026-06-05

### Added
- Optional, **opt-in Auto-Update-Check** (off by default; one-time consent prompt on first
  start): checks the GitHub releases API once per startup, and on a newer version turns the
  version number into a download link and shows a one-time tray notification. No telemetry,
  no auto-installer.
- The window now **remembers its size and position** across restarts.
- Live **end-to-end latency** readout (now includes plugin/graph latency) shown right under
  the device list, together with the sample rate and buffer size.
- **Per-plugin latency** shown in each plugin row (left of "Bypass"), updated live.

### Changed
- Sample rate and buffer size are now **read-only** info (they're fixed by Windows shared
  audio anyway); the *Advanced* toggle and their dropdowns were removed.
- Smaller, more compact default window; extra height now extends the **plugin list**.

### Added (docs)
- README **Latency** section explaining the shared-audio path and why ASIO4All won't help.

## [1.0.1] - 2026-06-04

### Added
- **How To** screen - a quick setup guide with clickable links (download VB-Cable,
  "Check for updates on GitHub").
- Clickable **version number** in the main window (opens the GitHub repo).

### Changed
- Moved the status line (Active / sample rate / latency) out of the main window into the
  **Advanced** section of the device panel.

## [1.0.0] - 2026-06-04

### Added
- Initial public release.
- Route one microphone through a **VST3 plugin chain** into a **virtual audio cable**
  (VB-Cable, VoiceMeeter, Virtual Audio Cable), usable as a mic in any app.
- **Automatic cable detection** with an in-app download hint when none is installed.
- Built-in **Mono → Stereo** and **Stereo → Mono** nodes; channel-aware routing.
- **Drag-to-reorder** plugin list, per-row bypass, remove with confirmation, per-plugin
  editor windows.
- **Manage custom VST3 folders**; crash-resistant plugin scan (a plugin that crashes the
  scan is remembered and skipped next time).
- Horizontal **in/out level meters** with a dB scale.
- **Tray app** with silent autostart and an autostart checkbox.
- Persistent device + plugin settings (`%APPDATA%\MicVST\config.xml`).
- Portable, statically linked `.exe` (no installer, no Visual C++ Redistributable).
- GitHub Actions CI: build + unit tests on every push/PR, release build on tag.

[1.1.1]: https://github.com/philipz794/MicVST/releases/tag/v1.1.1
[1.1.0]: https://github.com/philipz794/MicVST/releases/tag/v1.1.0
[1.0.3]: https://github.com/philipz794/MicVST/releases/tag/v1.0.3
[1.0.2]: https://github.com/philipz794/MicVST/releases/tag/v1.0.2
[1.0.1]: https://github.com/philipz794/MicVST/releases/tag/v1.0.1
[1.0.0]: https://github.com/philipz794/MicVST/releases/tag/v1.0.0
