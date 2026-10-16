# MicVST audio path measurements

## Part A - simulation: 1.1.1 (one thread) vs. 1.2.0 (plug-ins on the capture thread) vs. 1.2.1 (separate plug-in thread)

Same input signal (997 Hz sine), same timing per scenario, all designs with the same WASAPI buffers
(render and capture buffer 2.2 periods). 'Clicks' = audible events where the waveform jumps (hard
gaps, zero-padding, lost input), counted as events (jumps closer than 10 ms = one event); in brackets the
number of flagged samples. Faded gaps (bridge underruns) are no clicks but count under 'gaps';
'driver gaps' = capture-buffer overflows (audio lost inside Windows); 'skipped' = crossfaded skips of
1.2.1 when the chain falls behind by more than 120 ms. Latency = audio queued between microphone and
cable (input side + bridge + render buffer), device latency excluded.

| Scenario | Design | Clicks (samples) | Gaps / underruns | Driver gaps (lost) | Skipped | Mean added latency |
|---|---|---:|---:|---:|---:|---:|
| 1. Normal: drift 0 ppm, light jitter (0-1 ms) | 1.1.1 | 0 (0) | 0 | 0 | - | 41.8 ms |
| | 1.2.0 | 0 (0) | 0 | 0 | 0 | 47.7 ms (safety 10 ms) |
| | **1.2.1** | 0 (0) | 0 | 0 | 0 | 45.1 ms (safety 10 ms) |
| 2. Mic clock 60 ppm slower than cable | 1.1.1 | 3 (12) | 3 | 0 | - | 32.0 ms |
| | 1.2.0 | 0 (0) | 0 | 0 | 0 | 47.7 ms (safety 10 ms) |
| | **1.2.1** | 0 (0) | 0 | 0 | 0 | 45.1 ms (safety 10 ms) |
| 3. Mic clock 60 ppm faster than cable | 1.1.1 | 2 (4) | 0 | 2 (15 ms) | - | 61.9 ms |
| | 1.2.0 | 0 (0) | 0 | 0 | 0 | 47.7 ms (safety 10 ms) |
| | **1.2.1** | 0 (0) | 0 | 0 | 0 | 45.2 ms (safety 10 ms) |
| 4. Heavy game: plug-in time jitter 0-9 ms, drift +40 ppm | 1.1.1 | 0 (0) | 0 | 0 | - | 31.8 ms |
| | 1.2.0 | 0 (0) | 0 | 0 | 0 | 52.0 ms (safety 10 ms) |
| | **1.2.1** | 0 (0) | 0 | 0 | 0 | 48.9 ms (safety 10 ms) |
| 4b. Very heavy load: plug-in time jitter 0-14 ms (avg. load 95 %), drift +40 ppm | 1.1.1 | 1451 (11214) | 2761 | 709 (7085 ms) | - | 74.9 ms |
| | 1.2.0 | 20 (47) | 11 (+6 trims) | 24 (240 ms) | 0 | 102.8 ms (safety 65 ms) |
| | **1.2.1** | 0 (0) | 10 (+9 trims) | 0 | 0 | 103.0 ms (safety 60 ms) |
| 5. Load spikes: 25 ms processing stall every 4 s | 1.1.1 | 42 (94) | 30 | 27 (265 ms) | - | 70.3 ms |
| | 1.2.0 | 30 (84) | 9 | 30 (300 ms) | 0 | 63.8 ms (safety 55 ms) |
| | **1.2.1** | 0 (0) | 2 | 0 | 0 | 55.2 ms (safety 20 ms) |
| 6. System freeze (DPC storm) 30 ms every 6 s, all threads | 1.1.1 | 30 (80) | 20 | 18 (175 ms) | - | 64.0 ms |
| | 1.2.0 | 60 (173) | 0 | 20 (400 ms) | 0 | 45.9 ms (safety 10 ms) |
| | **1.2.1** | 60 (190) | 0 | 20 (400 ms) | 0 | 43.2 ms (safety 10 ms) |
| 7. Mic 44.1 kHz, cable 48 kHz | 1.1.1 | device does not open (rate mismatch) | - | - | - | - |
| | 1.2.0 | 0 (0) | 0 | 0 | 0 | 47.8 ms (safety 10 ms) |
| | **1.2.1** | 0 (0) | 0 | 0 | 0 | 45.2 ms (safety 10 ms) |
| 8. Render in the background (like the Topaz log): 45 % of blocks need 1.1-1.9x real time, avg. load ~92 % | 1.1.1 | 1831 (14596) | 3505 | 1280 (12795 ms) | - | 75.5 ms |
| | 1.2.0 | 95 (236) | 23 (+8 trims) | 124 (1240 ms) | 0 | 116.8 ms (safety 80 ms) |
| | **1.2.1** | 0 (0) | 18 (+27 trims) | 0 | 0 | 127.0 ms (safety 80 ms) |
| 9. Chain slower than real time: 60 % of blocks need 1.2-2.0x, avg. load ~112 % | 1.1.1 | 1521 (16993) | 4073 | 1943 (19425 ms) | - | 76.8 ms |
| | 1.2.0 | 806 (2318) | 134 (+1 trims) | 1238 (12380 ms) | 0 | 131.1 ms (safety 80 ms) |
| | **1.2.1** | 0 (0) | 156 (+8 trims) | 0 | 115 (12779 ms) | 139.8 ms (safety 80 ms) |
| 10. Plug-in spikes: 60 ms processing stall every 3 s | 1.1.1 | 80 (121) | 40 | 40 (1965 ms) | - | 66.4 ms |
| | 1.2.0 | 40 (229) | 25 | 40 (1970 ms) | 0 | 84.8 ms (safety 80 ms) |
| | **1.2.1** | 0 (0) | 10 (+6 trims) | 0 | 0 | 90.7 ms (safety 60 ms) |

