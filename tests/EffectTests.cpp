// Unit tests for the built-in effects (src/audio/BuiltInEffects.h, src/audio/fx/*): frequency
// responses against their design targets, latency (measured = reported), state round trip, damaged
// state, no click on any parameter change, broken input, mono/stereo, bit-exact transparency where
// promised, the limiter's ceiling (true peak), de-esser, saturation (harmonics, aliasing), LUFS
// meter against EBU Tech 3341/3342 cases, and the wiring in a real AudioProcessorGraph.
// (Test names are ASCII: the runner prints them as Latin-1.)
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/BuiltInEffects.h"
#include "audio/GraphConnections.h"
#include "audio/PluginChain.h"
#include "audio/fx/EffectFactories.h"
#include "audio/fx/FilterEffect.h"
#include "audio/fx/MeterEffects.h"
#include "SpeechSynth.h"
#include <cmath>
#include <map>
#include <random>
#include <vector>

namespace
{
    constexpr double twoPi = 6.283185307179586;
    using Buf = std::vector<std::vector<float>>;   // [channel][sample]

    Buf makeBuf (int channels, size_t n) { return Buf ((size_t) channels, std::vector<float> (n, 0.0f)); }

    std::vector<float> sine (size_t n, double fs, double f, double amp, double phase = 0.0)
    {
        std::vector<float> v (n);
        for (size_t i = 0; i < n; ++i) v[i] = (float) (amp * std::sin (twoPi * f / fs * (double) i + phase));
        return v;
    }

    std::unique_ptr<BuiltInEffect> make (const char* id, double fs = 48000.0, int block = 480, int channels = 2)
    {
        auto e = builtin::create (id);
        e->setActiveChannels (channels);
        e->setPlayConfigDetails (2, 2, fs, block);
        e->prepareToPlay (fs, block);
        return e;
    }

    // Runs the effect over the buffer (in place) in blocks; the buffer may have 1 or 2 channels.
    void run (BuiltInEffect& e, Buf& x, int block = 480)
    {
        const size_t n = x[0].size();
        juce::AudioBuffer<float> b (2, block);
        juce::MidiBuffer midi;
        for (size_t s = 0; s < n; s += (size_t) block)
        {
            const int len = (int) std::min<size_t> ((size_t) block, n - s);
            b.setSize (2, len, false, false, true);
            b.clear();
            for (size_t c = 0; c < x.size(); ++c) b.copyFrom ((int) c, 0, x[c].data() + s, len);
            e.processBlock (b, midi);
            for (size_t c = 0; c < x.size(); ++c) std::copy (b.getReadPointer ((int) c), b.getReadPointer ((int) c) + len, x[c].data() + s);
        }
    }

    double rms (const std::vector<float>& v, size_t from, size_t to)
    {
        double s = 0.0;
        for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
        return std::sqrt (s / (double) std::max<size_t> (1, to - from));
    }
    double db (double g) { return 20.0 * std::log10 (std::max (1.0e-12, g)); }

    // Steady-state gain (dB) of the effect for a sine at f (mono-identical stereo).
    double measureGainDb (BuiltInEffect& e, double fs, double f, double amp = 0.05, double minSeconds = 1.0)
    {
        e.reset();
        const size_t n = (size_t) (fs * std::max (minSeconds, 40.0 / f));   // >= 1 s, >= 40 periods
        Buf x { sine (n, fs, f, amp), sine (n, fs, f, amp) };
        run (e, x);
        const size_t from = n / 2;
        // whole periods for the RMS
        const size_t periods = (size_t) std::floor ((double) (n - from) * f / fs);
        const size_t to = from + (size_t) std::llround ((double) periods * fs / f);
        return db (rms (x[0], from, to) / (amp / std::sqrt (2.0)));
    }

    // Clicks as the engine test counts them, relative to the steady signal: samples whose second
    // difference exceeds 3 x the largest one of the steady parts + 2e-4.
    struct ClickResult { int clicks = 0; double worst = 0.0, limit = 0.0; };
    ClickResult clicksAround (const std::vector<float>& y, size_t changeAt, size_t window, size_t steadyLen)
    {
        auto d2max = [&] (size_t a, size_t b)
        {
            double m = 0.0;
            for (size_t i = std::max<size_t> (a, 1); i + 1 < b && i + 1 < y.size(); ++i)
                m = std::max (m, std::abs ((double) y[i + 1] - 2.0 * y[i] + y[i - 1]));
            return m;
        };
        const double steady = std::max (d2max (changeAt - steadyLen, changeAt),
                                         d2max (changeAt + window, changeAt + window + steadyLen));
        ClickResult r;
        r.limit = 3.0 * steady + 2.0e-4;
        for (size_t i = changeAt; i + 1 < changeAt + window && i + 1 < y.size(); ++i)
        {
            const double d = std::abs ((double) y[i + 1] - 2.0 * y[i] + y[i - 1]);
            r.worst = std::max (r.worst, d);
            if (d > r.limit) ++r.clicks;
        }
        return r;
    }

    // True peak of a signal measured independently of the effects' detector: 16x oversampling with
    // a 128-tap windowed sinc per phase.
    double truePeakDb (const std::vector<float>& v)
    {
        constexpr int phases = 16, half = 64;
        double m = 0.0;
        for (float s : v) m = std::max (m, (double) std::abs (s));
        for (size_t i = (size_t) half; i + (size_t) half < v.size(); ++i)
            for (int ph = 1; ph < phases; ++ph)
            {
                const double t0 = (double) ph / phases;
                double y = 0.0;
                for (int k = -half + 1; k <= half; ++k)
                {
                    const double t = t0 - k;
                    const double w = 0.5 + 0.5 * std::cos (3.141592653589793 * t / (half + 1));
                    y += v[i + (size_t) k] * std::sin (3.141592653589793 * t) / (3.141592653589793 * t) * w;
                }
                m = std::max (m, std::abs (y));
            }
        return db (m);
    }

    // Lag (samples) of the largest cross-correlation of y against x, searched in [0, maxLag].
    int bestLag (const std::vector<float>& x, const std::vector<float>& y, int maxLag)
    {
        int best = 0;
        double bestV = -1.0e300;
        for (int lag = 0; lag <= maxLag; ++lag)
        {
            double s = 0.0;
            for (size_t i = (size_t) maxLag; i < x.size(); ++i) s += (double) x[i - (size_t) lag] * y[i];
            if (s > bestV) { bestV = s; best = lag; }
        }
        return best;
    }

    std::vector<float> noise (size_t n, double amp, unsigned seed)
    {
        speech::Rng r (seed);
        std::vector<float> v (n);
        for (auto& s : v) s = (float) (amp * r.normal());
        return v;
    }

    std::vector<float> speechSignal (double seconds, double peakDb, unsigned seed, double fs = 48000.0)
    {
        speech::Params p;
        p.fs = fs;
        p.peakDb = peakDb;
        p.seed = seed;
        p.noiseDb = -70.0;
        return speech::generate (seconds, p).x;
    }

