# R3 read-back: /home/claude/work/L/rlm/MicVST_2026-10-23_00-27-29.mp3
recorder: 2879520 frames (59.990 s), microphone packets lost by the test machine: 0
soundfile: MP3 / MPEG_LAYER_III, 48000 Hz, 2 ch, 2879520 frames
- PASS soundfile: header length / rate / channels = the recorder's
- PASS soundfile: read through to the end: 2879520 frames (peak -10.74 dBFS)
ffprobe: mp3 / mp3, 48000 Hz, 2 ch, 60.0240 s, duration_ts 847058688
- PASS ffprobe: length from the header 60.0240 s = 59.9900 s
ffmpeg: decoded 2879520 samples, exit 0
- PASS ffmpeg: the whole file decodes without an error
- PASS ffmpeg: decoded length 2879520 = the frames written 2879520 (MP3: within one frame, difference 0)
MP3 tone: 59 seconds, level -0.001 .. -0.000 dB of -12 dBFS, worst residual after the sine fit -67.3 dB
- PASS MP3: every second is the tone (residual < -40 dB, level within 0.2 dB): 0 seconds off, allowed 1 (start + lost microphone packets)

result: all passed
