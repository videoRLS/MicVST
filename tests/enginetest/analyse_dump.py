#!/usr/bin/env python3
"""Offline analysis of an engine-test dump (MicVSTEngineTest dump ...): the exact input of the
virtual microphone vs. the recorded cable output, on one time base (capture time / play time).

All signals except a pure sine:
  * location: every 1024-sample window of the output is located in the input (FFT
    cross-correlation, parabolic peak). Windows quieter than -50 dB re the whole signal (speech
    pauses), matches with rho < 0.9 and ambiguous matches (second-best peak within 2 % of the best,
    periodic sound) are not used. Latency of a window = capture-to-playback time; a drop = audio
    skipped or lost, a rise = audio repeated or silence inserted; its slope = speed error (ppm).
    Periodic windows are retried with a 4096-sample window; single mislocated windows (> 1.5
    samples off the running median of 5) are reported but are not discontinuities - a real skip or
    repeat shifts the latency for good.
  * clicks: the input is resampled onto the output's time base along the measured latency curve
    (64-tap Kaiser-windowed sinc, so the controller's slow speed changes are followed exactly; the
    latency of every window refined to a fraction of a sample and smoothed robustly), then each
    256-sample output piece is compared with it (one gain factor, best of 9 offsets within +-0.12
    samples, a candidate again within +-0.6 samples; no filter that could hide a click); a residual peak above the run's threshold marks a discontinuity: 10 x the
    99th percentile of residual/RMS over all pieces (the method's floor for that signal), within
    2..20 % of the piece's RMS and above -60 dB re the whole signal; in pauses (pieces quieter
    than -50 dB re the signal) a residual above -50 dB re the signal. Pieces overlap by half.
  * level of every 1024-sample window vs. the resampled input; peaks, |y| >= 1 (clipping).
  Clicks and level are compared below 18 kHz (both signals through the same linear-phase lowpass):
  MicVST's resampler rolls off above ~20 kHz by design.
Pure sine: per 1024-sample window a sine fit with free frequency -> level, speed (ppm), THD+N;
  discontinuities by the second difference (limit 3 * A * w^2 + 2e-4).
Multitone (same tones on L and R): per 4096-sample window all tones are fitted with one common
  speed ratio -> frequency response, L/R balance, L/R phase, and TD+N = everything that is not one
  of the tones (distortion, intermodulation, noise).
Every finding is attributed to the events the test recorded: the virtual cable's own late wake-ups
(device gaps = stalls of the test machine), mic packets lost before MicVST could read them (also
stalls of the test machine), MicVST's own events (trims, skips, underruns, ...) and chain edits.
What none of them explains is listed as "unexplained".

usage: analyse_dump.py <name> [<name> ...]   (files in $MICVST_TEST_OUT, default ./engine-test)
"""
import json, os, sys
import numpy as np

D = os.environ.get('MICVST_TEST_OUT', os.path.join(os.getcwd(), 'engine-test'))
HOP, PIECE = 1024, 256
fs_max = 48000 * 3600.0   # 'until the end' windows


def load(name):
    meta = json.load(open(os.path.join(D, f'dump_{name}.json')))
    x = np.fromfile(os.path.join(D, f'dump_{name}_in.f32'), dtype=np.float32).reshape(-1, 2).astype(np.float64)
    y = np.fromfile(os.path.join(D, f'dump_{name}_out.f32'), dtype=np.float32).reshape(-1, 2).astype(np.float64)
    # chains of real plug-ins: the offline rendering of the same plug-ins is what the cable must
    # carry (the plug-ins change the sound on purpose)
    ref = os.path.join(D, f'dump_{name}_ref.f32')
    if meta.get('reference') and os.path.exists(ref):
        x = np.fromfile(ref, dtype=np.float32).reshape(-1, 2).astype(np.float64)
    return meta, x, y


def xcorr(seg_x, w_y):
    """normalised cross-correlation of w_y against every position in seg_x"""
    n = len(seg_x) + len(w_y)
    nfft = 1 << (n - 1).bit_length()
    c = np.fft.irfft(np.fft.rfft(seg_x, nfft) * np.conj(np.fft.rfft(w_y, nfft)), nfft)[: len(seg_x) - len(w_y) + 1]
    e = np.cumsum(np.concatenate(([0.0], seg_x * seg_x)))
    ex = e[len(w_y):] - e[: len(e) - len(w_y)]
    ey = float(np.dot(w_y, w_y))
    # floor: input stretches far quieter than the window (pauses; cumsum cancellation) cannot match
    return c / np.sqrt(np.maximum(ex, 1e-3 * ey) * ey + 1e-30)


