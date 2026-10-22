# R3 read-back: /home/claude/work/L/rlf/MicVST_2026-10-23_00-27-29.flac
recorder: 2880000 frames (60.000 s), microphone packets lost by the test machine: 0
soundfile: FLAC / PCM_24, 48000 Hz, 2 ch, 2880000 frames
- PASS soundfile: header length / rate / channels = the recorder's
- PASS soundfile: read through to the end: 2880000 frames (peak -12.04 dBFS)
ffprobe: flac / flac, 48000 Hz, 2 ch, 60.0000 s, duration_ts 2880000
- PASS ffprobe: length from the header 60.0000 s = 60.0000 s
ffmpeg: decoded 2880000 samples, exit 0
- PASS ffmpeg: the whole file decodes without an error
- PASS ffmpeg: decoded length 2880000 = the frames written 2880000

result: all passed