    bool allFinite (const Buf& b)
    {
        for (auto& c : b) for (float s : c) if (! std::isfinite (s)) return false;
        return true;
    }
    float maxAbs (const Buf& b, size_t from = 0)
    {
        float m = 0.0f;
        for (auto& c : b) for (size_t i = from; i < c.size(); ++i) m = std::max (m, std::abs (c[i]));
        return m;
    }
}

//==============================================================================
struct EffectBasicsTest : juce::UnitTest
{
    EffectBasicsTest() : juce::UnitTest ("Effects / registry, names, parameters, state") {}

    void runTest() override
    {
        beginTest ("24 effects, unique ids, all create, names and descriptions in Turkish");
        {
            const auto& list = builtin::effects();
            expectEquals ((int) list.size(), 24);
            std::map<juce::String, int> seen;
            for (auto& i : list)
            {
                expect (++seen[i.id] == 1, juce::String ("duplicate id ") + i.id);
                expect (juce::String (i.id).startsWith ("builtin:"));
                expect (juce::String (i.group).startsWith ("Dahili - "));
                auto e = builtin::create (i.id);
                expect (e != nullptr, i.id);
                expect (e->getName() == juce::String::fromUTF8 (i.name));
                expect (juce::String::fromUTF8 (i.description).length() > 20);
                expect (e->hasEditor());
                expectEquals (e->getTotalNumInputChannels(), 2);
                expectEquals (e->getTotalNumOutputChannels(), 2);
                std::map<juce::String, int> pids;
                for (auto* p : e->getEffectParameters())
                    expect (++pids[p->getParameterID()] == 1, juce::String (i.id) + " duplicate parameter " + p->getParameterID());
                // never a gate or a compressor
                expect (! juce::String (i.id).containsIgnoreCase ("gate") && ! juce::String (i.id).containsIgnoreCase ("comp"));
            }
            expect (builtin::create ("builtin:doesnotexist") == nullptr);
            expect (! builtin::isEffectId ("builtin:gain"));   // the older built-ins keep their own code
            expect (! builtin::isEffectId ("C:/x.vst3"));
        }

        beginTest ("state round trip: every parameter of every effect (random values)");
        {
            std::mt19937 rng (7);
            std::uniform_real_distribution<float> u (0.0f, 1.0f);
            for (auto& i : builtin::effects())
                for (int round = 0; round < 5; ++round)
                {
                    auto a = builtin::create (i.id);
                    for (auto* p : a->getEffectParameters()) p->setValueNotifyingHost (u (rng));
                    juce::MemoryBlock mb;
                    a->getStateInformation (mb);
                    auto b = builtin::create (i.id);
                    b->setStateInformation (mb.getData(), (int) mb.getSize());
                    for (auto* p : a->getEffectParameters())
                    {
                        auto* q = b->findParameter (p->getParameterID());
                        expect (q != nullptr);
                        // what the parameter really holds (a switch: 0 or 1, a list: an entry)
                        const float want = p->convertTo0to1 (p->convertFrom0to1 (p->getValue()));
                        if (q != nullptr)
                            expectWithinAbsoluteError (q->convertTo0to1 (q->convertFrom0to1 (q->getValue())), want, 1.0e-6f,
                                                       juce::String (i.id) + " " + p->getParameterID());
                    }
                    // state survives config.xml's base64 path too
                    juce::MemoryBlock back;
                    back.fromBase64Encoding (mb.toBase64Encoding());
                    expect (back == mb);
                }
        }

        beginTest ("damaged or foreign state keeps the current values");
        {
            auto e = builtin::create ("builtin:presence");
            e->setParam ("gain", 5.0f);
            const float before = e->getParam ("gain");
            juce::MemoryBlock junk;
            for (int k = 0; k < 64; ++k) junk.append ("\x13\x37", 2);
            e->setStateInformation (junk.getData(), (int) junk.getSize());
            expectWithinAbsoluteError (e->getParam ("gain"), before, 1.0e-4f);
            auto other = builtin::create ("builtin:body");
            other->setParam ("gain", -4.0f);
            juce::MemoryBlock mb;
            other->getStateInformation (mb);
            e->setStateInformation (mb.getData(), (int) mb.getSize());   // body's state: not presence's
            expectWithinAbsoluteError (e->getParam ("gain"), before, 1.0e-4f);
            // partial state: only one of two parameters -> the other keeps its value
            juce::XmlElement x ("MicVSTEffect");
            x.setAttribute ("id", "builtin:presence");
            auto* pe = x.createNewChildElement ("param");
            pe->setAttribute ("id", "freq");
            pe->setAttribute ("value", 3000.0);
            auto* bad = x.createNewChildElement ("param");
            bad->setAttribute ("id", "gain");
            bad->setAttribute ("value", "nan");
            expect (e->applyStateXml (x));
            expectWithinAbsoluteError (e->getParam ("freq"), 3000.0f, 1.0f);
            expectWithinAbsoluteError (e->getParam ("gain"), before, 1.0e-4f);
            // out of range values are clamped
            pe->setAttribute ("value", 1.0e9);
            e->applyStateXml (x);
            expectWithinAbsoluteError (e->getParam ("freq"), 6000.0f, 1.0f);
        }

        beginTest ("defaults: nothing enabled by itself; reset to defaults");
        {
            auto e = builtin::create ("builtin:mute");
            expect (e->getParam ("muted") < 0.5f);   // adding the mute element never silences
            auto l = builtin::create ("builtin:lowcut");
            expectWithinAbsoluteError (l->getParam ("freq"), 75.0f, 0.5f);
            expectEquals ((int) l->getParam ("slope"), 1);   // 24 dB/oct
            l->setParam ("freq", 120.0f);
            l->resetToDefaults();
            expectWithinAbsoluteError (l->getParam ("freq"), 75.0f, 0.5f);
        }

        beginTest ("Turkish value texts");
        {
            using U = BuiltInEffect::Unit;
            expectEquals (builtin::formatValue (75.0f, U::hertz), juce::String ("75 Hz"));
            expectEquals (builtin::formatValue (4000.0f, U::hertz), juce::String::fromUTF8 ("4,00 kHz"));
            expectEquals (builtin::formatValue (16000.0f, U::hertz), juce::String::fromUTF8 ("16,0 kHz"));
            expectEquals (builtin::formatValue (2.5f, U::decibel), juce::String::fromUTF8 ("+2,5 dB"));
            expectEquals (builtin::formatValue (-0.01f, U::decibel), juce::String::fromUTF8 ("0,0 dB"));
            expectEquals (builtin::formatValue (-3.0f, U::decibel), juce::String::fromUTF8 ("-3,0 dB"));
            expectEquals (builtin::formatValue (3.0f, U::dbAmount), juce::String::fromUTF8 ("3,0 dB"));
            expectEquals (builtin::formatValue (30.0f, U::percent), juce::String ("%30"));
            expectEquals (builtin::formatValue (250.0f, U::milliseconds), juce::String ("250 ms"));
            expectEquals (builtin::formatValue (12.5f, U::milliseconds), juce::String::fromUTF8 ("12,5 ms"));
            expectWithinAbsoluteError (builtin::parseValue ("4,5 kHz", U::hertz), 4500.0f, 0.01f);
            expectWithinAbsoluteError (builtin::parseValue ("-3,5", U::decibel), -3.5f, 0.001f);
            expectWithinAbsoluteError (builtin::parseValue ("%40", U::percent), 40.0f, 0.001f);
        }
    }
};
static EffectBasicsTest effectBasicsTest;

