# GPU / CPU switch of the main window: ghost, flicker, flash (screen captures)

Window client 640 x 760 px (scale 1.00), graphics card usable for Otomatik: no (Basic Render Driver: Direct2D still forced for this test).
Each switch: the marker turns from the old colour to the new one in the same message-loop turn as micvst::render::setMode; the screen is captured from another thread every ~2 ms for 900 ms.
Pass: the old colour gone within 50 ms (one refresh + capture), never back (no flicker), no frame unlike both the picture before and after (no flash).

| # | switch | engine before -> after | old colour on screen before | setMode ms | new colour first at ms | old colour last at ms | flicker (new->old) | flash frames | captures | result |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 | GPU -> CPU | 1 -> 0 | yes | 36.1 | 32.9 | - | 0 | 0 | 33 | PASS |
| 1 | CPU -> GPU | 0 -> 1 | yes | 0.4 | 34.8 | 2.2 | 0 | 0 | 28 | PASS |
| 2 | GPU -> CPU | 1 -> 0 | yes | 38.2 | 66.7 | - | 0 | 0 | 34 | PASS |
| 2 | CPU -> GPU | 0 -> 1 | yes | 0.4 | 51.7 | 3.3 | 0 | 0 | 29 | PASS |
| 3 | GPU -> CPU | 1 -> 0 | yes | 54.4 | 51.0 | - | 0 | 0 | 37 | PASS |
| 3 | CPU -> GPU | 0 -> 1 | yes | 0.4 | 34.3 | 2.4 | 0 | 0 | 31 | PASS |
| 4 | GPU -> CPU | 1 -> 0 | yes | 38.3 | 35.2 | - | 0 | 0 | 34 | PASS |
| 4 | CPU -> GPU | 0 -> 1 | yes | 0.4 | 49.4 | 2.1 | 0 | 0 | 29 | PASS |
| 5 | GPU -> CPU | 1 -> 0 | yes | 53.2 | 49.9 | - | 0 | 0 | 37 | PASS |
| 5 | CPU -> GPU | 0 -> 1 | yes | 0.4 | 34.8 | 2.3 | 0 | 0 | 29 | PASS |
| 6 | GPU -> CPU | 1 -> 0 | yes | 37.6 | 34.5 | - | 0 | 0 | 36 | PASS |
| 6 | CPU -> GPU | 0 -> 1 | yes | 0.4 | 66.7 | 18.9 | 0 | 0 | 30 | PASS |
| 7 | GPU -> CPU | 1 -> 0 | yes | 36.4 | 33.5 | - | 0 | 0 | 37 | PASS |
| 7 | CPU -> GPU | 0 -> 1 | yes | 0.4 | 34.3 | 2.3 | 0 | 0 | 29 | PASS |
| 8 | GPU -> CPU | 1 -> 0 | yes | 37.9 | 34.7 | - | 0 | 0 | 33 | PASS |
| 8 | CPU -> GPU | 0 -> 1 | yes | 0.5 | 66.6 | 19.3 | 0 | 0 | 29 | PASS |
| 9 | GPU -> CPU | 1 -> 0 | yes | 38.2 | 35.0 | - | 0 | 0 | 35 | PASS |
| 9 | CPU -> GPU | 0 -> 1 | yes | 0.5 | 34.6 | 2.5 | 0 | 0 | 29 | PASS |
| 10 | GPU -> CPU | 1 -> 0 | yes | 53.2 | 50.0 | - | 0 | 0 | 34 | PASS |
| 10 | CPU -> GPU | 0 -> 1 | yes | 0.4 | 36.4 | 2.8 | 0 | 0 | 31 | PASS |

Engines: 0 = processor (GDI), 1 = graphics card (Direct2D).

**Switches: 20, failures: 0**

# Spectrum and level meters moving (engine running on test devices, synthetic speech)

Each row 5 s. Frames/s: drawn frames of the spectrum and meters (they are paced on the screen's refresh). CPU: the whole test process (audio engine, both views, everything) in % of one core; 'hidden' = the same with the spectrum and meters switched off, so the difference is what they cost. On a CI runner the graphics card is Windows' software rasteriser (WARP): the GPU rows are not representative of a real card.

| engine | FPS setting | frames/s | longest pause ms | CPU % | CPU % hidden | difference |
|---|---|---|---|---|---|---|
| processor (GDI) | 60 | 60.0 | 32.5 | 8.7 | 1.6 | 7.2 |
| processor (GDI) | 30 | 30.0 | 48.5 | 4.7 | 1.6 | 3.1 |
| processor (GDI) | 20 | 20.0 | 65.1 | 5.3 | 1.6 | 3.7 |
| processor (GDI) | 10 | 10.0 | 113.1 | 2.8 | 1.6 | 1.3 |
| graphics card (Direct2D) | 60 | 59.8 | 32.6 | 16.9 | 2.2 | 14.7 |
| graphics card (Direct2D) | 30 | 30.0 | 48.5 | 12.5 | 2.2 | 10.3 |

Resources before: 42.6 MB private, 56 GDI, 21 USER, 452 handles; after: 42.4 MB private, 56 GDI, 21 USER, 459 handles

**Total failures (switches + animation): 0**
