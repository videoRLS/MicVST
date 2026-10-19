// Tests for the built-in "Otomatik Kazanç" (Auto Gain, src/audio/AutoGain.h) against its
// specification: typical speech at about -20 dBFS and shouting at about -15 dBFS, gain 0 ... +6 dB,
// last gain kept in silence (no gate), output ceiling -10 dBFS with 3 ms look-ahead, plosives and
// clicks not moving the gain, no clicks / distortion, latency, switching off, independence of block
// size and sample rate, broken input, and a long soak (MICVST_SOAK_HOURS, default 0.25 h of audio).
// Most tests use realistic synthetic speech (tests/SpeechSynth.h).
// (Test names are ASCII: the runner prints them as Latin-1.)
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/AutoGain.h"
#include "audio/BuiltInAutoGain.h"
#include "SpeechSynth.h"
#include <chrono>
#include <cstdlib>
#include <random>
#include <vector>

namespace
{
    constexpr double fs = 48000.0;
    const double twoPi = juce::MathConstants<double>::twoPi;

    double dbToLin (double db) { return std::pow (10.0, db / 20.0); }
    double linToDb (double v)  { return 20.0 * std::log10 (std::max (1.0e-12, v)); }
    juce::String d2 (double v) { return juce::String (v, 2); }

    speech::Result talk (double seconds, double typicalDb, unsigned seed, double noiseDb = -65.0, double plosiveDb = -30.0,
                         double popDb = -200.0, double rate = fs)
    {
        speech::Params p;
        p.fs = rate; p.peakDb = typicalDb; p.seed = seed; p.noiseDb = noiseDb; p.plosiveDb = plosiveDb; p.popDb = popDb;
        return speech::generate (seconds, p);
    }

    // Runs the stage over x in place with the given block size (<= 0: random 1 ... 2048); returns
    // the analysis gain after every block and (optionally) the block ends.
    std::vector<double> run (AutoGainStage& s, std::vector<float>& x, int block = 480, std::vector<size_t>* ends = nullptr, unsigned seed = 7)
    {
        std::vector<double> gains;
        std::mt19937 rng (seed);
        std::uniform_int_distribution<int> any (1, 2048);
        for (size_t p = 0; p < x.size();)
        {
            const int n = (int) std::min<size_t> ((size_t) (block > 0 ? block : any (rng)), x.size() - p);
            float* ch[] = { x.data() + p };
            s.process (ch, 1, n);
            p += (size_t) n;
            gains.push_back (s.getTargetGainDb());
            if (ends != nullptr) ends->push_back (p);
        }
        return gains;
    }

    double peakDbOf (const std::vector<float>& y, double fromSec, double toSec, double rate = fs)
    {
        float m = 0.0f;
        for (size_t i = (size_t) (fromSec * rate); i < std::min ((size_t) (toSec * rate), y.size()); ++i) m = std::max (m, std::abs (y[i]));
        return linToDb (m);
    }

    double typicalOf (const std::vector<float>& y, double fromSec, double toSec, double rate = fs)
    {
        const auto a = std::min (y.size(), (size_t) (fromSec * rate)), b = std::min (y.size(), (size_t) (toSec * rate));
        return speech::typicalLevel (y.data() + a, b - a, rate);
    }

    double mean (const std::vector<double>& v, size_t from = 0, size_t to = 0)
    {
        to = to == 0 ? v.size() : std::min (to, v.size());
        double s = 0.0;
        for (size_t i = from; i < to; ++i) s += v[i];
        return to > from ? s / (double) (to - from) : 0.0;
    }

    double percentile (std::vector<double> v, double q)
    {
        if (v.empty()) return 0.0;
        std::sort (v.begin(), v.end());
        return v[(size_t) std::lround (q * (double) (v.size() - 1))];
    }

    // THD+N of a sine of known frequency: least-squares fit of sin/cos/DC, residual RMS against the
    // fitted sine's RMS (dB).
    double thdnDb (const float* y, size_t n, double freq, double rate = fs)
    {
        double ss = 0, sc = 0, cc = 0, s1 = 0, c1 = 0, ys = 0, yc = 0, y1 = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = twoPi * freq * (double) i / rate, s = std::sin (w), c = std::cos (w);
            ss += s * s; sc += s * c; cc += c * c; s1 += s; c1 += c; ys += y[i] * s; yc += y[i] * c; y1 += y[i];
        }
        // solve [ss sc s1; sc cc c1; s1 c1 n] [a b d] = [ys yc y1]
        const double m[3][4] = { { ss, sc, s1, ys }, { sc, cc, c1, yc }, { s1, c1, (double) n, y1 } };
        double a[3][4];
        for (int r = 0; r < 3; ++r) for (int c = 0; c < 4; ++c) a[r][c] = m[r][c];
        for (int k = 0; k < 3; ++k)
            for (int r = k + 1; r < 3; ++r)
            {
                const double f = a[r][k] / a[k][k];
                for (int c = k; c < 4; ++c) a[r][c] -= f * a[k][c];
            }
        double x[3];
        for (int k = 2; k >= 0; --k) { double v = a[k][3]; for (int c = k + 1; c < 3; ++c) v -= a[k][c] * x[c]; x[k] = v / a[k][k]; }
        double res = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const double w = twoPi * freq * (double) i / rate;
            const double e = y[i] - (x[0] * std::sin (w) + x[1] * std::cos (w) + x[2]);
            res += e * e;
        }
        const double sigRms = std::sqrt ((x[0] * x[0] + x[1] * x[1]) / 2.0), resRms = std::sqrt (res / (double) n);
        return linToDb (resRms / sigRms);
    }

    // Largest change of the per-sample gain (output / delayed input, dB) between two samples where
    // the input is not tiny - a click or a gain step shows up here.
    double largestGainStepDb (const std::vector<float>& in, const std::vector<float>& out, size_t L, double minIn = 1.0e-3)
    {
        double worst = 0.0, prev = 0.0;
        bool havePrev = false;
        for (size_t i = L; i < out.size(); ++i)
        {
            const float a = in[i - L];
            if (std::abs (a) < minIn) { havePrev = false; continue; }
            const double g = linToDb (std::abs ((double) out[i] / a));
            if (havePrev) worst = std::max (worst, std::abs (g - prev));
            prev = g; havePrev = true;
        }
        return worst;
    }

    // True peak (dBFS): the highest value of the band-limited signal between the samples (8x,
    // windowed sinc over +-32 samples) - what a resampler or a D/A converter reconstructs.
    double truePeakDbOf (const std::vector<float>& y, double fromSec, double toSec, double rate = fs)
    {
        const auto a = std::min (y.size(), (size_t) (fromSec * rate)), b = std::min (y.size(), (size_t) (toSec * rate));
        double m = 0.0;
        for (size_t i = a; i < b; ++i) m = std::max (m, (double) std::abs (y[i]));
        const double sampleMax = m;
        for (size_t i = a + 32; i + 32 < b; ++i)
        {
            if (std::abs (y[i]) < 0.25 * sampleMax) continue;   // inter-sample peaks sit next to big samples
            for (int q = 1; q < 8; ++q)
            {
                const double frac = q / 8.0;
                double v = 0.0;
                for (int k = -31; k <= 32; ++k)
                {
                    const double t = (double) k - frac;
                    const double sinc = std::sin (juce::MathConstants<double>::pi * t) / (juce::MathConstants<double>::pi * t);
                    v += y[(size_t) ((long) i + k)] * sinc * (0.5 + 0.5 * std::cos (juce::MathConstants<double>::pi * t / 32.5));
                }
                m = std::max (m, std::abs (v));
            }
        }
        return linToDb (m);
    }

    std::vector<float> sine (double seconds, double freq, double db, double rate = fs)
    {
        std::vector<float> x ((size_t) (seconds * rate));
        for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (dbToLin (db) * std::sin (twoPi * freq * (double) i / rate));
        return x;
    }
}