def peak(rho):
    k = int(np.argmax(rho))
    frac = 0.0
    if 0 < k < len(rho) - 1:
        a, b, cc = rho[k - 1], rho[k], rho[k + 1]
        den = a - 2 * b + cc
        if abs(den) > 1e-12:
            frac = 0.5 * (a - cc) / den
    masked = rho.copy()
    masked[max(0, k - 24): k + 25] = -1.0
    return k + frac, float(rho[k]), float(masked.max()) if len(masked) else -1.0


_WARP_TABLE = None


def warp(x, pos):
    """x sampled at fractional positions pos: 64-tap Kaiser-windowed sinc (beta 8), cutoff 0.92 x
    Nyquist - flat to well above 18 kHz at 48 kHz (a shorter interpolator's own error near 18 kHz
    would look like a residual on broadband sound). Kernels tabulated for 2048 fractional phases,
    linearly interpolated between them."""
    global _WARP_TABLE
    j = np.arange(-31, 33)
    if _WARP_TABLE is None:
        fc, beta, half = 0.46, 8.0, 32.5
        fr = np.arange(2049) / 2048.0
        u = fr[:, None] - j[None, :]
        w = np.i0(beta * np.sqrt(np.clip(1.0 - (u / half) ** 2, 0.0, 1.0))) / np.i0(beta)
        _WARP_TABLE = 2 * fc * np.sinc(2 * fc * u) * w
    pos = np.asarray(pos, dtype=np.float64)
    out = np.zeros(len(pos))
    ip = np.floor(pos).astype(np.int64)
    ph = (pos - ip) * 2048.0
    k = np.minimum(ph.astype(np.int64), 2047)
    t = (ph - k)[:, None]
    ok = (ip - 31 >= 0) & (ip + 32 < len(x))
    for a in range(0, len(pos), 8192):
        sel = np.arange(a, min(a + 8192, len(pos)))
        sel = sel[ok[sel]]
        if len(sel) == 0:
            continue
        h = _WARP_TABLE[k[sel]] * (1.0 - t[sel]) + _WARP_TABLE[k[sel] + 1] * t[sel]
        out[sel] = np.sum(x[ip[sel, None] + j[None, :]] * h, axis=1)
    return out


def tone_freqs(fs):
    return np.array([np.round(40.0 * (18000.0 / 40.0) ** (k / 23.0) / 1.5) * 1.5 + 0.37 for k in range(24)])


def fit_tones(seg, freqs, fs, ratio):
    n = np.arange(len(seg))
    cols = []
    for f in freqs:
        w = 2 * np.pi * f * ratio / fs
        cols += [np.sin(w * n), np.cos(w * n)]
    cols.append(np.ones(len(seg)))
    A = np.array(cols).T
    c, *_ = np.linalg.lstsq(A, seg, rcond=None)
    r = seg - A @ c
    amps = np.hypot(c[0:-1:2], c[1:-1:2])
    phases = np.arctan2(c[1:-1:2], c[0:-1:2])
    return amps, phases, float(np.dot(r, r)), float(np.dot(seg - c[-1], seg - c[-1]))


def golden(f, lo, hi, it=40):
    for _ in range(it):
        a, b = hi - (hi - lo) * 0.618, lo + (hi - lo) * 0.618
        if f(a) < f(b): hi = b
        else: lo = a
    return (lo + hi) / 2


class Events:
    """known causes on the cable timeline (output sample index)"""
    def __init__(self, meta, fs, lat_samples, base):
        self.items = []
        for g, n in meta.get('deviceGaps', []):
            self.items.append((g - 0.02 * fs, g + n + 0.06 * fs, 'cable late (test machine stall)'))
        rate = meta['micRate']
        pk = meta.get('micPacket', 480)
        for n in meta.get('lostMicPackets', []):
            m = n * fs / rate + lat_samples - base
            self.items.append((m - 0.03 * fs, m + pk * fs / rate + 0.12 * fs, 'mic packet lost (test machine stall)'))
        for m, what in meta.get('engineEvents', []):
            self.items.append((m - 0.25 * fs, m + 0.05 * fs, 'MicVST: ' + what.split(' ')[0]))
        for m, n in meta.get('injected', []):
            self.items.append((m - 0.02 * fs, m + n + 0.06 * fs, 'injected cable stall (test)'))
        if meta.get('stop', 0) > 0:
            self.items.append((meta['stop'] - 0.02 * fs, float('inf'), 'program stop (planned fade-out)'))
        if meta.get('chainEdit', -1) > 0:
            c = meta['chainEdit']
            self.items.append((c - 0.6 * fs, c + 0.15 * fs, 'chain edit (planned fade)'))

    def cause(self, a, b):
        # the most specific cause: the event whose window centre is nearest
        hits = [(abs((lo + min(hi, lo + fs_max)) / 2 - (a + b) / 2), w) for (lo, hi, w) in self.items if lo <= b and hi >= a]
        return min(hits)[1] if hits else None

    def summary(self):
        kinds = {}
        for _, _, w in self.items:
            kinds[w] = kinds.get(w, 0) + 1
        return ', '.join(f'{k}: {v}' for k, v in kinds.items()) or 'none'


