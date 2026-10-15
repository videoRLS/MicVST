#include <juce_audio_basics/juce_audio_basics.h>
#include "audio/AudioBridge.h"
#include "audio/InputQueue.h"
#include "BridgeSim.h"
#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace
{
    // Least-squares fit of a sine with known frequency (cycles/sample); returns the residual
    // power relative to the fitted sine power in dB (= -SNR, THD+N).
    struct SineFit { double amplitude = 0.0; double residualDb = 0.0; };

    SineFit fitSine (const float* x, int n, double cyclesPerSample)
    {
        // 3-parameter least squares (sin, cos, DC) solved via the normal equations.
        double m[3][3] = {}, v[3] = {};
        for (int i = 0; i < n; ++i)
        {
            const double ph = juce::MathConstants<double>::twoPi * cyclesPerSample * i;
            const double basis[3] = { std::sin (ph), std::cos (ph), 1.0 };
            for (int r = 0; r < 3; ++r)
            {
                v[r] += basis[r] * x[i];
                for (int c = 0; c < 3; ++c) m[r][c] += basis[r] * basis[c];
            }
        }
        // Gaussian elimination (3x3, well conditioned for windows of many cycles)
        for (int col = 0; col < 3; ++col)
            for (int row = col + 1; row < 3; ++row)
            {
                const double f = m[row][col] / m[col][col];
                for (int k = col; k < 3; ++k) m[row][k] -= f * m[col][k];
                v[row] -= f * v[col];
            }
        double sol[3];
        for (int row = 2; row >= 0; --row)
        {
            double acc = v[row];
            for (int k = row + 1; k < 3; ++k) acc -= m[row][k] * sol[k];
            sol[row] = acc / m[row][row];
        }
        double res = 0, sig = 0;
        for (int i = 0; i < n; ++i)
        {
            const double ph = juce::MathConstants<double>::twoPi * cyclesPerSample * i;
            const double tone = sol[0] * std::sin (ph) + sol[1] * std::cos (ph);
            const double e = x[i] - tone - sol[2];
            res += e * e;
            sig += tone * tone;
        }
        return { std::sqrt (sol[0] * sol[0] + sol[1] * sol[1]), 10.0 * std::log10 ((res + 1.0e-30) / (sig + 1.0e-30)) };
    }

    std::vector<float> sine (int n, double cyclesPerSample, double amp = 0.5)
    {
        std::vector<float> v ((size_t) n);
        for (int i = 0; i < n; ++i)
            v[(size_t) i] = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * cyclesPerSample * i));
        return v;
    }
}

struct BridgeResamplerTest : juce::UnitTest
{
    BridgeResamplerTest() : juce::UnitTest ("Bridge resampler") {}

