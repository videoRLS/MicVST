# R5 - the clock changed during a recording
start: 2026-10-23T13:53:01.9314961+00:00, time zone UTC
  70.1 s: clock +1 hour -> now 2026-10-23 14:54:11 +00:00
 130.0 s: clock -1 hour -> now 2026-10-23 13:55:11 +00:00
 190.2 s: clock +1 day -> now 2026-10-24 13:56:12 +00:00
 250.1 s: clock -1 day -> now 2026-10-23 13:57:11 +00:00
 310.1 s: clock time zone Tokyo Standard Time -> now 2026-10-23 13:58:11 +00:00
 370.1 s: clock time zone back to UTC -> now 2026-10-23 13:59:12 +00:00
clock put back: 2026-10-23T14:00:07.1900271+00:00, time zone UTC

  PASS the recording starts
| minute | recorded (s) | wall (s) | working set (MB) | CPU % of one core | engine |
|---:|---:|---:|---:|---:|---|
| 1 | 60.00 | 60.00 | 34.1 | 1.77 | underruns 0, trims 0, ring drops 0, mic xruns 0 (lost packets 0), skips 0, queue overflows 0, device gaps 0, safety 10 ms, drift 0.1 ppm, shown latency 55.4 ms, cpu 0 % |
| 2 | 120.00 | 120.00 | 31.5 | 1.76 | underruns 0, trims 0, ring drops 0, mic xruns 0 (lost packets 0), skips 0, queue overflows 0, device gaps 0, safety 10 ms, drift -0.0 ppm, shown latency 55.8 ms, cpu 0 % |
| 3 | 180.00 | 180.00 | 28.5 | 1.74 | underruns 0, trims 0, ring drops 0, mic xruns 0 (lost packets 0), skips 0, queue overflows 0, device gaps 0, safety 10 ms, drift 0.1 ppm, shown latency 55.4 ms, cpu 0 % |
| 7 | 420.00 | 420.00 | 16.6 | 1.81 | underruns 0, trims 0, ring drops 0, mic xruns 0 (lost packets 0), skips 0, queue overflows 0, device gaps 0, safety 10 ms, drift -0.1 ppm, shown latency 55.4 ms, cpu 0 % |
cable: clicks 0 (unexplained 0), test-machine stalls: cable gaps 0, lost microphone packets 0
  PASS no click on the cable that a stall of the test machine does not explain
file: D:\a\_temp\r5-kayit\MicVST_2026-10-23_13-53-05.wav, 115.4 MB, 420.000 s (shown 420.00 s, wall 420.004 s), frames 20160000, dropped 0, verified yes
  PASS the file is whole and verified
  PASS no frame lost (dropped 0)
  PASS length = the time it ran: difference 0.004 s (0.00102 %)
  PASS a RIFF WAV below 4 GB
  PASS the file opens
  PASS length / rate / channels as recorded: 20160000 frames
  PASS the file's first sample found on the microphone's timeline
content: 20160000 frames compared with the microphone's samples: differing 0; timeline shifts 0 (0 samples, microphone packets lost by the test machine: 0)
  PASS nothing missing in the file except what the (virtual) microphone itself lost
  PASS every sample in the file is the microphone's (nothing missing, doubled or changed)

exit code 0; failed checks 0
