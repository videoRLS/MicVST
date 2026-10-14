#pragma once
// Deterministic simulation of two independently clocked audio devices around an AudioBridge.
// Used by the unit tests and by the MicVSTStress comparison tool. Virtual time, no threads:
// every scenario is exactly reproducible.
#include "audio/AudioBridge.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <utility>
#include <vector>

namespace bridgesim
{
    struct Stall { double atSeconds; double durationSeconds; };

    struct Scenario
    {
        double inputRate = 48000.0, outputRate = 48000.0;
        int    inputBlock = 480, outputBlock = 480;
        double inputDriftPpm = 0.0;     // real input clock = nominal * (1 + ppm)
        double inputJitterMs = 0.0;     // extra random delay of each processed input block (0..x)
        double seconds = 60.0;
        double safetyMs = AudioBridge::autoStartMs;
        bool   autoSafety = true;
        std::vector<Stall> stalls;          // input/processing thread stalls (blocks catch up after)
        std::vector<Stall> consumerStalls;  // output thread stalls
        double toneHz = 997.0;
        double baseLoad = 0.25;             // plug-in chain CPU time as share of the block (both designs)
        uint32_t seed = 1234;
    };

    struct Result
    {
        uint64_t underruns = 0, droppedBlocks = 0, resyncs = 0;
        int      clicksAfterStart = 0;
        double   finalDriftPpm = 0.0, finalMarginMs = 0.0, finalSafetyMs = 0.0, meanLatencyMs = 0.0;
        double   maxCorrectionPpmAfter60s = 0.0;
        bool     outputRunningAtEnd = false;
        bool     cannotOpen = false;        // old design: devices with different sample rates fail
        int      gapEvents = 0;             // old design: zero-padded blocks + render underruns + capture overflows

        juce::String describe() const
        {
            return "underruns " + juce::String ((int) underruns) + ", dropped " + juce::String ((int) droppedBlocks)
                 + ", resyncs " + juce::String ((int) resyncs) + ", clicks " + juce::String (clicksAfterStart)
                 + ", drift " + juce::String (finalDriftPpm, 1) + " ppm, margin " + juce::String (finalMarginMs, 2)
                 + " ms, safety " + juce::String (finalSafetyMs, 1) + " ms, latency "
                 + juce::String (meanLatencyMs, 2) + " ms";
        }
    };

    inline bool insideStall (const std::vector<Stall>& stalls, double t, double& stallEnd)
    {
        for (auto& s : stalls)
            if (t >= s.atSeconds && t < s.atSeconds + s.durationSeconds)
            {
                stallEnd = s.atSeconds + s.durationSeconds;
                return true;
            }
        return false;
    }

    // Counts sample-to-sample discontinuities: for a sine of amplitude A and frequency f the
    // second difference never exceeds A*(2*pi*f/fs)^2; a click / gap / zero-padded block does.
    inline int countClicks (const std::vector<float>& y, size_t from, double limit)
    {
        int clicks = 0;
        for (size_t i = std::max<size_t> (from, 1); i + 1 < y.size(); ++i)
            if (std::abs ((double) y[i + 1] - 2.0 * y[i] + y[i - 1]) > limit)
                ++clicks;
        return clicks;
    }