struct AutoGainTest : juce::UnitTest
{
    AutoGainTest() : juce::UnitTest ("AutoGain") {}

    void runTest() override
    {
        const double normal = AutoGainStage::normalPeakDb, shout = AutoGainStage::shoutPeakDb;
        const double maxBoost = AutoGainStage::maxBoostDb, ceiling = AutoGainStage::ceilingDb;
        const float ceilingLin = (float) (dbToLin (ceiling) * (1.0 + 1.0e-6));

        beginTest ("typical speech is led to about -20 dBFS, as far as the gain range 0 ... +6 dB allows");
        for (double in : { -32.0, -28.0, -26.0, -24.0, -22.0, -20.0, -17.0, -14.0 })
        {
            double outSum = 0.0, inSum = 0.0, gainSum = 0.0, lowest = 100.0, highest = -100.0;
            const unsigned seeds[] = { 3, 5, 8 };
            for (auto seed : seeds)
            {
                AutoGainStage s; s.prepare (fs);
                auto sp = talk (90.0, in, seed);
                inSum += typicalOf (sp.x, 20.0, 90.0);   // the same measure as the output (room noise included)
                const auto g = run (s, sp.x);
                outSum += typicalOf (sp.x, 20.0, 90.0);
                gainSum += mean (g, (size_t) (20.0 * fs / 480));
                for (auto v : g) { lowest = std::min (lowest, v); highest = std::max (highest, v); }
            }
            const double out = outSum / 3.0, inMeasured = inSum / 3.0, gain = gainSum / 3.0;
            const double expected = juce::jlimit (inMeasured, inMeasured + maxBoost, normal);
            logMessage ("    input typical " + d2 (in) + " dBFS (measured " + d2 (inMeasured) + "): mean gain " + d2 (gain) + " dB (range " + d2 (lowest) + " ... "
                        + d2 (highest) + "), output typical " + d2 (out) + " dBFS (expected " + d2 (expected) + ")");
            expectWithinAbsoluteError (out, expected, 1.0);
            expectGreaterOrEqual (lowest, 0.0, "never below 0 dB");
            expectLessOrEqual (highest, maxBoost + 1.0e-9, "never above +6 dB");
        }

        beginTest ("a very quiet voice gets exactly the maximum +6 dB, a loud one exactly 0 dB");
        {
            AutoGainStage q; q.prepare (fs);
            auto a = talk (30.0, -40.0, 4);
            const auto gq = run (q, a.x);
            AutoGainStage l; l.prepare (fs);
            auto b = talk (30.0, -9.0, 4);
            const auto gl = run (l, b.x);
            double hq = -100.0, ll = 100.0, hl = -100.0;
            for (auto v : gq) hq = std::max (hq, v);
            for (auto v : gl) { ll = std::min (ll, v); hl = std::max (hl, v); }
            logMessage ("    -40 dBFS voice: highest gain " + juce::String (hq, 6) + " dB; -9 dBFS voice: gain between " + juce::String (ll, 6)
                        + " and " + juce::String (hl, 6) + " dB, loudest output " + juce::String (peakDbOf (b.x, 0.0, 30.0), 3) + " dBFS");
            expectWithinAbsoluteError (hq, maxBoost, 1.0e-9);
            expectEquals (ll, 0.0);
            expectLessOrEqual (peakDbOf (b.x, 0.0, 30.0), ceiling + 1.0e-5, "the ceiling holds the loud voice");
        }

        beginTest ("shouting 10-12 dB over the natural voice: about -15 dBFS, never above -10 dBFS, no clicks");
        for (double rise : { 10.0, 12.0 })
        {
            AutoGainStage s; s.prepare (fs);
            auto calm = talk (40.0, -26.0, 11);
            run (s, calm.x);
            auto loud = talk (20.0, -26.0 + rise, 12);
            const auto in = loud.x;
            const int guardsBefore = s.getGuardCount();
            run (s, loud.x);
            const double outTyp = typicalOf (loud.x, 8.0, 20.0), loudest = peakDbOf (loud.x, 0.0, 20.0);
            const double step = largestGainStepDb (in, loud.x, (size_t) s.getLatencySamples());
            logMessage ("    +" + d2 (rise) + " dB: natural level " + d2 (s.getNaturalLevelDb()) + ", aim " + d2 (s.getAimDb()) + " dBFS; output typical "
                        + d2 (outTyp) + " dBFS, loudest " + juce::String (loudest, 3) + " dBFS; largest gain step " + juce::String (step, 4)
                        + " dB/sample; guard samples " + juce::String (s.getGuardCount() - guardsBefore));
            expectWithinAbsoluteError (outTyp, shout, 1.5);
            expectLessOrEqual (loudest, ceiling + 1.0e-5);
            expectLessThan (step, 0.2, "no gain steps");
        }

        beginTest ("a slightly raised voice (+3 dB) stays a normal voice; louder than that, the aim slides towards -15 dBFS");
        {
            AutoGainStage s; s.prepare (fs);
            auto calm = talk (40.0, -24.0, 13);
            run (s, calm.x);
            auto raised = talk (5.0, -21.0, 14);
            run (s, raised.x);
            const double aimRaised = s.getAimDb();
            auto louder = talk (5.0, -17.0, 15);
            run (s, louder.x);
            const double aimLouder = s.getAimDb();
            logMessage ("    aim after 5 s at +3 dB: " + d2 (aimRaised) + " dBFS, after 5 s more at +7 dB: " + d2 (aimLouder)
                        + " dBFS (the natural level follows a lastingly louder voice slowly, 20 s)");
            expectWithinAbsoluteError (aimRaised, normal, 0.5);
            expect (aimLouder > normal + 0.5 && aimLouder <= shout + 1.0e-9, "in between or at the shouting aim");
        }

        beginTest ("long silence: the gain is kept bit-exactly for 10 minutes, the signal is never gated (output = input x gain)");
        {
            AutoGainStage s; s.prepare (fs);
            auto sp = talk (40.0, -28.0, 16, -62.0);
            run (s, sp.x);
            const double held = s.getTargetGainDb(), heldApplied = s.getAppliedGainDb();
            bool unchanged = true;
            double worstRatioErr = 0.0;
            const double expectedGain = dbToLin (heldApplied);
            const auto L = (size_t) s.getLatencySamples();
            std::mt19937 rng (17);
            std::normal_distribution<double> nd (0.0, 1.0);
            std::vector<float> last ((size_t) fs);
            for (int second = 0; second < 600; ++second)
            {
                std::vector<float> x ((size_t) fs);
                const bool digital = second >= 300;   // 5 min room noise, 5 min digital silence
                for (auto& v : x) v = digital ? 0.0f : (float) (dbToLin (-62.0) * nd (rng));
                const auto in = x;
                const auto g = run (s, x);
                for (auto v : g) unchanged = unchanged && juce::exactlyEqual (v, held);
                if (! digital)
                    for (size_t i = L; i < x.size(); ++i)
                        if (std::abs (in[i - L]) > 1.0e-4f)
                            worstRatioErr = std::max (worstRatioErr, std::abs ((double) x[i] / in[i - L] - expectedGain) / expectedGain);
            }
            auto again = talk (10.0, -28.0, 18, -62.0);
            const auto ga = run (s, again.x);
            logMessage ("    gain " + d2 (held) + " dB kept through 5 min room noise + 5 min digital silence; largest deviation of output/input "
                        + juce::String (worstRatioErr * 100.0, 7) + " %; first block of speech afterwards " + d2 (ga.front()) + " dB");
            expect (unchanged, "gain unchanged");
            expectLessThan (worstRatioErr, 1.0e-6, "no gating, no fading");
            expectWithinAbsoluteError (ga.front(), held, 0.1, "continues from the kept gain");
        }

        beginTest ("the gain moves only while there is voice; never in pauses between words");
        {
            AutoGainStage s; s.prepare (fs);
            auto sp = talk (60.0, -26.0, 19, -60.0);
            const auto x0 = sp.x;
            std::vector<size_t> ends;
            const auto g = run (s, sp.x, 480, &ends);
            const double floorDb = s.getNoiseFloorDb();
            int movesInQuiet = 0, quietBlocks = 0;
            for (size_t b = 1; b < g.size(); ++b)
            {
                double ssum = 0.0;
                for (size_t i = ends[b - 1]; i < ends[b]; ++i) ssum += (double) x0[i] * x0[i];
                const double rms = 10.0 * std::log10 (std::max (1.0e-30, ssum / (double) (ends[b] - ends[b - 1])));
                if (rms < std::max (AutoGainStage::activityFloorDb, floorDb + AutoGainStage::activityMarginDb) - 3.0)
                {
                    ++quietBlocks;
                    if (! juce::exactlyEqual (g[b], g[b - 1])) ++movesInQuiet;
                }
            }
            logMessage ("    " + juce::String (quietBlocks) + " blocks clearly without voice: gain moved in " + juce::String (movesInQuiet));
            expectEquals (movesInQuiet, 0);
        }

        beginTest ("plosives and breath pops (1-30 ms) do not pull the gain down; the limiter catches their peaks");
        {
            double gainClean = 0.0, gainPlos = 0.0, gainPop = 0.0, loudest = -200.0;
            for (unsigned seed : { 3u, 5u, 8u })
            {
                AutoGainStage a; a.prepare (fs);
                auto clean = talk (90.0, -24.0, seed, -65.0, -30.0);
                gainClean += mean (run (a, clean.x), 2000) / 3.0;
                AutoGainStage b; b.prepare (fs);
                auto plos = talk (90.0, -24.0, seed, -65.0, 8.0);
                gainPlos += mean (run (b, plos.x), 2000) / 3.0;
                AutoGainStage c; c.prepare (fs);
                auto pop = talk (90.0, -24.0, seed, -65.0, -30.0, 12.0);
                gainPop += mean (run (c, pop.x), 2000) / 3.0;
                loudest = std::max ({ loudest, peakDbOf (plos.x, 0.0, 90.0), peakDbOf (pop.x, 0.0, 90.0) });
            }
            logMessage ("    mean gain: clean speech " + d2 (gainClean) + " dB, strong plosives (+8 dB) " + d2 (gainPlos) + " dB, breath pops (+12 dB) "
                        + d2 (gainPop) + " dB; loudest output " + juce::String (loudest, 3) + " dBFS");
            expectWithinAbsoluteError (gainPlos, gainClean, 0.6);
            expectWithinAbsoluteError (gainPop, gainClean, 0.8);
            expectLessOrEqual (loudest, ceiling + 1.0e-5);
        }

        beginTest ("short clicks (0.5 ms, 12 dB over the voice) do not pull the gain down");
        {
            AutoGainStage s; s.prepare (fs);
            auto sp = talk (40.0, -20.0, 20);
            run (s, sp.x);
            const double before = s.getTargetGainDb();
            auto y = talk (20.0, -20.0, 21);
            for (double t = 0.1; t < 19.9; t += 0.35)
            {
                const auto at = (size_t) (t * fs);
                for (size_t k = 0; k < 24 && at + k < y.x.size(); ++k) y.x[at + k] += (float) dbToLin (-8.0);
            }
            AutoGainStage ref; ref.prepare (fs);
            auto sp2 = talk (40.0, -20.0, 20);
            run (ref, sp2.x);
            auto y2 = talk (20.0, -20.0, 21);
            const auto gRef = run (ref, y2.x);
            const auto g = run (s, y.x);
            double worst = 0.0;
            for (size_t b = 0; b < std::min (g.size(), gRef.size()); ++b) worst = std::max (worst, gRef[b] - g[b]);
            logMessage ("    gain before " + d2 (before) + " dB; largest drop against the same speech without clicks " + juce::String (worst, 3)
                        + " dB; loudest output " + juce::String (peakDbOf (y.x, 0.0, 20.0), 3) + " dBFS");
            expectLessThan (worst, 0.3);
            expectLessOrEqual (peakDbOf (y.x, 0.0, 20.0), ceiling + 1.0e-5);
        }

        beginTest ("the ceiling: torture signals at the maximum gain never exceed -10 dBFS");
        {
            struct Torture { const char* name; std::function<float (size_t, std::mt19937&)> f; };
            std::uniform_real_distribution<double> u (-1.0, 1.0);
            const Torture tortures[] = {
                { "full-scale square 100 Hz", [] (size_t i, std::mt19937&) { return std::fmod ((double) i, 480.0) < 240.0 ? 1.0f : -1.0f; } },
                { "white noise 0 dBFS peak", [&u] (size_t, std::mt19937& r) { return (float) u (r); } },
                { "impulses every 7 ms", [] (size_t i, std::mt19937&) { return i % 336 == 0 ? 1.0f : 0.0f; } },
                { "sweep 20 Hz - 20 kHz, -1 dBFS", [] (size_t i, std::mt19937&) { const double t = (double) i / fs; return (float) (0.89 * std::sin (twoPi * 20.0 * 5.0 / std::log (1000.0) * (std::exp (t / 5.0 * std::log (1000.0)) - 1.0))); } },
                { "DC steps", [] (size_t i, std::mt19937&) { return (i / 4800) % 2 == 0 ? 0.95f : -0.95f; } },
            };
            for (auto& t : tortures)
            {
                AutoGainStage s; s.prepare (fs);
                auto quiet = talk (30.0, -40.0, 22);   // gain at +6 dB
                run (s, quiet.x);
                std::mt19937 rng (23);
                std::vector<float> x ((size_t) (5.0 * fs));
                for (size_t i = 0; i < x.size(); ++i) x[i] = t.f (i, rng);
                for (int bs : { 480, 37, 1024 })
                {
                    auto y = x;
                    run (s, y, bs);
                    float m = 0.0f;
                    bool finite = true;
                    for (auto v : y) { m = std::max (m, std::abs (v)); finite = finite && std::isfinite (v); }
                    if (bs == 480) logMessage ("    " + juce::String (t.name) + ": loudest output " + juce::String (linToDb (m), 4) + " dBFS");
                    expectLessOrEqual (m, ceilingLin, t.name);
                    expect (finite);
                }
            }
        }

        beginTest ("the ceiling also holds as a true peak (inter-sample peaks of plosives and noise, as after the engine's resampler)");
        {
            struct Case { const char* name; double plosive, pop; };
            const Case cases[] = { { "speech, strong plosives (+8 dB)", 8.0, -200.0 }, { "speech, breath pops (+12 dB)", -30.0, 12.0 }, { "speech, normal plosives", -10.0, -200.0 } };
            for (auto& c : cases)
            {
                AutoGainStage s; s.prepare (fs);
                auto sp = talk (40.0, -24.0, 36, -65.0, c.plosive, c.pop);
                run (s, sp.x);
                const double spk = peakDbOf (sp.x, 10.0, 40.0), tp = truePeakDbOf (sp.x, 10.0, 40.0);
                logMessage ("    " + juce::String (c.name) + ": sample peak " + juce::String (spk, 3) + " dBFS, true peak " + juce::String (tp, 3) + " dBFS");
                expectLessOrEqual (spk, ceiling + 1.0e-5);
                expectLessThan (tp, ceiling + 0.3, "true peak at most 0.3 dB over (4x detection)");
            }
            for (bool limited : { true, false })
            {
                AutoGainStage s; s.prepare (fs);
                auto quiet = talk (20.0, -40.0, 37);
                run (s, quiet.x);
                std::mt19937 rng (38);
                std::normal_distribution<double> nd (0.0, 0.3);
                std::vector<double> raw ((size_t) (5.0 * fs));
                for (auto& v : raw) v = juce::jlimit (-1.0, 1.0, nd (rng));
                if (limited) speech::bandLimit (raw, fs);
                std::vector<float> noise (raw.begin(), raw.end());
                run (s, noise);
                const double spk = peakDbOf (noise, 1.0, 5.0), tp = truePeakDbOf (noise, 1.0, 5.0);
                logMessage ("    white noise near 0 dBFS, " + juce::String (limited ? "band-limited to 20 kHz (as from a microphone)" : "full band up to 24 kHz (purely digital)")
                            + ", gain +6 dB: sample peak " + juce::String (spk, 3) + " dBFS, true peak " + juce::String (tp, 3) + " dBFS");
                expectLessOrEqual (spk, ceiling + 1.0e-5);
                expectLessThan (tp, ceiling + (limited ? 0.3 : 2.0));
            }
        }

        beginTest ("normal speech: the limiter only touches the onsets of the loudest syllables (measured)");
        for (double in : { -26.0, -23.0 })
        {
            double deepest = 0.0;
            int active = 0, blocks = 0;
            for (unsigned seed : { 24u, 3u, 5u })
            {
                AutoGainStage s; s.prepare (fs);
                auto sp = talk (90.0, in, seed);
                for (size_t p = 0; p + 480 <= sp.x.size(); p += 480)
                {
                    float* ch[] = { sp.x.data() + p };
                    s.process (ch, 1, 480);
                    if (p > (size_t) (20.0 * fs)) { deepest = std::min (deepest, s.getLimiterGainDb()); ++blocks; if (s.getLimiterGainDb() < -0.01) ++active; }
                }
            }
            logMessage ("    input typical " + d2 (in) + " dBFS: limiter active in " + juce::String (100.0 * active / blocks, 2) + " % of the blocks, deepest "
                        + d2 (deepest) + " dB");
            expectGreaterThan (deepest, -6.0);
            expectLessThan (100.0 * active / blocks, 15.0);
        }

        beginTest ("linear while the gain is steady: THD+N of a sine at the float noise floor");
        for (double freq : { 100.0, 997.0, 6000.0 })
        {
            AutoGainStage s; s.prepare (fs);
            auto x = sine (8.0, freq, -30.0);   // a steady tone: the gain settles at +6 dB and stays
            run (s, x);
            const double th = thdnDb (x.data() + (size_t) (6.0 * fs), (size_t) (1.0 * fs), freq);
            logMessage ("    " + juce::String (freq, 0) + " Hz at -30 dBFS (gain " + d2 (s.getTargetGainDb()) + " dB): THD+N " + d2 (th) + " dB");
            expectLessThan (th, -120.0);
        }

        beginTest ("the limiter on a loud steady tone: exact level, no ripple, low distortion");
        for (double freq : { 80.0, 120.0, 997.0, 4000.0 })
        {
            AutoGainStage s; s.prepare (fs);
            auto x = sine (4.0, freq, -4.0);
            run (s, x);
            const auto period = (size_t) std::lround (fs / freq);
            double lo = 1.0, hi = 0.0;
            for (size_t p = (size_t) (2.0 * fs); p + period <= x.size(); p += period)
            {
                float m = 0.0f;
                for (size_t i = p; i < p + period; ++i) m = std::max (m, std::abs (x[i]));
                lo = std::min (lo, (double) m); hi = std::max (hi, (double) m);
            }
            const double th = thdnDb (x.data() + (size_t) (2.0 * fs), (size_t) (1.0 * fs), freq);
            logMessage ("    " + juce::String (freq, 0) + " Hz at -4 dBFS: peaks per cycle " + juce::String (linToDb (lo), 3) + " ... " + juce::String (linToDb (hi), 3)
                        + " dBFS, THD+N " + d2 (th) + " dB");
            expectLessThan (linToDb (hi) - linToDb (lo), 0.05, "no ripple");
            expectLessOrEqual (linToDb (hi), ceiling + 1.0e-5);
            expectLessThan (th, -70.0, "sample-peak detection: a period that is not a whole number of samples modulates by ~0.02 dB");
        }

        beginTest ("no clicks: the gain changes smoothly on level steps, shouts and switching");
        {
            std::vector<float> x ((size_t) (24.0 * fs));
            const double levels[] = { -30.0, -12.0, -40.0, -20.0, -6.0, -35.0 };
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = (float) (dbToLin (levels[std::min<size_t> (5, i / (size_t) (4.0 * fs))]) * std::sin (twoPi * 997.0 * (double) i / fs));
            const auto in = x;
            AutoGainStage s; s.prepare (fs);
            run (s, x);
            const double step = largestGainStepDb (in, x, (size_t) s.getLatencySamples());
            logMessage ("    abrupt level steps of a 997 Hz tone (-30 / -12 / -40 / -20 / -6 / -35 dBFS): largest gain change between two samples "
                        + juce::String (step, 4) + " dB, loudest output " + juce::String (peakDbOf (x, 0.0, 24.0), 3) + " dBFS");
            expectLessThan (step, 0.2);
            expectLessOrEqual (peakDbOf (x, 0.0, 24.0), ceiling + 1.0e-5);
        }

        beginTest ("the result hardly depends on the block size (32 ... 4096 samples and random)");
        {
            auto sp = talk (120.0, -25.0, 25);
            auto ref = sp.x;
            AutoGainStage r; r.prepare (fs);
            const auto gRef = run (r, ref, 480);
            const double refTyp = typicalOf (ref, 20.0, 120.0), refGain = mean (gRef, 2000);
            for (int bs : { 32, 64, 128, 144, 256, 441, 512, 1024, 2048, 4096, -1 })
            {
                auto y = sp.x;
                AutoGainStage s; s.prepare (fs);
                const auto g = run (s, y, bs);
                const double typ = typicalOf (y, 20.0, 120.0);
                const double gm = mean (g, (size_t) (20.0 * (double) g.size() / 120.0));
                logMessage ("    block " + juce::String (bs > 0 ? juce::String (bs) : juce::String ("random")) + ": output typical " + d2 (typ)
                            + " dBFS (480: " + d2 (refTyp) + "), mean gain " + d2 (gm) + " dB (480: " + d2 (refGain) + ")");
                expectWithinAbsoluteError (typ, refTyp, 0.5);
                expectWithinAbsoluteError (gm, refGain, 0.5);
            }
        }

        beginTest ("sample rates 8 ... 384 kHz: latency, levels and ceiling");
        {
            double typ48 = 0.0;
            for (double rate : { 48000.0, 8000.0, 16000.0, 22050.0, 32000.0, 44100.0, 88200.0, 96000.0, 176400.0, 192000.0, 384000.0 })
            {
                AutoGainStage s; s.prepare (rate);
                const int expectedL = juce::jlimit (1, AutoGainStage::maxLookAheadSamples, (int) std::lround (AutoGainStage::lookAheadSeconds * rate));
                auto sp = talk (60.0, -24.0, 26, -65.0, -30.0, -200.0, rate);
                run (s, sp.x, (int) std::lround (0.01 * rate));
                const double typ = typicalOf (sp.x, 15.0, 60.0, rate), loudest = peakDbOf (sp.x, 0.0, 60.0, rate);
                if (juce::exactlyEqual (rate, 48000.0)) typ48 = typ;
                logMessage ("    " + juce::String (rate, 0) + " Hz: latency " + juce::String (s.getLatencySamples()) + " samples ("
                            + juce::String (1000.0 * s.getLatencySamples() / rate, 3) + " ms), output typical " + d2 (typ) + " dBFS, loudest " + d2 (loudest) + " dBFS");
                expectEquals (s.getLatencySamples(), expectedL);
                expectEquals (AutoGainStage::latencyFor (rate), expectedL);
                expectWithinAbsoluteError (typ, typ48, 0.8, "same level as at 48 kHz");
                expectWithinAbsoluteError (typ, normal, 1.5);
                expectLessOrEqual (loudest, ceiling + 1.0e-5);
            }
        }

        beginTest ("channels: one gain for all (mono, stereo, 8), missing channels skipped, channels beyond 8 untouched");
        {
            auto sp = talk (20.0, -26.0, 27);
            const auto n = sp.x.size();
            std::vector<std::vector<float>> ch (10, std::vector<float> (n));
            for (size_t i = 0; i < n; ++i)
                for (size_t c = 0; c < 10; ++c) ch[c][i] = sp.x[i] * (float) (c % 2 == 0 ? 1.0 : -0.5) * (c == 3 ? 0.0f : 1.0f);
            const auto orig = ch;
            AutoGainStage s; s.prepare (fs);
            for (size_t p = 0; p < n; p += 480)
            {
                float* ptr[10];
                for (size_t c = 0; c < 10; ++c) ptr[c] = c == 5 ? nullptr : ch[c].data() + p;
                s.process (ptr, 10, (int) std::min<size_t> (480, n - p));
            }
            bool same = true, untouched = true, missingUntouched = true;
            for (size_t i = 0; i < n; ++i)
            {
                same = same && juce::exactlyEqual (ch[1][i], -0.5f * ch[0][i]) && juce::exactlyEqual (ch[3][i], 0.0f) && juce::exactlyEqual (ch[7][i], -0.5f * ch[6][i]);
                untouched = untouched && juce::exactlyEqual (ch[8][i], orig[8][i]) && juce::exactlyEqual (ch[9][i], orig[9][i]);
                missingUntouched = missingUntouched && juce::exactlyEqual (ch[5][i], orig[5][i]);
            }
            expect (same, "all processed channels get the same gain");
            expect (untouched, "channels 9 and 10 untouched");
            expect (missingUntouched, "a missing channel pointer is skipped");
            float* none[] = { nullptr, nullptr };
            s.process (none, 2, 480);   // must not crash
            s.process (nullptr, 2, 480);
        }

        beginTest ("broken input (NaN, infinite, absurd) from a faulty plug-in: passed on, but the gain and the limiter are not disturbed");
        {
            const float bad[] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(), 1.0e9f };
            for (float b : bad)
            {
                AutoGainStage s, ref;
                s.prepare (fs); ref.prepare (fs);
                auto sp = talk (40.0, -26.0, 28);
                auto spRef = sp.x;
                run (s, sp.x);
                run (ref, spRef);
                std::vector<float> x ((size_t) (0.1 * fs), b);   // 100 ms of garbage ...
                std::vector<float> z ((size_t) (0.1 * fs), 0.0f);   // ... against 100 ms of digital silence
                run (s, x);
                run (ref, z);
                bool passedOn = false;
                for (auto v : x) passedOn = passedOn || ! std::isfinite (v) || std::abs (v) > 1.0e6f;
                auto after = talk (2.0, -26.0, 29);
                auto afterRef = after.x;
                run (s, after.x);
                run (ref, afterRef);
                bool finiteLater = true;
                for (size_t i = (size_t) s.getLatencySamples(); i < after.x.size(); ++i) finiteLater = finiteLater && std::isfinite (after.x[i]);
                logMessage ("    " + juce::String (b) + ": gain 2 s later " + d2 (s.getTargetGainDb()) + " dB (with silence instead: " + d2 (ref.getTargetGainDb())
                            + " dB), limiter " + juce::String (s.getLimiterGainDb(), 4) + " dB");
                expect (passedOn, "the engine still sees the broken block");
                expect (finiteLater, "valid again once the input is valid");
                expectWithinAbsoluteError (s.getTargetGainDb(), ref.getTargetGainDb(), 0.05);
                expectWithinAbsoluteError (s.getLimiterGainDb(), ref.getLimiterGainDb(), 0.01);
            }
        }

