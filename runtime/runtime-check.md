
# Closed box at run time (Windows CI, 2026-10-16 01:16 UTC)

Control (curl.exe connects to example.com): 6 Windows Filtering Platform events recorded -> the audit works.

## First start (no settings, plug-in scan)
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 58; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 4215 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Notifications\Data\418A073AA3BC2475
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Local\Temp\micvst_scan.xml
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF5f34fc.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF5f94df.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp5b5ef7bb.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_tempc3e257aa.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_tempb395b6a6.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempe3bbb834.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp88324b8c.xml
  - WriteFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_tempb395b6a6.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp5b5ef7bb.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempc3e257aa.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempe3bbb834.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp88324b8c.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 35 (list in procmon_files_*.txt)

## Normal start (saved settings)
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 2985 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF5fd311.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF5fd330.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF6033fd.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RF5fd301.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp1169a4bc.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_tempa0c42c5b.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_tempf8f70b2a.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_tempf09a599e.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp1169a4bc.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempa0c42c5b.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempf8f70b2a.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_tempf09a599e.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 31 (list in procmon_files_*.txt)

## Start in the tray (--tray, autostart)
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 2913 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF606d2e.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF606d3d.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF60ce78.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RF606d1e.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp1195ffa4.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp577adefa.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_tempd31883b6.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp9a2d1db6.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp1195ffa4.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp577adefa.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempd31883b6.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp9a2d1db6.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 31 (list in procmon_files_*.txt)

## Damaged settings file
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 3044 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF610b62.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF610b81.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF616cfa.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RF610b52.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp1e993b1e.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp7d4efb18.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_tempb63dc4cc.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_tempa80f4542.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp1e993b1e.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp7d4efb18.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempb63dc4cc.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_tempa80f4542.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 31 (list in procmon_files_*.txt)

## Damaged plug-in cache
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 3726 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Local\Temp\micvst_scan.xml
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF61a715.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF61a725.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF6207d3.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp8832ee71.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp9b716cd3.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_tempfbba2ce8.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp727c1d3d.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp521a6b40.xml
  - WriteFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp727c1d3d.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp8832ee71.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp9b716cd3.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempfbba2ce8.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp521a6b40.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 36 (list in procmon_files_*.txt)

## Settings pointing to missing devices, plug-ins and folders
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 3010 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF6240e4.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF6240f4.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF62a163.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RF6240c5.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp2543f00d.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp8dd64363.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp93ff5a01.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp3faedce2.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp2543f00d.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp8dd64363.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp93ff5a01.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp3faedce2.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 32 (list in procmon_files_*.txt)

## Result
Filtering Platform events of MicVST: 0; sockets seen: 0; Process Monitor TCP/UDP operations: 0.
OK: no network activity of MicVST in any scenario.
