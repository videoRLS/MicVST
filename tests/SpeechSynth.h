#pragma once
// Deterministic synthetic speech for the Auto Gain tests (tests/AutoGainTests.cpp) and the engine
// test: phrases of words, words of syllables; every syllable an optional consonant onset (a
// plosive - closure + short burst, the "p" pop - or a fricative) and a vowel (glottal pulses with
// a gliding f0 through three formants), stressed and unstressed syllables, phrase declination,
// short gaps between words, pauses with breaths between phrases, optional room noise.
//
// Level calibration uses the same measure as the stage (AutoGain.h): the "sustained peak" of every
// 10 ms window (the highest level present for at least 1 ms in total); the signal is scaled so that
// the level reached by the loudest 20 % of its voiced windows ("typical level") is peakDb.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <utility>
#include <vector>

namespace speech
{
    struct Params
    {
        double fs = 48000.0;
        double peakDb = -20.0;         // typical level (loudest 20 % of the voiced 10 ms windows)
        double noiseDb = -200.0;       // room noise RMS in dBFS; <= -150: digital silence between words
        unsigned seed = 1;
        bool   female = false;
        double declinationDb = 4.0;    // level fall from the start to the end of a phrase
        double plosiveDb = 3.0;        // plosive burst peak relative to its vowel's peak
        double softDb = 5.0;           // typical extra softness of unstressed syllables
        double phrasePauseScale = 1.0; // 0 = no pauses between phrases (continuous talk)
        double popDb = -200.0;         // breath "pop" on plosives (low thump, 15-30 ms) relative to the vowel; -200 = none
    };

    struct Syllable { size_t start = 0, vowelStart = 0, end = 0; double relDb = 0.0; bool stressed = false; };

    struct Result
    {
        std::vector<float> x;
        std::vector<Syllable> syllables;
        std::vector<std::pair<size_t, size_t>> phrases;   // [start, end) of every phrase
        double scale = 1.0;                               // applied to the raw synthesis
    };

    inline double dbToLin (double db) { return std::pow (10.0, db / 20.0); }

    // Band limit as a microphone's A/D converter does (nothing above 20 kHz, or 0.45 x the rate):
    // zero-phase 95-tap Kaiser-windowed sinc low-pass, in place.
    inline void bandLimit (std::vector<double>& x, double fs)
    {
        const int half = 47;
        const double fc = std::min (20000.0, 0.45 * fs) / fs, beta = 8.0;
        auto i0 = [] (double v) { double sum = 1.0, t = 1.0; for (int k = 1; k < 30; ++k) { t *= (v / (2.0 * k)) * (v / (2.0 * k)); sum += t; } return sum; };
        std::vector<double> h ((size_t) (2 * half + 1));
        double norm = 0.0;
        for (int k = -half; k <= half; ++k)
        {
            const double sinc = k == 0 ? 2.0 * fc : std::sin (6.283185307179586 * fc * k) / (3.141592653589793 * k);
            const double r = (double) k / (half + 1.0);
            h[(size_t) (k + half)] = sinc * i0 (beta * std::sqrt (1.0 - r * r)) / i0 (beta);
            norm += h[(size_t) (k + half)];
        }
        for (auto& v : h) v /= norm;
        std::vector<double> y (x.size(), 0.0);
        for (size_t n = 0; n < x.size(); ++n)
        {
            double acc = 0.0;
            for (int k = -half; k <= half; ++k)
            {
                const long m = (long) n - k;
                if (m >= 0 && m < (long) x.size()) acc += h[(size_t) (k + half)] * x[(size_t) m];
            }
            y[n] = acc;
        }
        x.swap (y);
    }

    // Sustained peak (dB) and RMS (dB) of every complete 10 ms window - the stage's measure.
    struct Window { double sustainedDb, rmsDb; size_t start; };
    inline std::vector<Window> windows (const float* y, size_t n, double fs)
    {
        const auto len = (size_t) std::lround (0.01 * fs), need = (size_t) std::max (1L, std::lround (0.001 * fs));
        std::vector<Window> w;
        std::vector<float> mag (len);
        for (size_t p = 0; p + len <= n; p += len)
        {
            double ss = 0.0;
            for (size_t i = 0; i < len; ++i) { mag[i] = std::abs (y[p + i]); ss += (double) y[p + i] * y[p + i]; }
            std::nth_element (mag.begin(), mag.begin() + (long) (need - 1), mag.end(), std::greater<float>());
            const double sp = mag[need - 1];
            w.push_back ({ sp > 1.0e-6 ? 20.0 * std::log10 (sp) : -120.0, ss > 0.0 ? 10.0 * std::log10 (ss / (double) len) : -200.0, p });
        }
        return w;
    }