        beginTest ("tiny signals (denormal range) pass correctly");
        {
            AutoGainStage s; s.prepare (fs);
            auto sp = talk (20.0, -26.0, 30);
            run (s, sp.x);
            const double g = dbToLin (s.getAppliedGainDb());
            std::vector<float> x (4800);
            for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (1.0e-38 * std::sin (0.01 * (double) i));
            const auto in = x;
            run (s, x);
            double err = 0.0;
            const auto L = (size_t) s.getLatencySamples();
            for (size_t i = L; i < x.size(); ++i) err = std::max (err, std::abs ((double) x[i] - g * in[i - L]));
            logMessage ("    largest error " + juce::String (err));
            expectLessThan (err, 1.0e-40);
        }

        beginTest ("switched off: glides back to exactly 0 dB, then the audio passes bit-exact (3 ms later)");
        {
            AutoGainStage s; s.prepare (fs);
            auto sp = talk (40.0, -26.0, 31);
            run (s, sp.x);
            const double start = s.getTargetGainDb();
            s.setEnabled (false);
            auto y = talk (2.0, -26.0, 32);
            const auto g = run (s, y.x);
            bool monotonic = true;
            for (size_t b = 1; b < g.size(); ++b) monotonic = monotonic && g[b] <= g[b - 1];
            size_t zeroAt = g.size();
            for (size_t b = 0; b < g.size(); ++b) if (juce::exactlyEqual (g[b], 0.0)) { zeroAt = b; break; }
            logMessage ("    from " + d2 (start) + " dB to 0 dB in " + juce::String ((double) zeroAt * 480.0 / fs * 1000.0, 0) + " ms");
            expect (monotonic);
            expectLessThan ((double) zeroAt * 480.0 / fs, start / AutoGainStage::offReturnDbPerSec + 0.05);
            auto z = talk (3.0, -6.0, 33, -60.0);
            const auto orig = z.x;
            run (s, z.x);
            const auto L = (size_t) s.getLatencySamples();
            bool exact = true;
            for (size_t i = L; i < z.x.size(); ++i) exact = exact && juce::exactlyEqual (z.x[i], orig[i - L]);
            expect (exact, "off and at 0 dB: not a single bit changed, only delayed");
            expectWithinAbsoluteError (s.getAppliedGainDb(), 0.0, 1.0e-12);
        }

