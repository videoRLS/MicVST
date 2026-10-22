# Changelog

All notable changes to MicVST are documented here.
The format is based on [Keep a Changelog](https://keepachangelog.com/), and this
project follows [Semantic Versioning](https://semver.org/).

## [1.2.2] - Unreleased

- Kayıt (recording): the "● Kayıt" button (and the tray menu) records what goes to the cable into
  a file, with the running time on the button; stopping closes the file and reads it back to
  check it. "Ayarlar > Kayıt": WAV 16/24/32 bit PCM and 32 bit float (Broadcast WAV; RF64 past
  4 GB), FLAC 16/24 bit with compression 0-8, MP3 (LAME 4.0, built in) CBR 128-320 kbps or VBR
  V0/V2/V4; 44.1 or 48 kHz (88.2-192 kHz when the microphone runs that fast) through a 140 dB
  linear-phase converter; stereo or mono; any folder (default Documents\MicVST Kayıtları).
  Default WAV 24 bit 48 kHz stereo. Names are date and time, never overwritten. The audio thread
  only copies into a 30 s lock-free buffer; a writer thread (above normal priority, below the
  audio) converts and writes; the WAV
  header is rewritten every second (a crash keeps everything up to the last second); a full disk
  or a write error stops the recording with a message. Settings saved in config.xml.
- Bildirim sesleri: a short sound when the microphone is muted / unmuted (Sustur: the button,
  the shortcut keys, the tray) - played in the same call as the mute itself, once per change,
  only on Windows' default output (never into the cable; if the default output is the cable,
  nothing plays). Default Ahşap (açılma / kapanma); 16 built-in sounds (Ahşap, Kadife, Damla, Tok,
  Kristal, Esinti, Yankı, Kıvılcım) and the user's own files in %APPDATA%\MicVST\Sesler (WAV,
  FLAC, MP3, AIFF, OGG, up to 10 s - listed by themselves). Ayarlar > Kısayollar: on / off, the
  unmute and the mute sound chosen separately with a listen button, volume, back to the default;
  the tray menu switches the sounds on and off. Saved in config.xml (cueSounds).
- Deneme kaydı: the playback on the headphones fades in and out (10 ms); before, it started and
  ended with a cut - a click (measured: one sample jumped by the full amplitude of the voice).
- The developer signature is no longer shown (main window footer, Ayarlar > Hakkında).
- İşlem önceliği (process priority) in the window: Normal (default), Normalin Üstünde, Yüksek.
  Applied at once, read back from Windows, saved; the audio threads stay MMCSS in every case.
- A mono part of the chain (mono microphone, a mono plug-in such as RNNoise) followed by a stereo
  plug-in feeds both of its inputs (before: the right cable channel stayed silent).
