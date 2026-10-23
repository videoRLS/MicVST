
# Closed box at run time (Windows CI, 2026-10-23 16:21 UTC)

Control (curl.exe connects to example.com): 2 Windows Filtering Platform events recorded -> the audit works.

## First start (no settings, plug-in scan)
- ran 25 s, quit normally after the Windows shutdown request (11 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 5071 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-3776380004-1005456608-3524014338-500\\Device\HarddiskVolume4\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Local\Temp\micvst_scan.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp42896fb.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp961e3ffe.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempadd25120.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp497556f.xml
  - WriteFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp42896fb.xml
  - WriteFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp961e3ffe.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempadd25120.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp497556f.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 39 (list in procmon_files_*.txt)

## Normal start (saved settings)
- ran 25 s, quit normally after the Windows shutdown request (11 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 2980 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-3776380004-1005456608-3524014338-500\\Device\HarddiskVolume4\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RFbe5632.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RFbe5613.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_tempa68b43a8.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp7d6662ab.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempa68b43a8.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp7d6662ab.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 31 (list in procmon_files_*.txt)

## Start in the tray (--tray, autostart)
- ran 25 s, quit normally after the Windows shutdown request (11 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 2898 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-3776380004-1005456608-3524014338-500\\Device\HarddiskVolume4\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RFbef1e5.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RFbef1d6.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp459b5c1e.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp1825f8b7.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp459b5c1e.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp1825f8b7.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 31 (list in procmon_files_*.txt)

## Damaged settings file
- ran 25 s, quit normally after the Windows shutdown request (11 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 2983 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-3776380004-1005456608-3524014338-500\\Device\HarddiskVolume4\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RFbf8d4a.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RFbf8d3b.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp7e37949e.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_tempd0e6ebd5.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp7e37949e.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_tempd0e6ebd5.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 31 (list in procmon_files_*.txt)

## Damaged plug-in cache
- ran 25 s, quit normally after the Windows shutdown request (11 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 4630 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-3776380004-1005456608-3524014338-500\\Device\HarddiskVolume4\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Local\Temp\micvst_scan.xml
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RFc02787.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_tempcd6c32d5.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp784b79eb.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_tempe787eb8b.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_tempd47b2ed4.xml
  - WriteFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp784b79eb.xml
  - WriteFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_tempe787eb8b.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempcd6c32d5.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_tempd47b2ed4.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 40 (list in procmon_files_*.txt)

## Settings pointing to missing devices, plug-ins and folders
- ran 25 s, quit normally after the Windows shutdown request (11 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 3006 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-3776380004-1005456608-3524014338-500\\Device\HarddiskVolume4\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RFc0c2fc.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RFc0c2dc.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_tempb91b1e4c.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_tempf1cd00a.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempb91b1e4c.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_tempf1cd00a.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 32 (list in procmon_files_*.txt)

## Process priority of the running MicVST.exe
- (no setting: older config.xml): Windows reports **Normal** (expected Normal) - OK; config.xml after exit: processPriority="0"
- 0 Normal: Windows reports **Normal** (expected Normal) - OK; config.xml after exit: processPriority="0"
- 1 Normalin Ustunde: Windows reports **AboveNormal** (expected AboveNormal) - OK; config.xml after exit: processPriority="1"
- 2 Yuksek: Windows reports **High** (expected High) - OK; config.xml after exit: processPriority="2"
- 2 again (restart): Windows reports **High** (expected High) - OK; config.xml after exit: processPriority="2"
  log: [16:23:35] İşlem önceliği: Normal (uygulandı, Windows'tan okundu)
  log: [16:24:15] İşlem önceliği: Normal (uygulandı, Windows'tan okundu)
  log: [16:24:55] İşlem önceliği: Normal (uygulandı, Windows'tan okundu)
  log: [16:25:30] İşlem önceliği: Normal (uygulandı, Windows'tan okundu)
  log: [16:25:41] İşlem önceliği: Normal (uygulandı, Windows'tan okundu)
  log: [16:25:52] İşlem önceliği: Normalin Üstünde (uygulandı, Windows'tan okundu)
  log: [16:26:03] İşlem önceliği: Yüksek (uygulandı, Windows'tan okundu)
  log: [16:26:14] İşlem önceliği: Yüksek (uygulandı, Windows'tan okundu)
- process priority checks failed: 0
## Result
Filtering Platform events of MicVST: 0; sockets seen: 0; Process Monitor TCP/UDP operations: 0.
OK: no network activity of MicVST in any scenario.