def attribute(events, spans, fs, what):
    by = {}
    unexplained = []
    for a, b in spans:
        c = events.cause(a, b)
        if c is None:
            unexplained.append(a)
        else:
            by[c] = by.get(c, 0) + 1
    parts = [f'{k} {v}' for k, v in by.items()]
    s = f'    {what}: {len(spans)}'
    if spans:
        s += ' (' + (', '.join(parts) + ', ' if parts else '') + f'UNEXPLAINED {len(unexplained)})'
    print(s)
    for a in unexplained[:6]:
        print(f'      unexplained at t={a / fs:7.3f} s (cable timeline)')
    return len(unexplained)


def splice_metrics(meta, xb, yb, lat, t_of, base, fs):
    """Quality of MicVST's own splices (trims, input skips): at the latency jump each causes,
    HF excess = high-frequency energy (6-18 kHz) of the output in 2.5 ms frames vs. the input's
    around the same place (a click raises it by 10-30 dB on tonal sound; a clean crossfade does
    not), and the level over the splice region vs. the input."""
    from scipy.signal import firwin
    evs = [(m, w.split(' ')[0]) for m, w in meta.get('engineEvents', []) if w.startswith(('trim', 'input-skip'))]
    if not evs:
        return
    hp = firwin(255, 6000.0, pass_zero=False, fs=fs)
    jumps = np.nonzero(np.abs(np.diff(lat)) > 1.5 + 6e-4 * np.diff(t_of))[0]
    F = int(0.0025 * fs)
    rows = []
    for m, kind in evs:
        cand = [i for i in jumps if m - 0.4 * fs <= t_of[i + 1] and t_of[i] <= m + 0.1 * fs]
        if not cand:
            rows.append(f'{kind} at {m / fs:.2f} s: no latency jump found'); continue
        i = cand[-1]
        a, b = int(t_of[i] - 0.03 * fs), int(t_of[i + 1] + 0.03 * fs)
        pre, post = lat[i], lat[i + 1]
        yo = np.convolve(yb[a - 300:b + 300, 0], hp, mode='same')[300:-300]
        xi_pre = np.convolve(xb[int(a + base - pre) - 300:int(b + base - pre) + 300, 0], hp, mode='same')[300:-300]
        xi_post = np.convolve(xb[int(a + base - post) - 300:int(b + base - post) + 300, 0], hp, mode='same')[300:-300]
        n = min(len(yo), len(xi_pre), len(xi_post)) // F * F
        eo = np.sum(yo[:n].reshape(-1, F) ** 2, axis=1)
        ei = np.maximum(np.sum(xi_pre[:n].reshape(-1, F) ** 2, axis=1), np.sum(xi_post[:n].reshape(-1, F) ** 2, axis=1))
        ei = np.maximum(np.convolve(ei, np.ones(3), mode='same') / 3, 1e-3 * np.mean(ei) + 1e-20)
        hf = 10 * np.log10(np.max(eo / ei))
        lo_out = np.sqrt(np.mean(yb[a:b, 0] ** 2))
        lo_in = 0.5 * (np.sqrt(np.mean(xb[int(a + base - pre):int(b + base - pre), 0] ** 2)) + np.sqrt(np.mean(xb[int(a + base - post):int(b + base - post), 0] ** 2)))
        rows.append(f'{kind} at {m / fs:.2f} s: skipped {pre - post:+.0f} samples, HF excess {hf:+.1f} dB, level over the splice {20 * np.log10(lo_out / max(lo_in, 1e-12)):+.2f} dB')
    print(f'    MicVST splices ({len(rows)}):')
    for r in rows:
        print('      ' + r)


