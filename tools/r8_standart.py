"""R8 (test task docs/TEST_PROMPTU_WIN11.md): MicVST's recordings readable for years - the containers
checked against their standards and the audio decoded by two independent readers:

  WAV / BWF  EBU Tech 3285: RIFF size, fmt, bext (size, origination date/time, version, reserved = 0),
             data size = the frames
  RF64       EBU Tech 3306: 'RF64', RIFF and data size fields 0xFFFFFFFF, ds64 first with the real
             RIFF size, data size and sample count
  FLAC       RFC 9639: STREAMINFO (rate, channels, bits, total samples = the frames, MD5 not zero), and
             the MD5 of the decoded samples = the STREAMINFO MD5 (what 'flac -t' checks)
  MP3        ISO/IEC 11172-3: every frame header valid (MPEG-1 Layer III, one rate), the frames back to
             back to the end; Xing/Info + LAME tag: frames, encoder delay and padding -> the length
  readers    libsndfile (Python soundfile; it decodes FLAC with libFLAC and MP3 with libmpg123) and
             ffmpeg: the same number of samples and the same samples (PCM: bit for bit; MP3: two
             decoders, within 1e-4)

usage as a script: python r8_standart.py <file> [<frames>] ...   (frames: what the recorder wrote)
"""
import hashlib, os, re, struct, subprocess, sys

import numpy as np
import soundfile as sf


def wav_checks(path, frames, check, out):
    size = os.path.getsize(path)
    with open(path, "rb") as f:
        riff, riff_size, wave = struct.unpack("<4sI4s", f.read(12))
        chunks, pos, ds64 = [], 12, None
        while pos + 8 <= size:
            f.seek(pos)
            cid, csz = struct.unpack("<4sI", f.read(8))
            body = f.read(min(csz, 65536)) if cid in (b"ds64", b"bext", b"fmt ") else b""
            real = csz
            if cid == b"ds64":
                ds64 = dict(zip(("riff", "data", "samples"), struct.unpack("<QQQ", body[:24])))
            if cid == b"data" and csz == 0xFFFFFFFF and ds64:
                real = ds64["data"]
            chunks.append((cid, real, csz, body))
            pos += 8 + real + (real & 1)
    names = [c[0].decode("ascii", "replace") for c in chunks]
    out(f"WAV chunks: {riff.decode()} / {wave.decode()}: " + ", ".join(f"{n} {c[1]}" for n, c in zip(names, chunks)))
    fmt = next((c[3] for c in chunks if c[0] == b"fmt "), b"")
    data = next((c for c in chunks if c[0] == b"data"), None)
    tag, ch, rate, _, block, bits = struct.unpack("<HHIIHH", fmt[:16]) if len(fmt) >= 16 else (0,) * 6
    out(f"fmt: tag {tag}, {ch} ch, {rate} Hz, {bits} bit, block {block}")
    check(data is not None and block > 0 and data[1] == frames * block, f"WAV: data chunk = {frames} frames x {block} bytes")
    if riff == b"RF64":
        check(riff_size == 0xFFFFFFFF and names[0] == "ds64" and data is not None and data[2] == 0xFFFFFFFF
              and ds64 and ds64["riff"] == size - 8 and ds64["data"] == data[1] and ds64["samples"] == frames,
              f"RF64 (EBU Tech 3306): size fields 0xFFFFFFFF, ds64 first: RIFF {ds64 and ds64['riff']} (file - 8 = {size - 8}), "
              f"data {ds64 and ds64['data']}, samples {ds64 and ds64['samples']}")
    else:
        check(riff == b"RIFF" and riff_size == size - 8, f"RIFF size field {riff_size} = file - 8")
    bext = next((c for c in chunks if c[0] == b"bext"), None)
    if bext is None:
        check(False, "BWF (EBU Tech 3285): bext chunk present")
        return
    b = bext[3]
    desc, orig, oref = b[0:256], b[256:288], b[288:320]
    date, tm = b[320:330].decode("ascii", "replace"), b[330:338].decode("ascii", "replace")
    tref, ver = struct.unpack("<QH", b[338:348])
    reserved = b[348 + 64 + 10:602]
    history = b[602:].split(b"\0")[0].decode("ascii", "replace").strip()
    orig_s = orig.split(b"\0")[0].decode("ascii", "replace")
    out(f"bext: {bext[1]} bytes, originator '{orig_s}', date {date} {tm}, "
        f"version {ver}, time reference {tref}, coding history '{history}'")
    check(bext[1] >= 602 and re.fullmatch(r"\d{4}[-_:/. ]\d{2}[-_:/. ]\d{2}", date) is not None
          and re.fullmatch(r"\d{2}[-_:/. ]\d{2}[-_:/. ]\d{2}", tm) is not None and ver in (0, 1, 2)
          and not any(reserved), "BWF (EBU Tech 3285): bext size >= 602, date yyyy-mm-dd, time hh-mm-ss, version 0..2, reserved bytes 0")


