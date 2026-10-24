#include "audio/Metering.h"
#include "ui/Visualiser.h"
#include "ui/VisualiserViews.h"
#include <thread>

// The main window's spectrum and level meters (interface only) and the meters' read-only sample
// copy the audio thread fills (SampleTap). Checked: the audio is never changed, a reader racing
// the writer never gets torn data, the spectrum is calibrated, the motion is the same at 10, 30
// and 60 FPS, the meters' ballistics, silence and broken samples.
struct VisualiserTest : juce::UnitTest
{
    VisualiserTest() : juce::UnitTest ("Visualiser") {}

    static void writeSine (SampleTap& tap, double fs, double hz, float amp, int frames, int64_t& phase, int block = 480)
    {
        juce::AudioBuffer<float> b (2, block);
        for (int done = 0; done < frames; done += block)
        {
            const int n = juce::jmin (block, frames - done);
            b.setSize (2, n, false, false, true);
            for (int i = 0; i < n; ++i, ++phase)
            {
                const float v = amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) phase / fs);
                b.setSample (0, i, v);
                b.setSample (1, i, v);
            }
            tap.write (b);
        }
    }

    static void writeSilence (SampleTap& tap, int frames, int block = 480)
    {
        juce::AudioBuffer<float> b (2, block);
        b.clear();
        for (int done = 0; done < frames; done += block)
        {
            b.setSize (2, juce::jmin (block, frames - done), false, true, true);
            b.clear();
            tap.write (b);
        }
    }

    static int nearestPoint (const micvst::viz::Feed& f, double hz)
    {
        int best = 0;
        for (int p = 1; p < f.numPoints(); ++p)
            if (std::abs (std::log (f.frequencyOf (p) / hz)) < std::abs (std::log (f.frequencyOf (best) / hz))) best = p;
        return best;
    }

    void runTest() override
    {
        beginTest ("SampleTap: the newest frames exactly, left and right; a mono block on both sides");
        {
            SampleTap tap;
            juce::AudioBuffer<float> b (2, 300);
            for (int blk = 0; blk < 10; ++blk)
            {
                for (int i = 0; i < 300; ++i) { b.setSample (0, i, (float) (blk * 300 + i)); b.setSample (1, i, -(float) (blk * 300 + i)); }
                tap.write (b);
            }
            expect (tap.total() == 3000);
            std::vector<float> l (1000), r (1000);
            expect (tap.read (tap.total(), l.data(), r.data(), 1000));
            bool exact = true;
            for (int i = 0; i < 1000; ++i) exact = exact && juce::exactlyEqual (l[(size_t) i], (float) (2000 + i)) && juce::exactlyEqual (r[(size_t) i], -(float) (2000 + i));
            expect (exact);
            expect (! tap.read (500, l.data(), r.data(), 1000), "older than the start");
            juce::AudioBuffer<float> m (1, 64);
            for (int i = 0; i < 64; ++i) m.setSample (0, i, 0.25f);
            tap.write (m);
            expect (tap.read (tap.total(), l.data(), r.data(), 64) && juce::exactlyEqual (l[63], 0.25f) && juce::exactlyEqual (r[63], 0.25f));
        }

        beginTest ("SampleTap: a range already overwritten is refused, never returned");
        {
            SampleTap tap;
            int64_t ph = 0;
            writeSine (tap, 48000.0, 1000.0, 0.5f, SampleTap::capacity * 3, ph);
            std::vector<float> l (100), r (100);
            const auto end = tap.total();
            expect (! tap.read (end - (uint64_t) (SampleTap::capacity - 10), l.data(), r.data(), 100));
            expect (tap.read (end, l.data(), r.data(), 100));
            expect (! tap.read (end, l.data(), r.data(), SampleTap::maxRead + 1), "more than one read may take");
        }

        beginTest ("SampleTap: a reader racing the writer (1 s, blocks of 32 ... 2048) never gets torn data");
        {
            SampleTap tap;
            std::atomic<bool> stop { false };
            std::thread writer ([&]
            {
                juce::AudioBuffer<float> b (2, 2048);
                uint32_t v = 0;
                juce::Random rnd (7);
                while (! stop.load())
                {
                    const int n = 32 << rnd.nextInt (7);
                    b.setSize (2, n, false, false, true);
                    for (int i = 0; i < n; ++i, ++v)
                    {
                        const float x = (float) (v & 0xffffu);   // exact in a float
                        b.setSample (0, i, x);
                        b.setSample (1, i, -x);
                    }
                    tap.write (b);
                }
            });
            std::vector<float> l (4096), r (4096);
            int good = 0, refused = 0, torn = 0;
            const auto until = juce::Time::getMillisecondCounterHiRes() + 1000.0;
            while (juce::Time::getMillisecondCounterHiRes() < until)
            {
                const auto end = tap.total();
                if (end < 4096) continue;
                if (! tap.read (end, l.data(), r.data(), 4096)) { ++refused; continue; }
                bool ok = true;
                for (int i = 1; i < 4096 && ok; ++i)
                    ok = (((uint32_t) l[(size_t) i - 1] + 1u) & 0xffffu) == (uint32_t) l[(size_t) i] && juce::exactlyEqual (r[(size_t) i], -l[(size_t) i]);
                if (ok) ++good; else ++torn;
            }
            stop = true;
            writer.join();
            expectEquals (torn, 0);
            expect (good > 100, juce::String (good) + " reads");
            logMessage ("racing reads: " + juce::String (good) + " consistent, " + juce::String (refused) + " refused (overwritten while copying), 0 torn");
        }

        beginTest ("LevelMeter: the block is not changed (bit-exact), the readings are those of computeLevel");
        {
            LevelMeter meter;
            juce::AudioBuffer<float> b (2, 480), copy (2, 480);
            juce::Random rnd (3);
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 480; ++i) b.setSample (c, i, rnd.nextFloat() * 2.0f - 1.0f);
            copy.makeCopyOf (b);
            meter.process (b);
            bool same = true;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < 480; ++i) same = same && juce::exactlyEqual (b.getSample (c, i), copy.getSample (c, i));
            expect (same);
            const auto want = computeLevel (copy);
            expect (juce::exactlyEqual (meter.read().rms, want.rms) && juce::exactlyEqual (meter.read().peak, want.peak));
            std::vector<float> l (480), r (480);
            expect (meter.tap().read (meter.tap().total(), l.data(), r.data(), 480));
            bool copied = true;
            for (int i = 0; i < 480; ++i) copied = copied && juce::exactlyEqual (l[(size_t) i], copy.getSample (0, i)) && juce::exactlyEqual (r[(size_t) i], copy.getSample (1, i));
            expect (copied);
        }

        beginTest ("Spectrum: a sine reads its level at its frequency (44.1 / 48 / 96 / 192 kHz)");
        {
            for (double fs : { 44100.0, 48000.0, 96000.0, 192000.0 })
            {
                micvst::viz::SpectrumAnalyser a;
                a.prepare (fs, 300);
                for (double hz : { 60.0, 250.0, 1000.0, 4000.0, 12000.0 })
                {
                    for (float amp : { 0.5f, 0.01f })
                    {
                        std::vector<float> x ((size_t) a.fftSize());
                        for (int i = 0; i < a.fftSize(); ++i) x[(size_t) i] = amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / fs + 0.3);
                        const auto& lv = a.analyse (x.data());
                        int best = 0;
                        for (int p = 1; p < a.numPoints(); ++p)
                            if (std::abs (std::log (a.frequencyOf (p) / hz)) < std::abs (std::log (a.frequencyOf (best) / hz))) best = p;
                        const float want = juce::Decibels::gainToDecibels (amp);
                        expect (lv[(size_t) best] <= want + 0.2f && lv[(size_t) best] >= want - 1.6f,
                                juce::String (fs) + " Hz rate, " + juce::String (hz) + " Hz: " + juce::String (lv[(size_t) best], 2) + " dB, want " + juce::String (want, 2));
                        float far = -200.0f;
                        for (int p = 0; p < a.numPoints(); ++p)
                            if (std::abs (std::log2 (a.frequencyOf (p) / hz)) > 2.0) far = juce::jmax (far, lv[(size_t) p]);
                        expect (far < want - 60.0f, "two octaves away: " + juce::String (far, 1) + " dB");
                    }
                }
            }
            std::vector<float> silence (4096, 0.0f), broken (4096, 0.1f);
            micvst::viz::SpectrumAnalyser a;
            a.prepare (48000.0, 200);
            for (auto v : a.analyse (silence.data())) expect (juce::exactlyEqual (v, micvst::viz::floorDb));
            broken[100] = std::numeric_limits<float>::quiet_NaN();
            broken[200] = std::numeric_limits<float>::infinity();
            bool finite = true;
            for (auto v : a.analyse (broken.data())) finite = finite && std::isfinite (v);
            expect (finite, "NaN / infinite samples: no NaN in the picture");
        }

        beginTest ("Motion in real time: the same spectrum and meters at 10, 30 and 60 FPS (and uneven frames)");
        {
            const double fs = 48000.0;
            struct Run { float specAt[4]; float peakAt[4]; float rmsAt[4]; };
            auto simulate = [&] (double fps, bool uneven)
            {
                SampleTap in, out;
                micvst::viz::Feed feed;
                feed.setPoints (300);
                int64_t ph = 0, phOut = 0;
                Run run {};
                const double checks[] { 0.5, 1.0, 1.3, 1.8 };   // sine 0 .. 1.2 s, silence after
                int next = 0;
                double t = 0.0;
                int64_t writtenFrames = 0;
                int frame = 0;
                while (next < 4)
                {
                    ++frame;
                    if (uneven) t += (1.0 / fps) * (frame % 3 == 0 ? 1.7 : 0.65);   // 28 ms, 11 ms, 11 ms ...
                    else        t = frame / fps;                                       // exactly on the check times
                    // whole blocks of 480, as the engine delivers them, up to the frame's time (integers: no drift)
                    const int64_t due = (int64_t) std::floor (t * fs / 480.0 + 1.0e-6) * 480;
                    while (writtenFrames < due)
                    {
                        if (writtenFrames < (int64_t) (1.2 * fs)) { writeSine (in, fs, 1000.0, 0.5f, 480, ph); writeSine (out, fs, 1000.0, 0.25f, 480, phOut); }
                        else { writeSilence (in, 480); writeSilence (out, 480); }
                        writtenFrames += 480;
                    }
                    feed.update (in, out, fs, t);
                    if (t >= checks[next] - 1.0e-9)
                    {
                        const int p = nearestPoint (feed, 1000.0);
                        run.specAt[next] = feed.spectrum (micvst::viz::Feed::input)[(size_t) p];
                        run.peakAt[next] = feed.level (micvst::viz::Feed::input, 0).peakDb;
                        run.rmsAt[next] = feed.level (micvst::viz::Feed::input, 0).rmsDb;
                        ++next;
                    }
                }
                return run;
            };
            const auto r60 = simulate (60.0, false), r30 = simulate (30.0, false), r10 = simulate (10.0, false), ru = simulate (60.0, true);
            for (int k = 0; k < 4; ++k)
            {
                // steady sound: the same values; falling: the same within what 10 ms of audio
                // (the engine's block that may or may not have arrived yet) changes
                for (const Run* r : { &r30, &r10 })
                {
                    expectWithinAbsoluteError (r->specAt[k], r60.specAt[k], k < 2 ? 0.5f : 1.0f);
                    expectWithinAbsoluteError (r->peakAt[k], r60.peakAt[k], k < 2 ? 0.05f : 0.5f);
                }
            }
            for (int k = 0; k < 2; ++k)   // uneven frames (28, 11, 11 ms ...): steady values unchanged
            {
                expectWithinAbsoluteError (ru.specAt[k], r60.specAt[k], 0.5f);
                expectWithinAbsoluteError (ru.peakAt[k], r60.peakAt[k], 0.05f);
            }
            expectWithinAbsoluteError (r60.specAt[1], -6.02f, 1.6f);      // steady sine: its level
            expectWithinAbsoluteError (r60.peakAt[1], -6.02f, 0.05f);
            // sine RMS = peak - 3.01 dB, 1.0 s into the 300 ms average: 1 - e^(-1/0.3) of the energy
            expectWithinAbsoluteError (r60.rmsAt[1], -9.03f + (float) (10.0 * std::log10 (1.0 - std::exp (-1.0 / 0.3))), 0.05f);
            expect (r60.specAt[3] < -60.0f, "0.6 s of silence: the spectrum is down (" + juce::String (r60.specAt[3], 1) + " dB)");
            logMessage ("1 kHz at 0.5 s: spectrum " + juce::String (r60.specAt[0], 2) + " / " + juce::String (r30.specAt[0], 2) + " / "
                        + juce::String (r10.specAt[0], 2) + " / uneven " + juce::String (ru.specAt[0], 2) + " dB (60 / 30 / 10 FPS); "
                        + "peak 0.6 s after the sine stopped: " + juce::String (r60.peakAt[3], 1) + " / " + juce::String (r10.peakAt[3], 1) + " dB");
        }

        beginTest ("Meters: peak, held peak (1.5 s), RMS, clip light; silence falls 20 dB/s");
        {
            micvst::viz::ChannelLevel c;
            std::vector<float> x (4800);
            for (int i = 0; i < 4800; ++i) x[(size_t) i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * i / 48000.0);
            for (int k = 0; k < 20; ++k) c.add (x.data(), 4800, 48000.0, 0.1 * (k + 1));   // 2 s
            expectWithinAbsoluteError (c.peakDb, -6.02f, 0.05f);
            expectWithinAbsoluteError (c.holdDb, -6.02f, 0.05f);
            expectWithinAbsoluteError (c.rmsDb, -9.03f, 0.05f);
            expect (c.clipUntil <= 0.0);
            c.fall (1.0, 3.0);   // 1 s without sound, the hold expired at 3.5 s? no: 2.0 + 1.5 = 3.5 > 3.0
            expectWithinAbsoluteError (c.peakDb, -26.02f, 0.05f);
            expectWithinAbsoluteError (c.holdDb, -6.02f, 0.05f);          // still held
            c.fall (1.0, 4.0);
            expect (c.holdDb < -6.5f && c.holdDb >= c.peakDb, "the hold falls after 1.5 s: " + juce::String (c.holdDb, 2));
            std::vector<float> hot { 0.2f, 1.0f, -0.3f };
            c.add (hot.data(), 3, 48000.0, 5.0);
            expect (c.clipUntil > 7.9 && c.clipUntil < 8.1, "full scale lights the clip light for 3 s");
            std::vector<float> bad { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 0.1f };
            c.add (bad.data(), 3, 48000.0, 5.1);
            expect (std::isfinite (c.peakDb) && std::isfinite (c.rmsDb) && std::isfinite (c.holdDb));
        }

        beginTest ("No audio (engine stopped): everything falls, then nothing moves - no redraws needed");
        {
            SampleTap in, out;
            micvst::viz::Feed feed;
            feed.setPoints (200);
            int64_t ph = 0, ph2 = 0;
            double t = 0.0;
            for (int f = 0; f < 60; ++f) { t += 1.0 / 60; writeSine (in, 48000.0, 500.0, 0.3f, 800, ph); writeSine (out, 48000.0, 500.0, 0.3f, 800, ph2); feed.update (in, out, 48000.0, t); }
            expect (feed.hasSignal() && ! feed.settled());
            for (int f = 0; f < 600; ++f) { t += 1.0 / 60; feed.update (in, out, 48000.0, t); }   // 10 s, nothing written
            expect (! feed.hasSignal());
            expect (feed.settled(), "settled after 10 s of nothing");
            feed.update (in, out, 0.0, t + 0.1);   // the engine reports no rate: still fine
            expect (feed.settled());
        }

        beginTest ("Switched off and on again in the settings (reset): the spectrum moves again (user report, deneme 16)");
        {
            SampleTap in, out;
            micvst::viz::Feed feed;
            feed.setPoints (200);
            int64_t ph = 0, ph2 = 0;
            double t = 0.0;
            auto run = [&] (int frames)
            {
                for (int f = 0; f < frames; ++f)
                {
                    t += 1.0 / 30;
                    writeSine (in, 48000.0, 1000.0, 0.5f, 1600, ph);
                    writeSine (out, 48000.0, 1000.0, 0.5f, 1600, ph2);
                    feed.update (in, out, 48000.0, t);
                }
            };
            run (30);
            const int p = nearestPoint (feed, 1000.0);
            expect (feed.spectrum (micvst::viz::Feed::output)[(size_t) p] > -12.0f, "before");
            for (int k = 0; k < 3; ++k)
            {
                feed.reset();     // hidden ...
                run (30);         // ... and shown again
                expect (feed.spectrum (micvst::viz::Feed::input).size() == 200 && feed.spectrum (micvst::viz::Feed::output).size() == 200,
                        "the spectrum has its points again");
                expect (feed.spectrum (micvst::viz::Feed::output).size() == 200 && feed.spectrum (micvst::viz::Feed::output)[(size_t) p] > -12.0f,
                        "after switching off and on " + juce::String (k + 1) + " time(s): the spectrum shows the sine again");
                expect (feed.level (micvst::viz::Feed::output, 0).peakDb > -7.0f, "the meters too");
            }
        }

        beginTest ("Cost per drawn frame (analysis of input + output, 300 points)");
        {
            SampleTap in, out;
            micvst::viz::Feed feed;
            feed.setPoints (300);
            int64_t ph = 0, ph2 = 0;
            double t = 0.0, worst = 0.0, sum = 0.0;
            const int frames = 600;
            for (int f = 0; f < frames; ++f)
            {
                t += 1.0 / 60;
                writeSine (in, 48000.0, 300.0, 0.3f, 800, ph);
                writeSine (out, 48000.0, 300.0, 0.3f, 800, ph2);
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                feed.update (in, out, 48000.0, t);
                const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                if (f > 10) { worst = juce::jmax (worst, ms); sum += ms; }
            }
            const double mean = sum / (frames - 11);
            logMessage ("analysis per frame: mean " + juce::String (mean, 3) + " ms, worst " + juce::String (worst, 3) + " ms (two 4096-point FFTs)");
            expect (mean < 2.0, juce::String (mean, 3) + " ms");
        }

        beginTest ("Cost of drawing one frame with the software renderer (what GDI mode does): spectrum + meters");
        {
            juce::ScopedJuceInitialiser_GUI gui;
            SampleTap in, out;
            micvst::viz::Feed feed;
            SpectrumView spectrum (feed);
            LevelMetersView meters (feed);
            juce::Random rnd (5);
            int64_t ph = 0;
            for (auto size : { std::pair<int, int> { 452, 118 }, std::pair<int, int> { 680, 166 } })   // default window, large window
            {
                spectrum.setSize (size.first, size.second);
                meters.setSize (size.first + 160, LevelMetersView::preferredHeight);
                feed.setPoints (spectrum.pointsWanted());
                double t = 0.0;
                auto frame = [&]
                {
                    t += 1.0 / 60;
                    juce::AudioBuffer<float> b (2, 800);
                    for (int i = 0; i < 800; ++i, ++ph)
                    {
                        const float v = 0.2f * (float) std::sin (0.031 * (double) ph) + 0.05f * (rnd.nextFloat() - 0.5f);   // tone + noise: full curves
                        b.setSample (0, i, v);
                        b.setSample (1, i, v * 0.7f);
                    }
                    in.write (b);
                    b.applyGain (1.6f);
                    out.write (b);
                    feed.update (in, out, 48000.0, t);
                };
                for (int i = 0; i < 40; ++i) frame();
                juce::Image a (juce::Image::ARGB, spectrum.getWidth(), spectrum.getHeight(), true, juce::SoftwareImageType());
                juce::Image b (juce::Image::ARGB, meters.getWidth(), meters.getHeight(), true, juce::SoftwareImageType());
                double sumMs = 0.0, worstMs = 0.0;
                const int n = 200;
                for (int i = 0; i < n; ++i)
                {
                    frame();
                    const double t0 = juce::Time::getMillisecondCounterHiRes();
                    { juce::Graphics g (a); spectrum.paintEntireComponent (g, false); }
                    { juce::Graphics g (b); meters.paintEntireComponent (g, false); }
                    const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
                    if (i > 5) { sumMs += ms; worstMs = juce::jmax (worstMs, ms); }
                }
                const double meanMs = sumMs / (n - 6);
                logMessage ("drawing " + juce::String (size.first) + " x " + juce::String (size.second) + " spectrum + meters: mean "
                            + juce::String (meanMs, 3) + " ms, worst " + juce::String (worstMs, 3) + " ms per frame -> at 60 FPS "
                            + juce::String (meanMs * 60.0 / 10.0, 1) + " % of one core");
                expect (meanMs < 6.0, juce::String (meanMs, 3) + " ms");
            }
        }
    }
};

static VisualiserTest visualiserTest;