//==============================================================================
struct EffectRobustnessTest : juce::UnitTest
{
    EffectRobustnessTest() : juce::UnitTest ("Effects / every effect: speech, random settings, broken input, mono/stereo") {}

    void runTest() override
    {
        const auto voice = speechSignal (6.0, -20.0, 3);

        beginTest ("speech through every effect (defaults and 20 random settings): finite, bounded");
        {
            std::mt19937 rng (11);
            std::uniform_real_distribution<float> u (0.0f, 1.0f);
            for (auto& i : builtin::effects())
                for (int round = 0; round < 21; ++round)
                {
                    auto e = make (i.id);
                    if (round > 0) for (auto* p : e->getEffectParameters()) p->setValueNotifyingHost (u (rng));
                    Buf x { voice, voice };
                    for (size_t k = 0; k < x[1].size(); ++k) x[1][k] *= 0.7f;
                    run (*e, x, 480);
                    expect (allFinite (x), i.id);
                    // the input peaks near -14 dBFS; the strongest settings (saturation, EQ +18 dB,
                    // echo feedback) must still stay far from the engine's +24 dBFS invalid mark
                    // (Parametrik EQ may stack four +18 dB bands: +36 dB - checked for finite output only)
                    expectLessThan (maxAbs (x), juce::String (i.id) == "builtin:parameq" ? 1000.0f : 8.0f,
                                    juce::String (i.id) + " round " + juce::String (round));
                    expectEquals ((int) e->getRepairedOutputBlocks(), 0, i.id);
                }
        }

        beginTest ("broken input (NaN, inf, +30 dBFS) is passed untouched and never enters the effect");
        {
            for (auto& i : builtin::effects())
            {
                auto e = make (i.id);
                juce::AudioBuffer<float> b (2, 480);
                juce::MidiBuffer midi;
                for (int round = 0; round < 3; ++round)
                {
                    for (int k = 0; k < 4; ++k)   // normal audio
                    {
                        for (int c = 0; c < 2; ++c)
                            for (int s = 0; s < 480; ++s) b.setSample (c, s, 0.1f * (float) std::sin (0.05 * (s + 480 * k)));
                        e->processBlock (b, midi);
                    }
                    for (int c = 0; c < 2; ++c) for (int s = 0; s < 480; ++s) b.setSample (c, s, 0.1f);
                    b.setSample (0, 100, round == 0 ? std::numeric_limits<float>::quiet_NaN()
                                                    : round == 1 ? std::numeric_limits<float>::infinity() : 31.0f);
                    juce::AudioBuffer<float> copy (b);
                    e->processBlock (b, midi);
                    bool same = true;
                    for (int c = 0; c < 2; ++c)
                        for (int s = 0; s < 480; ++s)
                        {
                            const float a = b.getSample (c, s), o = copy.getSample (c, s);
                            same = same && (a == o || (std::isnan (a) && std::isnan (o)));
                        }
                    expect (same, juce::String (i.id) + ": broken block passed untouched");
                    for (int k = 0; k < 40; ++k)   // valid again: clean output
                    {
                        for (int c = 0; c < 2; ++c)
                            for (int s = 0; s < 480; ++s) b.setSample (c, s, 0.1f * (float) std::sin (0.05 * s));
                        e->processBlock (b, midi);
                        bool finite = true;
                        for (int c = 0; c < 2; ++c) for (int s = 0; s < 480; ++s) finite = finite && std::isfinite (b.getSample (c, s));
                        expect (finite, i.id);
                    }
                }
                expectEquals ((int) e->getRejectedInputBlocks(), 3, i.id);
                expectEquals ((int) e->getRepairedOutputBlocks(), 0, i.id);
            }
        }

        beginTest ("channel-neutral effects: mono (1 channel) = left of the same stereo; stereo effects on L=R");
        {
            for (auto& i : builtin::effects())
            {
                if (i.stereo) continue;
                auto mono = make (i.id, 48000.0, 480, 1);
                auto st = make (i.id, 48000.0, 480, 2);
                Buf xm { voice };
                Buf xs { voice, voice };
                run (*mono, xm);
                run (*st, xs);
                double worst = 0.0;
                for (size_t k = 0; k < voice.size(); ++k)
                    worst = std::max ({ worst, std::abs ((double) xm[0][k] - xs[0][k]), std::abs ((double) xs[1][k] - xs[0][k]) });
                expectLessThan (worst, 1.0e-6, i.id);
            }
        }

        beginTest ("block size does not change the result (32 / 480 / 1024 samples)");
        {
            for (auto& i : builtin::effects())
            {
                Buf ref { voice, voice };
                auto e1 = make (i.id, 48000.0, 1024);
                run (*e1, ref, 1024);
                for (int bs : { 32, 480, 1000 })
                {
                    auto e2 = make (i.id, 48000.0, 1024);
                    Buf x { voice, voice };
                    run (*e2, x, bs);
                    double worst = 0.0;
                    for (size_t k = 0; k < voice.size(); ++k) worst = std::max (worst, std::abs ((double) x[0][k] - ref[0][k]));
                    expectLessThan (worst, 2.0e-5, juce::String (i.id) + " block " + juce::String (bs));
                }
            }
        }

        beginTest ("transparent where promised: bit-exact (meters, mute open, sync delay 0, channel tool neutral)");
        {
            for (auto* id : { "builtin:lufs", "builtin:spectrum", "builtin:mute", "builtin:syncdelay", "builtin:channel" })
            {
                auto e = make (id);
                Buf x { voice, noise (voice.size(), 0.05, 5) };
                const Buf in = x;
                run (*e, x);
                expect (x == in, id);
            }
            // EQ at 0 dB / gain-free: within float rounding
            for (auto* id : { "builtin:parameq" })
            {
                auto e = make (id);
                Buf x { voice, voice };
                const Buf in = x;
                run (*e, x);
                double worst = 0.0;
                for (size_t k = 0; k < voice.size(); ++k) worst = std::max (worst, std::abs ((double) x[0][k] - in[0][k]));
                expectLessThan (worst, 1.0e-6, id);
            }
        }

        beginTest ("latency: measured (cross-correlation with noise) = reported, at 44.1 / 48 / 96 kHz");
        {
            for (double fs : { 44100.0, 48000.0, 96000.0 })
                for (auto& i : builtin::effects())
                {
                    const juce::String id (i.id);
                    auto e = make (i.id, fs, 512);
                    if (id == "builtin:syncdelay") e->setParam ("delay", 37.0f);
                    if (id == "builtin:lofi" || id == "builtin:mute") continue;   // band-pass / silence: no defined lag
                    e->prepareToPlay (fs, 512);
                    const int reported = e->getLatencySamples();
                    const auto in = noise ((size_t) (fs * 1.0), 0.02, 9);
                    Buf x { in, in };
                    run (*e, x, 512);
                    const int lag = bestLag (in, x[0], std::max (64, reported + 64));
                    expect (std::abs (lag - reported) <= 1, id + " @" + juce::String (fs) + ": measured " + juce::String (lag)
                                                            + ", reported " + juce::String (reported));
                }
        }
    }
};
static EffectRobustnessTest effectRobustnessTest;

