#!/usr/bin/env python3
"""MicVST kayıt dosyası analizi (test görevi bölüm 6.3).

Kullanım:
  python3 tools/analiz_kayit.py DOSYA [--ref KABLO.wav] [--json cikti.json] [--sessiz BAS:SON]

Ne ölçer:
  * Başlık: RIFF/RF64, fmt (etiket, bit, hız, kanal, EXTENSIBLE alt biçimi), data uzunluğu, bext (BWF),
    ds64 (RF64), FLAC STREAMINFO + MD5 (flac -t), MP3 için ffprobe.
  * "24 bit gerçekten 24 bit mi": konuşma bölümünde alt 8 bitin sıfır olmama oranı (> %90 beklenir);
    32 bit float'ta değerlerin 2^-23 ızgarasına düşmeme oranı.
  * Kırpma (|x| >= 0,9999), gerçek tepe (4x aşırı örnekleme), LUFS (ITU-R BS.1770-4), DC, gürültü tabanı
    (en sessiz 1 s), boşluk (konuşma içinde >= 24 ardışık tam sıfır), tık adayı (yüksek frekans patlaması),
    süre = örnek sayısı / hız.
  * --ref ile karşılaştırma (ör. MicVST kaydı <-> CABLE Output kaydı): çapraz korelasyonla hizalar, seviye
    farkı (dB), korelasyon, artık seviyesi (dBFS ve 24 bit LSB), artıkta sıçrama sayısı.

Bağımlılıklar: numpy, scipy, soundfile; MP3 ve ek denetimler için ffmpeg/ffprobe ve flac (varsa).
"""
import argparse
import json
import math
import os
import shutil
import struct
import subprocess
import sys

import numpy as np
import soundfile as sf
from scipy import signal


# ---------------------------------------------------------------- başlık
def parse_riff(path):
    info = {"chunks": []}
    with open(path, "rb") as f:
        head = f.read(12)
        if len(head) < 12:
            return {"error": "dosya çok kısa"}
        riff, size, wave = struct.unpack("<4sI4s", head)
        info["container"] = riff.decode("latin1")
        info["riff_size"] = size
        if riff not in (b"RIFF", b"RF64") or wave != b"WAVE":
            return {"error": "RIFF/RF64 WAVE değil", "container": riff.decode("latin1")}
        file_size = os.path.getsize(path)
        ds64_data = None
        pos = 12
        while pos + 8 <= file_size:
            f.seek(pos)
            cid, csize = struct.unpack("<4sI", f.read(8))
            name = cid.decode("latin1")
            real = csize
            if name == "data" and csize == 0xFFFFFFFF and ds64_data is not None:
                real = ds64_data
            info["chunks"].append({"id": name, "size": real, "offset": pos})
            if name == "fmt ":
                d = f.read(min(csize, 64))
                tag, ch, sr, abps, align, bits = struct.unpack("<HHIIHH", d[:16])
                fmt = {"tag": tag, "channels": ch, "rate": sr, "byte_rate": abps, "block_align": align, "bits": bits}
                if tag == 0xFFFE and len(d) >= 40:
                    cb, valid, mask = struct.unpack("<HHI", d[16:24])
                    sub = d[24:40]
                    fmt["valid_bits"] = valid
                    fmt["channel_mask"] = mask
                    fmt["subformat"] = "pcm" if sub[:2] == b"\x01\x00" else ("float" if sub[:2] == b"\x03\x00" else sub.hex())
                info["fmt"] = fmt
            elif name == "ds64":
                d = f.read(min(csize, 28))
                riff64, data64, samples64 = struct.unpack("<QQQ", d[:24])
                ds64_data = data64
                info["ds64"] = {"riff_size": riff64, "data_size": data64, "sample_count": samples64}
            elif name == "bext":
                d = f.read(min(csize, 602))
                info["bext"] = {
                    "description": d[0:256].split(b"\0")[0].decode("latin1", "replace"),
                    "originator": d[256:288].split(b"\0")[0].decode("latin1", "replace"),
                    "originator_ref": d[288:320].split(b"\0")[0].decode("latin1", "replace"),
                    "date": d[320:330].decode("latin1", "replace"),
                    "time": d[330:338].decode("latin1", "replace"),
                    "time_reference": struct.unpack("<Q", d[338:346])[0] if len(d) >= 346 else None,
                    "version": struct.unpack("<H", d[346:348])[0] if len(d) >= 348 else None,
                }
            elif name == "data":
                info["data_offset"] = pos + 8
                info["data_size"] = real
            pos += 8 + real + (real & 1)
        info["file_size"] = file_size
        if "fmt" in info and "data_size" in info and info["fmt"]["block_align"]:
            info["frames_from_header"] = info["data_size"] // info["fmt"]["block_align"]
            info["data_complete"] = info["data_offset"] + info["data_size"] <= file_size
    return info