def flac_streaminfo(path):
    with open(path, "rb") as f:
        if f.read(4) != b"fLaC":
            return None
        head = f.read(4)
        btype, blen = head[0] & 0x7F, int.from_bytes(head[1:4], "big")
        si = f.read(blen)
    if btype != 0 or blen != 34:
        return None
    v = int.from_bytes(si[10:18], "big")
    return dict(rate=v >> 44, channels=((v >> 41) & 7) + 1, bits=((v >> 36) & 31) + 1, total=v & ((1 << 36) - 1), md5=si[18:34])


def flac_checks(path, frames, check, out):
    si = flac_streaminfo(path)
    if si is None:
        check(False, "FLAC (RFC 9639): 'fLaC' + STREAMINFO first")
        return
    out(f"STREAMINFO: {si['rate']} Hz, {si['channels']} ch, {si['bits']} bit, total samples {si['total']}, MD5 {si['md5'].hex()}")
    check(si["total"] == frames and any(si["md5"]), "FLAC (RFC 9639): STREAMINFO total samples = the frames, MD5 set")
    # the MD5 of the decoded samples (signed, little endian, ceil(bits/8) bytes, interleaved) - as 'flac -t'
    h, width, shift = hashlib.md5(), (si["bits"] + 7) // 8, 32 - si["bits"]
    for blk in sf.blocks(path, blocksize=1 << 18, dtype="int32", always_2d=True):
        v = (blk.astype("<i4") >> shift).astype("<i4")
        h.update(v.view(np.uint8).reshape(-1, 4)[:, :width].tobytes())
    check(h.digest() == si["md5"], f"FLAC: MD5 of the decoded audio = STREAMINFO MD5 ({h.hexdigest()})")


BITRATES = [0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320]