//==============================================================================
struct EffectClickTest : juce::UnitTest
{
    EffectClickTest() : juce::UnitTest ("Effects / no click on any parameter change (jumps min <-> max, every choice)") {}

    void runTest() override
    {
        beginTest ("every parameter of every effect: 200 Hz and 1 kHz sines, jumps while playing");
        constexpr double fs = 48000.0;
        int totalChanges = 0;
        for (auto& i : builtin::effects())
        {
            const juce::String id (i.id);
            if (id == "builtin:lufs" || id == "builtin:spectrum") continue;   // no parameters
            auto probe = builtin::create (i.id);
            for (auto* p0 : probe->getEffectParameters())
            {
                const auto pid = p0->getParameterID();
                std::vector<std::pair<float, float>> jumps;   // normalised from -> to
                if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p0))
                {
                    const int n = c->choices.size();
                    for (int a = 0; a < n; ++a) for (int b = 0; b < n; ++b)
                        if (a != b) jumps.push_back ({ c->convertTo0to1 ((float) a), c->convertTo0to1 ((float) b) });
                }
                else jumps = { { 0.0f, 1.0f }, { 1.0f, 0.0f }, { 0.3f, 0.7f } };

                // (the hum remover notches 200 Hz itself - it would remove the test tone: 230 Hz there)
                for (double f : { id == "builtin:hum" ? 230.0 : 200.0, 1000.0 })
                    for (auto [from, to] : jumps)
                    {
                        auto e = builtin::create (i.id);
                        // a setting where the parameter matters: effects at a clear amount
                        if (id == "builtin:mud" || id == "builtin:nasal") e->setParam ("gain", 6.0f);
                        if (id == "builtin:reverb" || id == "builtin:echo") e->setParam ("mix", 30.0f);
                        if (id == "builtin:chorus" && pid != "mode") e->setParam ("mix", 50.0f);
                        if (id == "builtin:deesser") e->setParam ("threshold", -40.0f);
                        if (id == "builtin:limiter" && pid != "ceiling") e->setParam ("ceiling", -18.0f);
                        e->findParameter (pid)->setValueNotifyingHost (from);
                        e->setActiveChannels (2);
                        e->setPlayConfigDetails (2, 2, fs, 480);
                        e->prepareToPlay (fs, 480);

                        const size_t half = (size_t) (fs * 1.2);
                        const auto s1 = sine (2 * half, fs, f, 0.1);
                        Buf x { s1, s1 };
                        // first half with "from", then the jump, then the rest
                        Buf a { std::vector<float> (s1.begin(), s1.begin() + (long) half), std::vector<float> (s1.begin(), s1.begin() + (long) half) };
                        run (*e, a);
                        e->findParameter (pid)->setValueNotifyingHost (to);
                        Buf b { std::vector<float> (s1.begin() + (long) half, s1.end()), std::vector<float> (s1.begin() + (long) half, s1.end()) };
                        run (*e, b);
                        std::vector<float> y (a[0]);
                        y.insert (y.end(), b[0].begin(), b[0].end());
                        std::vector<float> yr (a[1]);
                        yr.insert (yr.end(), b[1].begin(), b[1].end());
                        const size_t window = (size_t) (0.25 * fs), steady = (size_t) (0.3 * fs);
                        for (auto* ch : { &y, &yr })
                        {
                            const auto r = clicksAround (*ch, half, window, steady);
                            expectEquals (r.clicks, 0, id + " " + pid + " " + juce::String (from, 2) + " -> " + juce::String (to, 2)
                                                       + " @ " + juce::String (f) + " Hz: worst " + juce::String (r.worst, 6)
                                                       + " limit " + juce::String (r.limit, 6));
                        }
                        ++totalChanges;
                    }
            }
        }
        logMessage ("parameter jumps checked: " + juce::String (totalChanges));
    }
};
static EffectClickTest effectClickTest;

//==============================================================================
struct EffectResponseTest : juce::UnitTest
{
    EffectResponseTest() : juce::UnitTest ("Effects / EQ and cleaning: measured responses against the design targets") {}

    void check (BuiltInEffect& e, double fs, double f, double wantDb, double tolDb, const juce::String& what)
    {
        const double got = measureGainDb (e, fs, f);
        expect (std::abs (got - wantDb) <= tolDb, what + " @ " + juce::String (f) + " Hz: " + juce::String (got, 2)
                                                   + " dB (want " + juce::String (wantDb, 2) + " +/- " + juce::String (tolDb, 2) + ")");
        if (auto* fe = dynamic_cast<fx::FilterEffect*> (&e); fe != nullptr && wantDb > -20.0)   // the analytic response agrees with the measurement
            expect (std::abs (db (fe->responseAt (f)) - got) <= 0.05, what + " analytic vs measured @ " + juce::String (f));
    }

