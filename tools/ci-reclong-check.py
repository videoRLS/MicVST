"""R3 (test task docs/TEST_PROMPTU_WIN11.md): the hours-long recording of "MicVSTEngineTest recordlong"
read back by other programs - Python soundfile (libsndfile) and ffmpeg / ffprobe:

  * soundfile: the header's length = the frames the recorder wrote; read through to the end in blocks
  * ffprobe:   container and length from the header (WAV past 4 GB: RF64 with its ds64 chunk)
  * ffmpeg:    decodes the whole file; decoded samples = the frames written (MP3: within one frame)
  * MP3:       the tone in every second of the decoded file (997.123 Hz, -12 dBFS): level and the
               residual after a sine fit; a second with a gap or a jump stands out

usage: python ci-reclong-check.py <recordlong output .txt> <report .md>
exit 1 if a check failed.
"""
import json, math, re, subprocess, sys

import numpy as np
import soundfile as sf

FREQ, AMP, FS = 997.123, 0.25, 48000

log_path, report_path = sys.argv[1], sys.argv[2]
log = open(log_path, encoding="utf-8", errors="replace").read()
m = re.search(r"^file: (.*?), [\d.]+ MB, ([\d.]+) s .*?frames (\d+), dropped (\d+)", log, re.M)
lines, failures = [], []


def out(s):
    print(s)
    lines.append(s)


def check(ok, what):
    out(("- PASS " if ok else "- **FAIL** ") + what)
    if not ok:
        failures.append(what)


if not m:
    out("no recording in " + log_path)
    open(report_path, "w", encoding="utf-8").write("\n".join(lines) + "\n")
    sys.exit(1)

path, seconds, frames = m.group(1), float(m.group(2)), int(m.group(3))
lost = re.search(r"lost microphone packets (\d+)", log)
lost = int(lost.group(1)) if lost else 0
kind = path.rsplit(".", 1)[-1].lower()
out(f"# R3 read-back: {path}")
out(f"recorder: {frames} frames ({seconds:.3f} s), microphone packets lost by the test machine: {lost}")

# --- soundfile ---
try:
    info = sf.info(path)
    out(f"soundfile: {info.format} / {info.subtype}, {info.samplerate} Hz, {info.channels} ch, {info.frames} frames")
    check(info.frames == frames and info.samplerate == FS and info.channels == 2, "soundfile: header length / rate / channels = the recorder's")
    n = 0
    peak = 0.0
    for b in sf.blocks(path, blocksize=1 << 20, dtype="float32", always_2d=True):
        n += len(b)
        peak = max(peak, float(np.abs(b).max()) if len(b) else 0.0)
    check(n == frames, f"soundfile: read through to the end: {n} frames (peak {20 * math.log10(max(peak, 1e-12)):.2f} dBFS)")
except Exception as e:  # noqa: BLE001 - every failure is a result here
    check(False, f"soundfile: {type(e).__name__}: {e}")

if kind == "wav":
    with open(path, "rb") as f:
        head = f.read(64)
    big = len(head) >= 4 and head[:4] == b"RF64"
    import os
    size = os.path.getsize(path)
    check(big == (size > 0xFFFFFFFF), f"container {head[:4].decode('ascii', 'replace')} for {size / 2**30:.2f} GiB"
          + (" (ds64 chunk: " + ("yes" if head[12:16] == b"ds64" else "NO") + ")" if big else ""))

# --- ffprobe ---
try:
    p = subprocess.run(["ffprobe", "-v", "error", "-show_entries",
                        "format=format_name,duration:stream=codec_name,sample_rate,channels,duration_ts,duration",
                        "-of", "json", path], capture_output=True, text=True, timeout=600)
    j = json.loads(p.stdout or "{}")
    st = (j.get("streams") or [{}])[0]
    fm = j.get("format") or {}
    dur = float(st.get("duration") or fm.get("duration") or 0)
    out(f"ffprobe: {fm.get('format_name')} / {st.get('codec_name')}, {st.get('sample_rate')} Hz, {st.get('channels')} ch, "
        f"{dur:.4f} s, duration_ts {st.get('duration_ts')}" + (f", stderr: {p.stderr.strip()[:300]}" if p.stderr.strip() else ""))
    tol = 0.06 if kind == "mp3" else 0.5 / FS   # MP3: ffprobe counts the encoder's delay and padding (about 0.035 s)
    check(abs(dur - frames / FS) <= tol, f"ffprobe: length from the header {dur:.4f} s = {frames / FS:.4f} s")
