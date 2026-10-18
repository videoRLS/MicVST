// Unit tests for the built-in "Kazanç" (gain) element: DSP core (GainStage), the AudioProcessor
// wrapper (state, channels), the pass-through wiring in the chain and the 24-bit conversion that
// JUCE uses when a device does not take 32-bit float.
// (Test names are ASCII: the runner prints them as Latin-1.)
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/BuiltInGain.h"
#include "audio/GainStage.h"
#include "audio/GraphConnections.h"
#include <atomic>
#include <random>
#include <thread>
#include <vector>

namespace
{
    constexpr double fs = 48000.0;
    const double smootherCoeff = 1.0 - std::exp (-1.0 / (GainStage::smoothingSeconds * fs));
    const double rangeDb = (double) (GainStage::maxDb - GainStage::minDb);   // 42 dB

    std::vector<float> sine (int n, double freqHz, double amp)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * freqHz / fs * i));
        return v;
    }

    // Samples the engine test's click detector would flag: |second difference| above
    // 3 * amplitude * (2 pi f / fs)^2 + 2e-4 (MicVSTEngineTest, clicks()).
    int clickSamples (const std::vector<float>& y, double amp, double freqHz)
    {
        const double limit = 3.0 * amp * std::pow (juce::MathConstants<double>::twoPi * freqHz / fs, 2.0) + 2.0e-4;
        int n = 0;
        for (size_t i = 1; i + 1 < y.size(); ++i)
            if (std::abs ((double) y[i + 1] - 2.0 * y[i] + y[i - 1]) > limit) ++n;
        return n;
    }

    // Largest jump of the second difference of a gain curve, relative to the gain (= a jump in its
    // slope, a "corner"): a smooth glide has none, a straight ramp has one at every start and end.
    double worstCorner (const std::vector<float>& g)
    {
        double worst = 0.0;
        for (size_t i = 3; i < g.size(); ++i)
        {
            const double d2a = (double) g[i] - 2.0 * g[i - 1] + g[i - 2];
            const double d2b = (double) g[i - 1] - 2.0 * g[i - 2] + g[i - 3];
            worst = std::max (worst, std::abs (d2a - d2b) / std::max (1.0e-9, (double) std::abs (g[i - 1])));
        }
        return worst;
    }

    void run (GainStage& g, std::vector<float>& x, int block = 480)
    {
        for (size_t s = 0; s < x.size(); s += (size_t) block)
        {
            float* ch[] = { x.data() + s };
            g.process (ch, 1, (int) std::min<size_t> ((size_t) block, x.size() - s));
        }
    }

    // Slider-like target sequence: a new random value every 720..3840 samples (15..80 ms).
    template <typename Fn>
    void sliderMoves (int totalSamples, unsigned seed, Fn&& atSample)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> value (GainStage::minDb, GainStage::maxDb);
        std::uniform_int_distribution<int> gap (720, 3840);
        for (int s = 0; s < totalSamples; s += gap (rng))
            atSample (s, value (rng));
    }
}

struct GainStageTest : juce::UnitTest
{
    GainStageTest() : juce::UnitTest ("GainStage") {}