    void runTest() override
    {
        beginTest ("passband flat and clean at drift-sized ratios (48 kHz)");
        {
            AudioBridge b;   // default filter: 48 kHz -> 48 kHz
            const double step = 1.0002;   // 200 ppm
            for (double f : { 100.0, 1000.0, 5000.0, 10000.0, 16000.0, 18000.0 })
            {
                const double cps = f / 48000.0;
                auto src = sine (40000, cps);
                std::vector<float> dst (30000);
                b.testResampleMono (src.data(), (int) src.size(), 200.0, step, dst.data(), (int) dst.size());
                const auto fit = fitSine (dst.data() + 1000, 28000, cps * step);
                const double gainDb = juce::Decibels::gainToDecibels (fit.amplitude / 0.5);
                logMessage ("  " + juce::String (f, 0) + " Hz: gain " + juce::String (gainDb, 4)
                            + " dB, THD+N " + juce::String (fit.residualDb, 1) + " dB");
                expect (std::abs (gainDb) < 0.05, "passband gain at " + juce::String (f) + " Hz");
                expect (fit.residualDb < (f <= 10000.0 ? -100.0 : -85.0), "THD+N at " + juce::String (f) + " Hz");
            }
        }

        beginTest ("44.1 kHz -> 48 kHz conversion is clean");
        {
            AudioBridge b;
            b.setInputRate (44100.0);
            b.setOutputRate (48000.0);
            const double step = 44100.0 / 48000.0;
            const double cps = 1000.0 / 44100.0;
            auto src = sine (40000, cps);
            std::vector<float> dst (30000);
            b.testResampleMono (src.data(), (int) src.size(), 200.0, step, dst.data(), (int) dst.size());
            const auto fit = fitSine (dst.data() + 1000, 28000, cps * step);
            logMessage ("  1 kHz: THD+N " + juce::String (fit.residualDb, 1) + " dB");
            expect (fit.residualDb < -100.0);
            expectWithinAbsoluteError (fit.amplitude, 0.5, 0.003);
        }

        beginTest ("96 kHz -> 48 kHz: anti-aliasing removes content above 24 kHz");
        {
            AudioBridge b;
            b.setInputRate (96000.0);
            b.setOutputRate (48000.0);
            const double step = 2.0;

            auto passSrc = sine (80000, 10000.0 / 96000.0);
            std::vector<float> pass (30000);
            b.testResampleMono (passSrc.data(), (int) passSrc.size(), 400.0, step, pass.data(), (int) pass.size());
            const auto passFit = fitSine (pass.data() + 1000, 28000, 10000.0 / 48000.0);
            expectWithinAbsoluteError (passFit.amplitude, 0.5, 0.003);

            auto stopSrc = sine (80000, 30000.0 / 96000.0);   // would alias to 18 kHz
            std::vector<float> stop (30000);
            b.testResampleMono (stopSrc.data(), (int) stopSrc.size(), 400.0, step, stop.data(), (int) stop.size());
            double rms = 0;
            for (int i = 1000; i < 29000; ++i) rms += (double) stop[(size_t) i] * stop[(size_t) i];
            rms = std::sqrt (rms / 28000.0);
            const double attenDb = juce::Decibels::gainToDecibels (rms / (0.5 / std::sqrt (2.0)), -200.0);
            logMessage ("  30 kHz alias attenuation: " + juce::String (attenDb, 1) + " dB");
            expect (attenDb < -80.0);
        }
    }
};
static BridgeResamplerTest bridgeResamplerTest;

struct BridgeStreamTest : juce::UnitTest
{
    BridgeStreamTest() : juce::UnitTest ("Bridge stream") {}