- The shown latency includes a plug-in's latency change while running (EQ to linear phase).
- The exe's version information says Turkish (Türkiye).
- 24 built-in effects (picker groups "Dahili - Ton ve EQ / Temizlik / Renk ve ortam / Seviye ve
  koruma / Zaman ve kanal / Ölçüm"): Low-Cut, High-Cut, Gövde, Çamur Temizleyici, Genizlik Azaltıcı,
  Netlik, Hava, Yakınlık, Tilt EQ, Parametrik EQ, Uğultu Giderici, DC Kayması Giderici, De-esser,
  Sıcak Saturasyon, Telefon/Radyo/Megafon, Oda Yankısı, Gecikme/Eko, Chorus/Stereo Genişletici,
  Çıkış Limiteri, Sessize Alma, Ses Gecikmesi, Kanal Araçları, LUFS Ölçer, Spektrum Analizörü.
  Settings windows (double-click), saved with the chain, every change glides (no clicks), latency
  reported. (Kompresör and Gürültü Kapısı followed later, see below: 26 built-in effects in all.)
- Hazır ayarlar next to "+ Eklenti": folders "Erkek Sesi Ayarları" and "Kadın Sesi Ayarları" with
  factory presets (first ten each, now fifteen each - see below) (everyday, podcast, radio, narration, cinema, clarity, soft, warm,
  singing with a room, streaming), built-in effects only, for average adult Turkish voices; all
  twenty have the same elements, so switching glides, and the same loudness (within 0.15 LU);
  own presets of the whole chain with the VST3 states (save, overwrite, rename, delete), and
  "Önceki zincire dön".
- Tema (now in Ayarlar > Görünüm ve başlangıç): Gece (default, the colours so far), Gece Yarısı, Kül, Aydınlık, Okyanus,
  Ametist, Orman, Gün Batımı, Yüksek Kontrast; switched at once, saved in config.xml; the native
  title bar follows (light for Aydınlık).
- Dil (now in Ayarlar > Görünüm ve başlangıç): Türkçe (default), English, 中文, हिन्दी, Español, Français, العربية,
  Português, Русский, Deutsch, 日本語 - every text of the window, menus, dialogs, effect and preset
  names, the how-to window and the log; numbers with the language's decimal mark. Translations in
  i18n/src/<language>.txt (tools/i18n_src.py, tools/i18n.py check / generate); a unit test checks
  that every language has every text. Label columns grow with longer texts.
- Kompresör and Gürültü Kapısı (noise gate) as built-in effects (picker group "Dahili - Dinamik"):
  compressor with threshold, ratio, attack, release, soft knee, make-up and mix (parallel); gate with
  3 ms look-ahead (no cut word starts), hysteresis, hold, range and a detector high-pass. Every
  change glides; tested like the other effects (static curves, levels, no clicks on any jump).
- Factory presets: 15 for men's and 15 for women's voices (new: Haber Sunucusu, Sesli Kitap, Film
  Diyaloğu, Toplantı / Ders, ASMR / Fısıltı); all thirty with a gentle gate, a moderate compressor
  and an output limiter (-1 dBTP) after Otomatik Kazanç, same elements (switching glides), same
  loudness (within 0.1 LU on synthetic voices).
- Sustur (mute, 15 ms fade) and Ham ses (A/B: the microphone without the chain, delayed by the
  chain's latency, 30 ms equal-power crossfade) in a new row under the devices; system-wide
  shortcut keys (Sustur F9 by default, Ham ses F10 off), changeable and switchable off in
  "Kısayollar"; the tray menu has Sustur and every preset.
- Shortcuts can also be mouse buttons, separately for Sustur and Ham ses: the middle button, the
  side buttons (mouse 4 / 5) or the wheel with Ctrl / Alt / Shift (low-level mouse hook on its own
  thread, installed only while a mouse shortcut is on). The same shortcut cannot be given to both
  actions; config.xml keeps them ("enabled:key:modifiers:mouse", older entries still read).
- Kendini dinle: the processed voice on a headphone output (new "Kulaklık" row: device and level;
  WASAPI shared, own thread and bridge, opened only while needed; monitoring always starts off).
  Deneme kaydı: 10 s of what the cable gets, played back on the headphones at once.
- Presets can be exported to a .micvstpreset file and imported (menu "Kaydet").
- Fixed: the headphone volume (Kendini dinle, Deneme kaydı) followed a raised-cosine curve instead of
  the dB value set: -12 dB played at -16.6 dB, -20 dB at -32 dB, -40 dB at about -72 dB. The on/off
  fade and the volume are now separate; the volume is exact (measured: -12 dB -> -12.00 dB).
- The log names the real audio formats at every device setup: MicVST's stream and internal
  processing (32-bit float) and the format Windows runs each endpoint in (e.g. microphone 32 bit,
  cable 24 bit PCM, 48000 Hz). Ham ses on/off is logged like Sustur.
- New main window (640x600): level card with the peak values and a one-line status, a big Sustur
  button (red while muted, shows its key), microphone and cable side by side, the preset row with
  "+ Eklenti", on/off switches in the chain rows, and a bottom bar (Ham ses, Kendini dinle, Deneme
  kaydı, Kayıt, help, Ayarlar). Everything set once moved into the new "Ayarlar" window (sections: Ses,
  Kayıt, Kısayollar (with Bildirim sesleri), Görünüm ve başlangıç, Eklentiler, Hakkında).
- New app icon (microphone with the effect chain), tray icons for dark and light Windows; the tray
  icon shows a red dot while muted.
- The how-to window wraps its paragraphs and list items to the window width (hanging indent),
  right to left for Arabic. Its footer: the version (the developer signature was removed later).

## [1.2.1] - Unreleased

Focus: sound quality under heavy background load (e.g. a GPU/AI video render), no clicks when
the chain or the devices change, and a completely Turkish interface. Measurements and what could
not be tested: [docs/TEST_RAPORU_1.2.1.md](docs/TEST_RAPORU_1.2.1.md) (Turkish).

### Added
- Built-in "Kazanç" (gain) element next to "Mono -> Stereo" / "Stereo -> Mono" (picker, "Dahili"),
  like OBS Studio's Gain filter: -30 to +12 dB in 0.1 dB steps, set with a slider (or typed) right
  in its chain row; double-click returns to 0 dB. Changes while audio runs are handed to the plugin
  thread lock-free and glide to the new value (99 % after ~40 ms; neither the level nor its slope
  ever jumps, also while the slider is dragged), so no click or zipper noise; 0 dB is bit-exact
  passthrough. It keeps the channel width of the part of the chain it sits in (mono stays mono),
  and its value is saved with the chain. The output meter's peak mark turns red at 0 dBFS (the
  level at which the cable's 24-bit format clips) instead of disappearing past the right edge.
- Built-in "Otomatik Kazanç" (Auto Gain) element ("Dahili"): a real-time voice level rider. It
  analyses the input continuously and aims normal speech peaks at about -20 dBFS and shouting at
  about -15 dBFS (shouting = the voice rising 3..9 dB above the speaker's learned natural level;
  the aim slides smoothly). The level is a "sustained peak" (per 10 ms, the highest level held for
  at least 1 ms in total), so clicks, lip smacks and bursts shorter than that do not move the gain.
  No fixed gain value: the gain is the aim minus the analysed "typical level" - the level the
  loudest 20 % of the voiced 10 ms windows of the last 3 s of speech reach (pauses add nothing).
  A single strong syllable or a single soft word hardly changes it and the slow gain leaves most
  of the ups and downs of phrases alone (realistic synthetic speech: about 1.5 dB of gain movement
  inside a phrase, about 80 % of the loudness differences between phrases kept); a lasting change
  (further from the mic, speaking softer or louder) shifts it and the gain follows within about
  3-4 s (a lastingly louder voice is first treated as a raised voice and settles as the natural
  level adapts, about 10 s). Syllables keep their own dynamics up to the shouting level; one that
  stays above -15 dBFS for at least 40 ms raises the level at once (syllable envelope held 0.3 s),
  so the gain drops quickly - plosives and breath pops shorter than that do not pull the gain down.
  After a shout (the typical level rose 8 dB or more above the natural level) the shout's windows
  leave the typical level as soon as the voice is back down (the last 0.5 s of voice within 3 dB
  of the natural level and 9 dB under the shout, for 0.5 s of voice) and the gain returns with a
  0.5 s time constant: normal speech is back within 0.5 dB about 3.5 s after a 4 s shout instead
  of about 6.5 s. Normal speech, lasting changes and raised voices are not affected: in the tests
  (-6 ... +10 dB lasting changes, normal speech; 16 voices x 160 s each) the gain is identical,
  differences below 0.01 dB.
  The gain is limited to 0 ... +6 dB - it only raises the voice, as much as needed, never lowers
  it. It rises with a 1 s time constant (2..6 dB/s) and falls with 0.5 s, or 30 ms when more than
  3 dB too loud, through a click-free glide; after switching on it waits for 100 ms of voice. No
  noise gate: nothing is ever muted or faded; the gain moves only on blocks with voice, so between
  words and in silence the last gain is kept exactly and speaking again continues from it. Broken
  input (NaN, infinite, beyond +24 dBFS) is passed on but ignored by the analysis. A separate soft
  limiter with 3 ms look-ahead keeps every peak at or below -10 dBFS - also between the samples
  (true peak, 8x oversampled detection), so the engine's resampler and the D/A converter do not
  bring plosives over it (gentle from -12.3 dBFS, hold 20 ms, release 80 ms); typical speech and the shouting aim are below it, it acts on plosives,
  pops, clicks and the first moments of a syllable louder than -15 dBFS. Latency: the 3 ms
  look-ahead (reported, shown in the row; unchanged when switched off). Its position in the
  chain is free (added at the end, moved like any element); only one can be added; the row has an
  on/off box and a live readout (applied gain, input and output peak). Switched off, the gain
  glides back to 0 dB (12 dB/s) and the audio then passes bit-exact (3 ms later).

### Changed
- Process priority class is now HIGH (was "above normal"); scan processes stay "below normal".
  (1.2.2: a choice, Normal by default - see İşlem önceliği there.)
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
  had wandered by up to +-170 ppm and then needed minutes to settle again; now it stays put. The
  tail of a recovery (the level error being played off) is not learned as drift either, which
  roughly halves the wander of the learned value when disturbances come every few seconds.
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
- A chosen device that is listed but cannot be opened (held exclusively by another program, a
  driver error) started an endless reconnect loop: every failed attempt sent a device-change
  message, which started the next attempt at once. Measured in the engine test (30 s): 2.7 million
  open attempts, 8 million log lines and 2.7 million config.xml saves - a busy CPU core and a log
  file growing without limit. Now it is retried after 1, 2, 4 and 8 s and then every 10 s (it opens
  by itself within 10 s once it is free), logged once (and on every 30th attempt); "Ses: çalışıyor /
  durdu" is logged only when it changes.
- config.xml is only written when its content changed (repeated device notifications wrote the
  same file again and again).
- log.txt was only shortened at start; during a long session (weeks in the tray) it could grow
  without limit. It is now cut back to the newest 256 KB whenever it reaches 2 MB.
- A plugin that outputs NaN, infinite or absurd values (> +24 dBFS) no longer reaches the cable
  (or crashes the next plugin): the whole block is silenced and the sound fades in again (2 ms)
  once the plugin recovers; counted in the dropout tooltip and the log. A plugin that throws an
  exception costs one silent block. Neither starts or ends with a step: the output fades out from
  the valid audio still buffered and, once valid audio returns, refills and fades in (about 50 ms
  of faded silence for a single bad block; no underrun is counted and the safety buffer stays).
  The engine test had found clicks at both ends of such a block.
- A plugin that hangs for more than ~1.3 s: the microphone audio captured meanwhile is dropped
  (the input queue is full); the audio after the drop used to follow the old audio without a
  fade (a click). Now the queue never plays across that point and the output fades there.
- Clicks when the chain is edited during playback (add, remove, move, bypass): the output fades
  out (5 ms), the graph switches while muted, then fades in. Measured on virtual devices: 1.2.0
  clicked on 11 of 13 edits, 1.2.1 on none. The fade-in now also waits until the chain's own
  delay has run through: elements with latency (Otomatik Kazanç's 3 ms look-ahead, a look-ahead
  limiter, a linear-phase EQ) still play audio from before the edit until then, e.g. at the level
  before a bypass, and fading in over that seam clicked whenever the chain's latency was longer
  than one block (found in the long engine test: Otomatik Kazanç + 240 samples of plug-in latency
  at a 256-sample buffer clicked on bypass on/off; the same happened without Otomatik Kazanç with
  300 samples of latency). The edit's silence is longer by the chain's latency, rounded up to
  whole blocks.
- Clicks on the cable when MicVST quits or a device/buffer setting is changed: the output now
  fades out and drains before the device stops (1.2.0: two clicked samples per change).
- VST3 files that contain several plugins (Waves WaveShell, LSP, ...): every entry came back as
  the first plugin of the file after a restart (since 1.1.1). The plugin's identity is now stored;
  old configs still load.
- Non-ASCII text in the log file was double-encoded (UTF-8 read as Latin-1).
- Device errors reported by JUCE/WASAPI ("No such device: ...", "Can't open the audio device!",
  ...) were written to the log in English; they are Turkish now.
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