Plug-in load in the 1.2.1 runs (mean / share of blocks slower than real time / peak) is the same model as
for the other designs; see the scenario names.

## Part B - real Windows threads (4 logical CPUs, all busy with 4 normal-priority load threads)

### Wake-up lateness of a 10 ms periodic audio thread (ms)

| Thread setup | p50 | p99 | p99.9 | max | > 2 ms | > 5 ms |
|---|---:|---:|---:|---:|---:|---:|
| idle system, plain thread | 0.260 | 0.535 | 0.632 | 0.729 | 0 | 0 |
| under load, plain thread (worst case) | 8.863 | 28.605 | 33.210 | 39.282 | 2522 | 2052 |
| under load, MMCSS Pro Audio NORMAL (1.1.1 / JUCE default) | 0.277 | 0.539 | 0.688 | 0.832 | 0 | 0 |
| **under load, MMCSS Pro Audio HIGH + no power throttling (1.2)** | 0.278 | 0.538 | 0.633 | 0.712 | 0 | 0 |

MMCSS registration failures: 0
Note: the load threads have normal priority, so NORMAL vs. HIGH can only differ when other MMCSS
threads (game / OBS / Discord audio) compete - which this synthetic load does not create.

### Real AudioBridge between two realtime threads under full CPU load

| Duration | Underruns (faded gaps) | Clicks | Learned drift | Safety at end |
|---:|---:|---:|---:|---:|
| 60 s | 0 | 0 | 47.2 ppm (true: +70) | 10 ms |

## Part C - real threads: plug-ins on the capture thread (1.2.0) vs. separate plug-in thread (1.2.1)

Real InputQueue / WakeEvent / AudioBridge; capture thread modelled on JUCE's input-only WASAPI loop
(capture buffer 2 packets, reservoir 4); plug-in work like the Topaz log (45 % of the blocks need
1.1-1.9x real time, the rest 0.4x); 4 normal-priority load threads keep all 4 logical CPUs busy; 40 s per run. Device threads: MMCSS 'Pro Audio' HIGH; plug-in thread: MicVST's ProcessingThreadSetup (MMCSS NORMAL).

| Design | Driver gaps (lost) | Clicks | Faded gaps | Skips | Safety at end | Longest capture callback | Plug-in load avg | Thread on CPU while processing | Capture wake-ups > 15 ms late |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 1.2.0 plug-ins on the capture thread | 21 (210 ms) | 15 | 9 | 0 | 55 ms | 20.4 ms | 90 % | 89 % | 60 |
| **1.2.1** separate plug-in thread | 0 (0 ms) | 0 | 9 | 0 | 55 ms | 2.5 ms | 90 % | 88 % | 0 |

Device-thread priority setup failures: 0 (MMCSS / SCHED_FIFO refused -> those rows ran at normal priority)
CPU-time measurement check (rt::threadTimes): busy loop 100 % (expected ~100), 1 ms busy / 1 ms asleep 34 % (expected ~50; Windows Sleep(1) may round up and lower it).