    void runTest() override
    {
        using namespace bridgesim;

        for (double ppm : { -300.0, -50.0, 0.0, 80.0, 300.0 })
        {
            beginTest ("clock drift " + juce::String (ppm, 0) + " ppm: no gaps, converges, tracks drift");
            Scenario sc;
            sc.inputDriftPpm = ppm;
            sc.seconds = 180.0;
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expectEquals ((int) r.underruns, 0);
            expectEquals ((int) r.droppedBlocks, 0);
            expectEquals ((int) r.clicksAfterStart, 0);
            expect (std::abs (r.finalDriftPpm - ppm) < 25.0, "learned drift " + juce::String (r.finalDriftPpm, 1));
            expect (r.finalMarginMs > 5.0 && r.finalMarginMs < 25.0, "margin " + juce::String (r.finalMarginMs, 2));
            expectLessOrEqual (r.maxSpeedDeviationPpm, 501.0);   // beyond the drift: at most 0.05 %
            expectLessThan (r.maxPitchErrorPpmLast60s, 50.0, "settled: playback speed = microphone clock");
        }

        for (double ppm : { -1000.0, 1000.0 })
        {
            // Beyond the proportional range (+-500 ppm): only the integral can follow. Worst case
            // of the USB frame clock tolerance (+-500 ppm each side); real devices are far closer.
            beginTest ("clock drift " + juce::String (ppm, 0) + " ppm (worst case): learned completely, pitch steady afterwards");
            Scenario sc;
            sc.inputDriftPpm = ppm;
            sc.seconds = 240.0;
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expect (std::abs (r.finalDriftPpm - ppm) < 25.0, "learned drift " + juce::String (r.finalDriftPpm, 1));
            expectLessOrEqual ((int) r.underruns, 1);   // while learning only
            expectEquals ((int) r.resyncs, 0);
            expectEquals ((int) r.clicksAfterStart, 0);
            expectLessOrEqual (r.maxSpeedDeviationPpm, 501.0);
            expectLessThan (r.maxPitchErrorPpmLast60s, 100.0, "settled: playback speed = microphone clock");
        }

        beginTest ("producer jitter up to 6 ms is absorbed by a 10 ms safety buffer");
        {
            Scenario sc;
            sc.inputDriftPpm = 120.0;
            sc.inputJitterMs = 6.0;
            sc.seconds = 120.0;
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expectEquals ((int) r.underruns, 0);
            expectEquals ((int) r.clicksAfterStart, 0);
        }

        beginTest ("producer stall longer than the cushion: one faded gap, then recovery");
        {
            Scenario sc;
            sc.seconds = 60.0;
            sc.stalls = { { 20.0, 0.060 } };   // 60 ms stall at t = 20 s
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expectEquals ((int) r.underruns, 1);
            expectEquals ((int) r.clicksAfterStart, 0);   // gap is faded, not a click
            expectGreaterThan (r.finalSafetyMs, AudioBridge::autoStartMs);   // auto mode raised the cushion
            expect (r.outputRunningAtEnd);
        }

        beginTest ("tiny output blocks and repeated starvation: every gap is faded, never a click");
        {
            // 32-frame output periods (shorter than the 2 ms fade) and the smallest fixed cushion,
            // with a producer stall every 0.5 s: many underruns, re-primes and fades back to back.
            Scenario sc;
            sc.outputBlock = 32;
            sc.safetyMs = AudioBridge::minSafetyMs;
            sc.autoSafety = false;
            sc.seconds = 30.0;
            for (double t = 2.0; t < sc.seconds; t += 0.5)
                sc.stalls.push_back ({ t, 0.015 });
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expectGreaterThan ((int) r.underruns, 10);
            expectEquals ((int) r.clicksAfterStart, 0);
            expect (r.outputRunningAtEnd);
        }

        beginTest ("output stalls longer than the ring right after an underrun: recovers, no deadlock");
        {
            // Producer stall -> underrun (priming); the output thread then freezes for 2 s while the
            // producer keeps pushing: the ring (1.36 s) would overflow during priming.
            Scenario sc;
            sc.seconds = 30.0;
            sc.stalls = { { 10.0, 0.060 } };
            sc.consumerStalls = { { 10.04, 2.0 } };
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expectGreaterOrEqual ((int) r.underruns, 1);
            expectEquals ((int) r.clicksAfterStart, 0);   // silent while priming, fresh audio fades in
            expect (r.outputRunningAtEnd, "audio flows again after the stall");
            expect (r.finalMarginMs < 25.0, "latency back at the target");
        }

        beginTest ("output stalls longer than the ring while running: no click at the dropped input");
        {
            // The ring overflows and input blocks are dropped; afterwards the queued audio jumps at
            // the drop point - that jump must not be heard. The device gap itself clicks at both
            // edges (output froze mid-signal; on resume the device first plays the block it had
            // taken before freezing): 2 + 2 samples, nothing else.
            Scenario sc;
            sc.seconds = 30.0;
            sc.consumerStalls = { { 10.0, 2.0 } };
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expectGreaterThan ((int) r.droppedBlocks, 0);
            expectLessOrEqual ((int) r.clicksAfterStart, 4);
            expect (r.outputRunningAtEnd, "audio flows again after the stall");
            expect (r.finalMarginMs < 25.0, "latency back at the target");
        }

        beginTest ("consumer stall: queue overflow is trimmed back (resync), no underrun");
        {
            Scenario sc;
            sc.seconds = 60.0;
            sc.consumerStalls = { { 20.0, 0.200 } };
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expectGreaterOrEqual ((int) r.resyncs, 1);
            expectEquals ((int) r.underruns, 0);
            expect (r.finalMarginMs < 25.0, "latency returned to target");
        }

        beginTest ("output device gap (60 ms stall): excess latency trimmed once at a matching splice point, no pitch bend");
        {
            // The cable thread freezes for 60 ms: the device plays ~38 ms of silence while the
            // microphone keeps delivering, so ~38 ms too much audio is queued afterwards. Up to
            // 1.2.0 this was played off with up to +0.5 % speed (8.6 cent) for seconds.
            Scenario sc;
            sc.seconds = 40.0;
            sc.consumerStalls = { { 20.0, 0.060 } };
            std::vector<float> y;
            const auto r = runBridge (sc, &y);
            logMessage ("  " + r.describe());
            expectEquals ((int) r.resyncs, 1);
            expectEquals ((int) r.underruns, 0);
            expectLessOrEqual (r.maxSpeedDeviationPpm, 501.0);
            expect (r.finalMarginMs < 25.0, "latency back at the target");

            if (r.resyncAt.size() == 1)
            {
                const size_t at = r.resyncAt[0];
                const double cps = sc.toneHz / sc.outputRate;
                const double limit = 3.0 * 0.5 * std::pow (juce::MathConstants<double>::twoPi * cps, 2.0) + 2.0e-4;
                int clicks = 0;
                for (size_t i = at - 480; i < at + 2400 && i + 1 < y.size(); ++i)
                    if (std::abs ((double) y[i + 1] - 2.0 * y[i] + y[i - 1]) > limit) ++clicks;
                expectEquals (clicks, 0);

                // level during the crossfade (RMS over two periods of the tone)
                double lo = 1.0e9, hi = 0.0;
                for (size_t i = at - 240; i < at + 960; i += 8)
                {
                    double e = 0.0;
                    for (size_t j = i - 48; j < i + 48; ++j) e += (double) y[j] * y[j];
                    const double ampl = std::sqrt (2.0 * e / 96.0);
                    lo = std::min (lo, ampl); hi = std::max (hi, ampl);
                }
                logMessage ("  splice at sample " + juce::String ((int) at) + ": level " + juce::String (juce::Decibels::gainToDecibels (lo / 0.5), 2)
                            + " .. " + juce::String (juce::Decibels::gainToDecibels (hi / 0.5), 2) + " dB");
                expect (juce::Decibels::gainToDecibels (lo / 0.5) > -0.5 && juce::Decibels::gainToDecibels (hi / 0.5) < 0.5,
                        "no dip or swell at the splice");
            }
        }

        beginTest ("short output gap (30 ms stall): the small excess drains slowly, no trim, no pitch bend");
        {
            Scenario quiet;
            quiet.seconds = 70.0;
            const auto base = runBridge (quiet);
            Scenario sc = quiet;
            sc.consumerStalls = { { 10.0, 0.030 } };
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expectEquals ((int) r.resyncs, 0);
            expectEquals ((int) r.underruns, 0);
            expectLessOrEqual (r.maxSpeedDeviationPpm, 501.0);
            expectLessThan (std::abs (r.finalMarginMs - base.finalMarginMs), 1.5, "latency back at the target");
        }

        beginTest ("larger safety buffer chosen while running: applied at once (fade-out, refill, fade-in)");
        {
            Scenario sc;
            sc.seconds = 30.0;
            sc.safetyChanges = { { 10.0, 40.0 } };
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expectEquals ((int) r.underruns, 0);   // a requested refill is not an underrun
            expectEquals ((int) r.clicksAfterStart, 0);
            expect (r.finalMarginMs > 35.0 && r.finalMarginMs < 50.0, "margin " + juce::String (r.finalMarginMs, 2));
            expectLessOrEqual (r.maxSpeedDeviationPpm, 501.0);
        }

        beginTest ("smaller safety buffer chosen while running: the latency is trimmed at a matching splice point");
        {
            Scenario sc;
            sc.seconds = 40.0;
            sc.safetyChanges = { { 5.0, 40.0 }, { 20.0, 10.0 } };
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expectEquals ((int) r.resyncs, 1);
            expectEquals ((int) r.underruns, 0);
            expectEquals ((int) r.clicksAfterStart, 0);
            expect (r.finalMarginMs < 20.0, "margin " + juce::String (r.finalMarginMs, 2));
        }

        beginTest ("44.1 kHz microphone into a 48 kHz cable");
        {
            Scenario sc;
            sc.inputRate = 44100.0;
            sc.inputBlock = 441;
            sc.inputDriftPpm = 60.0;
            sc.seconds = 90.0;
            const auto r = runBridge (sc);
            logMessage ("  " + r.describe());
            expectEquals ((int) r.underruns, 0);
            expectEquals ((int) r.clicksAfterStart, 0);
        }

        beginTest ("fade out and hold before a device stops, then resume: no click, silence in between");
        {
            AudioBridge b;
            b.setConsumerActive (true);
            const double cps = 997.0 / 48000.0;
            auto tone = sine (48000 * 6, cps);
            std::vector<float> y, block (480);
            const double lim = 3.0 * 0.5 * std::pow (juce::MathConstants<double>::twoPi * cps, 2.0) + 2.0e-4;
            size_t holdAt = 0, resumeAt = 0;
            for (int k = 0; k < 500; ++k)   // 5 s: push one block, pull one block every 10 ms
            {
                const float* in[] = { tone.data() + (size_t) k * 480 };
                b.push (in, 1, 480, k * 0.01);
                if (k == 200) { b.fadeOutAndHold(); holdAt = y.size(); }
                if (k == 300) { b.resume();         resumeAt = y.size(); }
                float* out[] = { block.data() };
                b.pull (out, 1, 480, k * 0.01 + 0.001);
                if (k == 210) expect (! b.getStats().running, "silent while held");
                y.insert (y.end(), block.begin(), block.end());
            }
            int clicks = 0;
            for (size_t i = 1; i + 1 < y.size(); ++i)
                if (std::abs ((double) y[i + 1] - 2.0 * y[i] + y[i - 1]) > lim) ++clicks;
            expectEquals (clicks, 0);
            float heldPeak = 0.0f;
            for (size_t i = holdAt + 480; i < resumeAt; ++i) heldPeak = std::max (heldPeak, std::abs (y[i]));
            expectEquals (heldPeak, 0.0f);
            float after = 0.0f;
            for (size_t i = resumeAt + 4800; i < y.size(); ++i) after = std::max (after, std::abs (y[i]));
            expectGreaterThan (after, 0.45f, "audio again after resume");
            expectEquals ((int) b.getStats().underruns, 0);   // a requested pause is not an underrun
        }

        beginTest ("inactive consumer: push is a no-op (nothing queued, nothing counted)");
        {
            AudioBridge b;
            b.setConsumerActive (false);
            std::vector<float> x (480, 0.25f);
            const float* ch[] = { x.data() };
            for (int i = 0; i < 1000; ++i) b.push (ch, 1, 480, i * 0.01);
            expectEquals ((int) b.getStats().droppedBlocks, 0);
        }
    }
};
static BridgeStreamTest bridgeStreamTest;