def mp3_checks(path, frames, check, out):
    data = open(path, "rb").read()
    pos = 0
    if data[:3] == b"ID3":
        pos = 10 + ((data[6] & 127) << 21 | (data[7] & 127) << 14 | (data[8] & 127) << 7 | (data[9] & 127))
    end = len(data) - (128 if data[-128:-125] == b"TAG" else 0)
    first, n, bad, rates = pos, 0, 0, set()
    while pos + 4 <= end:
        h = int.from_bytes(data[pos:pos + 4], "big")
        sync, ver, layer, bri, sri, pad = h >> 21, (h >> 19) & 3, (h >> 17) & 3, (h >> 12) & 15, (h >> 10) & 3, (h >> 9) & 1
        if sync != 0x7FF or ver != 3 or layer != 1 or bri in (0, 15) or sri == 3:
            bad += 1
            break
        rate = (44100, 48000, 32000)[sri]
        rates.add(rate)
        pos += 144000 * BITRATES[bri] // rate + pad
        n += 1
    check(bad == 0 and pos == end and len(rates) == 1, f"MP3 (ISO/IEC 11172-3): {n} frames, every header MPEG-1 Layer III at {sorted(rates)} Hz, "
          f"back to back to the end ({pos} of {end} bytes)")
    x = data[first:first + 400]
    k = max(x.find(b"Xing"), x.find(b"Info"))
    if k < 0:
        check(False, "Xing/Info tag in the first frame")
        return
    flags = int.from_bytes(x[k + 4:k + 8], "big")
    q = k + 8
    xf = int.from_bytes(x[q:q + 4], "big") if flags & 1 else -1
    q += (4 if flags & 1 else 0) + (4 if flags & 2 else 0) + (100 if flags & 4 else 0) + (4 if flags & 8 else 0)
    lame = x[q:q + 36]
    if lame[:4] != b"LAME" and lame[:4] != b"Lavf" and lame[:4] != b"Lavc":
        check(False, f"LAME tag after the Xing header (found {lame[:9]!r})")
        return
    d = int.from_bytes(lame[21:24], "big")
    delay, padding = d >> 12, d & 0xFFF
    total = xf * 1152 - delay - padding
    out(f"Xing/{x[k:k + 4].decode()}: frames {xf} (walked {n}, the tag frame included), LAME '{lame[:9].decode('ascii', 'replace')}', "
        f"encoder delay {delay}, padding {padding} -> {total} samples")
    check(xf in (n - 1, n) and total == frames, f"Xing/LAME tag: length {total} = the frames written {frames}")


def pipe_reader(cmd, chn):
    """yields float32 [n, chn] blocks from a decoder writing raw float32 to stdout"""
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    carry = b""
    while True:
        b = p.stdout.read(1 << 20)
        if not b:
            break
        b = carry + b
        k = len(b) // (4 * chn) * 4 * chn
        carry = b[k:]
        yield np.frombuffer(b[:k], dtype="<f4").reshape(-1, chn)
    p.wait()


def mp3_readers(path, frames, check, out):
    """ffmpeg, mpg123 (if installed) and libsndfile: libsndfile 1.2 decodes MP3 wrongly when it is read
    in pieces (about 1600 samples after every read call differ - also with an MP3 made by ffmpeg, so
    a libsndfile bug), so it is read whole when the file is small enough, otherwise only its length"""
    import shutil
    chn = sf.info(path).channels
    decoders = {"ffmpeg": ["ffmpeg", "-v", "error", "-nostdin", "-i", path, "-map", "0:a:0", "-f", "f32le", "-"]}
    if shutil.which("mpg123"):
        decoders["mpg123"] = ["mpg123", "-q", "-e", "f32", "-s", path]
    whole = None
    if frames * chn * 4 < (1 << 30):
        whole, _ = sf.read(path, dtype="float32", always_2d=True)
    lengths, worst = {}, {}
    ref = None
    for name, cmd in decoders.items():
        pos, w = 0, 0.0
        if name == "ffmpeg":
            ref = []
        for blk in pipe_reader(cmd, chn):
            if name == "ffmpeg":
                if whole is not None:
                    m = min(len(blk), len(whole) - pos)
                    if m > 0:
                        w = max(w, float(np.abs(blk[:m] - whole[pos:pos + m]).max()))
                if whole is None and len(ref) < 64:
                    ref.append(blk)
            else:
                if whole is not None:
                    m = min(len(blk), len(whole) - pos)
                    if m > 0:
                        w = max(w, float(np.abs(blk[:m] - whole[pos:pos + m]).max()))
            pos += len(blk)
        lengths[name], worst[name] = pos, w
    lengths["libsndfile"] = sf.info(path).frames if whole is None else len(whole)
    out("MP3 readers: " + ", ".join(f"{k} {v}" for k, v in lengths.items()) + " samples"
        + ("; largest difference to libsndfile (read whole): " + ", ".join(f"{k} {v:.2e}" for k, v in worst.items()) if whole is not None
           else "; libsndfile: length only (file too large to read whole)"))
    check(all(v == frames for v in lengths.values()) and (whole is None or all(v < 1e-4 for v in worst.values())),
          f"MP3: {len(lengths)} decoders, the same length (= the frames){' and the same audio (< 1e-4)' if whole is not None else ''}")