    // Level reached by the loudest `share` of the windows whose RMS is within 40 dB of the loudest
    // window (the voiced part of clean speech).
    inline double typicalLevel (const float* y, size_t n, double fs, double share = 0.2)
    {
        auto w = windows (y, n, fs);
        double maxRms = -200.0;
        for (auto& v : w) maxRms = std::max (maxRms, v.rmsDb);
        std::vector<double> lv;
        for (auto& v : w) if (v.rmsDb > maxRms - 40.0 && v.rmsDb > -90.0) lv.push_back (v.sustainedDb);
        if (lv.empty()) return -120.0;
        std::sort (lv.begin(), lv.end(), std::greater<double>());
        const auto k = (size_t) std::max (0.0, std::ceil ((double) lv.size() * share) - 1.0);
        return lv[std::min (k, lv.size() - 1)];
    }

    inline Result generate (double seconds, const Params& p)
    {
        Result r;
        const double fs = p.fs;
        const auto total = (size_t) (seconds * fs);
        r.x.assign (total, 0.0f);
        std::mt19937 rng (p.seed);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        std::normal_distribution<double> g (0.0, 1.0);
        auto uni = [&] (double a, double b) { return a + (b - a) * u (rng); };
        const double twoPi = 6.283185307179586;

        // vowels: formant frequencies (Hz) - a, e, i, o, u, ə
        const double formants[6][3] = { { 730, 1090, 2440 }, { 530, 1840, 2480 }, { 300, 2290, 3010 },
                                        { 570, 840, 2410 }, { 320, 870, 2240 }, { 500, 1500, 2500 } };
        std::vector<double> raw (total, 0.0);
        size_t pos = (size_t) (0.05 * fs);
        double phase = 0.0;

        while (pos < total)
        {
            const size_t phraseStart = pos;
            const int words = 3 + (int) (u (rng) * 6.0);                 // 3..8 words
            const int emphasised = (int) (u (rng) * words);
            // phrase length estimate for the declination
            const double phraseSeconds = words * 0.45;
            for (int w = 0; w < words && pos < total; ++w)
            {
                const int sylls = 1 + (int) (u (rng) * 3.0);             // 1..3 syllables
                const int stressAt = (int) (u (rng) * sylls);
                for (int s = 0; s < sylls && pos < total; ++s)
                {
                    Syllable syl;
                    syl.start = pos;
                    syl.stressed = (s == stressAt);
                    const double t = (double) (pos - phraseStart) / fs;
                    double rel = syl.stressed ? -uni (0.0, 1.5) : -uni (p.softDb - 2.0, p.softDb + 2.0);
                    if (w == emphasised && syl.stressed) rel += 2.0;
                    rel -= p.declinationDb * std::min (1.0, t / phraseSeconds);
                    syl.relDb = rel;
                    const double amp = dbToLin (rel);

                    // onset consonant
                    const double kind = u (rng);
                    if (kind < 0.25)        // plosive: closure, then a short decaying burst
                    {
                        pos += (size_t) (uni (0.025, 0.05) * fs);
                        const double burstAmp = amp * dbToLin (p.plosiveDb) * 1.6;   // ~ the vowel's raw peak scale
                        const auto blen = (size_t) (uni (0.006, 0.012) * fs);
                        for (size_t i = 0; i < blen && pos + i < total; ++i)
                            raw[pos + i] += burstAmp * std::exp (-(double) i / (0.0025 * fs)) * g (rng) * 0.6;
                        if (p.popDb > -150.0)   // breath on the capsule: a damped low-frequency thump
                        {
                            const double f = uni (45.0, 90.0), tau = uni (0.006, 0.012) * fs;
                            const auto plen = (size_t) (uni (0.015, 0.03) * fs);
                            for (size_t i = 0; i < plen && pos + i < total; ++i)
                                raw[pos + i] += amp * dbToLin (p.popDb) * 1.6 * std::sin (twoPi * f * (double) i / fs) * std::exp (-(double) i / tau);
                        }
                        pos += blen + (size_t) (uni (0.01, 0.03) * fs);   // aspiration gap
                    }
                    else if (kind < 0.5)    // fricative: high-passed noise
                    {
                        const auto flen = (size_t) (uni (0.05, 0.11) * fs);
                        double hp = 0.0, prev = 0.0;
                        for (size_t i = 0; i < flen && pos + i < total; ++i)
                        {
                            const double nz = g (rng);
                            hp = 0.6 * (hp + nz - prev); prev = nz;
                            const double e = std::sin (3.141592653589793 * (double) i / (double) flen);
                            raw[pos + i] += amp * dbToLin (-14.0) * 1.6 * e * hp * 0.5;
                        }
                        pos += flen;
                    }

                    // vowel
                    syl.vowelStart = pos;
                    const auto vlen = (size_t) (uni (0.10, 0.22) * fs);
                    const auto& F = formants[(size_t) (u (rng) * 6.0) % 6];
                    const double f0base = (p.female ? 205.0 : 118.0) * (1.12 - 0.24 * std::min (1.0, t / phraseSeconds)) * (syl.stressed ? 1.08 : 1.0);
                    double y1[3][2] = {}, tilt = 0.0;
                    const auto attack = (size_t) (0.012 * fs), release = (size_t) (0.035 * fs);
                    for (size_t i = 0; i < vlen + release && pos + i < total; ++i)
                    {
                        const double f0 = f0base * (1.0 + 0.03 * std::sin (twoPi * 5.0 * (double) i / fs));
                        phase += f0 / fs;
                        double src = 0.0;
                        if (phase >= 1.0) { phase -= 1.0; src = 1.0; }
                        tilt += (src - tilt) * 0.25;   // glottal spectral tilt
                        double v = 0.0;
                        for (int k = 0; k < 3; ++k)
                        {
                            const double bw = 70.0 + 30.0 * k;
                            const double rr = std::exp (-3.141592653589793 * bw / fs), wk = twoPi * F[k] / fs;
                            const double yk = tilt * (1.0 - rr) * 4.0 + 2.0 * rr * std::cos (wk) * y1[k][0] - rr * rr * y1[k][1];
                            y1[k][1] = y1[k][0]; y1[k][0] = yk;
                            v += yk / (1.0 + k);
                        }
                        double e = 1.0;
                        if (i < attack) e = 0.5 - 0.5 * std::cos (3.141592653589793 * (double) i / (double) attack);
                        else if (i >= vlen) e = 0.5 + 0.5 * std::cos (3.141592653589793 * (double) (i - vlen) / (double) release);
                        raw[pos + i] += amp * e * v;
                    }
                    pos += vlen + release;
                    syl.end = std::min (pos, total);
                    if (syl.end > syl.vowelStart) r.syllables.push_back (syl);
                    pos += (size_t) (uni (0.005, 0.03) * fs);   // transition inside the word
                }
                pos += (size_t) (uni (0.03, 0.12) * fs);        // gap between words
            }
            r.phrases.push_back ({ phraseStart, std::min (pos, total) });

            // pause between phrases, sometimes with a breath
            const auto pause = (size_t) (uni (0.35, 0.9) * p.phrasePauseScale * fs);
            if (p.phrasePauseScale > 0.0 && u (rng) < 0.5)
            {
                const auto b0 = pos + pause / 4, blen = (size_t) (0.25 * fs);
                double lp = 0.0;
                for (size_t i = 0; i < blen && b0 + i < total; ++i)
                {
                    lp += (g (rng) - lp) * 0.3;
                    raw[b0 + i] += dbToLin (-36.0) * 1.6 * std::sin (3.141592653589793 * (double) i / (double) blen) * lp;
                }
            }
            pos += pause;
        }

        bandLimit (raw, fs);   // as recorded through a real microphone / A-D converter

        // calibrate: typical level = peakDb
        std::vector<float> tmp (total);
        for (size_t i = 0; i < total; ++i) tmp[i] = (float) raw[i];
        const double typ = typicalLevel (tmp.data(), total, fs);
        r.scale = typ > -119.0 ? dbToLin (p.peakDb - typ) : 1.0;
        const double noise = p.noiseDb > -150.0 ? dbToLin (p.noiseDb) : 0.0;
        for (size_t i = 0; i < total; ++i)
            r.x[i] = (float) (raw[i] * r.scale + (noise > 0.0 ? noise * g (rng) : 0.0));
        return r;
    }
}