//==============================================================================
struct InputQueueTest : juce::UnitTest
{
    InputQueueTest() : juce::UnitTest ("Input queue") {}

    static double secondDifferenceLimit (double amp, double cps)
    {
        return 3.0 * amp * std::pow (juce::MathConstants<double>::twoPi * cps, 2.0) + 2.0e-4;
    }

    void runTest() override
    {
        beginTest ("round trip across the wrap point: every frame arrives once, in order");
        {
            InputQueue q;
            const int sizes[] = { 7, 480, 1000, 333, 4096, 1 };
            std::vector<float> blockData (4096), l (480), r (480);
            int64_t written = 0, read = 0;
            bool ordered = true;
            for (int round = 0; round < 2000; ++round)
            {
                const int n = sizes[round % 6];
                for (int i = 0; i < n; ++i) blockData[(size_t) i] = (float) ((written + i) % 1000000);
                const float* ch[] = { blockData.data() };
                expect (q.write (ch, 1, n, 0.0));
                written += n;
                while (q.available() >= 480)
                {
                    float* dst[] = { l.data(), r.data() };
                    expectEquals ((int) q.readBlock (dst, 480, InputQueue::capacity), 0);
                    for (int i = 0; i < 480; ++i)
                    {
                        ordered = ordered && l[(size_t) i] == (float) ((read + i) % 1000000) && r[(size_t) i] == 0.0f;
                    }
                    read += 480;
                }
            }
            expect (written > 3 * InputQueue::capacity, "wrapped several times");
            expect (ordered, "frames in order, missing channel silent");
            expectEquals ((int) q.getOverflows(), 0);
        }

        beginTest ("two threads: no frame lost, duplicated or reordered");
        {
            InputQueue q;
            constexpr int64_t total = 3000000;
            std::atomic<bool> producerDone { false };
            std::thread producer ([&]
            {
                std::vector<float> l (480), r (480);
                for (int64_t w = 0; w < total;)
                {
                    for (int i = 0; i < 480; ++i) { l[(size_t) i] = (float) ((w + i) % 1000000); r[(size_t) i] = -l[(size_t) i]; }
                    const float* ch[] = { l.data(), r.data() };
                    if (q.write (ch, 2, 480, 0.0)) w += 480;
                    else std::this_thread::yield();
                }
                producerDone = true;
            });

            std::vector<float> l (480), r (480);
            int64_t read = 0, errors = 0;
            while (read < total)
            {
                float* dst[] = { l.data(), r.data() };
                if (q.readBlock (dst, 480, InputQueue::capacity) < 0) { std::this_thread::yield(); continue; }
                for (int i = 0; i < 480; ++i)
                    if (l[(size_t) i] != (float) ((read + i) % 1000000) || r[(size_t) i] != -l[(size_t) i]) ++errors;
                read += 480;
            }
            producer.join();
            logMessage ("  " + juce::String (total) + " frames, producer overflows (retried) " + juce::String ((int) q.getOverflows()));
            expectEquals ((int) errors, 0);
        }

        beginTest ("chain fell behind by more than the limit: one crossfaded block, then the newest audio");
        {
            InputQueue q;
            const double rate = 48000.0, cps = 997.0 / rate;
            const auto limit = InputQueue::backlogLimit (rate, 480);
            expectEquals ((int) limit, 5760);   // 120 ms at 48 kHz

            std::vector<float> src (14400 + 480);
            for (size_t i = 0; i < src.size(); ++i)
                src[i] = (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * cps * (double) i));

            const float* ch0[] = { src.data() };
            expect (q.write (ch0, 1, 14400, 1.0));          // 300 ms queued, nothing read

            std::vector<float> out, l (480), r (480);
            float* dst[] = { l.data(), r.data() };
            const auto skipped = q.readBlock (dst, 480, limit);
            expectEquals ((int) skipped, 14400 - 480);     // from the oldest block to the newest
            expectEquals ((int) q.available(), 0);
            out.insert (out.end(), l.begin(), l.end());

            const float* ch1[] = { src.data() + 14400 };   // the stream continues
            expect (q.write (ch1, 1, 480, 1.3));
            expectEquals ((int) q.readBlock (dst, 480, limit), 0);
            out.insert (out.end(), l.begin(), l.end());

            // equal-power crossfade from the oldest block (src[0..479]) into the newest
            // (src[13920..14399]); no click inside or at the seam to the next block
            for (int i : { 0, 240, 479 })
            {
                const double th = juce::MathConstants<double>::halfPi * (i + 0.5) / 480.0;
                expectWithinAbsoluteError ((double) out[(size_t) i],
                                           std::cos (th) * src[(size_t) i] + std::sin (th) * src[(size_t) (13920 + i)], 1.0e-6);
            }
            int clicks = 0;
            for (size_t i = 1; i + 1 < out.size(); ++i)
                if (std::abs ((double) out[i + 1] - 2.0 * out[i] + out[i - 1]) > secondDifferenceLimit (0.5, cps)) ++clicks;
            expectEquals (clicks, 0);
        }

