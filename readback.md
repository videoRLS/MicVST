# R3 read-back: D:\a\_temp\reclong\MicVST_2026-10-23_02-25-01.wav
recorder: 921600000 frames (19200.000 s), microphone packets lost by the test machine: 0
soundfile: RF64 / PCM_24, 48000 Hz, 2 ch, 921600000 frames
- PASS soundfile: header length / rate / channels = the recorder's
- PASS soundfile: read through to the end: 921600000 frames (peak -12.04 dBFS)
- PASS container RF64 for 5.15 GiB (ds64 chunk: yes)
ffprobe: wav / pcm_s24le, 48000 Hz, 2 ch, 19200.0000 s, duration_ts 921600000
- PASS ffprobe: length from the header 19200.0000 s = 19200.0000 s
ffmpeg: decoded 921600000 samples, exit 0
- PASS ffmpeg: the whole file decodes without an error
- PASS ffmpeg: decoded length 921600000 = the frames written 921600000

## R8 - standard and readers
WAV chunks: RF64 / WAVE: ds64 28, fmt  40, bext 644, smpl 60, data 5529600000
fmt: tag 65534, 2 ch, 48000 Hz, 24 bit, block 6
- PASS WAV: data chunk = 921600000 frames x 6 bytes
- **FAIL** RF64 (EBU Tech 3306): size fields 0xFFFFFFFF, ds64 first: RIFF 5529600816 (file - 8 = 5529600816), data 5529600000, samples 0
bext: 644 bytes, originator 'MicVST', date 2026-10-23 02:25:01, version 0, time reference 0, coding history 'A=PCM,F=48000,W=24,M=stereo,T=MicVST'
- PASS BWF (EBU Tech 3285): bext size >= 602, date yyyy-mm-dd, time hh-mm-ss, version 0..2, reserved bytes 0
readers: libsndfile 921600000, ffmpeg 921600000 samples; MD5 fe745734a9a6988027cace0c476e6b91 / fe745734a9a6988027cace0c476e6b91
- PASS libsndfile and ffmpeg: same length (= the frames), the same samples bit for bit

result: FAILED: RF64 (EBU Tech 3306): size fields 0xFFFFFFFF, ds64 first: RIFF 5529600816 (file - 8 = 5529600816), data 5529600000, samples 0
