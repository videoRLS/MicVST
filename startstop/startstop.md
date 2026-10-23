
# R6 - 1000 starts of MicVST.exe, every 5th killed in its first 2 s

markers in config.xml: recordFormat=flac:24:5|48000|2, recordFolder=D:\a\_temp\r6-kayit, monitorVolumeDb=-7.5
files in %APPDATA%\MicVST before: config.xml, log.txt, plugin_cache.xml

after 100 rounds (2.1 min): closed 80, killed 20, crashes 0, hangs 0, settings unreadable 0, markers lost 0, new files 1
after 200 rounds (4.2 min): closed 160, killed 40, crashes 0, hangs 0, settings unreadable 0, markers lost 0, new files 2
after 300 rounds (6.2 min): closed 240, killed 60, crashes 0, hangs 0, settings unreadable 0, markers lost 0, new files 2
after 400 rounds (8.3 min): closed 320, killed 80, crashes 0, hangs 0, settings unreadable 0, markers lost 0, new files 3
after 500 rounds (10.4 min): closed 400, killed 100, crashes 0, hangs 0, settings unreadable 0, markers lost 0, new files 3
after 600 rounds (12.5 min): closed 480, killed 120, crashes 0, hangs 0, settings unreadable 0, markers lost 0, new files 4
after 700 rounds (14.8 min): closed 560, killed 140, crashes 0, hangs 0, settings unreadable 0, markers lost 0, new files 5
after 800 rounds (16.9 min): closed 640, killed 160, crashes 0, hangs 0, settings unreadable 0, markers lost 0, new files 6
after 900 rounds (19 min): closed 720, killed 180, crashes 0, hangs 0, settings unreadable 0, markers lost 0, new files 6
after 1000 rounds (21.2 min): closed 800, killed 200, crashes 0, hangs 0, settings unreadable 0, markers lost 0, new files 6

time to the window: median 261 ms, 95 % 624 ms, max 3103 ms
log.txt: 257 KB
- new file in %APPDATA%\MicVST: config_tempfe9a7a5b.xml (first seen after round 330)
- new file in %APPDATA%\MicVST: config_temp815af3a8.xml (first seen after round 85)
- new file in %APPDATA%\MicVST: log.txt~RF195b07.TMP (first seen after round 720)
- new file in %APPDATA%\MicVST: plugin_cache_temp8cd14b76.xml (first seen after round 530)
- new file in %APPDATA%\MicVST: log.txt~RF184b7b.TMP (first seen after round 665)
- new file in %APPDATA%\MicVST: plugin_cache_tempd0769b42.xml (first seen after round 130)
Application Error / WER / hang events for MicVST: 0
temporary files left by killed writes: 6

result: all passed - 800 closed normally, 200 killed, 21.2 min