        beginTest ("switching on and off every block for 20 s: smooth, no clicks, valid");
        {
            AutoGainStage s; s.prepare (fs);
            auto sp = talk (20.0, -30.0, 34);
            const auto in = sp.x;
            for (size_t p = 0, k = 0; p < sp.x.size(); p += 480, ++k)
            {
                s.setEnabled ((k / 3) % 2 == 0);
                float* ch[] = { sp.x.data() + p };
                s.process (ch, 1, (int) std::min<size_t> (480, sp.x.size() - p));
            }
            bool finite = true;
            for (auto v : sp.x) finite = finite && std::isfinite (v);
            const double step = largestGainStepDb (in, sp.x, (size_t) s.getLatencySamples(), 3.0e-3);
            logMessage ("    largest gain change between two samples " + juce::String (step, 4) + " dB");
            expect (finite);
            expectLessThan (step, 0.2);
        }

        beginTest ("latency: an impulse comes out exactly 3 ms later (on at 0 dB and off)");
        for (bool on : { true, false })
        {
            AutoGainStage s; s.prepare (fs);
            s.setEnabled (on);
            std::vector<float> x (4800, 0.0f);
            x[100] = 0.1f;   // -20 dBFS: below the limiter's knee (-12.3 dBFS)
            run (s, x);
            const auto L = (size_t) s.getLatencySamples();
            float elsewhere = 0.0f;
            for (size_t i = 0; i < x.size(); ++i) if (i != 100 + L) elsewhere = std::max (elsewhere, std::abs (x[i]));
            expectEquals ((int) L, 144);
            if (on) expectWithinAbsoluteError (x[100 + L], 0.1f, 1.0e-7f, "on: a lone click does not move the gain");
            else    expectEquals (x[100 + L], 0.1f, "off: bit-exact");
            expectEquals (elsewhere, 0.0f);
        }