    void runTest() override
    {
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            const juce::String at = " (" + juce::String ((int) fs) + ")";
            beginTest ("Low-Cut / High-Cut: Butterworth 12 and 24 dB/oct" + at);
            {
                auto e = make ("builtin:lowcut", fs);   // 75 Hz, 24 dB/oct
                check (*e, fs, 75.0, -3.01, 0.15, "lowcut24");
                check (*e, fs, 37.5, -24.1, 0.4, "lowcut24");
                check (*e, fs, 300.0, 0.0, 0.02, "lowcut24");
                check (*e, fs, 120.0, -0.10, 0.1, "lowcut24 (voice fundamental)");
                e->setParam ("slope", 0.0f);
                for (int k = 0; k < 10; ++k) { Buf z = makeBuf (2, 480); run (*e, z); }   // let the cross-fade finish
                check (*e, fs, 75.0, -3.01, 0.15, "lowcut12");
                check (*e, fs, 37.5, -12.3, 0.3, "lowcut12");
                auto h = make ("builtin:highcut", fs);   // 16 kHz, 12 dB/oct
                check (*h, fs, 16000.0, -3.01, 0.2, "highcut12");
                check (*h, fs, 1000.0, 0.0, 0.02, "highcut12");
            }

            beginTest ("bells and shelves at their defaults" + at);
            {
                auto body = make ("builtin:body", fs);         check (*body, fs, 180.0, 2.0, 0.05, "body");
                check (*body, fs, 3000.0, 0.0, 0.05, "body");
                auto mud = make ("builtin:mud", fs);           check (*mud, fs, 400.0, -3.0, 0.05, "mud");
                check (*mud, fs, 4000.0, 0.0, 0.05, "mud");
                auto nas = make ("builtin:nasal", fs);         check (*nas, fs, 1000.0, -3.0, 0.05, "nasal");
                auto pre = make ("builtin:presence", fs);      check (*pre, fs, 4000.0, 2.5, 0.05, "presence");
                check (*pre, fs, 200.0, 0.0, 0.05, "presence");
                auto air = make ("builtin:air", fs);           check (*air, fs, 19000.0, 2.5, 0.35, "air");
                check (*air, fs, 1000.0, 0.0, 0.05, "air");
                auto prox = make ("builtin:proximity", fs);    check (*prox, fs, 40.0, -3.0, 0.2, "proximity");
                check (*prox, fs, 3000.0, 0.0, 0.05, "proximity");
                auto tilt = make ("builtin:tilt", fs);
                tilt->setParam ("tilt", 6.0f);
                tilt->prepareToPlay (fs, 480);
                check (*tilt, fs, 30.0, -3.0, 0.3, "tilt +6");
                check (*tilt, fs, 18000.0, 3.0, 0.3, "tilt +6");
                check (*tilt, fs, 800.0, 0.0, 0.1, "tilt +6");
                auto peq = make ("builtin:parameq", fs);
                for (double f : { 30.0, 100.0, 400.0, 1000.0, 2500.0, 8000.0, 16000.0 }) check (*peq, fs, f, 0.0, 0.001, "parameq flat");
                peq->setParam ("b2gain", 6.0f);
                peq->setParam ("b2freq", 1000.0f);
                peq->prepareToPlay (fs, 480);
                check (*peq, fs, 1000.0, 6.0, 0.05, "parameq band 2");
            }

            beginTest ("hum remover: 50 Hz and harmonics deep, voice range untouched" + at);
            {
                auto e = make ("builtin:hum", fs);
                for (double f : { 50.0, 100.0, 150.0, 200.0 })   // narrow notches settle slowly: measure 6 s
                    expectLessThan (measureGainDb (*e, fs, f, 0.05, 6.0), -30.0, "hum notch @ " + juce::String (f));
                check (*e, fs, 250.0, 0.0, 0.1, "hum (5th harmonic off by default)");
                check (*e, fs, 126.0, 0.0, 0.3, "hum (voice f0 126 Hz)");
                check (*e, fs, 1000.0, 0.0, 0.01, "hum");
                e->setParam ("mains", 1.0f);
                e->prepareToPlay (fs, 480);
                for (double f : { 60.0, 120.0, 180.0, 240.0 }) expectLessThan (measureGainDb (*e, fs, f, 0.05, 6.0), -30.0);
            }

            beginTest ("DC remover: DC gone, 40 Hz untouched" + at);
            {
                auto e = make ("builtin:dcblock", fs);
                Buf x = makeBuf (2, (size_t) fs * 3);
                for (auto& c : x) for (auto& s : c) s = 0.2f;
                run (*e, x);
                expectLessThan (std::abs (x[0].back()), 1.0e-4f);
                check (*e, fs, 50.0, -0.043, 0.01, "dc (1st order, 5 Hz)");
            }
        }
    }
};
static EffectResponseTest effectResponseTest;

//==============================================================================
struct EffectDynamicsTest : juce::UnitTest
{
    EffectDynamicsTest() : juce::UnitTest ("Effects / limiter, de-esser, saturation, colour, time and channel tools") {}

