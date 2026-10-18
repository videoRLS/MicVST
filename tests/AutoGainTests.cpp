// Basic tests for the built-in "Otomatik Kazanç" (Auto Gain): direction and limits of the gain,
// silence handling, switching off, click-free changes, meters = real values, the processor wrapper.
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
    // 250 ms) and a 1 s pause; peak level = peakDb. pauseNoiseDb: RMS of white noise in the pauses
    // and under the voice (-200 = digital silence).
    std::vector<float> voice (double seconds, double peakDb, double pauseNoiseDb = -200.0, unsigned seed = 1)
    {
        std::vector<float> x ((size_t) (seconds * fs));
        std::mt19937 rng (seed);
        std::normal_distribution<double> noise (0.0, 1.0);
        const double a = dbToLin (peakDb), n = pauseNoiseDb > -150.0 ? dbToLin (pauseNoiseDb) : 0.0;
        for (size_t i = 0; i < x.size(); ++i)
        {
            const double t = (double) i / fs;
            const double inPhrase = std::fmod (t, 3.5);
            double env = 0.0;
            if (inPhrase < 2.5)
            {
                const double inSyllable = std::fmod (inPhrase, 0.25);
                if (inSyllable < 0.15) env = 0.5 - 0.5 * std::cos (twoPi * inSyllable / 0.15);
            }
            x[i] = (float) (a * env * std::sin (twoPi * 300.0 * t) + n * noise (rng));
        }
        return x;
    }

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

    double peakDbOf (const std::vector<float>& y, size_t from, size_t to)
    {
        float m = 0.0f;
        for (size_t i = from; i < std::min (to, y.size()); ++i) m = std::max (m, std::abs (y[i]));
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
}

struct AutoGainTest : juce::UnitTest
{
    AutoGainTest() : juce::UnitTest ("AutoGain") {}