        beginTest ("the meters show the real gain and peaks");
        {
            AutoGainStage s; s.prepare (fs);
            auto sp = talk (40.0, -26.0, 35);
            run (s, sp.x);
            s.takeMeters();
            auto tone = sine (0.04, 1000.0, -20.0);
            const auto in = tone;
            run (s, tone);
            const auto m = s.takeMeters();
            const double realGain = linToDb (std::abs ((double) tone.back() / in[in.size() - 1 - (size_t) s.getLatencySamples()]));
            logMessage ("    meter gain " + juce::String (m.gainDb, 3) + " dB, real gain of the last sample " + juce::String (realGain, 3)
                        + " dB; peaks in " + d2 (m.inPeakDb) + " / out " + d2 (m.outPeakDb) + " dBFS");
            expectWithinAbsoluteError ((double) m.gainDb, realGain, 0.05);
            expectWithinAbsoluteError ((double) m.inPeakDb, -20.0, 0.1);
            expectWithinAbsoluteError ((double) m.outPeakDb, peakDbOf (tone, 0.0, 1.0), 0.01);
        }

        beginTest ("lasting changes (further from / closer to the mic): ensemble of 8 voices, the gain follows within seconds");
        for (double delta : { -6.0, -3.0, 3.0, 6.0 })
        {
            // mean gain trajectory of 8 voices (40 s at one level, then 50 s at the other), so the
            // response is not hidden by the phrase-to-phrase variation of a single voice
            const int voices = 8;
            std::vector<double> sum (9000, 0.0);
            double loudest = -200.0;
            for (int v = 0; v < voices; ++v)
            {
                const double base = delta < 0 ? -22.0 : -27.0;
                auto a = talk (40.0, base, (unsigned) (300 + v), -65.0, v % 3 == 0 ? 3.0 : -20.0);
                auto b = talk (50.0, base + delta, (unsigned) (600 + v), -65.0, v % 3 == 0 ? 3.0 : -20.0);
                std::vector<float> x = a.x;
                x.insert (x.end(), b.x.begin(), b.x.end());
                AutoGainStage s; s.prepare (fs);
                const auto g = run (s, x);
                for (size_t k = 0; k < std::min (sum.size(), g.size()); ++k) sum[k] += g[k] / voices;
                loudest = std::max (loudest, peakDbOf (x, 40.0, 90.0));
            }
            const double before = mean (sum, 2000, 4000), after = mean (sum, 8000, 9000);
            auto reach = [&] (double tol)
            {
                for (size_t k = 4000; k < sum.size(); ++k)
                {
                    bool ok = true;
                    for (size_t c = k; c < std::min (sum.size(), k + 200); ++c) ok = ok && std::abs (sum[c] - after) < tol;
                    if (ok) return (double) (k - 4000) / 100.0;
                }
                return 99.0;
            };
            const double t1 = reach (1.0), g1 = sum[4100], g3 = sum[4300], g5 = sum[4500], g10 = sum[5000];
            logMessage ("    " + juce::String (delta, 0) + " dB: mean gain " + d2 (before) + " -> " + d2 (after) + " dB; within 1 dB after " + d2 (t1)
                        + " s; gain after 1 / 3 / 5 / 10 s: " + d2 (g1) + " / " + d2 (g3) + " / " + d2 (g5) + " / " + d2 (g10) + " dB; loudest output " + d2 (loudest) + " dBFS");
            expectLessOrEqual (loudest, ceiling + 1.0e-5);
            if (delta < 0)
                expectLessThan (t1, 5.5, "a quieter voice is caught up within seconds");
            else
            {
                expectLessThan (g3, before - 0.5 * (before - after), "a louder voice: at least half of the correction within 3 s");
                expectLessThan (t1, 20.0, "and all of it as the natural level adapts (20 s)");
            }
        }

