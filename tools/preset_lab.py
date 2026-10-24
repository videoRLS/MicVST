#!/usr/bin/env python3
"""Measures what hazır ayar chains do to speech (renders by MicVSTPresetLab, tests/PresetLab.cpp).

  preset_lab.py measure <input.wav> <rendered.wav>...      -> one JSON line per rendered file
  preset_lab.py table <results.jsonl> [men|women]           -> markdown summary per preset

Every number compares the rendered file with its input, frame by frame (the renders are aligned):
  lufs        integrated loudness (ITU-R BS.1770-4, gated)
  tp          true peak, dBTP (4x oversampled)
  bal_*       tonal balance: band level re the whole active speech (dB), input-relative change in d_bal_*
              low 80-200, lowmid 200-500, mid 500-2000, pres 2000-5000, sib 5000-10000, air 10000-16000 Hz
  sib         sibilance: 4-12 kHz energy of the loudest sibilant frames (p90) re the vowels' 0.3-3 kHz (p50);
              d_sib = change against the input (negative: de-essed)
  floor       room noise in pauses (runs of 10 ms frames of the input within 6 dB of its quietest 5 %,
              at least 0.5 s long, without their first 0.35 s) re
              active speech (p50), d_floor = change (negative: noise turned down, positive: brought up -
              compression without a working gate); None when the input has hardly any pauses
  onset       first 5 ms of a word re 20-40 ms later, change against the input (negative: onsets cut);
              n_onsets = how many word starts it was measured on
  d_vair      vowels' 8-16 kHz re their 300-3000 Hz, change (the air of the voice itself, sibilants aside)
  d_crest     active speech: 99.9th percentile peak re RMS, change (negative: transients squashed)
  st_std      standard deviation of the 3 s loudness (macro dynamics), d_st_std change
  syl         p95 - p50 of 10 ms levels in speech (micro dynamics), d_syl change
Inputs are 48 kHz WAV (float or PCM), stereo or mono.
"""
import json, subprocess, sys
import numpy as np
import scipy.signal as sg

FS = 48000
GROUPS = {"low": (80, 200), "lowmid": (200, 500), "mid": (500, 2000), "pres": (2000, 5000),
          "sib": (5000, 10000), "air": (10000, 16000)}


def load(path):
    raw = subprocess.run(["ffmpeg", "-nostdin", "-v", "error", "-i", path, "-f", "f64le", "-acodec", "pcm_f64le",
                          "-ac", "2", "-ar", str(FS), "-"], capture_output=True, check=True).stdout
    return np.frombuffer(raw, np.float64).reshape(-1, 2)


def kweight(y):
    b1, a1 = [1.53512485958697, -2.69169618940638, 1.19839281085285], [1, -1.69065929318241, 0.73248077421585]
    b2, a2 = [1, -2, 1], [1, -1.99004745483398, 0.99007225036621]
    return sg.lfilter(b2, a2, sg.lfilter(b1, a1, y, axis=0), axis=0)


def lufs(x):
    k = kweight(x)
    blk, hop = int(0.4 * FS), int(0.1 * FS)
    ms = np.array([np.sum(np.mean(k[i:i + blk] ** 2, axis=0)) for i in range(0, len(k) - blk, hop)])
    lk = -0.691 + 10 * np.log10(ms + 1e-30)
    g = ms[lk > -70]
    if len(g) == 0:
        return -99.0
    rel = -0.691 + 10 * np.log10(np.mean(g)) - 10
    g = ms[lk > rel]
    return float(-0.691 + 10 * np.log10(np.mean(g)))


def short_term(x):
    k = kweight(x)
    w, hop = 3 * FS, FS // 2
    return np.array([-0.691 + 10 * np.log10(np.sum(np.mean(k[i:i + w] ** 2, axis=0)) + 1e-30) for i in range(0, len(k) - w, hop)])


def frames(m, n=480):
    c = len(m) // n
    return m[:c * n].reshape(c, n)


def db(p):
    return 10 * np.log10(p + 1e-30)


def band_energy(m, lo, hi):
    sos = sg.butter(4, [lo, hi], "bandpass", fs=FS, output="sos")
    return np.mean(frames(sg.sosfilt(sos, m)) ** 2, axis=1)


