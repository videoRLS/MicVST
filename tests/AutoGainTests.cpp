// Basic tests for the built-in "Otomatik Kazanç" (Auto Gain): normal speech near -15 dBFS, shouting
// near -12 dBFS, the 0 ... +6.5 dB gain range, sudden rises, short clicks, speech pauses and long
// silence (last gain kept, no gate), resuming, the -10 dBFS soft ceiling with 1.5 ms look-ahead, the
// latency, no jumps, switching off, meters = real values, the processor.
// (Test names are ASCII: the runner prints them as Latin-1.)
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/AutoGain.h"
#include "audio/BuiltInAutoGain.h"
#include <random>
#include <vector>

namespace
{
    constexpr double fs = 48000.0;
    constexpr int block = 480;
    const double twoPi = juce::MathConstants<double>::twoPi;

    double dbToLin (double db) { return std::pow (10.0, db / 20.0); }
    double linToDb (double v)  { return 20.0 * std::log10 (std::max (1.0e-12, v)); }

    // Speech-like test signal: 2.5 s phrases of "syllables" (150 ms Hann-shaped 300 Hz bursts every
    // 250 ms) and a 1 s pause; peak level = peakDb. noiseDb: RMS of white noise under everything
    // (-200 = digital silence in the pauses). pattern: level of syllable k relative to the peak
    // (cycled), e.g. { 0, -6 } = every second syllable 6 dB softer.
    std::vector<float> voice (double seconds, double peakDb, double noiseDb = -200.0, unsigned seed = 1,
                              std::vector<double> pattern = { 0.0 })
    {
        std::vector<float> x ((size_t) (seconds * fs));
        std::mt19937 rng (seed);
        std::normal_distribution<double> noise (0.0, 1.0);
        const double a = dbToLin (peakDb), n = noiseDb > -150.0 ? dbToLin (noiseDb) : 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / fs;
            const double inPhrase = std::fmod (t, 3.5);
            double env = 0.0;
            if (inPhrase < 2.5)
            {
                const double inSyllable = std::fmod (inPhrase, 0.25);
                const auto k = (size_t) (inPhrase / 0.25);
                if (inSyllable < 0.15) env = dbToLin (pattern[k % pattern.size()]) * (0.5 - 0.5 * std::cos (twoPi * inSyllable / 0.15));
            }
            x[i] = (float) (a * env * std::sin (twoPi * 300.0 * t) + n * noise (rng));
        }
        return x;
    }

    std::vector<float> noiseOnly (double seconds, double noiseDb, unsigned seed) { return voice (seconds, -200.0, noiseDb, seed); }

    // Runs the stage over x in place (block-wise); returns the analysis gain after every block.
    std::vector<double> run (AutoGainStage& s, std::vector<float>& x)
    {
        std::vector<double> gains;
        for (size_t p = 0; p < x.size(); p += (size_t) block)
        {
            float* ch[] = { x.data() + p };
            s.process (ch, 1, (int) std::min<size_t> ((size_t) block, x.size() - p));
            gains.push_back (s.getTargetGainDb());
        }
        return gains;
    }

    double peakDbOf (const std::vector<float>& y, double fromSec, double toSec)
    {
        float m = 0.0f;
        for (size_t i = (size_t) (fromSec * fs); i < std::min ((size_t) (toSec * fs), y.size()); ++i) m = std::max (m, std::abs (y[i]));
        return linToDb (m);
    }

    // Samples the engine test's click detector would flag (3 * amp * (2 pi f / fs)^2 + 2e-4).
    int clickSamples (const std::vector<float>& y, double amp, double freqHz)
    {
        const double limit = 3.0 * amp * std::pow (twoPi * freqHz / fs, 2.0) + 2.0e-4;
        int n = 0;
        for (size_t i = 1; i + 1 < y.size(); ++i)
            if (std::abs ((double) y[i + 1] - 2.0 * y[i] + y[i - 1]) > limit) ++n;
        return n;
    }

    juce::String d2 (double v) { return juce::String (v, 2); }
}