        beginTest ("naturalness: how much the gain moves inside phrases and between phrases (measured, see the log)");
        {
            std::vector<double> ranges, slopes, per100;
            for (unsigned seed : { 3u, 5u, 8u })
            {
                auto sp = talk (180.0, -23.0, seed);
                const auto x0 = sp.x;
                AutoGainStage s; s.prepare (fs);
                const auto g = run (s, sp.x);
                const auto L = (size_t) s.getLatencySamples();
                std::vector<double> xin, xout;
                for (auto& ph : sp.phrases)
                {
                    if (ph.first < (size_t) (20.0 * fs) || ph.second + L >= sp.x.size()) continue;
                    double lo = 1.0e9, hi = -1.0e9;
                    for (size_t b = ph.first / 480; b < ph.second / 480 && b < g.size(); ++b) { lo = std::min (lo, g[b]); hi = std::max (hi, g[b]); }
                    ranges.push_back (hi - lo);
                    xin.push_back (speech::typicalLevel (x0.data() + ph.first, ph.second - ph.first, fs));
                    xout.push_back (speech::typicalLevel (sp.x.data() + ph.first + L, ph.second - ph.first, fs));
                }
                const double mi = mean (xin), mo = mean (xout);
                double sxy = 0.0, sxx = 0.0;
                for (size_t i = 0; i < xin.size(); ++i) { sxy += (xin[i] - mi) * (xout[i] - mo); sxx += (xin[i] - mi) * (xin[i] - mi); }
                slopes.push_back (sxy / sxx);
                for (size_t b = 2000; b + 10 < g.size(); ++b) per100.push_back (std::abs (g[b + 10] - g[b]));
            }
            const double medRange = percentile (ranges, 0.5), p90Range = percentile (ranges, 0.9), slope = mean (slopes), p99Step = percentile (per100, 0.99);
            logMessage ("    gain range inside a phrase: median " + d2 (medRange) + " dB, 90 % " + d2 (p90Range) + " dB; loudness differences between phrases kept: "
                        + juce::String (juce::roundToInt (100.0 * slope)) + " % (100 % = untouched); gain change within 100 ms: 99 % below " + d2 (p99Step) + " dB");
            expectLessThan (medRange, 4.0);
            expectGreaterThan (slope, 0.45);
            expectLessThan (p99Step, 1.5);
        }