def multitone(x, y, start, end, fs, events=None):
    freqs = tone_freqs(fs)
    amp_in, _, _, _ = fit_tones(x[int(fs):int(fs) + 4096, 0], freqs, fs, 1.0)
    tdn, gains, balance, phase = [], [], [], []
    at_events = 0
    for m0 in range(start, end - 4096, 4096 * 2):
        segL, segR = y[m0:m0 + 4096, 0], y[m0:m0 + 4096, 1]
        if np.max(np.abs(segL)) < 1e-6:
            continue
        if events is not None and events.cause(m0, m0 + 4096) is not None:
            at_events += 1; continue            # a gap / splice inside: not a distortion measurement
        ratio = golden(lambda r: fit_tones(segL, freqs, fs, r)[2], 1 - 2e-3, 1 + 2e-3)
        aL, pL, resL, totL = fit_tones(segL, freqs, fs, ratio)
        aR, pR, resR, totR = fit_tones(segR, freqs, fs, ratio)
        tdn.append(10 * np.log10(max((resL + resR) / (totL + totR), 1e-30)))
        gains.append(20 * np.log10(aL / amp_in))
        balance.append(20 * np.log10(aL / aR))
        phase.append(np.degrees(np.angle(np.exp(1j * (pL - pR)))))
    tdn, gains, balance, phase = map(np.array, (tdn, gains, balance, phase))
    print(f'    multitone (24 tones 40 Hz-18 kHz, 4096-sample windows, {len(tdn)} windows; {at_events} windows at recorded events left out):')
    print(f'      TD+N (all that is not one of the tones): median {np.median(tdn):.1f} dB, p95 {np.percentile(tdn, 95):.1f} dB, worst {tdn.max():.1f} dB')
    g = np.median(gains, axis=0); b = np.median(balance, axis=0); p = np.median(phase, axis=0)
    print(f'      frequency response (gain per tone, median over windows): {g.min():+.4f} .. {g.max():+.4f} dB '
          f'({freqs[int(np.argmin(g))]:.0f} / {freqs[int(np.argmax(g))]:.0f} Hz)')
    for k in (0, 6, 12, 18, 21, 23):
        print(f'        {freqs[k]:8.1f} Hz: gain {g[k]:+.4f} dB, L-R {b[k]:+.5f} dB, L-R phase {p[k]:+.4f} deg')
    print(f'      L/R balance over all tones: {b.min():+.5f} .. {b.max():+.5f} dB; L/R phase {p.min():+.4f} .. {p.max():+.4f} deg')


def true_peak_db(v, offset=0, events=None):
    """inter-sample peak (4x oversampling); with events: outside their windows (an abrupt gap made
    by a stall rings in the oversampling filter - that is the gap, not the level)"""
    try:
        from scipy.signal import resample_poly
        o = np.abs(resample_poly(v, 4, 1))
        o[:512] = 0; o[-512:] = 0                # the cut ends of the excerpt ring: not counted
        if events is not None:
            for lo, hi, _ in events.items:
                a, b = int(max(0, (lo - offset) * 4)), int(min(len(o), (min(hi, offset + len(v)) - offset) * 4))
                if b > a: o[a:b] = 0
        return 20 * np.log10(max(float(np.max(o)), 1e-12))
    except Exception:
        return float('nan')


def analyse_sine(meta, x, y, start, end, fs, events):
    f0 = 997.0
    amp_in = float(np.max(np.abs(x[:, 0])))
    lv, ppm, thd = [], [], []
    at_events = 0
    for m0 in range(start, end - HOP, HOP):
        seg = y[m0:m0 + HOP, 0]
        if np.max(np.abs(seg)) < 1e-6:
            continue
        if events.cause(m0, m0 + HOP) is not None:
            at_events += 1; continue            # gap / splice inside: counted below as a discontinuity
        fr = [f0]
        r = golden(lambda q: fit_tones(seg, fr, fs, q)[2], 1 - 3e-3, 1 + 3e-3)
        a, _, res, tot = fit_tones(seg, fr, fs, r)
        lv.append(20 * np.log10(a[0] / amp_in)); ppm.append((r - 1) * 1e6); thd.append(10 * np.log10(max(res / tot, 1e-30)))
    lv, ppm, thd = map(np.array, (lv, ppm, thd))
    w = 2 * np.pi * f0 / fs
    lim = 3.0 * amp_in * w * w + 2e-4
    d2 = np.abs(y[start + 1:end - 1, 0] - 2 * y[start:end - 2, 0] + y[start - 1:end - 3, 0])
    idx = np.nonzero(d2 > lim)[0] + start
    spans = []
    for i in idx:
        if not spans or i - spans[-1][1] > 64: spans.append([i, i])
        else: spans[-1][1] = i
    med = np.median(lv)
    print(f'    recorded events: {events.summary()}')
    print(f'    sine fit per 1024-sample window ({len(lv)} windows; {at_events} at recorded events left out): level {med:+.4f} dB (p1..p99 {np.percentile(lv - med, 1):+.4f}..{np.percentile(lv - med, 99):+.4f}), '
          f'windows off by > 0.5 dB: {int(np.sum(np.abs(lv - med) > 0.5))}')
    print(f'    speed (fitted frequency): median {np.median(ppm):+.1f} ppm, max |.| {np.max(np.abs(ppm)):.0f} ppm; '
          f'THD+N median {np.median(thd):.1f} dB, p95 {np.percentile(thd, 95):.1f} dB, worst {thd.max():.1f} dB')
    attribute(events, [(a, b) for a, b in spans], fs, 'discontinuities (second difference above limit)')