    void runTest() override
    {
        constexpr double fs = 48000.0;

        beginTest ("limiter: true peak at the ceiling (speech, plosives, full-scale clicks; 0 / +6 / +12 dB input gain)");
        {
            for (float inputDb : { 0.0f, 6.0f, 12.0f })
                for (float ceiling : { -1.0f, -6.0f, -0.1f })
                {
                    auto e = make ("builtin:limiter");
                    e->setParam ("ceiling", ceiling);
                    e->setParam ("input", inputDb);
                    e->prepareToPlay (fs, 480);
                    auto v = speechSignal (8.0, -12.0, 21);
                    for (size_t k = 24000; k < v.size(); k += 48000) { v[k] = 0.99f; v[k + 1] = -0.99f; }   // clicks
                    Buf x { v, v };
                    run (*e, x);
                    const double tp = truePeakDb (x[0]);
                    // sample peaks: never above. True peak (independent 16x / 128-tap measurement): within
                    // 0.1 dB (BS.1770 allows a 4x meter 0.3 dB) - the remaining error is the 12-tap
                    // detector's reading of synthetic full-scale clicks; 0.2 dB with +12 dB into them
                    // (16+ dB of reduction)
                    const double tol = inputDb > 6.0f ? 0.2 : 0.1;
                    logMessage ("input +" + juce::String (inputDb, 0) + " dB, ceiling " + juce::String (ceiling, 1)
                                + " dBTP: output true peak " + juce::String (tp, 3));
                    expectLessOrEqual (maxAbs (x), (float) std::pow (10.0, ceiling / 20.0) * 1.000001f);
                    expectLessOrEqual (tp, (double) ceiling + tol, "true peak " + juce::String (tp, 3) + " ceiling " + juce::String (ceiling));
                }
        }

        beginTest ("limiter: below the knee the audio is only delayed (3 ms), unchanged");
        {
            auto e = make ("builtin:limiter");
            const auto s = sine (48000, fs, 440.0, 0.1);   // -20 dBFS, ceiling -1
            Buf x { s, s };
            run (*e, x);
            const int L = e->getLatencySamples();
            expectEquals (L, 144);
            double worst = 0.0;
            for (size_t k = (size_t) L; k < s.size(); ++k) worst = std::max (worst, std::abs ((double) x[0][k] - s[k - (size_t) L]));
            expectLessThan (worst, 1.0e-7);
        }

        beginTest ("de-esser: static curve (threshold, soft knee, 4:1, range)");
        {
            expectEquals (fx::deEsserReductionDb (-50.0, -30.0, 6.0), 0.0);
            expectWithinAbsoluteError (fx::deEsserReductionDb (-20.0, -30.0, 20.0), 7.5, 1.0e-9);   // 10 dB over at 4:1
            expectWithinAbsoluteError (fx::deEsserReductionDb (-10.0, -30.0, 6.0), 6.0, 1.0e-9);    // range cap
            double prev = -1.0;
            for (double l = -40.0; l <= -20.0; l += 0.1)   // monotonic, continuous through the knee
            {
                const double g = fx::deEsserReductionDb (l, -30.0, 12.0);
                expect (g >= prev - 1.0e-12);
                if (prev >= 0.0) expectLessThan (g - prev, 0.1);
                prev = g;
            }
        }

        beginTest ("de-esser: sibilant bursts turned down, the voice below them untouched (split mode)");
        {
            auto voice = speechSignal (6.0, -20.0, 4);
            // add "s" bursts: band noise 5-10 kHz, 120 ms, every 600 ms
            auto hiss = noise (voice.size(), 0.08, 17);
            juce::IIRFilter hp1, hp2, lp1;
            hp1.setCoefficients (juce::IIRCoefficients::makeHighPass (fs, 5000.0));
            hp2.setCoefficients (juce::IIRCoefficients::makeHighPass (fs, 5000.0));
            lp1.setCoefficients (juce::IIRCoefficients::makeLowPass (fs, 10000.0));
            hp1.processSamples (hiss.data(), (int) hiss.size());
            hp2.processSamples (hiss.data(), (int) hiss.size());
            lp1.processSamples (hiss.data(), (int) hiss.size());
            std::vector<bool> inBurst (voice.size(), false);
            for (size_t k = 0; k < voice.size(); ++k)
            {
                const size_t ph = k % 28800;
                if (ph < 5760) { voice[k] += hiss[k]; inBurst[k] = ph > 960 && ph < 4800; }
            }
            // energy above 5 kHz inside the bursts, energy below 3 kHz outside them
            auto band = [&] (const std::vector<float>& s, bool high, bool bursts)
            {
                std::vector<float> f (s);
                juce::IIRFilter a, b;
                a.setCoefficients (high ? juce::IIRCoefficients::makeHighPass (fs, 5000.0) : juce::IIRCoefficients::makeLowPass (fs, 3000.0));
                b.setCoefficients (high ? juce::IIRCoefficients::makeHighPass (fs, 5000.0) : juce::IIRCoefficients::makeLowPass (fs, 3000.0));
                a.processSamples (f.data(), (int) f.size());
                b.processSamples (f.data(), (int) f.size());
                double sum = 0.0; size_t n = 0;
                for (size_t k = 0; k < f.size(); ++k) if (inBurst[k] == bursts) { sum += (double) f[k] * f[k]; ++n; }
                return 10.0 * std::log10 (sum / std::max<size_t> (1, n));
            };
            auto deess = [&] (int detect, float thr, float levelDb, double& hiDrop, double& loChange)
            {
                const float g = (float) std::pow (10.0, levelDb / 20.0);
                std::vector<float> in (voice);
                for (auto& v : in) v *= g;
                auto e = make ("builtin:deesser");
                e->setParam ("detect", (float) detect);
                e->setParam ("threshold", thr);
                e->setParam ("range", 8.0f);
                e->prepareToPlay (fs, 480);
                Buf x { in, in };
                run (*e, x);
                hiDrop = band (in, true, true) - band (x[0], true, true);
                loChange = std::abs (band (in, false, false) - band (x[0], false, false));
            };
            double hiDrop = 0.0, loChange = 0.0;
            deess (1, -35.0f, 0.0f, hiDrop, loChange);   // absolute threshold
            logMessage ("absolute: sibilance reduced by " + juce::String (hiDrop, 2) + " dB; voice below 3 kHz changed by " + juce::String (loChange, 3) + " dB");
            expectGreaterThan (hiDrop, 3.0);
            expectLessThan (hiDrop, 8.5);
            expectLessThan (loChange, 0.1);
            // relative detection: the same reduction for a quiet and a loud microphone (-12 / 0 / +8 dB)
            double drops[3];
            int k = 0;
            for (float lv : { -12.0f, 0.0f, 8.0f })
            {
                deess (0, -15.0f, lv, hiDrop, loChange);
                logMessage ("relative, input " + juce::String (lv, 0) + " dB: sibilance reduced by " + juce::String (hiDrop, 2)
                            + " dB; voice below 3 kHz changed by " + juce::String (loChange, 3) + " dB");
                expectGreaterThan (hiDrop, 3.0);
                expectLessThan (hiDrop, 8.5);
                expectLessThan (loChange, 0.1);
                drops[k++] = hiDrop;
            }
            expectLessThan (std::abs (drops[0] - drops[2]), 1.0);
        }

        beginTest ("saturation: level kept, harmonics present but mild at 6 dB, aliasing low at 4x");
        {
            auto e = make ("builtin:saturation");   // tube, 6 dB, 100 %
            const auto s = sine (96000, fs, 1000.0, 0.1);
            Buf x { s, s };
            run (*e, x);
            // fundamental / harmonics by correlation with sines (whole seconds -> exact bins)
            auto amp = [&] (const std::vector<float>& y, double f)
            {
                double re = 0.0, im = 0.0;
                for (size_t k = 48000; k < 96000; ++k) { re += y[k] * std::cos (twoPi * f * k / fs); im += y[k] * std::sin (twoPi * f * k / fs); }
                return 2.0 * std::sqrt (re * re + im * im) / 48000.0;
            };
            const double a1 = amp (x[0], 1000.0), a2 = amp (x[0], 2000.0), a3 = amp (x[0], 3000.0);
            logMessage ("tube 6 dB at -20 dBFS: H1 " + juce::String (db (a1 / 0.1), 2) + " dB, H2 " + juce::String (db (a2 / a1), 1)
                        + " dBc, H3 " + juce::String (db (a3 / a1), 1) + " dBc");
            expectLessThan (std::abs (db (a1 / 0.1)), 0.5);
            expectLessThan (db (a2 / a1), -30.0);
            expectGreaterThan (db (a2 / a1), -80.0);   // it does colour

            auto hot = make ("builtin:saturation");
            hot->setParam ("type", 1.0f);
            hot->setParam ("drive", 24.0f);
            hot->prepareToPlay (fs, 480);
            const auto t = sine (96000, fs, 9000.0, 0.3);
            Buf y { t, t };
            run (*hot, y);
            // tape is symmetric: odd harmonics 27, 45 kHz fold back to 21 / 3 kHz (alias); measure 3 kHz
            const double fund = amp (y[0], 9000.0), alias = amp (y[0], 3000.0);
            logMessage ("tape 24 dB, 9 kHz: alias at 3 kHz " + juce::String (db (alias / fund), 1) + " dBc");
            expectLessThan (db (alias / fund), -50.0);
        }

        beginTest ("telephone / radio / megaphone: band limited, speech level kept within 4 dB");
        {
            const auto v = speechSignal (5.0, -20.0, 8);
            for (int m = 0; m < 3; ++m)
            {
                auto e = make ("builtin:lofi");
                e->setParam ("mode", (float) m);
                e->prepareToPlay (fs, 480);
                expectLessThan (measureGainDb (*e, fs, 80.0), -20.0);
                expectLessThan (measureGainDb (*e, fs, 9000.0), -15.0);
                Buf x { v, v };
                run (*e, x);
                const double change = db (rms (x[0], 0, v.size()) / rms (v, 0, v.size()));
                logMessage ("lofi mode " + juce::String (m) + ": speech level " + juce::String (change, 2) + " dB");
                expectLessThan (std::abs (change), 4.0);
            }
        }

        beginTest ("reverb: dry unchanged at mix 0, a tail after an impulse at the default mix");
        {
            auto e = make ("builtin:reverb");
            e->setParam ("mix", 0.0f);
            e->prepareToPlay (fs, 480);
            const auto v = speechSignal (2.0, -20.0, 2);
            Buf x { v, v };
            run (*e, x);
            double worst = 0.0;
            for (size_t k = 0; k < v.size(); ++k) worst = std::max (worst, std::abs ((double) x[0][k] - v[k]));
            expectLessThan (worst, 1.0e-6);
            auto r = make ("builtin:reverb");
            // (after a reset the reverb's input fades in over 10 ms from the first sound: a quiet lead-in
            // first, then the impulse at 100 ms)
            Buf imp = makeBuf (2, 96000);
            for (int k = 0; k < 960; ++k) imp[0][(size_t) k] = imp[1][(size_t) k] = 1.0e-4f;
            imp[0][4800] = imp[1][4800] = 0.5f;
            run (*r, imp);
            expectGreaterThan (rms (imp[0], 14400, 24000), 1.0e-5);   // 200-400 ms after it: room still sounding
            expectLessThan (rms (imp[0], 86400, 96000), rms (imp[0], 14400, 24000) * 0.1);   // and decaying
        }

        beginTest ("echo: repeats at the set time, decaying by the feedback");
        {
            auto e = make ("builtin:echo");
            e->setParam ("time", 250.0f);
            e->setParam ("feedback", 50.0f);
            e->setParam ("mix", 50.0f);
            e->setParam ("tone", 20000.0f);
            e->prepareToPlay (fs, 480);
            Buf x = makeBuf (2, 60000);
            for (int k = 0; k < 960; ++k) x[0][(size_t) k] = x[1][(size_t) k] = 1.0e-5f;   // lead-in (input fade after a reset)
            for (int k = 0; k < 48; ++k) x[0][(size_t) (1000 + k)] = x[1][(size_t) (1000 + k)] = 0.5f * (float) std::sin (3.14159 * k / 48.0);
            run (*e, x);
            auto peakNear = [&] (size_t at) { float m = 0; for (size_t k = at - 200; k < at + 300; ++k) m = std::max (m, std::abs (x[0][k])); return m; };
            const float p1 = peakNear (1000 + 12000), p2 = peakNear (1000 + 24000);
            expectGreaterThan (p1, 0.15f);
            expectWithinAbsoluteError (p2 / p1, 0.5f, 0.12f);
        }

        beginTest ("stereo widener: mono input becomes stereo, and L + R gives back the input exactly");
        {
            auto e = make ("builtin:chorus");
            e->setParam ("mode", 1.0f);
            e->setParam ("amount", 100.0f);
            e->prepareToPlay (fs, 480);
            const auto v = speechSignal (3.0, -20.0, 6);
            Buf x { v, v };
            run (*e, x);
            double diff = 0.0, sumErr = 0.0;
            for (size_t k = 0; k < v.size(); ++k)
            {
                diff = std::max (diff, std::abs ((double) x[0][k] - x[1][k]));
                sumErr = std::max (sumErr, std::abs (0.5 * ((double) x[0][k] + x[1][k]) - v[k]));
            }
            expectGreaterThan (diff, 0.01);
            expectLessThan (sumErr, 1.0e-6);
        }

        beginTest ("sync delay: exact delay, latency follows the setting");
        {
            auto e = make ("builtin:syncdelay");
            e->setParam ("delay", 100.0f);
            expectEquals (e->getLatencySamples(), 4800);
            e->prepareToPlay (fs, 480);
            Buf x = makeBuf (2, 9600);
            x[0][10] = 1.0f;
            run (*e, x);
            expectEquals (x[0][4810], 1.0f);
            e->setParam ("delay", 33.5f);   // 0.5 ms steps
            expectEquals (e->getLatencySamples(), 1608);
        }

        beginTest ("channel tool: modes, phase, balance");
        {
            auto e = make ("builtin:channel");
            auto check = [&] (std::function<void (BuiltInEffect&)> set, float wantL, float wantR)
            {
                auto t = make ("builtin:channel");
                set (*t);
                t->prepareToPlay (fs, 480);
                Buf x = makeBuf (2, 4800);
                for (auto& s : x[0]) s = 0.5f;
                for (auto& s : x[1]) s = 0.25f;
                run (*t, x);
                expectWithinAbsoluteError (x[0].back(), wantL, 1.0e-6f);
                expectWithinAbsoluteError (x[1].back(), wantR, 1.0e-6f);
            };
            check ([] (BuiltInEffect&) {}, 0.5f, 0.25f);
            check ([] (BuiltInEffect& t) { t.setParam ("mode", 1.0f); }, 0.5f, 0.5f);
            check ([] (BuiltInEffect& t) { t.setParam ("mode", 2.0f); }, 0.25f, 0.25f);
            check ([] (BuiltInEffect& t) { t.setParam ("mode", 3.0f); }, 0.25f, 0.5f);
            check ([] (BuiltInEffect& t) { t.setParam ("mode", 4.0f); }, 0.375f, 0.375f);
            check ([] (BuiltInEffect& t) { t.setParam ("phaseL", 1.0f); }, -0.5f, 0.25f);
            check ([] (BuiltInEffect& t) { t.setParam ("phaseR", 1.0f); }, 0.5f, -0.25f);
            check ([] (BuiltInEffect& t) { t.setParam ("balance", 50.0f); }, 0.25f, 0.25f);
            check ([] (BuiltInEffect& t) { t.setParam ("balance", -100.0f); }, 0.5f, 0.0f);
            juce::ignoreUnused (e);
        }

        beginTest ("mute: silence after the fade, back unchanged afterwards");
        {
            auto e = make ("builtin:mute");
            const auto s = sine (48000, fs, 300.0, 0.2);
            Buf a { s, s };
            e->setParam ("muted", 1.0f);
            run (*e, a);
            expectEquals (maxAbs (a, 1440), 0.0f);   // after 30 ms: exact silence
            e->setParam ("muted", 0.0f);
            Buf b { s, s };
            run (*e, b);
            double worst = 0.0;
            for (size_t k = 1440; k < s.size(); ++k) worst = std::max (worst, std::abs ((double) b[0][k] - s[k]));
            expectEquals (worst, 0.0);
        }
    }
};
static EffectDynamicsTest effectDynamicsTest;