        beginTest ("skip with alignment: the splice follows the waveform (voice-like tone, no dip, no click)");
        {
            const double rate = 48000.0, f0 = 220.0;
            std::vector<float> src (14400 + 4800);
            for (size_t i = 0; i < src.size(); ++i)
            {
                double v = 0.0;
                for (int k = 1; k <= 5; ++k)
                    v += (0.35 / k) * std::sin (juce::MathConstants<double>::twoPi * f0 * k * (double) i / rate + 0.3 * k);
                src[i] = (float) v;
            }
            float peak = 0.0f; for (auto v : src) peak = std::max (peak, std::abs (v));

            for (int aligned = 0; aligned < 2; ++aligned)
            {
                InputQueue q;
                const auto limit = InputQueue::backlogLimit (rate, 480);
                const float* ch0[] = { src.data() };
                q.write (ch0, 1, 14400, 1.0);
                std::vector<float> l (480), r (480);
                float* dst[] = { l.data(), r.data() };
                const auto skipped = q.readBlock (dst, 480, limit, aligned ? InputQueue::alignRange (rate) : 0);
                const int range = InputQueue::alignRange (rate);
                if (aligned) expect (skipped >= 14400 - 480 - range && skipped <= 14400 - 480, "skip " + juce::String ((int) skipped));
                else         expectEquals ((int) skipped, 14400 - 480);

                // deviation from the old waveform during the crossfade (a perfect splice of a periodic
                // signal changes nothing)
                double worst = 0.0;
                for (int i = 0; i < 480; ++i) worst = std::max (worst, (double) std::abs (l[(size_t) i] - src[(size_t) i]));
                logMessage (juce::String (aligned ? "  aligned" : "  plain") + ": skipped " + juce::String ((int) skipped)
                            + ", largest deviation during the crossfade " + juce::String (100.0 * worst / peak, 1) + " % of the peak");
                if (aligned)
                {
                    expectLessThan (worst / peak, 0.08);

                    // continues seamlessly with the audio after the splice point
                    std::vector<float> out (l.begin(), l.end());
                    const float* ch1[] = { src.data() + 14400 };
                    q.write (ch1, 1, 4800, 1.1);
                    while (q.readBlock (dst, 480, limit, range) == 0) out.insert (out.end(), l.begin(), l.end());
                    const double lim = 3.0 * 0.35 * 2.3 * std::pow (juce::MathConstants<double>::twoPi * 5.0 * f0 / rate, 2.0) + 2.0e-4;
                    int clicks = 0;
                    for (size_t i = 1; i + 1 < out.size(); ++i)
                        if (std::abs ((double) out[i + 1] - 2.0 * out[i] + out[i - 1]) > lim) ++clicks;
                    expectEquals (clicks, 0);
                    expectEquals ((int) out.size(), 480 + 4800 + (int) (14400 - 480 - skipped) - (int) ((14400 - 480 - skipped + 4800) % 480) );
                }
            }
        }