def run(cmd):
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=600)
        return p.returncode, p.stdout, p.stderr
    except Exception as e:  # noqa: BLE001
        return -1, "", str(e)


def ffprobe(path):
    if not shutil.which("ffprobe"):
        return None
    rc, out, err = run(["ffprobe", "-v", "error", "-show_streams", "-show_format", "-of", "json", path])
    if rc != 0:
        return {"error": err.strip()[:300]}
    try:
        return json.loads(out)
    except ValueError:
        return {"error": "json"}


def ffmpeg_decode(path):
    """ffmpeg ile float64 çözme (MP3 ve doğrulama için). (dizi, hız) ya da None."""
    if not shutil.which("ffmpeg"):
        return None
    pr = ffprobe(path) or {}
    st = next((s for s in pr.get("streams", []) if s.get("codec_type") == "audio"), None)
    if not st:
        return None
    ch = int(st["channels"])
    sr = int(st["sample_rate"])
    rc = subprocess.run(["ffmpeg", "-v", "error", "-i", path, "-f", "f64le", "-acodec", "pcm_f64le", "-"],
                        capture_output=True)
    if rc.returncode != 0:
        return {"error": rc.stderr.decode("utf-8", "replace")[:300]}
    x = np.frombuffer(rc.stdout, dtype="<f8").reshape(-1, ch)
    return x, sr, rc.stderr.decode("utf-8", "replace")


# ---------------------------------------------------------------- ölçümler
def db(x):
    return 20 * math.log10(x) if x > 0 else -math.inf


def k_weighting(sr):
    """ITU-R BS.1770 K-ağırlıklandırma (yüksek raf + yüksek geçiren), herhangi bir hız için."""
    # pre-filter (high shelf)
    f0 = 1681.974450955533
    G = 3.999843853973347
    Q = 0.7071752369554196
    K = math.tan(math.pi * f0 / sr)
    Vh = 10 ** (G / 20)
    Vb = Vh ** 0.4996667741545416
    a0 = 1 + K / Q + K * K
    b1 = [(Vh + Vb * K / Q + K * K) / a0, 2 * (K * K - Vh) / a0, (Vh - Vb * K / Q + K * K) / a0]
    a1 = [1, 2 * (K * K - 1) / a0, (1 - K / Q + K * K) / a0]
    # RLB high-pass
    f0 = 38.13547087602444
    Q = 0.5003270373238773
    K = math.tan(math.pi * f0 / sr)
    b2 = [1, -2, 1]
    a2 = [1, 2 * (K * K - 1) / (1 + K / Q + K * K), (1 - K / Q + K * K) / (1 + K / Q + K * K)]
    return (b1, a1), (b2, a2)


def lufs(x, sr):
    (b1, a1), (b2, a2) = k_weighting(sr)
    y = signal.lfilter(b2, a2, signal.lfilter(b1, a1, x, axis=0), axis=0)
    block = int(0.4 * sr)
    hop = int(0.1 * sr)
    if len(y) < block:
        return None
    z = []
    for s in range(0, len(y) - block + 1, hop):
        seg = y[s:s + block]
        z.append(np.sum(np.mean(seg * seg, axis=0)))
    z = np.array(z)
    with np.errstate(divide="ignore"):
        l = -0.691 + 10 * np.log10(z)
    g = z[l > -70]
    if len(g) == 0:
        return None
    rel = -0.691 + 10 * np.log10(np.mean(g)) - 10
    g2 = z[(l > -70) & (l > rel)]
    return float(-0.691 + 10 * np.log10(np.mean(g2))) if len(g2) else None