    void runTest() override
    {
        beginTest ("range: -30 ... +12 dB, NaN/inf -> 0 dB");
        {
            expectEquals (GainStage::clampDb (20.0f), 12.0f);
            expectEquals (GainStage::clampDb (12.0f), 12.0f);
            expectEquals (GainStage::clampDb (-50.0f), -30.0f);
            expectEquals (GainStage::clampDb (3.7f), 3.7f);
            expectEquals (GainStage::clampDb (std::numeric_limits<float>::quiet_NaN()), 0.0f);
            expectEquals (GainStage::clampDb (std::numeric_limits<float>::infinity()), 0.0f);
            GainStage g; g.setGainDb (99.0f);
            expectEquals (g.getGainDb(), 12.0f, "set above the maximum ends at +12 dB");
        }

        beginTest ("0 dB is bit-exact (block untouched)");
        {
            std::mt19937 rng (7); std::uniform_real_distribution<float> d (-1.0f, 1.0f);
            std::vector<float> x (48000), orig;
            for (auto& v : x) v = d (rng);
            orig = x;
            GainStage g; g.prepare (fs);
            run (g, x);
            expect (x == orig, "0 dB must not change a single bit");
        }

        beginTest ("static gain is exact: -30, -6, +6, +12 dB");
        for (float db : { -30.0f, -6.0f, 6.0f, 12.0f })
        {
            GainStage g; g.setGainDb (db); g.prepare (fs);   // prepared at the value: no glide
            expect (! g.isGliding());
            std::vector<float> x (4800, 0.25f);
            run (g, x);
            const float expected = (float) (0.25 * GainStage::dbToGain (db));
            float worst = 0.0f;
            for (auto v : x) worst = std::max (worst, std::abs (v - expected));
            expectLessOrEqual (worst, std::abs (expected) * 1.0e-6f, "gain " + juce::String (db) + " dB");
        }

        beginTest ("+12 dB on a full-scale sine: 4.0 (+12 dBFS), finite - float has the headroom");
        {
            GainStage g; g.setGainDb (12.0f); g.prepare (fs);
            auto x = sine (48000, 997.0, 1.0);
            run (g, x);
            float peak = 0.0f; bool finite = true;
            for (auto v : x) { peak = std::max (peak, std::abs (v)); finite = finite && std::isfinite (v); }
            expect (finite);
            expectWithinAbsoluteError (peak, (float) GainStage::dbToGain (12.0), 2.0e-3f);
            // AudioEngine::isValidAudio silences blocks above 16.0 (+24 dBFS): one gain stage at
            // the maximum on a full-scale signal stays far below that.
            expectLessThan (peak, 16.0f);
        }

        beginTest ("a change glides: monotonic, 99 % after < 45 ms, then exactly on the target");
        {
            GainStage g; g.prepare (fs);
            g.setGainDb (12.0f);
            std::vector<float> ones (9600, 1.0f);   // 200 ms
            run (g, ones);
            bool monotonic = true;
            for (size_t i = 1; i < ones.size(); ++i) monotonic = monotonic && ones[i] >= ones[i - 1];
            expect (monotonic, "a single change never overshoots or wobbles");
            auto reachedDb = [&] (double ms) { return 20.0 * std::log10 ((double) ones[(size_t) (ms * 48.0)]); };
            logMessage ("    0 -> +12 dB: " + juce::String (reachedDb (5.0), 2) + " dB after 5 ms, " + juce::String (reachedDb (20.0), 2)
                        + " after 20 ms, " + juce::String (reachedDb (40.0), 3) + " after 40 ms, " + juce::String (reachedDb (60.0), 4) + " after 60 ms");
            expectLessThan (reachedDb (5.0), 6.0, "not a jump");
            expectGreaterThan (reachedDb (45.0), 12.0 * 0.99);
            expect (! g.isGliding());
            const float target = (float) GainStage::dbToGain (12.0);
            expectEquals (ones.back(), target, "settled exactly on the target");
            std::vector<float> more (480, 1.0f); run (g, more);
            for (auto v : more) expectEquals (v, target);
        }

        beginTest ("no click: slider moves on a sine vs. the same moves as abrupt steps (control)");
        {
            const double amp = 0.1, f = 997.0;
            auto smooth = sine (96000, f, amp), stepped = smooth;
            std::vector<std::pair<int, float>> moves;
            sliderMoves (96000, 21, [&] (int s, float v) { moves.emplace_back (s, v); });

            GainStage g; g.prepare (fs);
            size_t next = 0;
            for (int s = 0; s < 96000; s += 64)   // the plug-in thread in 64-sample blocks
            {
                while (next < moves.size() && moves[next].first <= s) g.setGainDb (moves[next++].second);
                float* ch[] = { smooth.data() + s };
                g.process (ch, 1, 64);
            }
            double gain = 1.0; next = 0;
            for (int i = 0; i < 96000; ++i)
            {
                while (next < moves.size() && moves[next].first <= i) gain = GainStage::dbToGain (moves[next++].second);
                stepped[(size_t) i] = (float) (stepped[(size_t) i] * gain);
            }
            const int smoothClicks = clickSamples (smooth, amp * 4.0, f), stepClicks = clickSamples (stepped, amp * 4.0, f);
            logMessage ("    " + juce::String ((int) moves.size()) + " slider moves; samples over the engine test's click limit: glide "
                        + juce::String (smoothClicks) + ", abrupt steps " + juce::String (stepClicks));
            expectGreaterThan (stepClicks, 0, "the control must show the steps (detector works)");
            expectEquals (smoothClicks, 0, "gliding gain changes are not clicks");
        }

        beginTest ("no corners in the gain curve, also when the target changes during a glide");
        {
            std::vector<float> curve (96000, 1.0f);
            GainStage g; g.prepare (fs);
            std::vector<std::pair<int, float>> moves;
            sliderMoves (96000, 5, [&] (int s, float v) { moves.emplace_back (s, v); });
            size_t next = 0;
            for (int s = 0; s < 96000; s += 32)
            {
                while (next < moves.size() && moves[next].first <= s) g.setGainDb (moves[next++].second);
                float* ch[] = { curve.data() + s };
                g.process (ch, 1, 32);
            }
            // control: the same moves as straight 40 ms ramps in dB (what a linear smoother does)
            std::vector<float> ramp (96000);
            double db = 0.0, stepDb = 0.0; int left = 0; next = 0;
            for (int i = 0; i < 96000; ++i)
            {
                if (next < moves.size() && moves[next].first <= i) { stepDb = ((double) moves[next++].second - db) / 1920.0; left = 1920; }
                if (left > 0) { db += stepDb; --left; }
                ramp[(size_t) i] = (float) GainStage::dbToGain (db);
            }
            const double glide = worstCorner (curve), straight = worstCorner (ramp);
            logMessage ("    largest slope jump relative to the gain: glide " + juce::String (glide, 7) + ", straight ramps " + juce::String (straight, 7));
            expectGreaterThan (straight, 5.0 * glide, "straight ramps have corners, the glide does not");
            expectLessThan (glide, 1.0e-4);
        }

        beginTest ("fast slider: the gain never jumps between two samples");
        {
            GainStage g; g.prepare (fs);
            std::vector<float> ones (48000, 1.0f);
            std::mt19937 rng (3); std::uniform_real_distribution<float> d (-30.0f, 12.0f);
            for (size_t s = 0; s < ones.size(); s += 64)   // a new value every 64 samples
            {
                g.setGainDb (d (rng));
                float* ch[] = { ones.data() + s };
                g.process (ch, 1, 64);
            }
            double worstStepDb = 0.0;
            for (size_t i = 1; i < ones.size(); ++i)
                worstStepDb = std::max (worstStepDb, std::abs (20.0 * std::log10 ((double) ones[i] / ones[i - 1])));
            const double bound = rangeDb * smootherCoeff;   // each smoother stage moves at most this far per sample
            logMessage ("    largest gain change between two samples: " + juce::String (worstStepDb, 5) + " dB (bound " + juce::String (bound, 5) + " dB)");
            expectLessOrEqual (worstStepDb, bound * 1.001);
        }

        beginTest ("restore (immediate) jumps without a glide; reset() snaps too");
        {
            GainStage g; g.prepare (fs);
            g.setGainDb (6.0f, true);
            std::vector<float> x (480, 1.0f);
            run (g, x);
            expect (! g.isGliding());
            expectEquals (x[0], (float) GainStage::dbToGain (6.0));
            g.setGainDb (-6.0f);
            x.assign (100, 1.0f); run (g, x, 100);
            expect (g.isGliding());
            g.reset();
            expect (! g.isGliding());
            expectWithinAbsoluteError (g.getCurrentGain(), GainStage::dbToGain (-6.0), 1.0e-12);
        }

        beginTest ("stereo: both channels get exactly the same gain curve");
        {
            GainStage g; g.prepare (fs);
            std::vector<float> l (4800, 0.5f), r (4800, -0.5f);
            for (size_t s = 0; s < l.size(); s += 480)
            {
                if (s == 960) g.setGainDb (9.5f);
                float* ch[] = { l.data() + s, r.data() + s };
                g.process (ch, 2, 480);
            }
            bool same = true;
            for (size_t i = 0; i < l.size(); ++i) same = same && juce::exactlyEqual (l[i], -r[i]);
            expect (same);
        }

        beginTest ("sample rate: the glide takes the same time at 44.1, 48 and 96 kHz");
        {
            auto msTo99 = [] (double rate)
            {
                GainStage g; g.prepare (rate); g.setGainDb (12.0f);
                std::vector<float> x ((size_t) rate / 4, 1.0f);
                float* ch[] = { x.data() };
                g.process (ch, 1, (int) x.size());
                size_t i = 0;
                while (i < x.size() && 20.0 * std::log10 ((double) x[i]) < 12.0 * 0.99) ++i;
                return 1000.0 * (double) i / rate;
            };
            const double a = msTo99 (44100.0), b = msTo99 (48000.0), c = msTo99 (96000.0);
            logMessage ("    99 % of a 12 dB change after " + juce::String (a, 2) + " / " + juce::String (b, 2) + " / " + juce::String (c, 2) + " ms");
            expectWithinAbsoluteError (a, b, 0.2);
            expectWithinAbsoluteError (c, b, 0.2);
        }

        beginTest ("concurrent: UI thread moves the slider while the plug-in thread processes");
        {
            GainStage g; g.prepare (fs);
            std::atomic<bool> stop { false };
            std::thread ui ([&]
            {
                std::mt19937 rng (11); std::uniform_real_distribution<float> d (-40.0f, 20.0f);
                while (! stop.load()) { g.setGainDb (d (rng)); std::this_thread::yield(); }
            });
            std::vector<float> block (480);
            bool finite = true, inRange = true, continuous = true;
            float last = 1.0f;
            const double maxRatio = std::pow (10.0, rangeDb * smootherCoeff / 20.0) * 1.0001;
            const auto until = juce::Time::getMillisecondCounterHiRes() + 1500.0;
            int blocks = 0;
            while (juce::Time::getMillisecondCounterHiRes() < until)
            {
                std::fill (block.begin(), block.end(), 1.0f);
                float* ch[] = { block.data() };
                g.process (ch, 1, 480);
                for (auto v : block)
                {
                    finite = finite && std::isfinite (v);
                    inRange = inRange && v >= (float) GainStage::dbToGain (-30.0) * 0.9999f && v <= (float) GainStage::dbToGain (12.0) * 1.0001f;
                    continuous = continuous && v / last <= maxRatio && last / v <= maxRatio;
                    last = v;
                }
                ++blocks;
            }
            stop = true; ui.join();
            logMessage ("    " + juce::String (blocks) + " blocks processed while the gain changed continuously");
            expect (finite); expect (inRange); expect (continuous, "the gain curve never jumps, whatever the other thread does");
        }
    }
};
static GainStageTest gainStageTest;