//==============================================================================
struct LufsMeterTest : juce::UnitTest
{
    LufsMeterTest() : juce::UnitTest ("Effects / LUFS meter (EBU Tech 3341 / 3342 cases), spectrum ring") {}

    static void feed (BuiltInEffect& e, double fs, double seconds, double dbfs, double f = 1000.0, int channels = 2)
    {
        const size_t n = (size_t) (seconds * fs);
        const double a = std::pow (10.0, dbfs / 20.0);
        static double phase = 0.0;
        Buf x = makeBuf (channels, n);
        for (size_t k = 0; k < n; ++k)
        {
            const float v = (float) (a * std::sin (phase));
            phase += twoPi * f / fs;
            for (auto& c : x) c[k] = v;
        }
        run (e, x);
    }

    void runTest() override
    {
        for (double fs : { 44100.0, 48000.0 })
        {
            beginTest ("stereo 1 kHz sine at -23 dBFS = -23.0 LUFS (M, S, I) @" + juce::String ((int) fs));
            {
                auto e = make ("builtin:lufs", fs);
                feed (*e, fs, 20.0, -23.0);
                auto* m = dynamic_cast<fx::LufsMeter*> (e.get());
                const auto r = m->read();
                expectWithinAbsoluteError (r.momentary, -23.0f, 0.1f);
                expectWithinAbsoluteError (r.shortTerm, -23.0f, 0.1f);
                expectWithinAbsoluteError (r.integrated, -23.0f, 0.1f);
                expectWithinAbsoluteError (r.truePeakDb, -23.0f, 0.2f);
            }
            beginTest ("Tech 3341 case 3: -36 / -23 / -36 dBFS (10 / 60 / 10 s) -> -23.0 LUFS (gating)");
            {
                auto e = make ("builtin:lufs", fs);
                feed (*e, fs, 10.0, -36.0);
                feed (*e, fs, 60.0, -23.0);
                feed (*e, fs, 10.0, -36.0);
                expectWithinAbsoluteError ((float) dynamic_cast<fx::LufsMeter*> (e.get())->integratedLufs(), -23.0f, 0.1f);
            }
            beginTest ("Tech 3342 case 1: 20 s at -20, 20 s at -30 dBFS -> LRA 10 LU");
            {
                auto e = make ("builtin:lufs", fs);
                feed (*e, fs, 20.0, -20.0);
                feed (*e, fs, 20.0, -30.0);
                expectWithinAbsoluteError ((float) dynamic_cast<fx::LufsMeter*> (e.get())->loudnessRange(), 10.0f, 1.0f);
            }
        }

        beginTest ("mono element (one channel): -26 LUFS for the same sine; true peak between samples");
        {
            auto e = make ("builtin:lufs", 48000.0, 480, 1);
            feed (*e, 48000.0, 10.0, -23.0, 1000.0, 1);
            const auto r = dynamic_cast<fx::LufsMeter*> (e.get())->read();
            expectWithinAbsoluteError (r.integrated, -26.0f, 0.1f);
            expectEquals (r.channels, 1);

            auto t = make ("builtin:lufs");
            Buf x { sine (48000, 48000.0, 12000.0, 0.5, 3.14159265 / 4.0), sine (48000, 48000.0, 12000.0, 0.5, 3.14159265 / 4.0) };
            run (*t, x);   // samples at +/-0.354, the wave between them reaches 0.5 (-6.02 dBFS)
            expectWithinAbsoluteError (dynamic_cast<fx::LufsMeter*> (t.get())->read().truePeakDb, -6.02f, 0.3f);
        }

        beginTest ("reset clears the integrated value");
        {
            auto e = make ("builtin:lufs");
            feed (*e, 48000.0, 5.0, -20.0);
            auto* m = dynamic_cast<fx::LufsMeter*> (e.get());
            m->requestReset();
            feed (*e, 48000.0, 5.0, -30.0);
            expectWithinAbsoluteError ((float) m->integratedLufs(), -30.0f, 0.15f);
        }

        beginTest ("spectrum: the window gets the newest samples (mean of the channels)");
        {
            auto e = make ("builtin:spectrum");
            Buf x { sine (10000, 48000.0, 440.0, 0.4), sine (10000, 48000.0, 440.0, 0.2) };
            const Buf in = x;
            run (*e, x);
            expect (x == in);
            std::vector<float> got (8192);
            expectEquals (dynamic_cast<fx::SpectrumAnalyser*> (e.get())->copyLatest (got.data(), 8192), 8192);
            double worst = 0.0;
            for (int k = 0; k < 8192; ++k)
                worst = std::max (worst, std::abs ((double) got[(size_t) k] - 0.5 * (in[0][(size_t) (10000 - 8192 + k)] + in[1][(size_t) (10000 - 8192 + k)])));
            expectLessThan (worst, 1.0e-7);
        }
    }
};
static LufsMeterTest lufsMeterTest;