    //==============================================================================
    // Model of MicVST <= 1.1.1: ONE JUCE WASAPI device thread for microphone + cable, paced by
    // the cable (render events). Per iteration (see juce_WASAPI_windows.cpp, run()):
    //   read one block from the input reservoir (missing samples are ZERO-PADDED at the start),
    //   process it, then wait for room in the render buffer and write it.
    // A processing delay longer than the render buffer slack lets the cable run dry; a faster
    // microphone fills reservoir + capture buffer until WASAPI discards a packet.
    inline Result runOldCoupled (const Scenario& sc, std::vector<float>* outputCapture = nullptr)
    {
        Result r;
        if (std::abs (sc.inputRate - sc.outputRate) > 0.5)
        {
            r.cannotOpen = true;   // JUCE's combined device opens both at ONE rate
            return r;
        }

        const int    B = sc.outputBlock;
        const double rate = sc.outputRate;
        const double inFrameRate = sc.inputRate * (1.0 + sc.inputDriftPpm * 1.0e-6);
        const double period = B / rate;
        const auto   renderCap = (int64_t) std::lround (2.2 * B);    // shared-mode buffer ~ 2 periods
        int64_t reservoirCap = 1;
        while (reservoirCap < renderCap + B) reservoirCap <<= 1;      // nextPowerOfTwo(actualBuffer + B)
        const int64_t captureCap = renderCap;

        std::mt19937 rng (sc.seed);
        std::uniform_real_distribution<double> jitter (0.0, sc.inputJitterMs / 1000.0);
        const double amp = 0.5, cps = sc.toneHz / sc.inputRate;

        std::vector<float> y ((size_t) renderCap, 0.0f);   // start: the buffer is pre-filled with silence
        y.reserve ((size_t) (sc.seconds * rate) + 8192);
        std::vector<float> block ((size_t) B);

        int64_t written = renderCap, consumedIn = 0, droppedIn = 0;
        int zeroPads = 0, renderUnderruns = 0, overflows = 0;
        double latencySum = 0.0; int latencyCount = 0;
        double t = 0.0;

        auto capturedAt = [&] (double tt) { return (int64_t) std::floor (tt * inFrameRate / B) * B; };
        auto playedAt   = [&] (double tt) { return (int64_t) std::floor (tt * rate); };

        while (t < sc.seconds)
        {
            double stallEnd = 0.0;
            if (insideStall (sc.stalls, t, stallEnd) || insideStall (sc.consumerStalls, t, stallEnd))
                t = stallEnd;

            // capture side: what WASAPI + JUCE's reservoir can hold; older packets are discarded
            int64_t unread = capturedAt (t) - consumedIn - droppedIn;
            if (unread > reservoirCap + captureCap)
            {
                droppedIn += unread - (reservoirCap + captureCap);
                ++overflows;
                unread = reservoirCap + captureCap;
            }

            const int take = (int) std::min<int64_t> (unread, B);
            const int pad  = B - take;
            if (pad > 0 && t > 0.5) ++zeroPads;

            for (int i = 0; i < pad; ++i) block[(size_t) i] = 0.0f;
            for (int i = 0; i < take; ++i)
                block[(size_t) (pad + i)] = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * cps
                                                                     * (double) (consumedIn + droppedIn + i)));
            consumedIn += take;

            t += sc.baseLoad * period + jitter (rng);   // plug-in processing on the same thread
            if (insideStall (sc.stalls, t, stallEnd)) t = stallEnd;

            int64_t played = playedAt (t);
            if (played > written)   // the cable ran dry while we were busy: silence was played
            {
                if (t > 0.5) ++renderUnderruns;
                y.insert (y.end(), (size_t) (played - written), 0.0f);
                written = played;
            }

            const int64_t needPlayed = written + B - renderCap;   // wait for room (render events)
            if (needPlayed > played)
                t = std::ceil ((double) needPlayed / rate / period - 1.0e-9) * period;

            y.insert (y.end(), block.begin(), block.end());
            written += B;

