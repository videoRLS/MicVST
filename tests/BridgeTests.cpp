#include <juce_audio_basics/juce_audio_basics.h>
#include "audio/AudioBridge.h"
#include "BridgeSim.h"
#include <cmath>
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