//==============================================================================
struct EffectChainTest : juce::UnitTest
{
    EffectChainTest() : juce::UnitTest ("Effects / in the chain: wiring, channel widths, move, bypass, restore") {}

    void runTest() override
    {
        beginTest ("mono microphone: channel-neutral effects stay mono, a stereo effect gets the voice on both inputs");
        {
            juce::AudioProcessorGraph graph;
            using IO = juce::AudioProcessorGraph::AudioGraphIOProcessor;
            graph.setPlayConfigDetails (1, 2, 48000.0, 480);
            auto in  = graph.addNode (std::make_unique<IO> (IO::audioInputNode));
            auto out = graph.addNode (std::make_unique<IO> (IO::audioOutputNode));
            graph.prepareToPlay (48000.0, 480);
            PluginChain chain (graph, in->nodeID, out->nodeID);
            expect (chain.addBuiltInEffect ("builtin:lowcut"));
            expect (chain.addBuiltInEffect ("builtin:reverb"));
            expect (chain.addBuiltInEffect ("builtin:limiter"));
            expect (! chain.addBuiltInEffect ("builtin:nope"));
            chain.rebuildConnections();
            expectEquals (chain.getBuiltInEffect (0)->getActiveChannels(), 1);
            expectEquals (chain.getBuiltInEffect (1)->getActiveChannels(), 2);
            expectEquals (chain.getBuiltInEffect (2)->getActiveChannels(), 2);
            expect (! chain.isOutputMono());
            // the reverb's inputs are both fed from the mono part
            int intoReverb = 0;
            for (auto& c : graph.getConnections())
                if (c.destination.nodeID == chain.entries()[1].node) ++intoReverb;
            expectEquals (intoReverb, 2);

            chain.movePlugin (1, 2);   // lowcut, limiter, reverb
            chain.rebuildConnections();
            expectEquals (chain.getBuiltInEffect (1)->getActiveChannels(), 1);   // limiter now in the mono part
            chain.setBypass (2, true);   // reverb off -> the chain ends mono
            chain.rebuildConnections();
            expect (chain.isOutputMono());
            expect (chain.getBuiltInEffect (5) == nullptr);

            // process a block through the real graph: finite, the voice arrives
            juce::AudioBuffer<float> b (2, 480);
            juce::MidiBuffer midi;
            for (int k = 0; k < 50; ++k)
            {
                b.clear();
                for (int s = 0; s < 480; ++s) b.setSample (0, s, 0.1f * (float) std::sin (0.06 * (s + 480 * k)));
                graph.processBlock (b, midi);
            }
            expectGreaterThan (b.getMagnitude (0, 0, 480), 0.05f);
        }
    }
};
static EffectChainTest effectChainTest;