    void runTest() override
    {
        // the last phrase of a 12.5 s signal: 10.5 .. 13.0 s
        const size_t lastFrom = (size_t) (10.5 * fs), lastTo = (size_t) (13.0 * fs);
        // test levels relative to the target (-15 dBFS)
        const double T = AutoGainStage::targetPeakDb;
        auto dbfs = [] (double v) { return juce::String (v, 0) + " dBFS"; };

        beginTest ("quiet voice (peaks " + dbfs (T - 5.0) + "): gain rises to about +5 dB, output peaks near " + dbfs (T));
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, T - 5.0);
            const auto g = run (s, x);
            const double out = peakDbOf (x, lastFrom, lastTo);
            logMessage ("    gain after 2 s " + juce::String (g[200], 2) + " dB, at the end " + juce::String (g.back(), 2)
                        + " dB; output peak of the last phrase " + juce::String (out, 2) + " dBFS");
            expectGreaterThan (g[200], 0.5, "the gain goes up");
            expectWithinAbsoluteError (g.back(), 5.0, 1.0);
            expectWithinAbsoluteError (out, AutoGainStage::targetPeakDb, 1.5);
        }

        beginTest ("very quiet voice (peaks " + dbfs (T - 15.0) + "): boost stops at +" + juce::String (AutoGainStage::maxBoostDb, 0) + " dB");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, T - 15.0);
            const auto g = run (s, x);
            double highest = -100.0;
            for (auto v : g) highest = std::max (highest, v);
            logMessage ("    highest gain " + juce::String (highest, 4) + " dB");
            expectLessOrEqual (highest, AutoGainStage::maxBoostDb + 1.0e-9);
            expectGreaterThan (g.back(), AutoGainStage::maxBoostDb - 0.1);
        }

        beginTest ("loud voice (peaks " + dbfs (T + 5.0) + "): gain goes down to about -5 dB, output near " + dbfs (T));
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, T + 5.0);
            const auto g = run (s, x);
            const double out = peakDbOf (x, lastFrom, lastTo);
            logMessage ("    gain at the end " + juce::String (g.back(), 2) + " dB, output peak " + juce::String (out, 2) + " dBFS");
            expectWithinAbsoluteError (g.back(), -5.0, 1.0);
            expectWithinAbsoluteError (out, AutoGainStage::targetPeakDb, 1.5);
        }

        beginTest ("shouting near full scale (peaks -1 dBFS): the cut stops at -12 dB");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, -1.0);
            const auto g = run (s, x);
            double lowest = 100.0;
            for (auto v : g) lowest = std::min (lowest, v);
            logMessage ("    lowest gain " + juce::String (lowest, 4) + " dB, output peak " + juce::String (peakDbOf (x, lastFrom, lastTo), 2) + " dBFS");
            expectGreaterOrEqual (lowest, AutoGainStage::maxCutDb - 1.0e-9);
        }

        beginTest ("voice already at the target (" + dbfs (T) + "): practically no gain");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, T);
            const auto g = run (s, x);
            double worst = 0.0;
            for (auto v : g) worst = std::max (worst, std::abs (v));
            logMessage ("    largest |gain| " + juce::String (worst, 3) + " dB");
            expectLessThan (worst, 1.0);
        }

        beginTest ("shouting after quiet speech: the gain comes down quickly, without clicks");
        {
            AutoGainStage s; s.prepare (fs);
            auto quiet = voice (10.5, T - 10.0);
            const auto g0 = run (s, quiet);
            const double before = g0.back();
            const double shoutDb = -3.0, needed = std::max (AutoGainStage::maxCutDb, T - shoutDb);
            auto shout = voice (3.5, shoutDb, -200.0, 7);   // starts with a syllable at t = 0
            const auto g1 = run (s, shout);
            size_t blocksToTarget = g1.size();
            for (size_t b = 0; b < g1.size(); ++b)
                if (g1[b] <= needed + AutoGainStage::fastZoneDb) { blocksToTarget = b + 1; break; }
            const int clicks = clickSamples (shout, dbToLin (shoutDb + before), 300.0);
            logMessage ("    gain before " + juce::String (before, 2) + " dB; within 4 dB of the needed " + juce::String (needed, 0) + " dB after "
                        + juce::String ((double) blocksToTarget * block / fs * 1000.0, 0) + " ms; end " + juce::String (g1.back(), 2)
                        + " dB; samples over the click limit " + juce::String (clicks));
            expectLessThan ((double) blocksToTarget * block / fs, 0.25);
            expectEquals (clicks, 0);
        }

        beginTest ("silence and quiet room noise hold the gain (no creeping up)");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (10.5, T - 10.0);
            run (s, x);
            const double held = s.getTargetGainDb();
            std::vector<float> silence ((size_t) (5.0 * fs), 0.0f);
            run (s, silence);
            expectEquals (s.getTargetGainDb(), held, "digital silence: unchanged");
            auto room = voice (5.0, -200.0, -70.0, 3);   // noise only, -70 dBFS RMS
            run (s, room);
            expectEquals (s.getTargetGainDb(), held, "room noise: unchanged");
            logMessage ("    held at " + juce::String (held, 2) + " dB through 5 s silence + 5 s noise");
        }

        beginTest ("loud room noise alone (-45 dBFS RMS) does not count as speech");
        {
            AutoGainStage s; s.prepare (fs);
            auto room = voice (10.0, -200.0, -45.0, 5);
            const auto g = run (s, room);
            double worst = 0.0;
            for (auto v : g) worst = std::max (worst, std::abs (v));
            logMessage ("    largest |gain| " + juce::String (worst, 3) + " dB, noise floor estimate " + juce::String (s.getNoiseFloorDb(), 1) + " dBFS");
            expectLessThan (worst, 0.5);
        }

        beginTest ("speech over room noise still raises the gain");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (13.0, T - 12.0, -60.0, 9);
            const auto g = run (s, x);
            logMessage ("    gain at the end " + juce::String (g.back(), 2) + " dB");
            expectGreaterThan (g.back(), AutoGainStage::maxBoostDb - 1.0);
        }

        beginTest ("switched off: glides back to exactly 0 dB, then the audio passes bit-exact");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (10.5, T - 10.0);
            run (s, x);
            const double start = s.getTargetGainDb();
            s.setEnabled (false);
            auto y = voice (2.0, T - 10.0, -200.0, 11);
            const auto g = run (s, y);
            bool monotonic = true;
            for (size_t b = 1; b < g.size(); ++b) monotonic = monotonic && g[b] <= g[b - 1];
            size_t zeroAt = g.size();
            for (size_t b = 0; b < g.size(); ++b) if (g[b] == 0.0) { zeroAt = b; break; }
            logMessage ("    from " + juce::String (start, 2) + " dB to 0 dB in " + juce::String ((double) zeroAt * block / fs * 1000.0, 0) + " ms");
            expect (monotonic);
            expectLessThan ((double) zeroAt * block / fs, start / AutoGainStage::offReturnDbPerSec + 0.05);
            auto z = voice (1.0, -16.0, -60.0, 13);
            const auto orig = z;
            run (s, z);
            expect (z == orig, "off and at 0 dB: not a single bit changed");
            expectWithinAbsoluteError (s.getAppliedGainDb(), 0.0, 1.0e-12);
        }

        beginTest ("no clicks while the gain adapts to smooth level changes");
        {
            // sine at 997 Hz whose level glides (50 ms ramps) between -30, -12 and -2 dBFS every 3 s
            std::vector<float> x ((size_t) (12.0 * fs));
            const double levels[] = { -30.0, -12.0, -2.0, -20.0 };
            double amp = dbToLin (levels[0]);
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double want = dbToLin (levels[std::min<size_t> (3, i / (size_t) (3.0 * fs))]);
                amp += (want - amp) * (1.0 - std::exp (-1.0 / (0.05 * fs)));
                x[i] = (float) (amp * std::sin (twoPi * 997.0 * (double) i / fs));
            }
            const auto in = x;
            AutoGainStage s; s.prepare (fs);
            run (s, x);
            const double outPeak = std::pow (10.0, peakDbOf (x, 0, x.size()) / 20.0);
            const int inClicks = clickSamples (in, outPeak, 997.0), outClicks = clickSamples (x, outPeak, 997.0);
            logMessage ("    output peak " + juce::String (linToDb (outPeak), 2) + " dBFS; samples over the click limit: input "
                        + juce::String (inClicks) + ", output " + juce::String (outClicks));
            expectEquals (outClicks, 0);
        }

        beginTest ("the meters show the real gain and peaks");
        {
            AutoGainStage s; s.prepare (fs);
            auto x = voice (12.5, -20.0);
            run (s, x);
            s.takeMeters();   // clear
            // one steady block of a sine at -20 dBFS: measure what really happens to it
            std::vector<float> tone ((size_t) block * 4);
            for (size_t i = 0; i < tone.size(); ++i) tone[i] = (float) (dbToLin (-20.0) * std::sin (twoPi * 1000.0 * (double) i / fs));
            const auto in = tone;
            run (s, tone);
            const auto m = s.takeMeters();
            const double realGain = linToDb (std::abs ((double) tone.back() / in.back()));
            logMessage ("    meter gain " + juce::String (m.gainDb, 3) + " dB, real gain of the last sample " + juce::String (realGain, 3)
                        + " dB; peaks in " + juce::String (m.inPeakDb, 2) + " / out " + juce::String (m.outPeakDb, 2) + " dBFS");
            expectWithinAbsoluteError ((double) m.gainDb, realGain, 0.05);
            expectWithinAbsoluteError ((double) m.inPeakDb, -20.0, 0.1);
            expectWithinAbsoluteError ((double) m.outPeakDb, peakDbOf (tone, 0, tone.size()), 0.01);
        }

        beginTest ("stereo: both channels get the same gain");
        {
            AutoGainStage s; s.prepare (fs);
            auto l = voice (6.0, -16.0);
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
        beginTest ("name, channels, on by default, no latency");
        {
            BuiltInAutoGainProcessor p;
            expectEquals (p.getName(), juce::String::fromUTF8 ("Otomatik Kazanç"));
            expectEquals (p.getTotalNumInputChannels(), 2);
            expectEquals (p.getTotalNumOutputChannels(), 2);
            expect (p.isAutoGainEnabled());
            expectEquals (p.getLatencySamples(), 0);
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