        beginTest ("long soak: random speech, shouts, silence, noise, pops, broken blocks, switching, random blocks (MICVST_SOAK_HOURS)");
        {
            double hours = 0.25;
            if (auto* e = std::getenv ("MICVST_SOAK_HOURS")) hours = std::max (0.01, std::atof (e));
            soak (hours);
        }
    }

    void soak (double hours)
    {
        // A pool of speech material (different voices and levels) is played with random level
        // offsets, silences, noise floors, shouts, pops and rare broken blocks; the stage is checked
        // after every block. Runs faster than real time.
        std::vector<std::vector<float>> pool;
        for (unsigned seed = 50; seed < 56; ++seed)
        {
            speech::Params p; p.peakDb = -20.0; p.seed = seed; p.female = (seed % 2) == 1; p.noiseDb = -200.0;
            p.plosiveDb = seed % 3 == 0 ? 6.0 : -10.0; p.popDb = seed == 52 ? 10.0 : -200.0;
            pool.push_back (speech::generate (60.0, p).x);
        }
        std::mt19937 rng (99);
        std::uniform_real_distribution<double> u (0.0, 1.0);
        std::normal_distribution<double> nd (0.0, 1.0);
        AutoGainStage s; s.prepare (fs);
        const auto total = (int64_t) (hours * 3600.0 * fs);
        const float ceilingLin = (float) (dbToLin (AutoGainStage::ceilingDb) * (1.0 + 1.0e-6));
        int64_t done = 0, violations = 0, nonFinite = 0, gainOut = 0, blocks = 0, brokenBlocks = 0, toggles = 0;
        double worstBlockUs = 0.0, sumBlockUs = 0.0;
        std::vector<float> buf (4096);
        size_t src = 0, pos = 0;
        double offsetDb = 0.0, noiseDb = -70.0;
        int64_t segmentLeft = 0, brokenLeftUntilValid = 0;
        enum { speak, silence, shoutSeg } kind = speak;
        const auto t0 = std::chrono::steady_clock::now();
        while (done < total)
        {
            if (segmentLeft <= 0)
            {
                const double r = u (rng);
                kind = r < 0.65 ? speak : (r < 0.85 ? silence : shoutSeg);
                segmentLeft = (int64_t) ((kind == silence ? 1.0 + 60.0 * u (rng) * u (rng) : 3.0 + 40.0 * u (rng)) * fs);
                offsetDb = kind == shoutSeg ? 4.0 + 8.0 * u (rng) : -14.0 + 16.0 * u (rng);   // typical -34 ... -2 dBFS
                noiseDb = -90.0 + 35.0 * u (rng);
                src = (size_t) (u (rng) * (double) pool.size()) % pool.size();
                pos = (size_t) (u (rng) * (double) pool[src].size());
                if (u (rng) < 0.1) { s.setEnabled (! s.isEnabled()); ++toggles; }
            }
            const int n = 1 + (int) (u (rng) * u (rng) * 2047.0);
            const bool broken = u (rng) < 2.0e-4;
            const double g = dbToLin (offsetDb), nz = noiseDb > -89.0 ? dbToLin (noiseDb) : 0.0;
            for (int i = 0; i < n; ++i)
            {
                float v = 0.0f;
                if (kind != silence) { v = pool[src][pos] * (float) g; pos = (pos + 1) % pool[src].size(); }
                buf[(size_t) i] = v + (float) (nz * nd (rng));
                if (broken) buf[(size_t) i] = i % 3 == 0 ? std::numeric_limits<float>::quiet_NaN() : 1.0e12f;
            }
            if (broken) { ++brokenBlocks; brokenLeftUntilValid = s.getLatencySamples() + n; }
            float* ch[] = { buf.data() };
            const auto b0 = std::chrono::steady_clock::now();
            s.process (ch, 1, n);
            const double us = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - b0).count();
            worstBlockUs = std::max (worstBlockUs, us);
            sumBlockUs += us;
            const double gd = s.getTargetGainDb();
            if (! (gd >= 0.0 && gd <= AutoGainStage::maxBoostDb)) ++gainOut;
            for (int i = 0; i < n; ++i)
            {
                const float v = buf[(size_t) i];
                if (brokenLeftUntilValid > 0) { --brokenLeftUntilValid; continue; }
                if (! std::isfinite (v)) ++nonFinite;
                else if (std::abs (v) > ceilingLin && s.isEnabled()) ++violations;
            }
            done += n; segmentLeft -= n; ++blocks;
        }
        const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        logMessage ("    " + juce::String (hours, 2) + " h of audio in " + juce::String (wall, 1) + " s (" + juce::String (hours * 3600.0 / wall, 0) + "x real time), "
                    + juce::String (blocks) + " blocks, " + juce::String (brokenBlocks) + " broken blocks, " + juce::String (toggles) + " on/off switches; "
                    + "samples above -10 dBFS " + juce::String (violations) + ", invalid output samples " + juce::String (nonFinite)
                    + ", gain outside 0 ... +6 dB " + juce::String (gainOut) + "; processing time per 10 ms of audio: mean "
                    + juce::String (sumBlockUs / ((double) done / 480.0), 2) + " us, slowest block " + juce::String (worstBlockUs, 1) + " us");
        expectEquals ((int) violations, 0);
        expectEquals ((int) nonFinite, 0);
        expectEquals ((int) gainOut, 0);
    }
};
static AutoGainTest autoGainTest;