struct AutoGainTest : juce::UnitTest
{
    AutoGainTest() : juce::UnitTest ("AutoGain") {}

    void runTest() override
    {
        const double normal = AutoGainStage::normalPeakDb, shout = AutoGainStage::shoutPeakDb;

        beginTest ("normal speech: peaks led to about -15 dBFS, from a quieter and from a louder microphone");
        for (double in : { -20.0, -17.0 })
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, in);
            const auto g = run (s, x);
            const double out = peakDbOf (x, 10.5, 13.0);   // last phrase
            logMessage ("    input peaks " + d2 (in) + " dBFS: gain " + d2 (g.back()) + " dB, output peaks " + d2 (out) + " dBFS");
            expectWithinAbsoluteError (out, normal, 1.0);
        }

        beginTest ("gain range: only as much as needed, at most +6.5 dB up, never below 0 dB");
        {
            AutoGainStage m; m.prepare (fs);
            auto v = voice (13.0, -18.0);
            const auto gm = run (m, v);
            logMessage ("    -18 dBFS voice: gain " + d2 (gm.back()) + " dB (needs +3)");
            expectWithinAbsoluteError (gm.back(), 3.0, 0.3);

            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, -30.0);
            const auto g = run (s, x);
            double highest = -100.0;
            for (auto v2 : g) highest = std::max (highest, v2);
            logMessage ("    -30 dBFS voice: highest gain " + juce::String (highest, 4) + " dB, output peaks " + d2 (peakDbOf (x, 10.5, 13.0)) + " dBFS");
            expectLessOrEqual (highest, AutoGainStage::maxBoostDb + 1.0e-9);
            expectGreaterThan (g.back(), AutoGainStage::maxBoostDb - 0.1);

            AutoGainStage h; h.prepare (fs);
            auto y = voice (13.0, -2.0);
            const auto gh = run (h, y);
            double lowest = 100.0;
            for (auto v2 : gh) lowest = std::min (lowest, v2);
            const double outHot = peakDbOf (y, 0.0, 13.0);
            logMessage ("    -2 dBFS voice: lowest gain " + juce::String (lowest, 4) + " dB, loudest output " + juce::String (outHot, 3) + " dBFS");
            expectGreaterOrEqual (lowest, 0.0, "never below 0 dB");
            expectLessThan (outHot, AutoGainStage::ceilingDb + 0.001, "the ceiling holds the rest");
        }

        beginTest ("shouting (12 dB over the natural voice): peaks at about -12 dBFS, never above -10 dBFS, no clicks");
        {
            AutoGainStage s; s.prepare (fs);
            auto calm = voice (14.0, -26.0);
            run (s, calm);
            const int guardsBefore = s.getGuardCount();
            auto loud = voice (10.5, -14.0, -200.0, 2);
            const auto g = run (s, loud);
            const double lastShout = peakDbOf (loud, 7.0, 9.5), firstSyllables = peakDbOf (loud, 0.0, 0.5);
            const int clicks = clickSamples (loud, dbToLin (peakDbOf (loud, 0.0, 10.5)), 300.0);
            logMessage ("    natural level " + d2 (s.getNaturalLevelDb()) + " dBFS, aim " + d2 (s.getAimDb()) + " dBFS; output peaks of the shouting "
                        + d2 (lastShout) + " dBFS (first 0.5 s: " + d2 (firstSyllables) + "); gain " + d2 (g.back()) + " dB; click samples " + juce::String (clicks)
                        + "; samples the guard had to catch " + juce::String (s.getGuardCount() - guardsBefore));
            expectWithinAbsoluteError (lastShout, shout, 1.0);
            expectLessThan (peakDbOf (loud, 0.0, 10.5), AutoGainStage::ceilingDb + 0.001, "never above -10 dBFS");
            expectEquals (clicks, 0);
        }

        beginTest ("a slightly raised voice (+3 dB) stays a normal voice; between that and shouting the aim slides smoothly");
        {
            AutoGainStage s; s.prepare (fs);
            auto calm = voice (14.0, -20.0);
            run (s, calm);
            auto raised = voice (6.0, -17.0, -200.0, 3);
            run (s, raised);
            const double aimRaised = s.getAimDb();
            auto louder = voice (6.0, -14.0, -200.0, 4);
            run (s, louder);
            const double aimLouder = s.getAimDb();
            logMessage ("    aim at +3 dB: " + d2 (aimRaised) + " dBFS, at +6 dB: " + d2 (aimLouder) + " dBFS");
            expectWithinAbsoluteError (aimRaised, normal, 0.3);
            expect (aimLouder > normal + 0.3 && aimLouder < shout - 0.3, "in between");
        }

        beginTest ("sudden rise of the voice (12 dB at once): the ceiling catches the first syllable, the gain does the rest");
        {
            AutoGainStage s; s.prepare (fs);
            auto calm = voice (14.0, -26.0);
            run (s, calm);
            const int guardsBefore = s.getGuardCount();
            auto jump = voice (3.5, -14.0, -200.0, 5);
            run (s, jump);
            const double first = peakDbOf (jump, 0.0, 0.25), second = peakDbOf (jump, 0.25, 0.5), later = peakDbOf (jump, 0.75, 2.5);
            logMessage ("    first syllable " + d2 (first) + " dBFS, second " + d2 (second) + " dBFS, from 0.75 s on " + d2 (later) + " dBFS (aim " + d2 (s.getAimDb())
                        + "); click samples " + juce::String (clickSamples (jump, dbToLin (first), 300.0)) + ", guard " + juce::String (s.getGuardCount() - guardsBefore));
            expectEquals (clickSamples (jump, dbToLin (first), 300.0), 0, "look-ahead: no clicks even at the first syllable");
            expectLessThan (first, AutoGainStage::ceilingDb + 0.001);
            expectLessThan (second, AutoGainStage::ceilingDb - 0.5, "second syllable already under the ceiling's knee region");
            expectWithinAbsoluteError (later, shout, 0.7);
        }

        beginTest ("after shouting, normal speech is back near -15 dBFS within a few seconds");
        {
            AutoGainStage s; s.prepare (fs);
            auto calm = voice (14.0, -20.0);
            run (s, calm);
            auto loud = voice (7.0, -8.0, -200.0, 6);
            run (s, loud);
            auto back = voice (10.5, -20.0, -200.0, 7);
            run (s, back);
            const double second = peakDbOf (back, 3.5, 6.0), third = peakDbOf (back, 7.0, 9.5);
            logMessage ("    normal phrases after the shouting: " + d2 (peakDbOf (back, 0.0, 2.5)) + " / " + d2 (second) + " / " + d2 (third) + " dBFS");
            expectWithinAbsoluteError (second, normal, 2.0);
            expectWithinAbsoluteError (third, normal, 1.0);
        }

        beginTest ("long silence: the last gain is kept exactly, the signal is not gated (output = input x gain)");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, -28.0, -62.0, 8);
            run (s, x);
            const double held = s.getTargetGainDb(), heldApplied = s.getAppliedGainDb();
            auto quiet = noiseOnly (20.0, -62.0, 9);
            const auto in = quiet;
            const auto g = run (s, quiet);
            bool unchanged = true;
            for (auto v : g) unchanged = unchanged && v == held;
            double worstRatioErr = 0.0;
            const double expectedGain = dbToLin (heldApplied);
            const auto L = (size_t) s.getLatencySamples();   // the output is the input of 1.5 ms earlier
            for (size_t i = L; i < in.size(); ++i)
                if (std::abs (in[i - L]) > 1.0e-4f)
                    worstRatioErr = std::max (worstRatioErr, std::abs ((double) quiet[i] / in[i - L] - expectedGain) / expectedGain);
            auto zeros = std::vector<float> ((size_t) (10.0 * fs), 0.0f);
            const auto gz = run (s, zeros);
            for (auto v : gz) unchanged = unchanged && v == held;
            logMessage ("    gain " + d2 (held) + " dB kept through 20 s room noise + 10 s digital silence; largest deviation of output/input from it "
                        + juce::String (worstRatioErr * 100.0, 6) + " %");
            expect (unchanged, "gain unchanged");
            expectLessThan (worstRatioErr, 1.0e-5, "no gating, no fading");
        }

        beginTest ("pauses between words and phrases: no gain jumps, breaths and word ends untouched");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (20.0, -20.0, -60.0, 10);
            const auto g = run (s, x);
            double lo = 1.0e9, hi = -1.0e9, step = 0.0;
            for (size_t b = (size_t) (10.0 * fs / block); b < g.size(); ++b)
            {
                lo = std::min (lo, g[b]); hi = std::max (hi, g[b]);
                if (b > 0) step = std::max (step, std::abs (g[b] - g[b - 1]));
            }
            logMessage ("    last 10 s: gain between " + d2 (lo) + " and " + d2 (hi) + " dB, largest change between two blocks " + juce::String (step, 4) + " dB");
            expectLessThan (hi - lo, 0.5, "no pumping");
        }

        beginTest ("speaking again after silence continues from the kept gain and keeps adapting");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, -17.0);
            run (s, x);
            const double held = s.getTargetGainDb();
            auto zeros = std::vector<float> ((size_t) (8.0 * fs), 0.0f);
            run (s, zeros);
            auto again = voice (13.0, -21.0, -200.0, 11);   // 4 dB quieter than before
            const auto g = run (s, again);
            const double out = peakDbOf (again, 10.5, 13.0);
            logMessage ("    kept " + d2 (held) + " dB; after the first speech block " + d2 (g[0]) + " dB; at the end " + d2 (g.back())
                        + " dB, output peaks " + d2 (out) + " dBFS");
            expectWithinAbsoluteError (g[0], held, 0.2);
            expectGreaterThan (g.back(), held + 3.5);
            expectWithinAbsoluteError (out, normal, 1.0);
        }

        beginTest ("natural dynamics inside a phrase are kept (soft syllables stay 6 dB softer)");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, -20.0, -200.0, 12, { 0.0, -6.0 });
            run (s, x);
            const double loudSyl = peakDbOf (x, 10.5, 10.65), softSyl = peakDbOf (x, 10.75, 10.9);
            logMessage ("    loud syllable " + d2 (loudSyl) + " dBFS, soft syllable " + d2 (softSyl) + " dBFS");
            expectWithinAbsoluteError (loudSyl - softSyl, 6.0, 0.3);
        }

        beginTest ("the ceiling: a full-scale phrase right after quiet speech (gain +6.5 dB) never exceeds -10 dBFS");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, -35.0);
            run (s, x);
            const double gainBefore = s.getTargetGainDb();
            auto burst = voice (3.5, 0.0, -200.0, 13);
            run (s, burst);
            const double peak = peakDbOf (burst, 0.0, 3.5);
            logMessage ("    gain before " + d2 (gainBefore) + " dB; output peak of the 0 dBFS phrase " + juce::String (peak, 3) + " dBFS");
            expectLessThan (peak, AutoGainStage::ceilingDb + 0.001);
        }

        beginTest ("normal speech stays below the limiter's knee: not compressed at all");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, -20.0, -200.0, 16, { 0.0, -2.0, -4.0 });
            double deepest = 0.0;
            for (size_t p = 0; p < x.size(); p += (size_t) block)
            {
                float* ch[] = { x.data() + p };
                s.process (ch, 1, block);
                if (p > (size_t) (4.0 * fs)) deepest = std::min (deepest, s.getLimiterGainDb());
            }
            logMessage ("    deepest limiter gain after 4 s: " + juce::String (deepest, 6) + " dB; output peaks " + d2 (peakDbOf (x, 10.5, 13.0)) + " dBFS");
            expectEquals (deepest, 0.0);
        }

        beginTest ("switched off: glides back to exactly 0 dB, then the audio passes bit-exact (1.5 ms later)");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, -20.0);
            run (s, x);
            const double start = s.getTargetGainDb();
            s.setEnabled (false);
            auto y = voice (2.0, -20.0, -200.0, 14);
            const auto g = run (s, y);
            bool monotonic = true;
            for (size_t b = 1; b < g.size(); ++b) monotonic = monotonic && g[b] <= g[b - 1];
            size_t zeroAt = g.size();
            for (size_t b = 0; b < g.size(); ++b) if (g[b] == 0.0) { zeroAt = b; break; }
            logMessage ("    from " + d2 (start) + " dB to 0 dB in " + juce::String ((double) zeroAt * block / fs * 1000.0, 0) + " ms");
            expect (monotonic);
            expectLessThan ((double) zeroAt * block / fs, start / AutoGainStage::offReturnDbPerSec + 0.05);
            auto z = voice (1.0, -6.0, -60.0, 15);
            const auto orig = z;
            run (s, z);
            const auto L = (size_t) s.getLatencySamples();
            bool exact = true;
            for (size_t i = L; i < z.size(); ++i) exact = exact && juce::exactlyEqual (z[i], orig[i - L]);
            expect (exact, "off and at 0 dB: not a single bit changed, only delayed");
            expectWithinAbsoluteError (s.getAppliedGainDb(), 0.0, 1.0e-12);
        }

        beginTest ("no clicks while the gain follows smooth level changes");
        {
            std::vector<float> x ((size_t) (12.0 * fs));
            const double levels[] = { -30.0, -12.0, -40.0, -20.0 };
            double amp = dbToLin (levels[0]);
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double want = dbToLin (levels[std::min<size_t> (3, i / (size_t) (3.0 * fs))]);
                amp += (want - amp) * (1.0 - std::exp (-1.0 / (0.05 * fs)));
                x[i] = (float) (amp * std::sin (twoPi * 997.0 * (double) i / fs));
            }
            AutoGainStage s; s.prepare (fs);
            run (s, x);
            const double outPeak = dbToLin (peakDbOf (x, 0.0, 12.0));
            const int outClicks = clickSamples (x, outPeak, 997.0);
            logMessage ("    output peak " + d2 (linToDb (outPeak)) + " dBFS; samples over the click limit " + juce::String (outClicks));
            expectEquals (outClicks, 0);
        }

        beginTest ("the meters show the real gain and peaks");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (12.5, -20.0);
            run (s, x);
            s.takeMeters();
            std::vector<float> tone ((size_t) block * 4);
            for (size_t i = 0; i < tone.size(); ++i) tone[i] = (float) (dbToLin (-20.0) * std::sin (twoPi * 1000.0 * (double) i / fs));
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

        beginTest ("short clicks and bursts (0.5 ms, 12 dB over the voice) do not pull the gain down; the ceiling still catches them");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, -20.0);
            run (s, x);
            const double before = s.getTargetGainDb();
            auto y = voice (7.0, -20.0, -200.0, 21);
            for (double t = 0.1; t < 6.9; t += 0.35)   // a 0.5 ms click of -8 dBFS every 350 ms, in speech and in pauses
            {
                const auto at = (size_t) (t * fs);
                for (size_t k = 0; k < 24 && at + k < y.size(); ++k) y[at + k] += (float) dbToLin (-8.0);
            }
            const auto g = run (s, y);
            double lowest = 100.0;
            for (auto v : g) lowest = std::min (lowest, v);
            const double outPeak = peakDbOf (y, 0.0, 7.0);
            logMessage ("    gain before " + d2 (before) + " dB, lowest while clicking " + d2 (lowest) + " dB; loudest output " + juce::String (outPeak, 3) + " dBFS");
            expectGreaterThan (lowest, before - 0.3, "the gain stays");
            expectLessThan (outPeak, AutoGainStage::ceilingDb + 0.001);
        }

        beginTest ("latency: exactly 1.5 ms at 44.1, 48 and 96 kHz; the output is the input that much later");
        {
            for (double rate : { 44100.0, 48000.0, 96000.0 })
            {
                AutoGainStage s; s.prepare (rate);
                expectEquals (s.getLatencySamples(), (int) std::lround (0.0015 * rate));
            }
            AutoGainStage s; s.prepare (fs);
            s.setEnabled (false);
            std::vector<float> x ((size_t) block * 2, 0.0f);
            x[100] = 0.5f;
            run (s, x);
            const auto L = (size_t) s.getLatencySamples();
            expectEquals (x[100 + L], 0.5f, "impulse arrives exactly 72 samples later");
            float elsewhere = 0.0f;
            for (size_t i = 0; i < x.size(); ++i) if (i != 100 + L) elsewhere = std::max (elsewhere, std::abs (x[i]));
            expectEquals (elsewhere, 0.0f);
        }

        beginTest ("hold 20 ms: a deep loud voice (80 Hz) is limited without ripple");
        {
            AutoGainStage s; s.prepare (fs);
            std::vector<float> x ((size_t) (2.0 * fs));
            for (size_t i = 0; i < x.size(); ++i) x[i] = (float) (dbToLin (-7.0) * std::sin (twoPi * 80.0 * (double) i / fs));
            run (s, x);
            double lo = 1.0, hi = 0.0;
            const auto period = (size_t) (fs / 80.0);
            for (size_t p = (size_t) (1.0 * fs); p + period <= x.size(); p += period)
            {
                float m = 0.0f;
                for (size_t i = p; i < p + period; ++i) m = std::max (m, std::abs (x[i]));
                lo = std::min (lo, (double) m); hi = std::max (hi, (double) m);
            }
            logMessage ("    peaks per cycle between " + juce::String (linToDb (lo), 3) + " and " + juce::String (linToDb (hi), 3) + " dBFS");
            expectLessThan (linToDb (hi) - linToDb (lo), 0.05, "no ripple");
            expectLessThan (linToDb (hi), AutoGainStage::ceilingDb + 0.001);
        }

        beginTest ("stereo: both channels get the same gain");
        {
            AutoGainStage s; s.prepare (fs);
            auto l = voice (6.0, -25.0);
            std::vector<float> r (l.size());
            for (size_t i = 0; i < l.size(); ++i) r[i] = -0.5f * l[i];
            for (size_t p = 0; p < l.size(); p += (size_t) block)
            {
                float* ch[] = { l.data() + p, r.data() + p };
                s.process (ch, 2, block);
            }
            bool same = true;
            for (size_t i = 0; i < l.size(); ++i) same = same && juce::exactlyEqual (r[i], -0.5f * l[i]);
            expect (same);
        }
    }
};
static AutoGainTest autoGainTest;

struct BuiltInAutoGainTest : juce::UnitTest
{
    BuiltInAutoGainTest() : juce::UnitTest ("BuiltInAutoGain") {}

    void runTest() override
    {
        beginTest ("name, channels, on by default, latency = the 1.5 ms look-ahead");
        {
            BuiltInAutoGainProcessor p;
            expectEquals (p.getName(), juce::String::fromUTF8 ("Otomatik Kazanç"));
            expectEquals (p.getTotalNumInputChannels(), 2);
            expectEquals (p.getTotalNumOutputChannels(), 2);
            expect (p.isAutoGainEnabled());
            expectEquals (p.getLatencySamples(), 72, "before prepare: 48 kHz");
            p.prepareToPlay (44100.0, 512);
            expectEquals (p.getLatencySamples(), 66, "44.1 kHz");
            p.prepareToPlay (96000.0, 512);
            expectEquals (p.getLatencySamples(), 144, "96 kHz");
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
