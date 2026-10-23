# R3 read-back: D:\a\_temp\reclong\MicVST_2026-10-23_02-22-10.flac
recorder: 921600000 frames (19200.000 s), microphone packets lost by the test machine: 0
soundfile: FLAC / PCM_24, 48000 Hz, 2 ch, 921600000 frames
- PASS soundfile: header length / rate / channels = the recorder's
- PASS soundfile: read through to the end: 921600000 frames (peak -12.04 dBFS)
ffprobe: flac / flac, 48000 Hz, 2 ch, 19200.0000 s, duration_ts 921600000
- PASS ffprobe: length from the header 19200.0000 s = 19200.0000 s
ffmpeg: decoded 921600000 samples, exit 0
- PASS ffmpeg: the whole file decodes without an error
- PASS ffmpeg: decoded length 921600000 = the frames written 921600000

## R8 - standard and readers
STREAMINFO: 48000 Hz, 2 ch, 24 bit, total samples 921600000, MD5 0178324f8a471c1f0415e74d78ef9f45
- PASS FLAC (RFC 9639): STREAMINFO total samples = the frames, MD5 set
- PASS FLAC: MD5 of the decoded audio = STREAMINFO MD5 (0178324f8a471c1f0415e74d78ef9f45)
readers: libsndfile 921600000, ffmpeg 921600000 samples; MD5 fe745734a9a6988027cace0c476e6b91 / fe745734a9a6988027cace0c476e6b91
- PASS libsndfile and ffmpeg: same length (= the frames), the same samples bit for bit

result: all passed