struct BuiltInAutoGainTest : juce::UnitTest
{
    BuiltInAutoGainTest() : juce::UnitTest ("BuiltInAutoGain") {}

    void runTest() override
    {
        beginTest ("name, channels, on by default, latency = the 3 ms look-ahead");
        {
            BuiltInAutoGainProcessor p;
            expectEquals (p.getName(), juce::String::fromUTF8 ("Otomatik Kazanç"));
            expectEquals (p.getTotalNumInputChannels(), 2);
            expectEquals (p.getTotalNumOutputChannels(), 2);
            expect (p.isAutoGainEnabled());
            expectEquals (p.getLatencySamples(), 144, "before prepare: 48 kHz");
            p.prepareToPlay (44100.0, 512);
            expectEquals (p.getLatencySamples(), 132, "44.1 kHz");
            p.prepareToPlay (96000.0, 512);
            expectEquals (p.getLatencySamples(), 288, "96 kHz");
            p.prepareToPlay (48000.0, 480);
            expectEquals (p.getLatencySamples(), 144, "48 kHz");
        }

        beginTest ("processBlock with a mono and a stereo buffer");
        {
            BuiltInAutoGainProcessor p;
            p.prepareToPlay (48000.0, 480);
            juce::MidiBuffer midi;
            for (int chans : { 1, 2 })
            {
                juce::AudioBuffer<float> b (chans, 480);
                for (int c = 0; c < chans; ++c)
                    for (int i = 0; i < 480; ++i) b.setSample (c, i, 0.1f * (float) std::sin (0.05 * i));
                for (int k = 0; k < 200; ++k) p.processBlock (b, midi);
                bool finite = true;
                for (int c = 0; c < chans; ++c) for (int i = 0; i < 480; ++i) finite = finite && std::isfinite (b.getSample (c, i));
                expect (finite);
            }
        }

        beginTest ("on/off survives the config.xml round trip; damaged state is ignored");
        {
            BuiltInAutoGainProcessor a;
            a.setAutoGainEnabled (false);
            juce::MemoryBlock blob;
            a.getStateInformation (blob);
            BuiltInAutoGainProcessor b;
            b.setStateInformation (blob.getData(), (int) blob.getSize());
            expect (! b.isAutoGainEnabled());
            const char junk[] = "nonsense";
            b.setStateInformation (junk, (int) sizeof (junk));
            expect (! b.isAutoGainEnabled(), "unchanged by junk");
        }
    }
};
static BuiltInAutoGainTest builtInAutoGainTest;
