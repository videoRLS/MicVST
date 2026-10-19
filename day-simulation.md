# MicVST: simulated day

## Part D - simulated day: 24 h of virtual time (1.2.1: real AudioBridge + InputQueue, deterministic)

Schedule (seed 20261015): mic clock drift random walk within +-300 ppm (0.5 ppm/min) with a jump to a new value every 6 h; 364 cable stalls (5-80 ms), 98 system freezes (2-30 ms, all threads), 229 plug-in thread stalls (10-250 ms, incl. the freezes); 8 render periods of 30 min (45 % of the blocks need 1.1-1.9x real time). Buffer 480 at 48 kHz, safety Auto. Clicks = jumps of the 997 Hz tone's waveform (second difference), events 10 ms apart; 'inevitable' = at a cable gap (the cable's driver was late longer than its buffer: silence inserted) or at audio lost in a system freeze (capture buffer overflowed); 'at MicVST' = at an underrun, trim or skip of MicVST (should be none: those are faded/crossfaded).

| Hour | Underruns | Trims | Skips | Capture overflows | Cable gaps | Clicks: inevitable / at MicVST / other | Pitch error max (ppm) | time > 100 ppm | Latency mean / max (ms) | Safety (ms) |
|---:|---:|---:|---:|---:|---:|---|---:|---:|---|---:|
| 1 | 21 | 31 | 2 | 0 | 14 | 23 / 0 / 0 | 506 | 45.03 % | 76.3 / 191.4 | 80 |
| 2 | 2 | 3 | 1 | 1 | 14 | 22 / 0 / 0 | 513 | 7.60 % | 110.4 / 185.6 | 80 |
| 3 | 2 | 3 | 2 | 3 | 9 | 19 / 0 / 0 | 502 | 2.93 % | 110.6 / 177.5 | 80 |
| 4 | 12 | 22 | 1 | 0 | 4 | 6 / 0 / 0 | 536 | 41.25 % | 112.4 / 196.5 | 80 |
| 5 | 1 | 2 | 1 | 0 | 3 | 5 / 0 / 0 | 503 | 1.37 % | 112.9 / 183.4 | 80 |
| 6 | 2 | 3 | 2 | 0 | 9 | 15 / 0 / 0 | 506 | 4.77 % | 110.5 / 184.8 | 80 |
| 7 | 12 | 16 | 3 | 0 | 6 | 10 / 0 / 0 | 566 | 42.47 % | 112.5 / 195.0 | 80 |
| 8 | 0 | 7 | 0 | 0 | 13 | 23 / 0 / 0 | 504 | 5.66 % | 110.9 / 186.0 | 80 |
| 9 | 2 | 2 | 2 | 0 | 7 | 10 / 0 / 0 | 505 | 2.08 % | 110.6 / 171.4 | 80 |
| 10 | 8 | 13 | 3 | 0 | 8 | 14 / 0 / 0 | 509 | 43.34 % | 110.6 / 195.6 | 80 |
| 11 | 0 | 1 | 0 | 0 | 3 | 5 / 0 / 0 | 540 | 1.68 % | 112.6 / 149.3 | 80 |
| 12 | 0 | 1 | 0 | 0 | 7 | 10 / 0 / 0 | 502 | 2.76 % | 111.6 / 181.8 | 80 |
| 13 | 17 | 23 | 3 | 1 | 10 | 17 / 0 / 0 | 510 | 43.94 % | 110.5 / 208.2 | 80 |
| 14 | 0 | 3 | 0 | 0 | 10 | 17 / 0 / 0 | 546 | 7.38 % | 109.8 / 165.1 | 80 |
| 15 | 0 | 3 | 0 | 0 | 9 | 15 / 0 / 0 | 509 | 4.51 % | 110.3 / 179.7 | 80 |
| 16 | 4 | 9 | 1 | 0 | 10 | 15 / 0 / 0 | 537 | 42.45 % | 109.3 / 200.5 | 80 |
| 17 | 2 | 5 | 2 | 0 | 8 | 14 / 0 / 0 | 501 | 3.18 % | 111.9 / 184.4 | 80 |
| 18 | 4 | 7 | 1 | 1 | 10 | 20 / 0 / 0 | 508 | 5.00 % | 111.6 / 181.2 | 80 |
| 19 | 14 | 24 | 6 | 1 | 17 | 28 / 0 / 0 | 512 | 43.18 % | 109.3 / 201.9 | 80 |
| 20 | 1 | 6 | 0 | 0 | 8 | 14 / 0 / 0 | 525 | 3.71 % | 111.4 / 158.9 | 80 |
| 21 | 1 | 2 | 1 | 1 | 9 | 15 / 0 / 0 | 537 | 5.00 % | 108.6 / 148.3 | 80 |
| 22 | 11 | 17 | 2 | 0 | 10 | 13 / 0 / 0 | 511 | 42.70 % | 111.7 / 207.1 | 80 |
| 23 | 0 | 0 | 0 | 0 | 5 | 7 / 0 / 0 | 502 | 3.33 % | 112.4 / 134.5 | 80 |
| 24 | 1 | 2 | 0 | 0 | 6 | 9 / 0 / 0 | 503 | 3.31 % | 111.2 / 162.0 | 80 |

Day total: underruns 117, trims 205, skips 33, capture overflows 8 (80 ms lost), cable gaps 209; clicks: inevitable 346, at MicVST 0, other 0; pitch error max 566 ppm (0.98 cent), above 100 ppm for 14711.4 s; learned drift at the end -76.5 ppm (true -76.3 ppm), safety 80 ms.