struct BuiltInGainTest : juce::UnitTest
{
    BuiltInGainTest() : juce::UnitTest ("BuiltInGain") {}

    void runTest() override
    {
        beginTest ("name, channels, no editor, no latency");
        {
            BuiltInGainProcessor p;
            expectEquals (p.getName(), juce::String::fromUTF8 ("Kazanç"));
            expectEquals (p.getTotalNumInputChannels(), 2);
            expectEquals (p.getTotalNumOutputChannels(), 2);
            expect (! p.hasEditor());
            expectEquals (p.getLatencySamples(), 0);
            expectEquals (p.getTailLengthSeconds(), 0.0);
        }

        beginTest ("state round trip (config.xml blob), restored value applies without a glide");
        {
            BuiltInGainProcessor a;
            a.setGainDb (7.3f);
            juce::MemoryBlock blob;
            a.getStateInformation (blob);
            expect (blob.getSize() > 0);

            BuiltInGainProcessor b;
            b.prepareToPlay (fs, 480);
            b.setStateInformation (blob.getData(), (int) blob.getSize());
            expectWithinAbsoluteError (b.getGainDb(), 7.3f, 1.0e-5f);
            juce::AudioBuffer<float> buf (2, 480);
            for (int c = 0; c < 2; ++c) juce::FloatVectorOperations::fill (buf.getWritePointer (c), 1.0f, 480);
            juce::MidiBuffer midi;
            b.processBlock (buf, midi);
            expectWithinAbsoluteError (buf.getSample (0, 0), (float) GainStage::dbToGain (7.3), 1.0e-5f);
            expectWithinAbsoluteError (buf.getSample (1, 479), (float) GainStage::dbToGain (7.3), 1.0e-5f);
        }

        beginTest ("damaged / foreign state is ignored, out-of-range state is clamped");
        {
            BuiltInGainProcessor p;
            p.setGainDb (3.0f);
            const char junk[] = "not a state";
            p.setStateInformation (junk, (int) sizeof (junk));
            expectEquals (p.getGainDb(), 3.0f);
            p.setStateInformation (nullptr, 0);
            expectEquals (p.getGainDb(), 3.0f);

            juce::MemoryBlock other;
            juce::AudioProcessor::copyXmlToBinary (juce::XmlElement ("SomethingElse"), other);
            p.setStateInformation (other.getData(), (int) other.getSize());
            expectEquals (p.getGainDb(), 3.0f);

            juce::XmlElement big ("MicVSTGain"); big.setAttribute ("version", 1); big.setAttribute ("gainDb", 40.0);
            juce::MemoryBlock bigBlob; juce::AudioProcessor::copyXmlToBinary (big, bigBlob);
            p.setStateInformation (bigBlob.getData(), (int) bigBlob.getSize());
            expectEquals (p.getGainDb(), 12.0f, "a hand-edited config cannot exceed +12 dB");
        }
    }
};
static BuiltInGainTest builtInGainTest;