            latencySum += 1000.0 * ((double) (written - playedAt (t)) + (double) (capturedAt (t) - consumedIn - droppedIn)) / rate;
            ++latencyCount;
        }

        r.underruns = (uint64_t) renderUnderruns;
        r.droppedBlocks = (uint64_t) overflows;
        r.gapEvents = zeroPads + renderUnderruns + overflows;
        r.meanLatencyMs = latencyCount > 0 ? latencySum / latencyCount : 0.0;
        const double limit = 3.0 * amp * std::pow (juce::MathConstants<double>::twoPi * cps, 2.0) + 2.0e-4;
        r.clicksAfterStart = countClicks (y, (size_t) (rate * 0.5), limit);
        if (outputCapture != nullptr) *outputCapture = std::move (y);
        return r;
    }

    //==============================================================================
    // MicVST 1.2: input thread (processing) -> AudioBridge -> output thread. The output side is
    // modelled like JUCE's output-only WASAPI loop: pull a block, wait for room in the render
    // buffer, write it; after a stall it refills the render buffer back-to-back. If the output
    // thread itself is frozen longer than the render buffer lasts, the device plays silence -
    // that gap is part of the captured device output, exactly as in runOldCoupled().
    inline Result runBridge (const Scenario& sc, std::vector<float>* outputCapture = nullptr)
    {
        AudioBridge bridge;
        bridge.setInputRate (sc.inputRate);
        bridge.setOutputRate (sc.outputRate);
        bridge.setSafety (sc.safetyMs, sc.autoSafety);
        bridge.setConsumerActive (true);

        std::mt19937 rng (sc.seed);
        std::uniform_real_distribution<double> jitter (0.0, sc.inputJitterMs / 1000.0);

        const int    B = sc.outputBlock;
        const double rate = sc.outputRate;
        const double inPeriod  = sc.inputBlock / (sc.inputRate * (1.0 + sc.inputDriftPpm * 1.0e-6));
        const double outPeriod = B / rate;
        const auto   renderCap = (int64_t) std::lround (2.2 * B);
        const double amp = 0.5, cps = sc.toneHz / sc.inputRate;

        std::vector<float> inBuf ((size_t) sc.inputBlock), block ((size_t) B);
        std::vector<float> y ((size_t) renderCap, 0.0f);   // render buffer pre-filled with silence
        y.reserve ((size_t) (sc.seconds * rate) + 8192);

        int64_t inBlockIndex = 0, inputSample = 0, written = renderCap;
        double lastInTime = 0.0, tCons = 0.0;
        size_t firstRunningSample = (size_t) -1;
        double latencySum = 0.0, marginSum = 0.0, totalLatencySum = 0.0;
        int latencyCount = 0, totalLatencyCount = 0, deviceGaps = 0;
        Result r;

        auto playedAt = [&] (double tt) { return (int64_t) std::floor (tt * rate); };
        auto nextInputTime = [&]
        {
            // Block k is complete at (k+1)*inPeriod. One thread processes the blocks in order
            // (a queue): it starts when the block is there AND the previous one is done, needs
            // baseLoad of a block plus random jitter, and stalls freeze it.
            double start = std::max ((double) (inBlockIndex + 1) * inPeriod, lastInTime);
            double stallEnd = 0.0;
            if (insideStall (sc.stalls, start, stallEnd)) start = stallEnd;
            double t = start + sc.baseLoad * (sc.inputBlock / sc.inputRate) + jitter (rng);
            if (insideStall (sc.stalls, t, stallEnd)) t = stallEnd;
            return t;
        };

        double tIn = nextInputTime();

        while (std::min (tIn, tCons) < sc.seconds)
        {
            if (tIn <= tCons)
            {
                for (int i = 0; i < sc.inputBlock; ++i, ++inputSample)
                    inBuf[(size_t) i] = (float) (amp * std::sin (juce::MathConstants<double>::twoPi * cps * (double) inputSample));
                const float* chans[] = { inBuf.data() };
                bridge.push (chans, 1, sc.inputBlock, tIn);
                lastInTime = tIn;
                ++inBlockIndex;
                tIn = nextInputTime();
                continue;
            }

            double t = tCons, stallEnd = 0.0;
            if (insideStall (sc.consumerStalls, t, stallEnd)) t = stallEnd;

            float* chans[] = { block.data() };
            bridge.pull (chans, 1, B, t);
            const auto st = bridge.getStats();
            if (st.running && firstRunningSample == (size_t) -1)
                firstRunningSample = y.size();

            // wait for room in the render buffer (render events every period)
            const int64_t needPlayed = written + B - renderCap;
            if (needPlayed > playedAt (t))
                t = std::ceil ((double) needPlayed / rate / outPeriod - 1.0e-9) * outPeriod;
            if (insideStall (sc.consumerStalls, t, stallEnd)) t = stallEnd;

            const int64_t played = playedAt (t);
            if (played > written)   // output thread frozen longer than the render buffer lasted
            {
                if (t > 0.5) ++deviceGaps;
                y.insert (y.end(), (size_t) (played - written), 0.0f);
                written = played;
            }
            y.insert (y.end(), block.begin(), block.end());
            written += B;
            tCons = t;

            totalLatencySum += st.bufferedMs + 1000.0 * (double) (written - playedAt (t)) / rate;
            ++totalLatencyCount;
            if (t > sc.seconds - 10.0)
            {
                latencySum += st.bufferedMs;
                marginSum  += st.bufferedMs - 1000.0 * outPeriod - 1000.0 * bridge.getResamplerDelayFrames() / sc.inputRate;
                ++latencyCount;
            }
            if (t > 60.0)
                r.maxCorrectionPpmAfter60s = std::max (r.maxCorrectionPpmAfter60s, std::abs (st.correctionPpm));
        }

        const auto st = bridge.getStats();
        r.underruns = st.underruns;
        r.droppedBlocks = st.droppedBlocks;
        r.resyncs = st.resyncs;
        r.finalDriftPpm = st.driftPpm;
        r.finalSafetyMs = st.safetyMs;
        r.outputRunningAtEnd = st.running;
        r.gapEvents = (int) st.underruns + deviceGaps;
        r.meanLatencyMs = totalLatencyCount > 0 ? totalLatencySum / totalLatencyCount : 0.0;
        r.finalMarginMs = latencyCount > 0 ? marginSum / latencyCount : 0.0;
        (void) latencySum;

        const double cpsOut = sc.toneHz / sc.outputRate;
        const double limit = 3.0 * amp * std::pow (juce::MathConstants<double>::twoPi * cpsOut, 2.0) + 2.0e-4;
        if (firstRunningSample != (size_t) -1)
            r.clicksAfterStart = countClicks (y, firstRunningSample + 1, limit);

        if (outputCapture != nullptr) *outputCapture = std::move (y);
        return r;
    }
}