def analyse(inp, out):
    mi, mo = inp.mean(axis=1), out.mean(axis=1)
    n = min(len(mi), len(mo))
    mi, mo = mi[:n], mo[:n]
    fi, fo = frames(mi), frames(mo)
    li = db(np.mean(fi ** 2, axis=1))
    lo = db(np.mean(fo ** 2, axis=1))
    active = li > np.percentile(li, 40)
    # real pauses: runs of at least 0.5 s within 6 dB of the quietest 5 %, without their first 0.35 s
    # (a gate rightly holds open over the gaps between words, and closes over its hold + release)
    quiet = li < np.percentile(li, 5) + 6.0
    pauses = np.zeros_like(quiet)
    k = 0
    while k < len(quiet):
        if quiet[k]:
            e = k
            while e < len(quiet) and quiet[e]:
                e += 1
            if e - k >= 50:
                pauses[k + 35:e] = True
            k = e
        else:
            k += 1
    if pauses.sum() < 30 or np.percentile(li, 50) - np.percentile(li, 5) < 15.0:
        pauses = None   # hardly any real pauses (continuous reading): the quiet frames are soft speech
    r = {}
    r["lufs"] = lufs(out[:n])
    r["tp"] = float(20 * np.log10(np.max(np.abs(sg.resample_poly(out[:n], 4, 1, axis=0))) + 1e-12))
    r["clip"] = int(np.sum(np.abs(out) >= 0.9999))
    # tonal balance (active frames)
    act_i = fi[active].reshape(-1)
    act_o = fo[active].reshape(-1)
    fr, pi = sg.welch(act_i, FS, nperseg=8192)
    _, po = sg.welch(act_o, FS, nperseg=8192)
    ti, to = np.sum(pi[(fr >= 80) & (fr < 16000)]), np.sum(po[(fr >= 80) & (fr < 16000)])
    for name, (a, b) in GROUPS.items():
        s = (fr >= a) & (fr < b)
        bi, bo = db(np.sum(pi[s]) / ti), db(np.sum(po[s]) / to)
        r["bal_" + name] = float(bo)
        r["d_bal_" + name] = float(bo - bi)
    # 1/3 octave curve change (for plots / checks)
    centres = [100, 125, 160, 200, 250, 315, 400, 500, 630, 800, 1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500]
    third = []
    for c in centres:
        s = (fr >= c / 2 ** (1 / 6)) & (fr < c * 2 ** (1 / 6))
        third.append(float(db(np.sum(po[s]) / to) - db(np.sum(pi[s]) / ti)))
    r["third_change"] = dict(zip([str(c) for c in centres], third))
    # sibilance
    hi_i, lo_i = band_energy(mi, 4000, 12000), band_energy(mi, 300, 3000)
    hi_o, lo_o = band_energy(mo, 4000, 12000), band_energy(mo, 300, 3000)
    sib = active & (db(hi_i) - db(lo_i) > 3)
    vow = active & ~sib
    if sib.sum() > 10 and vow.sum() > 10:
        si = np.percentile(db(hi_i[sib]), 90) - np.percentile(db(lo_i[vow]), 50)
        so = np.percentile(db(hi_o[sib]), 90) - np.percentile(db(lo_o[vow]), 50)
        r["sib"], r["d_sib"] = float(so), float(so - si)
    else:
        r["sib"], r["d_sib"] = None, None
    # vowels' air
    if sib.sum() > 10 and vow.sum() > 10:
        ai, ao = band_energy(mi, 8000, 16000), band_energy(mo, 8000, 16000)
        r["d_vair"] = float((db(np.mean(ao[vow])) - db(np.mean(lo_o[vow]))) - (db(np.mean(ai[vow])) - db(np.mean(lo_i[vow]))))
    else:
        r["d_vair"] = None
    # crest
    ci = 20 * np.log10(np.percentile(np.abs(fi[active]), 99.9) / np.sqrt(np.mean(fi[active] ** 2)))
    co = 20 * np.log10(np.percentile(np.abs(fo[active]), 99.9) / np.sqrt(np.mean(fo[active] ** 2)))
    r["d_crest"] = float(co - ci)
    # pause floor
    act_i_med, act_o_med = np.percentile(li[active], 50), np.percentile(lo[active], 50)
    if pauses is not None:
        r["floor"] = float(db(np.mean(10 ** (lo[pauses] / 10))) - act_o_med)
        r["d_floor"] = float(r["floor"] - (db(np.mean(10 ** (li[pauses] / 10))) - act_i_med))
    else:
        r["floor"], r["d_floor"] = None, None
    # onsets: a pause (3 frames below p30) followed by speech (> p50)
    p30, p50 = np.percentile(li, 30), np.percentile(li, 50)
    d = []
    for k in range(4, len(li) - 5):
        if li[k] > p50 and np.all(li[k - 3:k] < p30):
            s = k * 480
            e_i = np.mean(mi[s:s + 240] ** 2) / (np.mean(mi[s + 960:s + 1920] ** 2) + 1e-30)
            e_o = np.mean(mo[s:s + 240] ** 2) / (np.mean(mo[s + 960:s + 1920] ** 2) + 1e-30)
            d.append(db(e_o) - db(e_i))
    r["onset"] = float(np.median(d)) if d else None
    r["n_onsets"] = len(d)
    # dynamics
    sti, sto = short_term(inp[:n]), short_term(out[:n])
    m_i = sti > np.max(sti) - 20
    r["st_std"] = float(np.std(sto[m_i])) if m_i.sum() > 2 else None
    r["d_st_std"] = float(np.std(sto[m_i]) - np.std(sti[m_i])) if m_i.sum() > 2 else None
    r["syl"] = float(np.percentile(lo[active], 95) - np.percentile(lo[active], 50))
    r["d_syl"] = float(r["syl"] - (np.percentile(li[active], 95) - np.percentile(li[active], 50)))
    return r


def main():
    if len(sys.argv) >= 4 and sys.argv[1] == "measure":
        inp = load(sys.argv[2])
        for path in sys.argv[3:]:
            r = analyse(inp, load(path))
            r["input"], r["file"] = sys.argv[2], path
            print(json.dumps(r))
        return
    print(__doc__)


if __name__ == "__main__":
    main()