def true_peak(x):
    y = signal.resample_poly(x, 4, 1, axis=0)
    return float(np.max(np.abs(y))) if y.size else 0.0


def speech_mask(m, sr, thr_db=-50.0):
    """10 ms parçalarda RMS > eşik olan örnekler (konuşma/ses var)."""
    w = max(1, int(0.01 * sr))
    n = len(m) // w
    if n == 0:
        return np.zeros(len(m), bool)
    r = np.sqrt(np.mean(m[:n * w].reshape(n, w) ** 2, axis=1))
    on = r > 10 ** (thr_db / 20)
    mask = np.repeat(on, w)
    return np.concatenate([mask, np.zeros(len(m) - len(mask), bool)])


def zero_runs(x, mask, min_len=24):
    """Sesli bölüm içinde >= min_len ardışık tam sıfır (tüm kanallar)."""
    z = np.all(x == 0, axis=1) & mask
    if not z.any():
        return []
    d = np.diff(np.concatenate([[0], z.astype(np.int8), [0]]))
    starts = np.where(d == 1)[0]
    ends = np.where(d == -1)[0]
    return [(int(s), int(e - s)) for s, e in zip(starts, ends) if e - s >= min_len]


def hf_bursts(m, sr, ratio=12.0):
    """Tık adayları: 12 kHz üstü (ya da hız/4) enerjinin yerel ortancanın 'ratio' katını aştığı, 480 örnek içinde tek olay."""
    fc = min(12000.0, sr / 4)
    sos = signal.butter(4, fc, "highpass", fs=sr, output="sos")
    h = np.abs(signal.sosfilt(sos, m))
    w = max(1, int(0.002 * sr))
    n = len(h) // w
    if n < 50:
        return []
    e = np.sqrt(np.mean(h[:n * w].reshape(n, w) ** 2, axis=1))
    med = signal.medfilt(e, 101) + 1e-9
    floor = 10 ** (-80 / 20)
    idx = np.where((e > ratio * med) & (e > floor))[0]
    events = []
    last = -10 ** 9
    for i in idx:
        s = i * w
        if s - last > 480:
            events.append(round(s / sr, 4))
            last = s
    return events