struct GainWiringTest : juce::UnitTest
{
    GainWiringTest() : juce::UnitTest ("GainWiring") {}

    void runTest() override
    {
        const NodeID in { 1 }, out { 2 }, gain { 10 }, m2s { 11 }, gain2 { 12 }, s2m { 13 };

        beginTest ("mono microphone -> Kazanc -> output: stays one channel (engine copies it to both cable channels)");
        {
            NodeChannels g { gain, 2, 2 }; g.passThrough = true;
            auto c = computeChainConnections ({ { in, 0, 1 }, g, { out, 2, 0 } });
            expectEquals ((int) c.size(), 2);
            expect (c[0] == ChannelConnection { in, 0, gain, 0 });
            expect (c[1] == ChannelConnection { gain, 0, out, 0 });   // exactly one channel into the output = mono
        }

        beginTest ("stereo microphone -> Kazanc -> output: two channels");
        {
            NodeChannels g { gain, 2, 2 }; g.passThrough = true;
            auto c = computeChainConnections ({ { in, 0, 2 }, g, { out, 2, 0 } });
            expectEquals ((int) c.size(), 4);
            expect (c[3] == ChannelConnection { gain, 1, out, 1 });
        }

        beginTest ("mono -> Kazanc -> Mono->Stereo -> Kazanc -> output: 1, 1, 2, 2 channels");
        {
            NodeChannels g1 { gain, 2, 2 }, g2 { gain2, 2, 2 }; g1.passThrough = g2.passThrough = true;
            auto c = computeChainConnections ({ { in, 0, 1 }, g1, { m2s, 1, 2 }, g2, { out, 2, 0 } });
            expectEquals ((int) c.size(), 1 + 1 + 2 + 2);
            expect (c[0] == ChannelConnection { in, 0, gain, 0 });
            expect (c[1] == ChannelConnection { gain, 0, m2s, 0 });
            expect (c[2] == ChannelConnection { m2s, 0, gain2, 0 });
            expect (c[3] == ChannelConnection { m2s, 1, gain2, 1 });
            expect (c[4] == ChannelConnection { gain2, 0, out, 0 });
            expect (c[5] == ChannelConnection { gain2, 1, out, 1 });
        }

        beginTest ("stereo -> Stereo->Mono -> Kazanc -> output: mono after the downmix");
        {
            NodeChannels g { gain, 2, 2 }; g.passThrough = true;
            auto c = computeChainConnections ({ { in, 0, 2 }, { s2m, 2, 1 }, g, { out, 2, 0 } });
            expectEquals ((int) c.size(), 2 + 1 + 1);
            expect (c.back() == ChannelConnection { gain, 0, out, 0 });
        }

        beginTest ("no microphone channels: nothing is wired through Kazanc");
        {
            NodeChannels g { gain, 2, 2 }; g.passThrough = true;
            expect (computeChainConnections ({ { in, 0, 0 }, g, { out, 2, 0 } }).empty());
        }
    }
};
static GainWiringTest gainWiringTest;