        beginTest ("capture time of the newest frame read");
        {
            InputQueue q;
            std::vector<float> x (480, 0.1f);
            const float* ch[] = { x.data() };
            for (int k = 0; k < 3; ++k) q.write (ch, 1, 480, 2.0 + 0.01 * k);   // last write at 2.02 s
            std::vector<float> l (480), r (480);
            float* dst[] = { l.data(), r.data() };
            q.readBlock (dst, 480, InputQueue::capacity);
            double t = 0.0;
            expect (q.lastReadCaptureTime (48000.0, t));
            expectWithinAbsoluteError (t, 2.02 - 960.0 / 48000.0, 1.0e-9);   // two blocks are newer
        }

        beginTest ("consumer stuck: the producer drops (counted) instead of overwriting");
        {
            InputQueue q;
            std::vector<float> x (4096, 0.0f);
            const float* ch[] = { x.data() };
            int accepted = 0;
            for (int k = 0; k < InputQueue::capacity / 4096 + 2; ++k)
                accepted += q.write (ch, 1, 4096, 0.0) ? 1 : 0;
            expectEquals (accepted, InputQueue::capacity / 4096);
            expectEquals ((int) q.getOverflows(), 2);
        }
    }
};
static InputQueueTest inputQueueTest;

//==============================================================================
// The microphone path under plug-in overload: 1.2.0 (plug-ins on the WASAPI capture thread) vs.
// 1.2.1 (separate plug-in thread behind the InputQueue). Same timing model for both.
struct InputPathTest : juce::UnitTest
{
    InputPathTest() : juce::UnitTest ("Input path") {}