def readers(path, frames, kind, check, out):
    """libsndfile and ffmpeg: same count, same samples."""
    if kind == "mp3":
        return mp3_readers(path, frames, check, out)
    if kind == "wav" and "FLOAT" in sf.info(path).subtype:
        kind = "float"   # compared as float32, bit for bit
    ff = subprocess.Popen(["ffmpeg", "-v", "error", "-nostdin", "-i", path, "-map", "0:a:0", "-f",
                           "f32le" if kind in ("mp3", "float") else "s32le", "-"], stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    info = sf.info(path)
    chn = info.channels
    n_sf = n_ff = 0
    diff, same = 0.0, True
    hs, hf = hashlib.md5(), hashlib.md5()
    for blk in sf.blocks(path, blocksize=1 << 16, dtype="float32" if kind in ("mp3", "float") else "int32", always_2d=True):
        n_sf += len(blk)
        want = len(blk) * chn * 4
        buf = b""
        while len(buf) < want:
            more = ff.stdout.read(want - len(buf))
            if not more:
                break
            buf += more
        g = np.frombuffer(buf, dtype="<f4" if kind in ("mp3", "float") else "<i4").reshape(-1, chn)
        n_ff += len(g)
        m = min(len(g), len(blk))
        if kind == "mp3":
            if m:
                diff = max(diff, float(np.abs(blk[:m] - g[:m]).max()))
        else:
            hs.update(np.ascontiguousarray(blk).astype("<f4" if kind == "float" else "<i4").tobytes())
            hf.update(g.tobytes())
    rest = ff.stdout.read()
    n_ff += len(rest) // (4 * chn)
    err = ff.stderr.read().decode("utf-8", "replace").strip()
    ff.wait()
    if kind == "mp3":
        out(f"readers: libsndfile (libmpg123) {n_sf}, ffmpeg {n_ff} samples; largest difference {diff:.2e}")
        check(n_sf == frames and n_ff == frames and diff < 1e-4 and not err, "two MP3 decoders: same length (= the frames) and the same audio (< 1e-4)")
    else:
        same = hs.digest() == hf.digest()
        out(f"readers: libsndfile {n_sf}, ffmpeg {n_ff} samples; MD5 {hs.hexdigest()} / {hf.hexdigest()}")
        check(n_sf == frames and n_ff == frames and same and not err, "libsndfile and ffmpeg: same length (= the frames), the same samples bit for bit")


def run(path, frames, check, out):
    kind = path.rsplit(".", 1)[-1].lower()
    if kind == "wav":
        wav_checks(path, frames, check, out)
    elif kind == "flac":
        flac_checks(path, frames, check, out)
    elif kind == "mp3":
        mp3_checks(path, frames, check, out)
    readers(path, frames, kind, check, out)


if __name__ == "__main__":
    failures = []

    def check(ok, what):
        print(("  PASS " if ok else "  FAIL ") + what)
        if not ok:
            failures.append(what)

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        p = args[i]
        fr = int(args[i + 1]) if i + 1 < len(args) and args[i + 1].isdigit() else sf.info(p).frames
        i += 2 if i + 1 < len(args) and args[i + 1].isdigit() else 1
        print(f"## {os.path.basename(p)} ({fr} frames)")
        try:
            run(p, fr, check, print)
        except Exception as e:  # noqa: BLE001
            check(False, f"{type(e).__name__}: {e}")
    print("result: " + ("all passed" if not failures else f"FAILED {len(failures)}"))
    sys.exit(1 if failures else 0)