except Exception as e:  # noqa: BLE001
    check(False, f"ffprobe: {type(e).__name__}: {e}")

# --- ffmpeg: decode all (left channel, float) ---
try:
    proc = subprocess.Popen(["ffmpeg", "-v", "error", "-nostdin", "-i", path, "-map", "0:a:0", "-af", "pan=mono|c0=c0",
                             "-f", "f32le", "-acodec", "pcm_f32le", "-"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    n, carry = 0, np.zeros(0, dtype=np.float32)
    bad, worst_res, levels = [], -999.0, []
    t = np.arange(FS) / FS
    basis = np.stack([np.sin(2 * np.pi * FREQ * t), np.cos(2 * np.pi * FREQ * t), np.ones(FS)], axis=1)
    pinv = np.linalg.pinv(basis)
    second = 0
    while True:
        chunk = proc.stdout.read(FS * 4 * 60)
        if not chunk:
            break
        x = np.concatenate([carry, np.frombuffer(chunk, dtype=np.float32)])
        n += len(x) - len(carry)
        whole = len(x) // FS
        if kind == "mp3":
            for k in range(whole):
                w = x[k * FS:(k + 1) * FS].astype(np.float64)
                c = pinv @ w
                res = w - basis @ c
                a = math.hypot(c[0], c[1])
                rdb = 10 * math.log10(max(np.mean(res ** 2), 1e-20) / max(a * a / 2, 1e-20))
                levels.append(a)
                if second > 0:
                    worst_res = max(worst_res, rdb)
                if rdb > -40.0 or abs(20 * math.log10(max(a, 1e-9) / AMP)) > 0.2:
                    bad.append((second, rdb, 20 * math.log10(max(a, 1e-9) / AMP)))
                second += 1
        carry = x[whole * FS:]
    err = proc.stderr.read().decode("utf-8", "replace").strip()
    proc.wait()
    out(f"ffmpeg: decoded {n} samples, exit {proc.returncode}" + (f", messages: {err[:300]}" if err else ""))
    check(proc.returncode == 0 and not err, "ffmpeg: the whole file decodes without an error")
    if kind == "mp3":
        check(abs(n - frames) <= 1152, f"ffmpeg: decoded length {n} = the frames written {frames} (MP3: within one frame, difference {n - frames})")
        lv = np.array(levels[1:-1]) if len(levels) > 2 else np.array(levels)
        out(f"MP3 tone: {second} seconds, level {20 * math.log10(lv.min() / AMP):+.3f} .. {20 * math.log10(lv.max() / AMP):+.3f} dB of -12 dBFS, "
            f"worst residual after the sine fit {worst_res:.1f} dB")
        for s_, r_, l_ in bad[:20]:
            out(f"  second {s_}: residual {r_:.1f} dB, level {l_:+.2f} dB")
        # the first second holds the start; a second with a lost microphone packet (test machine) holds a jump
        allowed = 1 + lost
        check(len(bad) <= allowed, f"MP3: every second is the tone (residual < -40 dB, level within 0.2 dB): "
              f"{len(bad)} seconds off, allowed {allowed} (start + lost microphone packets)")
    else:
        check(n == frames, f"ffmpeg: decoded length {n} = the frames written {frames}")
except Exception as e:  # noqa: BLE001
    check(False, f"ffmpeg: {type(e).__name__}: {e}")

out("")
out("result: " + ("all passed" if not failures else "FAILED: " + "; ".join(failures)))
open(report_path, "w", encoding="utf-8").write("\n".join(lines) + "\n")
sys.exit(1 if failures else 0)
