
## 1. Defaults: Sustur = F9 (keyboard), Ham ses = mouse 4 (back) from config.xml
MicVST.exe --tray running: True
- PASS: F9 mutes (log: [16:26:35] Ses kapatıldı (sustur) - bildirim sesi: Ahşap - kapanma)
- PASS: mute plays the default cue 'Ahsap - kapanma'
- PASS: F9 again unmutes
- PASS: unmute plays the default cue 'Ahsap - acilma' (log: [16:26:37] Ses açıldı - bildirim sesi: Ahşap - açılma)
- PASS: mouse 4 switches Ham ses on (log: [16:26:38] Ham ses: açık (zincir atlanıyor))
- PASS: mouse 4 again switches it off
- PASS: mouse 5 (not bound) does nothing
- PASS: the plain wheel does nothing

## 2. Mouse for both: Sustur = middle button, Ham ses = Ctrl + wheel up
- PASS: middle button mutes (log: [16:26:53] Ses kapatıldı (sustur) - bildirim sesi: Ahşap - kapanma)
- PASS: middle button again unmutes
- PASS: F9 (no longer bound) does nothing
- PASS: wheel up without Ctrl does nothing
- PASS: Ctrl + wheel up switches Ham ses on (log: [16:26:59] Ham ses: açık (zincir atlanıyor))
- PASS: Ctrl + wheel down (not bound) does nothing
- PASS: Ctrl + wheel up again switches it off
- PASS: bindings kept in config.xml after quitting: mute '1:0:0:1', compare '1:0:2:4', quit by itself: True

## 3. Restart with the saved bindings; a switched-off shortcut does nothing
- PASS: after the restart the middle button still mutes
- PASS: and unmutes
- PASS: switched off: the middle button does nothing
- PASS: switched off: Ctrl + wheel does nothing

Result: all checks passed