def analyse(name):
    meta, x, y = load(name)
    fs = meta['cableRate']
    print(f"=== {name}: signal {meta['signal']} {meta['level']} dBFS, chain {meta['chain']}, mic channels {meta['micChannels']}, "
          f"buffer {meta['buffer']}, {meta['seconds']} s, mic {meta['micRate']:.0f} Hz -> cable {fs:.0f} Hz")
    print(f"    engine: {meta['diag']}")
    micStart = meta['micStart']
    if meta['micRate'] != fs:
        from scipy.signal import resample_poly
        from fractions import Fraction
        q = Fraction(int(fs), int(meta['micRate']))
        x = np.stack([resample_poly(x[:, c], q.numerator, q.denominator) for c in (0, 1)], axis=1)
    if meta['micChannels'] == 1:
        x[:, 1] = x[:, 0]                      # a mono microphone feeds both cable channels
    base = (meta['outputStart'] - micStart) * fs   # output index m <-> input index n: latency = m + base - n
    nz = np.nonzero(np.abs(y[:, 0]) > 1e-6)[0]
    start = (int(nz[0]) if len(nz) else 0) + int(0.3 * fs)
    if meta.get('reference'):
        # compared with the offline rendering of the plug-ins: from 1 s after the chain was
        # switched in (their envelopes started there in the live run, at sample 0 offline)
        start = max(start, int(meta.get('chainEdit', 0) + 1.0 * fs))
        print(f"    compared with: the offline rendering of the same {meta.get('chain')} plug-ins (same state, same blocks); "
              f"plug-in latency reported to the graph {meta.get('chainLatencySamples', 0)} samples")
    end = len(y) - int(0.3 * fs)               # the shutdown fade-out is not analysed
    peak_in = float(np.max(np.abs(x)))
    peak_out = float(np.max(np.abs(y[start:end]))) if end > start else 0.0
    ev0 = Events(meta, fs, 0.045 * fs, base)   # for the true peak only (exact latency not needed)
    print(f'    peak in {20*np.log10(max(peak_in,1e-12)):.2f} dBFS (true peak {true_peak_db(x[:, 0]):.2f}), '
          f'peak out {20*np.log10(max(peak_out,1e-12)):.2f} dBFS (true peak outside recorded events {true_peak_db(y[start:end, 0], start, ev0):.2f}), '
          f'samples |out| >= 1.0: {int(np.sum(np.abs(y[start:end]) >= 1.0))}, non-finite: {int(np.sum(~np.isfinite(y)))}')
    if peak_in == 0.0:
        print(f'    silence in: max |out| over the whole run = {float(np.max(np.abs(y))):.3g}')
        return
    if meta['signal'] == 'sine':
        lat0 = 0.0448 * fs
        analyse_sine(meta, x, y, start, end, fs, Events(meta, fs, lat0, base))
        if np.allclose(x[:, 0], x[:, 1]):
            print(f'    L == R exactly: {bool(np.array_equal(y[start:end, 0], y[start:end, 1]))}')
        return

    # --- location
    ref_rms = np.sqrt(np.mean(y[start:end, 0] ** 2))
    t_of, lat, quiet, unmatched, ambiguous, long_used, resolved = [], [], 0, 0, 0, 0, 0

    def locate(a, n):
        lo, hi = int(a + base - 0.45 * fs), int(a + base) + n
        if lo < 64 or hi >= len(x) or a < 0 or a + n > len(y):
            return None
        k, r, r2 = peak(xcorr(x[lo:hi, 0], y[a:a + n, 0]))
        return a + base - (lo + k), r, r2

    pending = []   # ambiguous windows: (m0, all candidate latencies with rho >= 0.9)
    for m0 in range(start, end - HOP, HOP):
        yw = y[m0:m0 + HOP, 0]
        if np.sqrt(np.mean(yw * yw)) < 0.003 * ref_rms:   # -50 dB re the whole signal: a pause
            quiet += 1; continue
        res = locate(m0, HOP)
        if res is None:
            continue
        if res[1] < 0.5 or res[2] > 0.98 * res[1]:
            # periodic sound (sustained tones, voiced speech): a 4096-sample window around it
            res2 = locate(m0 + HOP // 2 - 2048, 4096)
            if res2 is not None and res2[1] >= 0.9 and res2[2] <= 0.98 * res2[1]:
                res = res2; long_used += 1
            elif res[1] < 0.5:
                unmatched += 1; continue
            else:
                pending.append(m0); continue
        lat.append(res[0]); t_of.append(m0 + HOP // 2)
    # ambiguous windows: the candidate (rho >= 0.95) within 64 samples of both located neighbours'
    # latency - continuity decides between the periods of a sustained sound; none -> not located
    t_arr, l_arr = np.array(t_of), np.array(lat)
    for m0 in pending:
        tc = m0 + HOP // 2
        j = np.searchsorted(t_arr, tc)
        if j == 0 or j >= len(t_arr) or abs(l_arr[j - 1] - l_arr[j]) > 64:
            ambiguous += 1; continue
        guess = 0.5 * (l_arr[j - 1] + l_arr[j])
        lo, hi = int(m0 + base - guess) - 80, int(m0 + base - guess) + 80 + HOP
        if lo < 64 or hi >= len(x):
            ambiguous += 1; continue
        rho = xcorr(x[lo:hi, 0], y[m0:m0 + HOP, 0])
        k, r, _ = peak(rho)
        cand = m0 + base - (lo + k)
        if r >= 0.95 and abs(cand - guess) <= 64:
            lat.append(cand); t_of.append(tc); resolved += 1
        else:
            ambiguous += 1
    order = np.argsort(t_of)
    lat, t_of = np.array(lat)[order], np.array(t_of)[order]
    if len(lat) < 8:
        print('    too few audio windows'); return
    # isolated mislocations (imprecise peaks of tonal sound) are not discontinuities: a real skip or
    # repeat shifts the latency for good -> running median over 5 located windows
    raw = lat
    pad = np.concatenate((np.full(2, raw[0]), raw, np.full(2, raw[-1])))
    lat = np.median(np.lib.stride_tricks.sliding_window_view(pad, 5), axis=1)
    outliers = int(np.sum(np.abs(raw - lat) > 1.5))
    med_lat = float(np.median(lat))
    events = Events(meta, fs, med_lat, base)
    print(f'    recorded events: {events.summary()}')
    print(f'    {len(lat)} of {len(lat) + quiet + unmatched + ambiguous} 1024-sample windows located ({long_used} with a 4096 window, {resolved} periodic ones by continuity); quiet (< -50 dB re the signal) {quiet}, '
          f'not matching the input (rho < 0.5) {unmatched}, ambiguous (periodic) {ambiguous}, single outliers (> 1.5 samples off the running median) {outliers}')
    latms = lat / fs * 1000.0
    print(f'    latency capture -> playback: median {np.median(latms):.2f} ms, p1..p99 {np.percentile(latms,1):.2f}..{np.percentile(latms,99):.2f}, '
          f'min {latms.min():.2f}, max {latms.max():.2f} ms (shown by MicVST: {meta["shownLatencyMs"]:.1f} ms)')
    d = np.diff(lat)
    # the controller changes the speed by up to 500 ppm: allowed drift between two located windows
    jumps = np.nonzero(np.abs(d) > 1.5 + 6e-4 * np.diff(t_of))[0]
    spans = [(t_of[i], t_of[i + 1]) for i in jumps]
    print(f'    continuity: latency drops (audio skipped/lost) {int(np.sum(d[jumps] < 0))} ({-np.sum(d[jumps][d[jumps] < 0]):.0f} samples), '
          f'rises (silence inserted / audio repeated) {int(np.sum(d[jumps] > 0))} ({np.sum(d[jumps][d[jumps] > 0]):.0f} samples)')
    unexplained = attribute(events, spans, fs, 'latency jumps')

    # speed from the latency slope in 1 s pieces between jumps
    speeds = []
    seg_start = 0
    for i in range(1, len(lat) + 1):
        if i == len(lat) or abs(lat[i] - lat[i - 1]) > 1.5 + 6e-4 * (t_of[i] - t_of[i - 1]) or t_of[i] - t_of[i - 1] > 4 * HOP:
            tt, ll = t_of[seg_start:i] / fs, lat[seg_start:i]
            per = int(fs // HOP)
            for j in range(0, len(tt) - per + 1, per):
                speeds.append(-np.polyfit(tt[j:j + per], ll[j:j + per], 1)[0] / fs * 1e6)
            seg_start = i
    speeds = np.array(speeds)
    if len(speeds):
        print(f'    playback speed vs. capture (1 s pieces): median {np.median(speeds):+.1f} ppm, p95 |.| {np.percentile(np.abs(speeds),95):.0f}, '
              f'max |.| {np.max(np.abs(speeds)):.0f} ppm ({np.max(np.abs(speeds)) * 1e-6 * 1731.2:.2f} cent)')

    # --- input on the output's time base, piece by piece between jumps. Both signals are first
    # band-limited to 18 kHz with the same linear-phase filter: MicVST's resampler rolls off above
    # ~20 kHz (by design), which is not a discontinuity.
    from scipy.signal import firwin
    lp = firwin(255, 18000.0, fs=fs)
    xb = np.stack([np.convolve(x[:, c], lp, mode='same') for c in (0, 1)], axis=1)
    yb = np.stack([np.convolve(y[:, c], lp, mode='same') for c in (0, 1)], axis=1)
    # sub-sample latency of every located window: the offset (within +-1 sample of the estimate)
    # at which the interpolated input matches the window best (golden section). The correlation
    # peak alone is a few hundredths of a sample off on broadband sound - visible at 18 kHz.
    fine = lat.copy()
    for i, (tc, l0) in enumerate(zip(t_of, lat)):
        mw = np.arange(tc - HOP // 2, tc + HOP // 2)
        yw = yb[mw, 0]
        def cost(dl):
            xw = warp(xb[:, 0], mw + base - (l0 + dl))
            return -np.dot(xw, yw) / np.sqrt(max(np.dot(xw, xw) * np.dot(yw, yw), 1e-30))
        fine[i] = l0 + golden(cost, -1.0, 1.0, 14)
    # The latency itself changes smoothly between jumps (speed control: at most 500 ppm, smoothed
    # over seconds); single windows of tonal sound are less precise. Robust local line through
    # +-5 located windows of the same segment (biweight, 3 iterations).
    seg_id = np.concatenate(([0], np.cumsum(np.abs(np.diff(fine)) > 1.5 + 6e-4 * np.diff(t_of))))
    smooth = fine.copy()
    for i in range(len(fine)):
        idx = [k for k in range(max(0, i - 5), min(len(fine), i + 6)) if seg_id[k] == seg_id[i]]
        if len(idx) < 3:
            continue
        tt = (t_of[idx] - t_of[i]).astype(np.float64); vv = fine[idx]; w = np.ones(len(idx))
        for _ in range(3):
            W = np.sum(w); tm = np.sum(w * tt) / W; vm = np.sum(w * vv) / W
            b = np.sum(w * (tt - tm) * (vv - vm)) / max(np.sum(w * (tt - tm) ** 2), 1e-12)
            a = vm - b * tm
            r = vv - (a + b * tt)
            sc = max(6.0 * np.median(np.abs(r)), 0.05)
            w = np.where(np.abs(r) < sc, (1 - (r / sc) ** 2) ** 2, 0.0) + 1e-9
        smooth[i] = a
    lat = smooth
    m = np.arange(start, end)
    L = np.interp(m, t_of, lat)
    near_jump = np.zeros(len(m), dtype=bool)
    for i in jumps:
        near_jump[max(0, t_of[i] - start - HOP): min(len(m), t_of[i + 1] - start + HOP)] = True
    covered = (m >= t_of[0]) & (m <= t_of[-1])   # between located windows only (no extrapolation of a drifting latency)
    # ... and not across a long stretch without a located window (a stall, a splice in periodic
    # sound): the latency there is unknown, those pieces are not judged
    idx = np.searchsorted(t_of, m)
    dist = np.minimum(np.abs(m - t_of[np.clip(idx - 1, 0, len(t_of) - 1)]), np.abs(t_of[np.clip(idx, 0, len(t_of) - 1)] - m))
    covered &= dist <= 2 * HOP
    print(f'    not judged (no located window within 2048 samples): {100.0 * (1.0 - covered.mean()):.1f} % of the analysed time')
    level, level_t, pieces = [], [], []
    # every piece is compared at 9 offsets within +-0.12 samples of the measured latency curve (the
    # best one counts): an abrupt full-band onset (a hi-hat) needs a few hundredths of a sample,
    # more than a window's latency estimate guarantees; no click is explained by such a shift.
    deltas = np.arange(-0.12, 0.1201, 0.03)
    for c in (0, 1):
        xws = [warp(xb[:, c], m + base - L - dl) for dl in deltas]
        xw = xws[len(deltas) // 2]
        for p0 in range(0, len(m) - PIECE, PIECE // 2):   # half-overlapping: every sample lies inside a fit
            sl = slice(p0, p0 + PIECE)
            if near_jump[sl].any() or not covered[sl].all():
                continue
            ys = yb[start + p0:start + p0 + PIECE, c]
            rms = np.sqrt(np.mean(ys * ys))
            best = None
            for xwd in xws:
                xs = xwd[sl]
                g = float(np.dot(xs, ys)) / max(float(np.dot(xs, xs)), float(np.dot(ys, ys)), 1e-30)   # gain (<= 1 where the input is silent)
                r = float(np.max(np.abs(ys - g * xs)))
                best = r if best is None else min(best, r)
            pieces.append((start + p0, best, rms, c, p0))
        if c == 0:
            for p0 in range(0, len(m) - HOP, HOP):
                sl = slice(p0, p0 + HOP)
                if near_jump[sl].any() or not covered[sl].all():
                    continue
                ey, ex = np.dot(yb[start + p0:start + p0 + HOP, 0], yb[start + p0:start + p0 + HOP, 0]), np.dot(xw[sl], xw[sl])
                if ex > HOP * (0.003 * ref_rms) ** 2:   # same quiet limit
                    level.append(10 * np.log10(max(ey, 1e-30) / ex)); level_t.append(start + p0)
    # Threshold per run: 10 x the 99th percentile of the residual/RMS ratio of all pieces (the
    # method's own floor for this signal), at least 2 %, at most 20 % of the piece RMS, and above
    # -60 dB re the whole signal. Pauses (pieces quieter than -50 dB re the signal): the plug-ins'
    # float noise floor is not judged against the piece, only residuals above -50 dB re the signal.
    loud = [p[1] / p[2] for p in pieces if p[2] >= 0.003 * ref_rms]
    floor99 = float(np.percentile(loud, 99)) if loud else 0.0
    rel = float(np.clip(10.0 * floor99, 0.02, 0.2))
    def limit_of(rms):
        return max(rel * rms, 0.001 * ref_rms) if rms >= 0.003 * ref_rms else 0.003 * ref_rms
    click_spans = []
    for (a, r, rms, c, p0) in pieces:
        if r <= limit_of(rms):
            continue
        # second look at a candidate: offsets within +-0.6 samples (a sharp onset in the input itself,
        # e.g. a chord starting at full level, needs the alignment closer than the curve gives)
        ys = yb[a:a + PIECE, c]
        mm = m[p0:p0 + PIECE]
        for dl in np.arange(-0.6, 0.601, 0.05):
            xs = warp(xb[:, c], mm + base - L[p0:p0 + PIECE] - dl)
            g = float(np.dot(xs, ys)) / max(float(np.dot(xs, xs)), float(np.dot(ys, ys)), 1e-30)
            r = min(r, float(np.max(np.abs(ys - g * xs))))
        if r > limit_of(rms):
            click_spans.append((a, a + PIECE))
    merged = []
    for a, b in sorted(click_spans):
        if merged and a <= merged[-1][1]: merged[-1][1] = max(merged[-1][1], b)
        else: merged.append([a, b])
    if loud:
        print(f'    detector floor (residual peak / piece RMS): median {np.median(loud):.4f}, p99 {floor99:.4f}, p99.9 {np.percentile(loud, 99.9):.4f} -> threshold {100 * rel:.1f} % of the piece RMS')
    unexplained += attribute(events, merged, fs, f'clicks / discontinuities (residual > {100 * rel:.1f} % of the piece RMS and > -60 dB re the signal; in pauses > -50 dB re the signal; 256-sample pieces, both channels)')
    level, level_t = np.array(level), np.array(level_t)
    if len(level):
        lv = level - np.median(level)
        print(f'    level out/in per 1024 window: median {np.median(level):+.4f} dB, p1..p99 {np.percentile(lv,1):+.3f}..{np.percentile(lv,99):+.3f} dB')
        off = [(t, t + HOP) for t, v in zip(level_t, lv) if abs(v) > 0.5]
        unexplained += attribute(events, off, fs, 'windows with the level off by > 0.5 dB')
    splice_metrics(meta, xb, yb, lat, t_of, base, fs)
    print(f'    => unexplained findings: {unexplained}')
    if np.allclose(x[:, 0], x[:, 1]) and meta['signal'] != 'multitone':
        g = 10 * np.log10(np.sum(y[start:end, 0] ** 2) / max(np.sum(y[start:end, 1] ** 2), 1e-30))
        print(f'    L/R level difference {g:+.5f} dB, L == R exactly: {bool(np.array_equal(y[start:end, 0], y[start:end, 1]))}')
    if meta['signal'] == 'multitone':
        multitone(x, y, start, end, fs, events)


if __name__ == '__main__':
    for n in sys.argv[1:]:
        analyse(n)
