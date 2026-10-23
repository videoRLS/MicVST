
## MicVST_Oncelik.vbs
- PASS: shortcut created: MicVST Öncelik - Normal.lnk
- PASS:   target is wscript.exe (C:\Windows\System32\wscript.exe)
- PASS:   arguments: "C:\MicVST\MicVST_Oncelik.vbs" 0
- PASS:   icon: C:\MicVST\MicVST.exe,0
- PASS: shortcut created: MicVST Öncelik - Normalin Üstünde.lnk
- PASS:   target is wscript.exe (C:\Windows\System32\wscript.exe)
- PASS:   arguments: "C:\MicVST\MicVST_Oncelik.vbs" 1
- PASS:   icon: C:\MicVST\MicVST.exe,0
- PASS: shortcut created: MicVST Öncelik - Yüksek.lnk
- PASS:   target is wscript.exe (C:\Windows\System32\wscript.exe)
- PASS:   arguments: "C:\MicVST\MicVST_Oncelik.vbs" 2
- PASS:   icon: C:\MicVST\MicVST.exe,0
MicVST.exe started (pid 8936), priority at start: Normal
- PASS: argument 2 -> High: got High, ended silently: True
- PASS: argument 1 -> AboveNormal: got AboveNormal, ended silently: True
- PASS: argument 0 -> Normal: got Normal, ended silently: True
- PASS: argument 2 -> High: got High, ended silently: True
- PASS: shortcut 'Normal' clicked: Normal
- PASS: no message box left open after the shortcut
- PASS: MicVST not running: the script shows its warning (waits on the box) instead of ending silently
- PASS: MicVST not running: nothing was started

## MicVST_Simgeleri.vbs
- PASS: Test Renk_Ince.lnk: icon C:\MicVST\MicVST.exe,0
- PASS:   target unchanged: C:\Users\runneradmin\AppData\Local\Temp\masaustu-docs\Renk_Ince.vbs
- PASS: Test Renk_Tok.lnk: icon C:\MicVST\MicVST.exe,0
- PASS:   target unchanged: C:\Users\runneradmin\AppData\Local\Temp\masaustu-docs\Renk_Tok.vbs
- PASS: Test Ince VST.lnk: icon C:\MicVST\MicVST.exe,0
- PASS:   target unchanged: C:\Users\runneradmin\AppData\Local\Temp\masaustu-docs\Ince VST.vbs
- PASS: Test Tok VST.lnk: icon C:\MicVST\MicVST.exe,0
- PASS:   target unchanged: C:\Users\runneradmin\AppData\Local\Temp\masaustu-docs\Tok VST.vbs
- PASS: another shortcut is left alone: C:\Windows\System32\notepad.exe,0

Result: all checks passed