def analyse(path, silent_range=None):
    res = {"dosya": os.path.basename(path), "boyut": os.path.getsize(path)}
    ext = os.path.splitext(path)[1].lower()
    raw_int = None
    if ext in (".wav", ".wave"):
        res["baslik"] = parse_riff(path)
    if ext == ".flac" and shutil.which("flac"):
        rc, out, err = run(["flac", "-t", "-s", path])
        res["flac_t"] = "geçti (MD5 doğrulandı)" if rc == 0 else ("HATA: " + (err or out).strip()[:300])
        if shutil.which("metaflac"):
            rc, out, err = run(["metaflac", "--show-md5sum", "--show-sample-rate", "--show-bps", "--show-channels",
                                "--show-total-samples", path])
            res["streaminfo"] = out.split()
    res["ffprobe"] = None
    pr = ffprobe(path)
    if pr and "streams" in pr:
        st = next((s for s in pr["streams"] if s.get("codec_type") == "audio"), {})
        res["ffprobe"] = {k: st.get(k) for k in ("codec_name", "sample_fmt", "sample_rate", "channels",
                                                  "bits_per_raw_sample", "bits_per_sample", "duration_ts", "duration",
                                                  "bit_rate")}
        res["ffprobe"]["format_bit_rate"] = pr.get("format", {}).get("bit_rate")
    elif pr:
        res["ffprobe"] = pr
    if ext == ".mp3":
        dec = ffmpeg_decode(path)
        if dec is None or isinstance(dec, dict):
            res["hata"] = "ffmpeg çözemedi: " + str(dec)
            return res, None, None
        x, sr, ff_err = dec
        res["ffmpeg_uyari"] = ff_err.strip()[:300]
        if shutil.which("mpg123"):
            rc = subprocess.run(["mpg123", "-q", "-e", "f32", "-s", path], capture_output=True)
            if rc.returncode == 0:
                y = np.frombuffer(rc.stdout, dtype="<f4")
                res["mpg123_ornek"] = int(len(y) // max(1, x.shape[1]))
    else:
        try:
            x, sr = sf.read(path, dtype="float64", always_2d=True)
            info = sf.info(path)
            res["soundfile"] = {"subtype": info.subtype, "format": info.format, "frames": info.frames,
                                "rate": info.samplerate, "channels": info.channels}
            if info.subtype in ("PCM_24", "PCM_16", "PCM_32"):
                raw_int = sf.read(path, dtype="int32", always_2d=True)[0]
        except Exception as e:  # noqa: BLE001
            res["hata"] = "okunamadı: " + str(e)
            return res, None, None
    n, ch = x.shape
    res["ornek"] = int(n)
    res["hiz"] = int(sr)
    res["kanal"] = int(ch)
    res["sure_sn"] = round(n / sr, 6)
    m = np.mean(x, axis=1)
    mask = speech_mask(m, sr)
    res["sesli_oran"] = round(float(np.mean(mask)), 4)
    peak = float(np.max(np.abs(x))) if n else 0.0
    res["tepe_dbfs"] = round(db(peak), 3)
    res["gercek_tepe_dbtp"] = round(db(true_peak(x)), 3) if n else None
    res["kirpma_0_9999"] = int(np.sum(np.abs(x) >= 0.9999))
    res["ust_minus10_dbfs"] = int(np.sum(np.abs(x) > 10 ** (-10 / 20)))
    res["dc"] = [round(float(v), 8) for v in np.mean(x, axis=0)]
    res["lufs"] = round(lufs(x, sr), 2) if lufs(x, sr) is not None else None
    # gürültü tabanı: en sessiz 1 s
    w = int(sr)
    if n >= w:
        k = n // w
        rms = np.sqrt(np.mean((m[:k * w].reshape(k, w)) ** 2, axis=1))
        res["gurultu_tabani_dbfs"] = round(db(float(np.min(rms))), 2)
        res["en_yuksek_1sn_rms_dbfs"] = round(db(float(np.max(rms))), 2)
    if silent_range:
        a, b = int(silent_range[0] * sr), int(silent_range[1] * sr)
        seg = m[a:b]
        res["sessiz_bolum_rms_dbfs"] = round(db(float(np.sqrt(np.mean(seg ** 2)))) if len(seg) else math.nan, 2)
    res["l_r_fark_dbfs"] = round(db(float(np.sqrt(np.mean((x[:, 0] - x[:, 1]) ** 2)))), 2) if ch == 2 else None
    res["bosluk_24_sifir"] = zero_runs(x, mask)[:20]
    res["tik_adaylari_sn"] = hf_bursts(m, sr)[:50]
    # gerçek bit derinliği
    if raw_int is not None:
        sub = res.get("soundfile", {}).get("subtype")
        sel = raw_int[mask] if mask.any() else raw_int
        if sub == "PCM_24":
            v = sel >> 8
            res["alt8bit_sifir_olmayan_oran"] = round(float(np.mean((v & 0xFF) != 0)), 4)
        elif sub == "PCM_32":
            res["alt8bit_sifir_olmayan_oran"] = round(float(np.mean((sel & 0xFF) != 0)), 4)
        elif sub == "PCM_16":
            v = sel >> 16
            res["alt4bit_sifir_olmayan_oran"] = round(float(np.mean((v & 0xF) != 0)), 4)
    if res.get("soundfile", {}).get("subtype") == "FLOAT":
        xf = sf.read(path, dtype="float32", always_2d=True)[0]
        sel = xf[mask] if mask.any() else xf
        q = sel.astype(np.float64) * (2 ** 23)
        res["float_2e-23_izgara_disi_oran"] = round(float(np.mean(q != np.round(q))), 4)
    return res, x, sr


def align_compare(a, sra, b, srb):
    """a: test (MicVST), b: referans (kablo). Hizalar, karşılaştırır."""
    out = {}
    if sra != srb:
        return {"hata": f"hızlar farklı {sra} / {srb}"}
    ma = np.mean(a, axis=1)
    mb = np.mean(b, axis=1)
    L = min(len(ma), len(mb), sra * 30)
    # kaba hizalama: FFT çapraz korelasyon (ilk 30 s üzerinde, tam aralık)
    nfft = 1 << int(math.ceil(math.log2(len(ma) + len(mb))))
    seg_a = ma[: min(len(ma), sra * 60)]
    c = np.fft.irfft(np.fft.rfft(mb, nfft) * np.conj(np.fft.rfft(seg_a, nfft)), nfft)
    lag = int(np.argmax(np.abs(c)))
    if lag > nfft // 2:
        lag -= nfft
    # lag: b[i+lag] ~ a[i]
    out["kayma_ornek"] = lag
    out["kayma_ms"] = round(1000 * lag / sra, 3)
    if lag >= 0:
        aa, bb = a, b[lag:]
    else:
        aa, bb = a[-lag:], b
    n = min(len(aa), len(bb))
    aa, bb = aa[:n], bb[:n]
    out["ortak_ornek"] = int(n)
    out["ortak_sure_sn"] = round(n / sra, 3)
    ra = float(np.sqrt(np.mean(aa ** 2)))
    rb = float(np.sqrt(np.mean(bb ** 2)))
    out["seviye_farki_db"] = round(db(ra) - db(rb), 4) if ra > 0 and rb > 0 else None
    fa, fb = aa.ravel(), bb.ravel()
    out["korelasyon"] = round(float(np.corrcoef(fa, fb)[0, 1]), 7) if np.std(fa) > 0 and np.std(fb) > 0 else None
    r = aa - bb
    out["artik_rms_dbfs"] = round(db(float(np.sqrt(np.mean(r ** 2)))), 2)
    mx = float(np.max(np.abs(r)))
    out["artik_tepe_dbfs"] = round(db(mx), 2)
    out["artik_tepe_lsb24"] = round(mx * 2 ** 23, 3)
    out["bit_ayni_oran"] = round(float(np.mean(r == 0)), 6)
    # artıkta sıçrama: 256 örneklik parçalarda artık RMS > -60 dBFS ve > sinyal RMS'sinin %2'si
    w = 256
    k = n // w
    if k:
        rr = np.sqrt(np.mean((r[:k * w].reshape(k, w, -1)) ** 2, axis=(1, 2)))
        sr_ = np.sqrt(np.mean((bb[:k * w].reshape(k, w, -1)) ** 2, axis=(1, 2)))
        bad = np.where((rr > 10 ** (-60 / 20)) & (rr > 0.02 * sr_))[0]
        out["artik_bulgu_parca"] = int(len(bad))
        out["artik_bulgu_sn"] = [round(i * w / sra, 3) for i in bad[:30]]
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dosya")
    ap.add_argument("--ref")
    ap.add_argument("--json")
    ap.add_argument("--sessiz", help="sessiz bölüm, saniye BAS:SON")
    a = ap.parse_args()
    sil = tuple(float(v) for v in a.sessiz.split(":")) if a.sessiz else None
    res, x, sr = analyse(a.dosya, sil)
    if a.ref and x is not None:
        rres, y, sry = analyse(a.ref, None)
        res["referans"] = {k: rres.get(k) for k in ("dosya", "ornek", "hiz", "kanal", "tepe_dbfs", "lufs", "tik_adaylari_sn")}
        res["karsilastirma"] = align_compare(x, sr, y, sry)
    txt = json.dumps(res, ensure_ascii=False, indent=1, default=str)
    if a.json:
        with open(a.json, "w", encoding="utf-8") as f:
            f.write(txt)
    print(txt)


if __name__ == "__main__":
    main()