struct Pcm24ConversionTest : juce::UnitTest
{
    Pcm24ConversionTest() : juce::UnitTest ("Pcm24Conversion") {}

    void runTest() override
    {
        // In shared mode Windows hands MicVST 32-bit float and converts to the cable's 24-bit
        // format itself. Only when a device refuses float does JUCE's WASAPI code convert to 24-bit,
        // with these AudioData converters (float -> int32 -> int24) - checked here: values beyond
        // full scale (too much gain) saturate instead of wrapping around, and the error stays below
        // 1 LSB (JUCE truncates instead of rounding: at most -138 dBFS, far below any microphone's
        // own noise).
        using Src = juce::AudioData::Pointer<juce::AudioData::Float32, juce::AudioData::NativeEndian, juce::AudioData::NonInterleaved, juce::AudioData::Const>;
        using Dst = juce::AudioData::Pointer<juce::AudioData::Int24, juce::AudioData::LittleEndian, juce::AudioData::Interleaved, juce::AudioData::NonConst>;

        auto toInt24 = [] (float v)
        {
            uint8_t bytes[3] = {};
            Dst d (bytes, 1);
            d.convertSamples (Src (&v), 1);
            int32_t x = (int32_t) bytes[0] | ((int32_t) bytes[1] << 8) | ((int32_t) bytes[2] << 16);
            if (x & 0x800000) x -= 0x1000000;   // sign-extend
            return x;
        };

        beginTest ("overs (e.g. +12 dB on a loud voice) clip at full scale, never wrap around");
        {
            const int top = toInt24 (4.0f), bottom = toInt24 (-4.0f);
            logMessage ("    +4.0 -> " + juce::String (top) + ", -4.0 -> " + juce::String (bottom));
            expectGreaterOrEqual (top, 0x7FFFFF - 1);
            expectLessOrEqual (bottom, -0x7FFFFF);
            expectGreaterOrEqual (toInt24 (1.0f), 0x7FFFFF - 1);
            expectLessOrEqual (toInt24 (-1.0f), -0x7FFFFF + 1);
        }

        beginTest ("in range: error below 1 LSB of 24 bit");
        {
            std::mt19937 rng (5); std::uniform_real_distribution<float> d (-0.999f, 0.999f);
            double worst = 0.0;
            for (int i = 0; i < 100000; ++i)
            {
                const float v = d (rng);
                worst = std::max (worst, std::abs ((double) toInt24 (v) - (double) v * 8388608.0));   // full scale = 2^23
            }
            logMessage ("    worst error: " + juce::String (worst, 3) + " LSB");
            expectLessThan (worst, 1.0);
        }
    }
};
static Pcm24ConversionTest pcm24ConversionTest;
