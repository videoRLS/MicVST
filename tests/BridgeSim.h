#pragma once
// Deterministic simulation of two independently clocked audio devices around an AudioBridge.
// Used by the unit tests and by the MicVSTStress comparison tool. Virtual time, no threads:
// every scenario is exactly reproducible.
#include "audio/AudioBridge.h"
#include "audio/InputQueue.h"
#include <algorithm>
#include <deque>
#include <memory>
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
        std::vector<Stall> stalls;          // plug-in (processing) thread stalls (work catches up after)
        std::vector<Stall> captureStalls;   // the capture thread frozen too (e.g. DPC storm on all cores)
        std::vector<Stall> consumerStalls;  // output thread stalls
        std::vector<std::pair<double, double>> safetyChanges;   // (time, ms): fixed safety buffer set while running
        double toneHz = 997.0;
        double baseLoad = 0.25;             // plug-in chain CPU time as share of the block (both designs)
        double spikeChance = 0.0;           // share of plug-in blocks that are slow (CPU contention) ...
        double spikeMin = 0.0, spikeMax = 0.0;   // ... needing this much of the block time (uniform)
        double captureBufferPeriods = 2.2;  // WASAPI capture buffer in device periods (shared mode ~2.2)
        uint32_t seed = 1234;
    };

    // Where the plug-in chain runs relative to the microphone's WASAPI capture thread.
    enum class InputPath
    {
        captureThread,    // MicVST 1.2.0: on the capture thread itself - while a block is processed,
                          // only the WASAPI capture buffer (~2 periods) holds the incoming audio
        separateThread    // MicVST 1.2.1: capture thread -> InputQueue -> plug-in thread
    };

    struct Result
    {
        uint64_t underruns = 0, droppedBlocks = 0, resyncs = 0;
        int      clicksAfterStart = 0;      // output samples flagged as discontinuous
        int      clickEvents = 0;           // ... grouped into audible events (10 ms apart)
        double   finalDriftPpm = 0.0, finalMarginMs = 0.0, finalSafetyMs = 0.0, meanLatencyMs = 0.0;
        double   maxCorrectionPpmAfter60s = 0.0;
        double   maxSpeedDeviationPpm = 0.0;    // |correction - learned drift| after 5 s (the level-correcting part)
        double   maxPitchErrorPpmLast60s = 0.0; // |correction - true clock drift| during the last 60 s (audible error)
        std::vector<size_t> resyncAt;           // output sample index of each trim (start of its block)
        bool     outputRunningAtEnd = false;
        bool     cannotOpen = false;        // old design: devices with different sample rates fail
        int      gapEvents = 0;             // zero-padded blocks + underruns + device gaps + capture overflows
        int      captureOverflows = 0;      // WASAPI capture buffer overflowed = "microphone driver gaps"
        double   lostInputMs = 0.0;         // audio lost in those overflows
        uint64_t inputSkips = 0;            // 1.2.1: crossfaded skips because the chain fell behind
        double   skippedMs = 0.0;
        double   meanInputDelayMs = 0.0;    // capture -> push into the bridge (queueing + processing)
        double   maxInputDelayMs = 0.0;
        double   meanLoad = 0.0;            // plug-in time / block time, averaged over processed blocks
        double   lateBlockShare = 0.0;      // share of processed blocks slower than real time
        double   peakLoad = 0.0;

        juce::String describe() const
        {
            return "underruns " + juce::String ((int) underruns) + ", dropped " + juce::String ((int) droppedBlocks)
                 + ", resyncs " + juce::String ((int) resyncs) + ", clicks " + juce::String (clicksAfterStart)
                 + ", drift " + juce::String (finalDriftPpm, 1) + " ppm, margin " + juce::String (finalMarginMs, 2)
                 + " ms, safety " + juce::String (finalSafetyMs, 1) + " ms, latency "
                 + juce::String (meanLatencyMs, 2) + " ms, capture overflows " + juce::String (captureOverflows)
                 + " (" + juce::String (juce::roundToInt (lostInputMs)) + " ms lost), skips " + juce::String ((int) inputSkips)
                 + " (" + juce::String (juce::roundToInt (skippedMs)) + " ms), input delay mean " + juce::String (meanInputDelayMs, 1)
                 + " / max " + juce::String (maxInputDelayMs, 1) + " ms, load mean " + juce::String (juce::roundToInt (100.0 * meanLoad))
                 + " % (" + juce::String (juce::roundToInt (100.0 * lateBlockShare)) + " % of blocks late, peak "
                 + juce::String (juce::roundToInt (100.0 * peakLoad)) + " %), speed deviation max "
                 + juce::String (juce::roundToInt (maxSpeedDeviationPpm)) + " ppm, pitch error (last 60 s) max "
                 + juce::String (juce::roundToInt (maxPitchErrorPpmLast60s)) + " ppm";
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

    inline std::vector<Stall> mergedStalls (const std::vector<Stall>& a, const std::vector<Stall>& b)
    {
        auto m = a;
        m.insert (m.end(), b.begin(), b.end());
        std::sort (m.begin(), m.end(), [] (const Stall& x, const Stall& y) { return x.atSeconds < y.atSeconds; });
        return m;
    }

    // Finish time of 'work' seconds of thread time started at 'start' on a thread that is frozen
    // during the given stalls (the work continues after each stall).
    inline double advanceWork (double start, double work, const std::vector<Stall>& stalls)
    {
        double t = start, remaining = work;
        for (int guard = 0; guard < 100000; ++guard)
        {
            double stallEnd = 0.0;
            if (insideStall (stalls, t, stallEnd)) { t = stallEnd; continue; }

            double next = 1.0e300;
            for (auto& s : stalls)
                if (s.atSeconds > t) next = std::min (next, s.atSeconds);

            if (t + remaining <= next) return t + remaining;
            remaining -= next - t;
            t = next;
        }
        return t + remaining;
    }

    // Plug-in time per processed block: baseLoad + random jitter, or (spikeChance) a slow block.
    struct WorkModel
    {
        WorkModel (const Scenario& s, double blockSeconds) : sc (s), block (blockSeconds), rng (s.seed) {}

        double next()
        {
            double w;
            if (sc.spikeChance > 0.0 && uni (rng) < sc.spikeChance)
                w = block * (sc.spikeMin + (sc.spikeMax - sc.spikeMin) * uni (rng));
            else
                w = sc.baseLoad * block + uni (rng) * sc.inputJitterMs / 1000.0;

            const double load = w / block;
            loadSum += load;
            peak = std::max (peak, load);
            if (load > 1.0) ++late;
            ++count;
            return w;
        }

        void report (Result& r) const
        {
            r.meanLoad = count > 0 ? loadSum / (double) count : 0.0;
            r.lateBlockShare = count > 0 ? (double) late / (double) count : 0.0;
            r.peakLoad = peak;
        }

        const Scenario& sc;
        double block;
        std::mt19937 rng;
        std::uniform_real_distribution<double> uni { 0.0, 1.0 };
        double loadSum = 0.0, peak = 0.0;
        int64_t late = 0, count = 0;
    };

    // Counts sample-to-sample discontinuities: for a sine of amplitude A and frequency f the
    // second difference never exceeds A*(2*pi*f/fs)^2; a click / gap / zero-padded block does.
    inline int countClicks (const std::vector<float>& y, size_t from, double limit, int* events = nullptr,
                            size_t eventSpacing = 480)
    {
        int clicks = 0, ev = 0;
        size_t last = 0;
        for (size_t i = std::max<size_t> (from, 1); i + 1 < y.size(); ++i)
            if (std::abs ((double) y[i + 1] - 2.0 * y[i] + y[i - 1]) > limit)
            {
                // one audible event = flagged samples closer than eventSpacing (10 ms at 48 kHz):
                // a band-limited jump (resampler) is flagged on several neighbouring samples
                if (clicks == 0 || i - last > eventSpacing) ++ev;
                last = i;
                ++clicks;
            }
        if (events != nullptr) *events = ev;
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

        WorkModel work (sc, period);
        const auto stalls = mergedStalls (sc.stalls, sc.captureStalls);   // one thread for everything
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
            if (insideStall (stalls, t, stallEnd) || insideStall (sc.consumerStalls, t, stallEnd))
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

            t = advanceWork (t, work.next(), stalls);   // plug-in processing on the same thread

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

        work.report (r);
        r.underruns = (uint64_t) renderUnderruns;
        r.droppedBlocks = (uint64_t) overflows;
        r.captureOverflows = overflows;
        r.lostInputMs = 1000.0 * (double) droppedIn / sc.inputRate;
        r.gapEvents = zeroPads + renderUnderruns + overflows;
        r.meanLatencyMs = latencyCount > 0 ? latencySum / latencyCount : 0.0;
        const double limit = 3.0 * amp * std::pow (juce::MathConstants<double>::twoPi * cps, 2.0) + 2.0e-4;
        r.clicksAfterStart = countClicks (y, (size_t) (rate * 0.5), limit, &r.clickEvents, (size_t) (0.01 * rate));
        if (outputCapture != nullptr) *outputCapture = std::move (y);
        return r;
    }

    //==============================================================================
    // Microphone side of MicVST 1.2.x: produces the blocks that are pushed into the bridge, in time
    // order. The WASAPI capture model: packet k (one device period) is complete at
    // (k+1) * period; the capture buffer holds captureBufferPeriods (whole packets) - a packet that
    // arrives while it is full is lost and the next one read carries the "data discontinuity"
    // flag (one "microphone driver gap" per overflow episode).
    struct Push { double time = 0.0, stamp = 0.0; };

    class MicSide
    {
    public:
        explicit MicSide (const Scenario& s)
            : sc (s),
              packet (s.inputBlock),
              period ((double) s.inputBlock / (s.inputRate * (1.0 + s.inputDriftPpm * 1.0e-6))),
              capPackets (std::max (1, (int) std::floor (s.captureBufferPeriods + 1.0e-9))),
              work (s, (double) s.inputBlock / s.inputRate)
        {}
        virtual ~MicSide() = default;

        // Next block pushed into the bridge (mono, 'packet' samples) and its push time / stamp.
        virtual void next (Push& p, float* block) = 0;

        void report (Result& r) const
        {
            r.captureOverflows = overflowEvents;
            r.lostInputMs = 1000.0 * (double) lostPackets * (double) packet / sc.inputRate;
            r.meanInputDelayMs = delayCount > 0 ? 1000.0 * delaySum / (double) delayCount : 0.0;
            r.maxInputDelayMs = 1000.0 * delayMax;
            work.report (r);
        }

    protected:
        double arrival (int64_t k) const noexcept { return (double) (k + 1) * period; }

        void fillPacket (float* dst, int64_t k) const noexcept
        {
            const double cps = sc.toneHz / sc.inputRate;
            for (int i = 0; i < packet; ++i)
                dst[i] = (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * cps * (double) (k * packet + i)));
        }

        // A packet arriving while the capture buffer already holds capPackets is lost.
        bool acceptPacket (int bufferedPackets) noexcept
        {
            if (bufferedPackets < capPackets) { previousLost = false; return true; }
            ++lostPackets;
            if (! previousLost) ++overflowEvents;
            previousLost = true;
            return false;
        }

        void recordDelay (double d) noexcept { delaySum += d; delayMax = std::max (delayMax, d); ++delayCount; }

        const Scenario& sc;
        const int packet;
        const double period;
        const int capPackets;
        WorkModel work;
        int64_t nextArrival = 0, lostPackets = 0;
        int overflowEvents = 0;
        bool previousLost = false;
        double delaySum = 0.0, delayMax = 0.0;
        int64_t delayCount = 0;
    };

    // 1.2.0: JUCE's input-only WASAPI loop runs the plug-in chain inside the device callback.
    // Between callbacks it moves whole packets from the capture buffer into its reservoir
    // (nextPowerOfTwo(captureBuffer + block) frames) and processes a backlog at once (MicVST patch);
    // DURING a callback only the capture buffer itself can take new packets.
    class CaptureThreadMic final : public MicSide
    {
    public:
        explicit CaptureThreadMic (const Scenario& s)
            : MicSide (s), stalls (mergedStalls (s.stalls, s.captureStalls))
        {
            int64_t r = 1;
            while (r < (int64_t) std::lround (s.captureBufferPeriods * packet) + packet) r <<= 1;
            reservoirPackets = std::max (1, (int) (r / packet));
        }

        void next (Push& p, float* block) override
        {
            for (int guard = 0; guard < 10000000; ++guard)
            {
                double stallEnd = 0.0;
                if (insideStall (stalls, t, stallEnd)) t = stallEnd;

                handleDeviceBuffer (t);

                if (reservoir.empty())
                {
                    t = std::max (t, arrival (nextArrival));   // wait for the next capture event
                    continue;
                }

                const int64_t k = reservoir.front();
                reservoir.pop_front();
                fillPacket (block, k);

                const double end = advanceWork (t, work.next(), stalls);
                p.time = end;
                p.stamp = end;            // 1.2.0 pushed with the time after processing
                recordDelay (end - arrival (k));
                t = end;
                return;
            }
        }

    private:
        void handleDeviceBuffer (double now)
        {
            // packets that arrived since the last call, in order (nobody read the buffer meanwhile)
            while (arrival (nextArrival) <= now)
            {
                if (acceptPacket ((int) wasapi.size()))
                    wasapi.push_back (nextArrival);
                ++nextArrival;
            }
            while (! wasapi.empty() && (int) reservoir.size() < reservoirPackets)
            {
                reservoir.push_back (wasapi.front());
                wasapi.pop_front();
            }
        }

        std::vector<Stall> stalls;   // plug-in stalls AND capture stalls freeze the same thread
        int reservoirPackets = 4;
        std::deque<int64_t> wasapi, reservoir;
        double t = 0.0;
    };

    // 1.2.1: the capture thread only copies each packet into the InputQueue (the real class) and
    // returns to WASAPI; the plug-in thread reads blocks from the queue (with its backlog limit).
    class SeparateThreadMic final : public MicSide
    {
    public:
        explicit SeparateThreadMic (const Scenario& s)
            : MicSide (s),
              maxBacklog (InputQueue::backlogLimit (s.inputRate, s.inputBlock)),
              tmp ((size_t) s.inputBlock), scratch1 ((size_t) s.inputBlock)
        {}

        void next (Push& p, float* block) override
        {
            double s = procFree;
            for (int guard = 0; guard < 10000000; ++guard)
            {
                double stallEnd = 0.0;
                if (insideStall (sc.stalls, s, stallEnd)) { s = stallEnd; continue; }

                captureUntil (s);
                if (queue.available() >= packet)
                    break;
                s = std::max (s, handleTime (nextArrival));   // woken by the next capture write
            }

            float* dst[] = { block, scratch1.data() };
            const int64_t skipped = queue.readBlock (dst, packet, maxBacklog, InputQueue::alignRange (sc.inputRate));
            if (skipped > 0) { ++skips; skippedFrames += skipped; }

            const double end = advanceWork (s, work.next(), sc.stalls);
            captureUntil (end);

            double stamp = end;
            queue.lastReadCaptureTime (sc.inputRate, stamp);
            p.time = end;
            p.stamp = stamp;
            recordDelay (end - stamp);
            procFree = end;
        }

        void reportQueue (Result& r) const
        {
            r.inputSkips = skips;
            r.skippedMs = 1000.0 * (double) skippedFrames / sc.inputRate;
        }

    private:
        // The capture thread handles packet k at its arrival, or at the end of a capture stall.
        double handleTime (int64_t k) const
        {
            double h = arrival (k), stallEnd = 0.0;
            if (insideStall (sc.captureStalls, h, stallEnd)) h = stallEnd;
            return h;
        }

        void captureUntil (double until)
        {
            for (int guard = 0; guard < 10000000; ++guard)
            {
                const double h = handleTime (nextArrival);
                if (h > until)
                    return;

                // everything that arrived up to h is read now; during a capture stall the buffer
                // took only capPackets of it
                int waiting = 0;
                while (arrival (nextArrival + waiting) <= h) ++waiting;

                for (int j = 0; j < waiting; ++j)
                {
                    if (acceptPacket (j))
                    {
                        fillPacket (tmp.data(), nextArrival + j);
                        const float* ch[] = { tmp.data() };
                        queue.write (ch, 1, packet, h);
                    }
                }
                nextArrival += waiting;
            }
        }

        InputQueue queue;
        const int64_t maxBacklog;
        std::vector<float> tmp, scratch1;
        double procFree = 0.0;
        uint64_t skips = 0;
        int64_t skippedFrames = 0;
    };

    //==============================================================================
    // MicVST 1.2.x: microphone side (see InputPath) -> AudioBridge -> output thread. The output
    // side is modelled like JUCE's output-only WASAPI loop: pull a block, wait for room in the
    // render buffer, write it; after a stall it refills the render buffer back-to-back. If the
    // output thread itself is frozen longer than the render buffer lasts, the device plays
    // silence - that gap is part of the captured device output, exactly as in runOldCoupled().
    inline Result runBridge (const Scenario& sc, std::vector<float>* outputCapture = nullptr,
                             InputPath path = InputPath::separateThread)
    {
        AudioBridge bridge;
        bridge.setInputRate (sc.inputRate);
        bridge.setOutputRate (sc.outputRate);
        bridge.setSafety (sc.safetyMs, sc.autoSafety);
        bridge.setConsumerActive (true);

        std::unique_ptr<MicSide> mic;
        SeparateThreadMic* separate = nullptr;
        if (path == InputPath::captureThread)
            mic = std::make_unique<CaptureThreadMic> (sc);
        else
        {
            auto m = std::make_unique<SeparateThreadMic> (sc);
            separate = m.get();
            mic = std::move (m);
        }

        const int    B = sc.outputBlock;
        const double rate = sc.outputRate;
        const double outPeriod = B / rate;
        const auto   renderCap = (int64_t) std::lround (2.2 * B);
        const double amp = 0.5;

        std::vector<float> inBuf ((size_t) sc.inputBlock), block ((size_t) B);
        std::vector<float> y ((size_t) renderCap, 0.0f);   // render buffer pre-filled with silence
        y.reserve ((size_t) (sc.seconds * rate) + 8192);

        int64_t written = renderCap;
        double tCons = 0.0;
        size_t firstRunningSample = (size_t) -1;
        double marginSum = 0.0, totalLatencySum = 0.0;
        int latencyCount = 0, totalLatencyCount = 0, deviceGaps = 0;
        Result r;

        auto playedAt = [&] (double tt) { return (int64_t) std::floor (tt * rate); };

        Push nextPush;
        mic->next (nextPush, inBuf.data());
        size_t nextSafetyChange = 0;

        while (std::min (nextPush.time, tCons) < sc.seconds)
        {
            if (nextPush.time <= tCons)
            {
                const float* chans[] = { inBuf.data() };
                bridge.push (chans, 1, sc.inputBlock, nextPush.stamp, nextPush.time);
                mic->next (nextPush, inBuf.data());
                continue;
            }

            double t = tCons, stallEnd = 0.0;
            if (insideStall (sc.consumerStalls, t, stallEnd)) t = stallEnd;

            while (nextSafetyChange < sc.safetyChanges.size() && sc.safetyChanges[nextSafetyChange].first <= t)
                bridge.setSafety (sc.safetyChanges[nextSafetyChange++].second, false);

            float* chans[] = { block.data() };
            bridge.pull (chans, 1, B, t);
            const auto st = bridge.getStats();
            if (t > 5.0)
                r.maxSpeedDeviationPpm = std::max (r.maxSpeedDeviationPpm, std::abs (st.correctionPpm - st.driftPpm));
            if (t > sc.seconds - 60.0)
                r.maxPitchErrorPpmLast60s = std::max (r.maxPitchErrorPpmLast60s, std::abs (st.correctionPpm - sc.inputDriftPpm));
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
            if (st.resyncs > (uint64_t) r.resyncAt.size())
                r.resyncAt.push_back (y.size());
            y.insert (y.end(), block.begin(), block.end());
            written += B;
            tCons = t;

            totalLatencySum += st.bufferedMs + 1000.0 * (double) (written - playedAt (t)) / rate;
            ++totalLatencyCount;
            if (t > sc.seconds - 10.0)
            {
                // time-continuous level beyond what the next pull needs (bufferedMs is the mean ring
                // wait: it excludes the frames on their way in and half an output block)
                marginSum += st.bufferedMs + st.inputDelayMs - 500.0 * outPeriod - 1000.0 * bridge.getResamplerDelayFrames() / sc.inputRate;
                ++latencyCount;
            }
            if (t > 60.0)
                r.maxCorrectionPpmAfter60s = std::max (r.maxCorrectionPpmAfter60s, std::abs (st.correctionPpm));
        }

        mic->report (r);
        if (separate != nullptr)
            separate->reportQueue (r);

        const auto st = bridge.getStats();
        r.underruns = st.underruns;
        r.droppedBlocks = st.droppedBlocks;
        r.resyncs = st.resyncs;
        r.finalDriftPpm = st.driftPpm;
        r.finalSafetyMs = st.safetyMs;
        r.outputRunningAtEnd = st.running;
        r.gapEvents = (int) st.underruns + deviceGaps + r.captureOverflows;
        // bridge + render buffer, plus the time each block spent between capture and the bridge
        r.meanLatencyMs = (totalLatencyCount > 0 ? totalLatencySum / totalLatencyCount : 0.0) + r.meanInputDelayMs;
        r.finalMarginMs = latencyCount > 0 ? marginSum / latencyCount : 0.0;

        const double cpsOut = sc.toneHz / sc.outputRate;
        const double limit = 3.0 * amp * std::pow (juce::MathConstants<double>::twoPi * cpsOut, 2.0) + 2.0e-4;
        if (firstRunningSample != (size_t) -1)
            r.clicksAfterStart = countClicks (y, firstRunningSample + 1, limit, &r.clickEvents, (size_t) (0.01 * rate));

        if (outputCapture != nullptr) *outputCapture = std::move (y);
        return r;
    }
}