    void runTest() override
    {
        using namespace bridgesim;

        beginTest ("render load like the Topaz log (45 % of blocks 1.1-1.9x real time): 1.2.0 loses audio, 1.2.1 none");
        {
            Scenario sc;
            sc.baseLoad = 0.4; sc.inputJitterMs = 1.0; sc.spikeChance = 0.45; sc.spikeMin = 1.1; sc.spikeMax = 1.9;
            sc.inputDriftPpm = 40.0; sc.seconds = 90.0;
            const auto before = runBridge (sc, nullptr, InputPath::captureThread);
            const auto after  = runBridge (sc, nullptr, InputPath::separateThread);
            logMessage ("  1.2.0: " + before.describe());
            logMessage ("  1.2.1: " + after.describe());
            expectGreaterThan (before.captureOverflows, 0);
            expectGreaterThan (before.clickEvents, 0);
            expectEquals (after.captureOverflows, 0);
            expectEquals (after.clickEvents, 0);
            expectEquals ((int) after.inputSkips, 0);
            expect (after.outputRunningAtEnd);
        }

        beginTest ("chain slower than real time (avg. 112 %): crossfaded skips, bounded latency, no clicks");
        {
            Scenario sc;
            sc.baseLoad = 0.35; sc.inputJitterMs = 1.0; sc.spikeChance = 0.6; sc.spikeMin = 1.2; sc.spikeMax = 2.0;
            sc.seconds = 60.0;
            const auto r = runBridge (sc, nullptr, InputPath::separateThread);
            logMessage ("  " + r.describe());
            expectEquals (r.captureOverflows, 0);
            expectEquals (r.clickEvents, 0);
            expectGreaterThan ((int) r.inputSkips, 0);
            // queue limit (120 ms) + the block itself + its processing time (<= 2 blocks)
            expectLessThan (r.maxInputDelayMs, 120.0 + 10.0 + 20.0 + 1.0);
        }

        beginTest ("60 ms plug-in stall every 3 s: nothing lost at the capture side");
        {
            Scenario sc;
            sc.seconds = 60.0; sc.inputJitterMs = 1.0;
            for (double t = 2.0; t < sc.seconds; t += 3.0) sc.stalls.push_back ({ t, 0.060 });
            const auto before = runBridge (sc, nullptr, InputPath::captureThread);
            const auto after  = runBridge (sc, nullptr, InputPath::separateThread);
            logMessage ("  1.2.0: " + before.describe());
            logMessage ("  1.2.1: " + after.describe());
            expectGreaterThan (before.captureOverflows, 0);
            expectEquals (after.captureOverflows, 0);
            expectEquals (after.clickEvents, 0);
        }

        beginTest ("whole system frozen 30 ms (capture thread too): audio is lost in either design");
        {
            // Documents the limit: a separate plug-in thread cannot help when Windows does not run
            // the capture thread itself (DPC/driver storms).
            Scenario sc;
            sc.seconds = 30.0; sc.inputJitterMs = 1.0;
            for (double t = 5.0; t < sc.seconds; t += 6.0)
            {
                sc.stalls.push_back ({ t, 0.030 });
                sc.captureStalls.push_back ({ t, 0.030 });
            }
            const auto after = runBridge (sc, nullptr, InputPath::separateThread);
            logMessage ("  1.2.1: " + after.describe());
            expectGreaterThan (after.captureOverflows, 0);
        }
    }
};
static InputPathTest inputPathTest;
