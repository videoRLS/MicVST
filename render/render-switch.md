# GPU / CPU switch of the main window: ghost, flicker, flash (screen captures)

Window client 640 x 760 px (scale 1.00), graphics card usable for Otomatik: no (Basic Render Driver: Direct2D still forced for this test).
Each switch: the marker turns from the old colour to the new one in the same message-loop turn as micvst::render::setMode; the screen is captured from another thread every ~2 ms for 900 ms.
Pass: the old colour gone within 50 ms (one refresh + capture), never back (no flicker), no frame unlike both the picture before and after (no flash).

| # | switch | engine before -> after | old colour on screen before | setMode ms | new colour first at ms | old colour last at ms | flicker (new->old) | flash frames | captures | result |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | GPU -> CPU | 1 -> 0 | yes | 12.3 | 66.9 | 21.2 | 0 | 0 | 29 | PASS |
| 1 | CPU -> GPU | 0 -> 1 | yes | 0.6 | 33.9 | 4.0 | 0 | 0 | 31 | PASS |
| 2 | GPU -> CPU | 1 -> 0 | yes | 9.1 | 33.4 | - | 0 | 0 | 29 | PASS |
| 2 | CPU -> GPU | 0 -> 1 | yes | 0.5 | 49.9 | 19.2 | 0 | 0 | 29 | PASS |
| 3 | GPU -> CPU | 1 -> 0 | yes | 8.9 | 68.0 | 25.3 | 0 | 0 | 32 | PASS |
| 3 | CPU -> GPU | 0 -> 1 | yes | 0.5 | 34.2 | 2.8 | 0 | 0 | 31 | PASS |
| 4 | GPU -> CPU | 1 -> 0 | yes | 9.8 | 33.5 | - | 0 | 0 | 31 | PASS |
| 4 | CPU -> GPU | 0 -> 1 | yes | 0.5 | 66.6 | 19.1 | 0 | 0 | 29 | PASS |
| 5 | GPU -> CPU | 1 -> 0 | yes | 9.3 | 36.1 | - | 0 | 0 | 31 | PASS |
| 5 | CPU -> GPU | 0 -> 1 | yes | 0.5 | 34.1 | 2.7 | 0 | 0 | 28 | PASS |
| 6 | GPU -> CPU | 1 -> 0 | yes | 10.0 | 33.5 | - | 0 | 0 | 31 | PASS |
| 6 | CPU -> GPU | 0 -> 1 | yes | 0.6 | 50.3 | 3.2 | 0 | 0 | 30 | PASS |
| 7 | GPU -> CPU | 1 -> 0 | yes | 9.8 | 35.6 | - | 0 | 0 | 31 | PASS |
| 7 | CPU -> GPU | 0 -> 1 | yes | 0.6 | 50.2 | 19.2 | 0 | 0 | 29 | PASS |
| 8 | GPU -> CPU | 1 -> 0 | yes | 9.5 | 48.7 | - | 0 | 0 | 30 | PASS |
| 8 | CPU -> GPU | 0 -> 1 | yes | 0.6 | 50.8 | 3.1 | 0 | 0 | 29 | PASS |
| 9 | GPU -> CPU | 1 -> 0 | yes | 10.2 | 34.9 | - | 0 | 0 | 33 | PASS |
| 9 | CPU -> GPU | 0 -> 1 | yes | 0.5 | 50.7 | 19.7 | 0 | 0 | 29 | PASS |
| 10 | GPU -> CPU | 1 -> 0 | yes | 10.1 | 32.6 | - | 0 | 0 | 30 | PASS |
| 10 | CPU -> GPU | 0 -> 1 | yes | 0.6 | 65.1 | 18.3 | 0 | 0 | 29 | PASS |

Engines: 0 = processor (GDI), 1 = graphics card (Direct2D).

**Switches: 20, failures: 0**

# Spectrum and level meters moving (engine running on test devices, synthetic speech)

Each row 5 s. Frames/s: drawn frames of the spectrum and meters (they are paced on the screen's refresh). CPU: the whole test process (audio engine, both views, everything) in % of one core; 'hidden' = the same with the spectrum and meters switched off, so the difference is what they cost. On a CI runner the graphics card is Windows' software rasteriser (WARP): the GPU rows are not representative of a real card.

| engine | FPS setting | frames/s | longest pause ms | CPU % | CPU % hidden | difference |
|---|---|---|---|---|---|---|
| processor (GDI) | 60 | 60.0 | 32.6 | 8.7 | 2.8 | 5.9 |
| processor (GDI) | 30 | 30.0 | 48.6 | 5.9 | 2.8 | 3.1 |
| processor (GDI) | 20 | 20.0 | 64.6 | 5.3 | 2.8 | 2.5 |
| processor (GDI) | 10 | 10.0 | 112.7 | 2.8 | 2.8 | 0.0 |
| graphics card (Direct2D) | 60 | 60.0 | 32.6 | 21.9 | 2.8 | 19.1 |
| graphics card (Direct2D) | 30 | 30.0 | 48.5 | 11.9 | 2.8 | 9.1 |

Resources before: 43.1 MB private, 56 GDI, 21 USER, 452 handles; after: 45.5 MB private, 56 GDI, 21 USER, 479 handles

**Total failures (switches + animation): 0**
