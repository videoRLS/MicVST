
# Closed box at run time (Windows CI, 2026-10-19 22:09 UTC)

Control (curl.exe connects to example.com): 6 Windows Filtering Platform events recorded -> the audit works.

## First start (no settings, plug-in scan)
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 12
- Process Monitor: 4267 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Notifications\Data\418A073AA3BC2475
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Local\Temp\micvst_scan.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp34a9fc74.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp611ad733.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp11571eb1.xml
  - WriteFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp34a9fc74.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp611ad733.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp11571eb1.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 31 (list in procmon_files_*.txt)

## Normal start (saved settings)
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 2842 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF7180e2.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RF7180d2.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp8d57c582.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp2d073520.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp8d57c582.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp2d073520.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 27 (list in procmon_files_*.txt)

## Start in the tray (--tray, autostart)
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 2772 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF7248d6.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RF7248b6.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp8d97e8df.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp6561f021.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp8d97e8df.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp6561f021.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 27 (list in procmon_files_*.txt)

## Damaged settings file
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 2847 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF731c04.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RF731be5.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_tempbdc76178.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp3d9ba674.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempbdc76178.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp3d9ba674.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 27 (list in procmon_files_*.txt)

## Damaged plug-in cache
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 3590 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\Notifications\Data\418A073AA3BC1C75
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Local\Temp\micvst_scan.xml
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF73d65b.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_temp7f5456c4.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp32977e1c.xml
  - SetRenameInformationFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp7a862b83.xml
  - WriteFile %USERPROFILE%\AppData\Local\Temp\micvst_scan_temp32977e1c.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_temp7f5456c4.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp7a862b83.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 32 (list in procmon_files_*.txt)

## Settings pointing to missing devices, plug-ins and folders
- ran 25 s, quit normally after the Windows shutdown request (9 windows asked), exit code 0
- Windows Filtering Platform events of MicVST processes (connect, bind, blocked): **0**
- TCP connections / UDP endpoints owned by MicVST processes: **0**
- loaded DLLs: 57; networking DLLs among them: none
- new/changed files with 'MicVST' in the path: %USERPROFILE%\AppData\Roaming\MicVST\config.xml; %USERPROFILE%\AppData\Roaming\MicVST\log.txt; %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
- other new/changed files in the watched folders (any process on the CI machine): 0
- Process Monitor: 2844 events of MicVST processes; TCP/UDP operations: **0**
- successful writes (files, registry):
  - RegSetValue HKLM\System\CurrentControlSet\Services\bam\State\UserSettings\S-1-5-21-4239583824-265399229-1323058055-500\\Device\HarddiskVolume5\a\MicVST\MicVST\build\MicVST_artefacts\Release\MicVST.exe
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml~RF74be3b.TMP
  - SetDispositionInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml~RF74be1b.TMP
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config_tempbaf61710.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\config.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp98476d8.xml
  - SetRenameInformationEx %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\config_tempbaf61710.xml
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\log.txt
  - WriteFile %USERPROFILE%\AppData\Roaming\MicVST\plugin_cache_temp98476d8.xml
  - WriteFile C:\$LogFile
  - WriteFile C:\$Mft
- files/folders opened outside C:\Windows and the program folder: 28 (list in procmon_files_*.txt)

## Result
Filtering Platform events of MicVST: 0; sockets seen: 0; Process Monitor TCP/UDP operations: 0.
OK: no network activity of MicVST in any scenario.
