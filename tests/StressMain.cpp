// MicVSTStress - measurable comparison of MicVST 1.1.1's audio path with the 1.2 bridge.
//
//  Part A (any OS, deterministic): the SAME scenarios (clock drift, plug-in load jitter, stalls,
//          system freezes, mismatched sample rates) run through a model of the old single-thread
//          JUCE design and through the real AudioBridge code. Counts audible discontinuities
//          (clicks / gaps) with one detector for both.
//  Part B (Windows only, real threads): wake-up lateness of a 10 ms periodic audio thread while
//          all CPU cores are busy - plain thread vs. MMCSS "Pro Audio" HIGH + power-throttling
//          opt-out (what MicVST 1.2 does) - and the real AudioBridge driven by two real threads
//          under the same load.
//  Part C (real threads; realtime priorities on Windows (MMCSS) and on Linux when permitted):
//          the microphone path of 1.2.0 (plug-ins on the capture thread) vs. 1.2.1 (capture
//          thread -> InputQueue -> plug-in thread) with the real InputQueue, WakeEvent,
//          ProcessingThreadSetup and AudioBridge, a modelled WASAPI capture buffer, render-like
//          plug-in load and all cores busy. Also checks the CPU-time measurement (threadTimes).
//
//  Part D (any OS, deterministic, --day <hours>): a simulated day under a random but reproducible
//          schedule of clock drift changes, cable stalls, system freezes, plug-in stalls and
//          render periods; clicks attributed to their causes, hour by hour.
//
// Usage: MicVSTStress [report.md] [--quick] | MicVSTStress [report.md] --day <hours>
#include <juce_core/juce_core.h>
#include "audio/AudioBridge.h"
#include "audio/InputQueue.h"
#include "audio/RealtimeSupport.h"
#include "BridgeSim.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <random>
#include <thread>
#if JUCE_LINUX
 #include <pthread.h>
 #include <sched.h>
#endif

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <avrt.h>
 #pragma comment (lib, "avrt.lib")
#endif

namespace
{
    juce::String report;
    void out (const juce::String& line) { std::printf ("%s\n", line.toRawUTF8()); std::fflush (stdout); report << line << "\n"; }

    //==============================================================================
    void partA (bool quick)
    {
        using namespace bridgesim;
        struct Case { const char* name; Scenario sc; };
        std::vector<Case> cases;

        auto add = [&] (const char* name, auto&& setup)
        {
            Scenario sc;
            setup (sc);
            if (quick) sc.seconds = std::min (sc.seconds, 60.0);
            cases.push_back ({ name, sc });
        };

        add ("1. Normal: drift 0 ppm, light jitter (0-1 ms)", [] (Scenario& s) { s.inputJitterMs = 1.0; s.seconds = 120; });
        add ("2. Mic clock 60 ppm slower than cable", [] (Scenario& s) { s.inputDriftPpm = -60; s.inputJitterMs = 1.0; s.seconds = 600; });
        add ("3. Mic clock 60 ppm faster than cable", [] (Scenario& s) { s.inputDriftPpm = 60; s.inputJitterMs = 1.0; s.seconds = 1200; });
        add ("4. Heavy game: plug-in time jitter 0-9 ms, drift +40 ppm", [] (Scenario& s) { s.inputDriftPpm = 40; s.inputJitterMs = 9.0; s.seconds = 180; });
        add ("4b. Very heavy load: plug-in time jitter 0-14 ms (avg. load 95 %), drift +40 ppm", [] (Scenario& s) { s.inputDriftPpm = 40; s.inputJitterMs = 14.0; s.seconds = 180; });
        add ("5. Load spikes: 25 ms processing stall every 4 s", [] (Scenario& s)
             { s.seconds = 120; s.inputJitterMs = 1.0; for (double t = 3; t < s.seconds; t += 4) s.stalls.push_back ({ t, 0.025 }); });
        add ("6. System freeze (DPC storm) 30 ms every 6 s, all threads", [] (Scenario& s)
             { s.seconds = 120; s.inputJitterMs = 1.0;
               for (double t = 5; t < s.seconds; t += 6)
               { s.stalls.push_back ({ t, 0.030 }); s.captureStalls.push_back ({ t, 0.030 }); s.consumerStalls.push_back ({ t, 0.030 }); } });
        add ("7. Mic 44.1 kHz, cable 48 kHz", [] (Scenario& s) { s.inputRate = 44100; s.inputBlock = 441; s.inputJitterMs = 1.0; s.seconds = 120; });
        add ("8. Render in the background (like the Topaz log): 45 % of blocks need 1.1-1.9x real time, avg. load ~92 %",
             [] (Scenario& s) { s.baseLoad = 0.4; s.inputJitterMs = 1.0; s.spikeChance = 0.45; s.spikeMin = 1.1; s.spikeMax = 1.9; s.inputDriftPpm = 40; s.seconds = 180; });
        add ("9. Chain slower than real time: 60 % of blocks need 1.2-2.0x, avg. load ~112 %",
             [] (Scenario& s) { s.baseLoad = 0.35; s.inputJitterMs = 1.0; s.spikeChance = 0.6; s.spikeMin = 1.2; s.spikeMax = 2.0; s.inputDriftPpm = 40; s.seconds = 120; });
        add ("10. Plug-in spikes: 60 ms processing stall every 3 s", [] (Scenario& s)
             { s.seconds = 120; s.inputJitterMs = 1.0; for (double t = 2; t < s.seconds; t += 3) s.stalls.push_back ({ t, 0.060 }); });

        out ("## Part A - simulation: 1.1.1 (one thread) vs. 1.2.0 (plug-ins on the capture thread) vs. 1.2.1 (separate plug-in thread)");
        out ("");
        out ("Same input signal (997 Hz sine), same timing per scenario, all designs with the same WASAPI buffers");
        out ("(render and capture buffer 2.2 periods). 'Clicks' = audible events where the waveform jumps (hard");
        out ("gaps, zero-padding, lost input), counted as events (jumps closer than 10 ms = one event); in brackets the");
        out ("number of flagged samples. Faded gaps (bridge underruns) are no clicks but count under 'gaps';");
        out ("'driver gaps' = capture-buffer overflows (audio lost inside Windows); 'skipped' = crossfaded skips of");
        out ("1.2.1 when the chain falls behind by more than 120 ms. Latency = audio queued between microphone and");
        out ("cable (input side + bridge + render buffer), device latency excluded.");
        out ("");
        out ("| Scenario | Design | Clicks (samples) | Gaps / underruns | Driver gaps (lost) | Skipped | Mean added latency |");
        out ("|---|---|---:|---:|---:|---:|---:|");

        for (auto& c : cases)
        {
            const auto oldR = runOldCoupled (c.sc);
            const auto r120 = runBridge (c.sc, nullptr, InputPath::captureThread);
            const auto r121 = runBridge (c.sc, nullptr, InputPath::separateThread);

            auto lost = [] (const Result& r)
            {
                return juce::String (r.captureOverflows) + (r.lostInputMs > 0.0 ? " (" + juce::String (juce::roundToInt (r.lostInputMs)) + " ms)" : juce::String());
            };
            auto bridgeRow = [&] (const char* design, const Result& r)
            {
                out ("| | " + juce::String (design) + " | " + juce::String (r.clickEvents) + " (" + juce::String (r.clicksAfterStart) + ") | "
                     + juce::String ((int) r.underruns) + (r.resyncs > 0 ? " (+" + juce::String ((int) r.resyncs) + " trims)" : juce::String())
                     + " | " + lost (r) + " | "
                     + (r.inputSkips > 0 ? juce::String ((int) r.inputSkips) + " (" + juce::String (juce::roundToInt (r.skippedMs)) + " ms)" : juce::String ("0"))
                     + " | " + juce::String (r.meanLatencyMs, 1) + " ms (safety " + juce::String (juce::roundToInt (r.finalSafetyMs)) + " ms) |");
            };

            if (oldR.cannotOpen)
                out ("| " + juce::String (c.name) + " | 1.1.1 | device does not open (rate mismatch) | - | - | - | - |");
            else
                out ("| " + juce::String (c.name) + " | 1.1.1 | " + juce::String (oldR.clickEvents) + " (" + juce::String (oldR.clicksAfterStart) + ") | "
                     + juce::String (oldR.gapEvents - oldR.captureOverflows) + " | " + lost (oldR) + " | - | "
                     + juce::String (oldR.meanLatencyMs, 1) + " ms |");

            bridgeRow ("1.2.0", r120);
            bridgeRow ("**1.2.1**", r121);
        }
        out ("");
        out ("Plug-in load in the 1.2.1 runs (mean / share of blocks slower than real time / peak) is the same model as");
        out ("for the other designs; see the scenario names.");
        out ("");
    }

    //==============================================================================
    struct LoadThreads
    {
        explicit LoadThreads (int n)
        {
            for (int i = 0; i < n; ++i)
                threads.emplace_back ([this, i]
                {
                    std::vector<float> mem ((size_t) 4 << 20);   // 16 MB per thread: cache + memory traffic
                    uint32_t x = 12345u + (uint32_t) i;
                    while (! stop.load (std::memory_order_relaxed))
                        for (size_t k = 0; k < mem.size(); k += 16)
                        {
                            x = x * 1664525u + 1013904223u;
                            mem[(x >> 8) % mem.size()] += 1.0f;
                        }
                });
        }
        ~LoadThreads() { stop = true; for (auto& t : threads) t.join(); }
        std::atomic<bool> stop { false };
        std::vector<std::thread> threads;
    };

    //==============================================================================
    // Part C - real threads, both microphone paths.
    double hiResSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    struct Waiter   // sleep until an absolute time with sub-millisecond precision
    {
       #if JUCE_WINDOWS
        Waiter()
        {
            timer = CreateWaitableTimerExW (nullptr, nullptr, 0x00000002 /*HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
            if (timer == nullptr) timer = CreateWaitableTimerExW (nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
        ~Waiter() { if (timer != nullptr) CloseHandle (timer); }
        void until (double t)
        {
            const double now = hiResSeconds();
            if (t <= now) return;
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG) ((t - now) * 1.0e7);
            SetWaitableTimer (timer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject (timer, INFINITE);
        }
        HANDLE timer = nullptr;
       #else
        void until (double t)
        {
            const double now = hiResSeconds();
            if (t > now) std::this_thread::sleep_for (std::chrono::duration<double> (t - now));
        }
       #endif
    };

    std::atomic<int> deviceThreadSetupFailures { 0 };

    // Like JUCE's WASAPI device threads with the MicVST patch (MMCSS "Pro Audio" HIGH).
    void enterDeviceThread()
    {
       #if JUCE_WINDOWS
        DWORD idx = 0;
        if (auto h = AvSetMmThreadCharacteristicsW (L"Pro Audio", &idx))
            AvSetMmThreadPriority (h, AVRT_PRIORITY_HIGH);
        else
            ++deviceThreadSetupFailures;
        rt::configureAudioThread();
       #elif JUCE_LINUX
        sched_param p {}; p.sched_priority = 80;
        if (pthread_setschedparam (pthread_self(), SCHED_FIFO, &p) != 0) ++deviceThreadSetupFailures;
       #endif
    }

    struct PipelineResult
    {
        int captureXruns = 0, lostPackets = 0, clickEvents = 0, captureLateWakes = 0;
        uint64_t underruns = 0, skips = 0;
        double maxCaptureCallbackMs = 0.0, cpuSharePercent = -1.0, loadPercent = 0.0, safetyMs = 0.0;
        bool processingMmcss = false;
    };

    // split = false: 1.2.0 (plug-ins inside the capture callback); true: 1.2.1.
    PipelineResult runPipeline (bool split, double seconds)
    {
        constexpr int block = 480;
        constexpr double rate = 48000.0, period = block / rate;
        const double cps = 997.0 / rate;

        AudioBridge bridge;
        bridge.setInputRate (rate);
        bridge.setOutputRate (rate);
        bridge.setSafety (AudioBridge::autoStartMs, true);
        bridge.setConsumerActive (true);

        InputQueue queue;
        rt::WakeEvent wake;
        std::atomic<bool> running { true };
        std::atomic<int> xruns { 0 }, lost { 0 }, lateWakes { 0 };
        std::atomic<uint64_t> skips { 0 }, busyTicks { 0 }, cpuTicks { 0 };
        std::atomic<bool> processingMmcss { false };
        double maxCallback = 0.0, loadSum = 0.0;   // written by one thread each, read after join
        int64_t processed = 0;

        // The "plug-in chain": render-like contention (45 % of the blocks 1.1-1.9x real time,
        // the rest 0.4x), then the push into the bridge.
        auto process = [&] (std::vector<float>& l, std::mt19937& rng, bool stampAfter, double stamp)
        {
            std::uniform_real_distribution<double> u (0.0, 1.0);
            const double need = period * (u (rng) < 0.45 ? 1.1 + 0.8 * u (rng) : 0.4);
            const auto t0 = rt::threadTimes();
            const double w0 = hiResSeconds();
            volatile double sink = 0.0;
            while (hiResSeconds() < w0 + need) sink = sink + 1.0;
            const auto t1 = rt::threadTimes();
            if (t1.wall > t0.wall && t1.cpu >= t0.cpu) { busyTicks += t1.wall - t0.wall; cpuTicks += t1.cpu - t0.cpu; }
            loadSum += (hiResSeconds() - w0) / period;
            ++processed;
            const float* ch[] = { l.data() };
            const double arrival = hiResSeconds();
            bridge.push (ch, 1, block, stampAfter ? arrival : stamp, arrival);
        };

        const double start = hiResSeconds() + 0.1;

        std::thread capture ([&]
        {
            enterDeviceThread();
            Waiter waiter;
            std::mt19937 rng (7);
            constexpr int capPackets = 2, reservoirPackets = 4;   // 2.2-period capture buffer, JUCE reservoir 2048
            std::deque<int64_t> wasapi, reservoir;
            int64_t next = 0;
            bool previousLost = false;
            std::vector<float> l ((size_t) block);

            while (running)
            {
                if (reservoir.empty())
                {
                    const double due = start + (double) (next + 1) * period;
                    waiter.until (due);
                    if (hiResSeconds() - due > 0.015) ++lateWakes;
                }

                const double t = hiResSeconds() - start;
                while ((double) (next + 1) * period <= t)
                {
                    if ((int) wasapi.size() < capPackets) { wasapi.push_back (next); previousLost = false; }
                    else { ++lost; if (! previousLost) ++xruns; previousLost = true; }
                    ++next;
                }
                while (! wasapi.empty() && (int) reservoir.size() < reservoirPackets)
                {
                    reservoir.push_back (wasapi.front());
                    wasapi.pop_front();
                }
                if (reservoir.empty())
                    continue;

                const int64_t k = reservoir.front();
                reservoir.pop_front();
                for (int i = 0; i < block; ++i)
                    l[(size_t) i] = (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * cps * (double) (k * block + i)));

                const double cb0 = hiResSeconds();
                if (split)
                {
                    const float* ch[] = { l.data() };
                    queue.write (ch, 1, block, hiResSeconds());
                    wake.signal();
                }
                else
                {
                    process (l, rng, true, 0.0);   // 1.2.0: pushed with the time after processing
                }
                maxCallback = std::max (maxCallback, hiResSeconds() - cb0);
            }
            wake.signal();
        });

        std::thread processing;
        if (split)
            processing = std::thread ([&]
            {
                const rt::ProcessingThreadSetup setup;
                processingMmcss = setup.mmcssActive();
               #if JUCE_LINUX
                sched_param p {}; p.sched_priority = 70;   // stands in for MMCSS "Pro Audio" NORMAL
                pthread_setschedparam (pthread_self(), SCHED_FIFO, &p);
               #endif
                std::mt19937 rng (7);
                std::vector<float> l ((size_t) block), r ((size_t) block);
                const auto maxBacklog = InputQueue::backlogLimit (rate, block);
                while (running)
                {
                    wake.wait (100);
                    while (running && queue.available() >= block)
                    {
                        float* dst[] = { l.data(), r.data() };
                        if (queue.readBlock (dst, block, maxBacklog, InputQueue::alignRange (rate)) > 0) ++skips;
                        double stamp = hiResSeconds();
                        queue.lastReadCaptureTime (rate, stamp);
                        process (l, rng, false, stamp);
                    }
                }
            });

        int clickEvents = 0;
        std::thread output ([&]
        {
            enterDeviceThread();
            Waiter waiter;
            std::vector<float> buf ((size_t) block);
            float p2 = 0.0f, p1 = 0.0f;
            bool started = false;
            int64_t sample = 0, lastClick = -100000;
            const double limit = 3.0 * 0.5 * std::pow (juce::MathConstants<double>::twoPi * cps, 2.0) + 2.0e-4;
            for (int k = 0; running; ++k)
            {
                waiter.until (start + (double) k * period);
                float* ch[] = { buf.data() };
                bridge.pull (ch, 1, block, hiResSeconds());
                if (bridge.getStats().running) started = true;
                for (float v : buf)
                {
                    if (started && std::abs ((double) v - 2.0 * p1 + p2) > limit)
                    {
                        if (sample - lastClick > block) ++clickEvents;
                        lastClick = sample;
                    }
                    p2 = p1; p1 = v; ++sample;
                }
            }
        });

        juce::Thread::sleep ((int) (seconds * 1000.0) + 100);
        running = false;
        wake.signal();
        capture.join();
        if (processing.joinable()) processing.join();
        output.join();

        PipelineResult res;
        const auto st = bridge.getStats();
        res.captureXruns = xruns;
        res.lostPackets = lost;
        res.captureLateWakes = lateWakes;
        res.clickEvents = clickEvents;
        res.underruns = st.underruns;
        res.safetyMs = st.safetyMs;
        res.skips = skips;
        res.maxCaptureCallbackMs = 1000.0 * maxCallback;
        res.loadPercent = processed > 0 ? 100.0 * loadSum / (double) processed : 0.0;
        res.cpuSharePercent = busyTicks > 0 ? 100.0 * (double) cpuTicks.load() / (double) busyTicks.load() : -1.0;
        res.processingMmcss = processingMmcss;
        return res;
    }

    // CPU-time measurement check: a thread busy 1 ms / sleeping 1 ms must show ~50 %, a busy
    // loop ~100 % (both units come from the same counter; else the share is meaningless).
    double measureShare (bool halfIdle)
    {
        double share = -1.0;
        std::thread th ([&]
        {
            const auto t0 = rt::threadTimes();
            const double end = hiResSeconds() + 0.4;
            volatile double sink = 0.0;
            while (hiResSeconds() < end)
            {
                const double until = hiResSeconds() + 0.001;
                while (hiResSeconds() < until) sink = sink + 1.0;
                if (halfIdle) std::this_thread::sleep_for (std::chrono::milliseconds (1));
            }
            const auto t1 = rt::threadTimes();
            if (t1.wall > t0.wall) share = 100.0 * (double) (t1.cpu - t0.cpu) / (double) (t1.wall - t0.wall);
        });
        th.join();
        return share;
    }

    void partC (bool quick)
    {
        const int cores = (int) std::thread::hardware_concurrency();
        const double secs = quick ? 15.0 : 40.0;
        out ("## Part C - real threads: plug-ins on the capture thread (1.2.0) vs. separate plug-in thread (1.2.1)");
        out ("");
        out ("Real InputQueue / WakeEvent / AudioBridge; capture thread modelled on JUCE's input-only WASAPI loop");
        out ("(capture buffer 2 packets, reservoir 4); plug-in work like the Topaz log (45 % of the blocks need");
        out ("1.1-1.9x real time, the rest 0.4x); " + juce::String (cores) + " normal-priority load threads keep all "
             + juce::String (cores) + " logical CPUs busy; " + juce::String (juce::roundToInt (secs)) + " s per run. Device threads: "
            #if JUCE_WINDOWS
             "MMCSS 'Pro Audio' HIGH; plug-in thread: MicVST's ProcessingThreadSetup (MMCSS NORMAL)."
            #else
             "SCHED_FIFO 80; plug-in thread: SCHED_FIFO 70 (stand-ins for MMCSS)."
            #endif
             );
        out ("");
        out ("| Design | Driver gaps (lost) | Clicks | Faded gaps | Skips | Safety at end | Longest capture callback | Plug-in load avg | Thread on CPU while processing | Capture wake-ups > 15 ms late |");
        out ("|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|");
        {
            LoadThreads load (cores);
            juce::Thread::sleep (300);
            for (bool split : { false, true })
            {
                const auto r = runPipeline (split, secs);
                out (juce::String ("| ") + (split ? "**1.2.1** separate plug-in thread" : "1.2.0 plug-ins on the capture thread")
                     + " | " + juce::String (r.captureXruns) + " (" + juce::String (r.lostPackets * 10) + " ms)"
                     + " | " + juce::String (r.clickEvents) + " | " + juce::String ((int) r.underruns)
                     + " | " + juce::String ((int) r.skips) + " | " + juce::String (juce::roundToInt (r.safetyMs)) + " ms"
                     + " | " + juce::String (r.maxCaptureCallbackMs, 1) + " ms | " + juce::String (juce::roundToInt (r.loadPercent)) + " %"
                     + " | " + (r.cpuSharePercent >= 0.0 ? juce::String (juce::roundToInt (r.cpuSharePercent)) + " %" : juce::String ("n/a"))
                     + " | " + juce::String (r.captureLateWakes) + " |");
            }
        }
        out ("");
        out ("Device-thread priority setup failures: " + juce::String (deviceThreadSetupFailures.load())
             + " (MMCSS / SCHED_FIFO refused -> those rows ran at normal priority)");
        out ("CPU-time measurement check (rt::threadTimes): busy loop " + juce::String (juce::roundToInt (measureShare (false)))
             + " % (expected ~100), 1 ms busy / 1 ms asleep " + juce::String (juce::roundToInt (measureShare (true)))
             + " % (expected ~50; Windows Sleep(1) may round up and lower it).");
        out ("");
    }

   #if JUCE_WINDOWS
    //==============================================================================
    // plain     = ordinary thread (worst case)
    // mmcssNormal = what JUCE 8.0.13 / MicVST 1.1.1 did: MMCSS "Pro Audio", AVRT_PRIORITY_NORMAL
    // mmcssHigh   = MicVST 1.2: MMCSS "Pro Audio", AVRT_PRIORITY_HIGH + power-throttling opt-out
    enum class ThreadSetup { plain, mmcssNormal, mmcssHigh };

    std::atomic<int> mmcssFailures { 0 };

    struct Periodic
    {
        explicit Periodic (ThreadSetup s) : setup (s)
        {
            timer = CreateWaitableTimerExW (nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
            if (timer == nullptr) timer = CreateWaitableTimerExW (nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        }
        ~Periodic() { if (timer) CloseHandle (timer); }

        void enterThread()
        {
            if (setup == ThreadSetup::plain) return;
            DWORD idx = 0;
            if (auto h = AvSetMmThreadCharacteristicsW (L"Pro Audio", &idx))
                AvSetMmThreadPriority (h, setup == ThreadSetup::mmcssHigh ? AVRT_PRIORITY_HIGH : AVRT_PRIORITY_NORMAL);
            else
                ++mmcssFailures;   // e.g. the MMCSS service is not running - reported below
            if (setup == ThreadSetup::mmcssHigh)
                rt::configureAudioThread();   // the same code MicVST 1.2 runs on its audio threads
        }

        // waits until absolute QPC time 'when' (seconds); returns lateness in ms
        double waitUntil (double when)
        {
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            if (when > now)
            {
                LARGE_INTEGER due;
                due.QuadPart = -(LONGLONG) ((when - now) * 1.0e7);   // relative, 100 ns units
                SetWaitableTimer (timer, &due, 0, nullptr, nullptr, FALSE);
                WaitForSingleObject (timer, INFINITE);
            }
            return (juce::Time::getMillisecondCounterHiRes() * 0.001 - when) * 1000.0;
        }

        ThreadSetup setup;
        HANDLE timer = nullptr;
    };

    juce::String percentileRow (const juce::String& name, std::vector<double> v)
    {
        std::sort (v.begin(), v.end());
        auto pct = [&] (double p) { return v.empty() ? 0.0 : v[(size_t) std::min ((double) v.size() - 1, p * (double) v.size())]; };
        const auto over = [&] (double ms) { return (int) std::count_if (v.begin(), v.end(), [ms] (double x) { return x > ms; }); };
        return "| " + name + " | " + juce::String (pct (0.5), 3) + " | " + juce::String (pct (0.99), 3) + " | "
             + juce::String (pct (0.999), 3) + " | " + juce::String (v.empty() ? 0.0 : v.back(), 3) + " | "
             + juce::String (over (2.0)) + " | " + juce::String (over (5.0)) + " |";
    }

    std::vector<double> measureWakeups (ThreadSetup setup, double seconds)
    {
        std::vector<double> lateness;
        lateness.reserve ((size_t) (seconds * 100) + 10);
        std::thread th ([&]
        {
            Periodic p (setup);
            p.enterThread();
            const double start = juce::Time::getMillisecondCounterHiRes() * 0.001 + 0.05;
            for (int k = 0; k < (int) (seconds * 100); ++k)
            {
                lateness.push_back (p.waitUntil (start + k * 0.010));
                volatile double sink = 0;                 // ~1 ms of "DSP"
                const double until = juce::Time::getMillisecondCounterHiRes() + 1.0;
                while (juce::Time::getMillisecondCounterHiRes() < until) sink = sink + 1.0;
            }
        });
        th.join();
        return lateness;
    }

    void partB (bool quick)
    {
        const int cores = (int) std::thread::hardware_concurrency();
        const double secs = quick ? 10.0 : 30.0;
        out ("## Part B - real Windows threads (" + juce::String (cores) + " logical CPUs, all busy with "
             + juce::String (cores) + " normal-priority load threads)");
        out ("");
        out ("### Wake-up lateness of a 10 ms periodic audio thread (ms)");
        out ("");
        out ("| Thread setup | p50 | p99 | p99.9 | max | > 2 ms | > 5 ms |");
        out ("|---|---:|---:|---:|---:|---:|---:|");
        out (percentileRow ("idle system, plain thread", measureWakeups (ThreadSetup::plain, secs / 2)));
        {
            LoadThreads load (cores);
            juce::Thread::sleep (300);
            out (percentileRow ("under load, plain thread (worst case)", measureWakeups (ThreadSetup::plain, secs)));
            out (percentileRow ("under load, MMCSS Pro Audio NORMAL (1.1.1 / JUCE default)", measureWakeups (ThreadSetup::mmcssNormal, secs)));
            out (percentileRow ("**under load, MMCSS Pro Audio HIGH + no power throttling (1.2)**", measureWakeups (ThreadSetup::mmcssHigh, secs)));
        }
        out ("");
        out ("MMCSS registration failures: " + juce::String (mmcssFailures.load())
             + (mmcssFailures.load() > 0 ? " (MMCSS service unavailable - the MMCSS rows then show plain threads)" : ""));
        out ("Note: the load threads have normal priority, so NORMAL vs. HIGH can only differ when other MMCSS");
        out ("threads (game / OBS / Discord audio) compete - which this synthetic load does not create.");
        out ("");

        // Real AudioBridge, two real realtime threads, 70 ppm clock offset, plug-in work 2-4 ms per block.
        out ("### Real AudioBridge between two realtime threads under full CPU load");
        out ("");
        AudioBridge bridge;
        bridge.setInputRate (48000.0);
        bridge.setOutputRate (48000.0);
        bridge.setSafety (AudioBridge::autoStartMs, true);
        bridge.setConsumerActive (true);

        std::atomic<bool> running { true };
        std::vector<float> outBuf (480);
        int clicks = 0; float prev2 = 0, prev1 = 0; bool started = false;
        const double limit = 3.0 * 0.5 * std::pow (juce::MathConstants<double>::twoPi * 997.0 / 48000.0, 2.0) + 2.0e-4;
        {
            LoadThreads load (cores);
            juce::Thread::sleep (300);
            const double start = juce::Time::getMillisecondCounterHiRes() * 0.001 + 0.1;

            std::thread producer ([&]
            {
                Periodic p (ThreadSetup::mmcssHigh); p.enterThread();
                std::vector<float> in (480); int64_t n = 0;
                juce::Random rnd (7);
                for (int k = 0; running; ++k)
                {
                    p.waitUntil (start + (k + 1) * 0.010 * (1.0 - 70.0e-6));
                    for (auto& v : in) v = (float) (0.5 * std::sin (juce::MathConstants<double>::twoPi * 997.0 / 48000.0 * (double) n++));
                    const double until = juce::Time::getMillisecondCounterHiRes() + 2.0 + 2.0 * rnd.nextDouble();
                    volatile double sink = 0;
                    while (juce::Time::getMillisecondCounterHiRes() < until) sink = sink + 1.0;
                    const float* ch[] = { in.data() };
                    bridge.push (ch, 1, 480, juce::Time::getMillisecondCounterHiRes() * 0.001);
                }
            });
            std::thread consumer ([&]
            {
                Periodic p (ThreadSetup::mmcssHigh); p.enterThread();
                for (int k = 0; running; ++k)
                {
                    p.waitUntil (start + k * 0.010);
                    float* ch[] = { outBuf.data() };
                    bridge.pull (ch, 1, 480, juce::Time::getMillisecondCounterHiRes() * 0.001);
                    if (bridge.getStats().running) started = true;
                    if (started)
                        for (float v : outBuf)
                        {
                            if (std::abs ((double) v - 2.0 * prev1 + prev2) > limit) ++clicks;
                            prev2 = prev1; prev1 = v;
                        }
                }
            });
            juce::Thread::sleep ((int) (secs * 2000));
            running = false;
            producer.join();
            consumer.join();
        }
        const auto st = bridge.getStats();
        out ("| Duration | Underruns (faded gaps) | Clicks | Learned drift | Safety at end |");
        out ("|---:|---:|---:|---:|---:|");
        out ("| " + juce::String (secs * 2, 0) + " s | " + juce::String ((int) st.underruns) + " | " + juce::String (clicks)
             + " | " + juce::String (st.driftPpm, 1) + " ppm (true: +70) | " + juce::String (st.safetyMs, 0) + " ms |");
        out ("");
    }
   #endif
}

namespace
{
    //==============================================================================
    // Part D: a simulated day (deterministic, virtual time) - the real AudioBridge and InputQueue
    // with the 1.2.1 microphone path, under a random but reproducible schedule of everything that
    // happens on a real PC over hours: the mic clock drifts with temperature (random walk, plus a
    // jump every 6 h like plugging in another microphone), the cable's driver stalls, the whole
    // system freezes briefly (DPC), the plug-in thread stalls, and every 3 h (from 0:30 on) a
    // 30-minute video render loads the CPU like the Topaz log. The output is analysed while it is produced (a day
    // of audio is not kept): clicks of the 997 Hz tone, each attributed to its cause.
    void partD (double hours)
    {
        using namespace bridgesim;
        std::mt19937 rng (20261015);
        std::uniform_real_distribution<double> uni (0.0, 1.0);
        std::normal_distribution<double> gauss (0.0, 1.0);
        auto logUniform = [&] (double lo, double hi) { return lo * std::pow (hi / lo, uni (rng)); };
        auto poisson = [&] (double meanInterval, double lo, double hi, double seconds)
        {
            std::vector<Stall> v;
            for (double t = 30.0 - meanInterval * std::log (1.0 - uni (rng)); t < seconds; t += -meanInterval * std::log (1.0 - uni (rng)))
                v.push_back ({ t, logUniform (lo, hi) / 1000.0 });
            return v;
        };

        Scenario sc;
        sc.seconds = hours * 3600.0;
        double drift = 40.0;
        for (int m = 0; m < (int) std::ceil (sc.seconds / 60.0) + 2; ++m)
        {
            if (m > 0 && m % 360 == 0) drift = -300.0 + 600.0 * uni (rng);       // another microphone
            drift = juce::jlimit (-300.0, 300.0, drift + 0.5 * gauss (rng));       // temperature
            sc.driftByMinutePpm.push_back (drift);
        }
        sc.consumerStalls = poisson (300.0, 5.0, 80.0, sc.seconds);    // cable driver late
        sc.captureStalls  = poisson (900.0, 2.0, 30.0, sc.seconds);    // whole system frozen (DPC)
        sc.stalls         = poisson (600.0, 10.0, 250.0, sc.seconds);  // plug-in thread stalls
        sc.captureStalls  = mergedStalls (sc.captureStalls, {});
        sc.stalls = mergedStalls (sc.stalls, sc.captureStalls);        // a system freeze stops the plug-in thread too
        sc.consumerStalls = mergedStalls (sc.consumerStalls, sc.captureStalls);
        for (double t = 1800.0; t < sc.seconds; t += 3.0 * 3600.0)   // 0:30-1:00, 3:30-4:00, ...
            sc.heavyLoad.push_back ({ t, std::min (sc.seconds, t + 1800.0) });
        sc.baseLoad = 0.25; sc.spikeChance = 0.45; sc.spikeMin = 1.1; sc.spikeMax = 1.9;

        out ("## Part D - simulated day: " + juce::String (hours, 0) + " h of virtual time (1.2.1: real AudioBridge + InputQueue, deterministic)");
        out ("");
        out ("Schedule (seed 20261015): mic clock drift random walk within +-300 ppm (0.5 ppm/min) with a jump to a new value every 6 h; "
             + juce::String ((int) sc.consumerStalls.size()) + " cable stalls (5-80 ms), " + juce::String ((int) sc.captureStalls.size())
             + " system freezes (2-30 ms, all threads), " + juce::String ((int) sc.stalls.size()) + " plug-in thread stalls (10-250 ms, incl. the freezes); "
             + juce::String ((int) sc.heavyLoad.size()) + " render periods of 30 min (45 % of the blocks need 1.1-1.9x real time). "
             "Buffer 480 at 48 kHz, safety Auto. Clicks = jumps of the 997 Hz tone's waveform (second difference), events 10 ms apart; "
             "'inevitable' = at a cable gap (the cable's driver was late longer than its buffer: silence inserted) or at audio lost in a "
             "system freeze (capture buffer overflowed); 'at MicVST' = at an underrun, trim or skip of MicVST (should be none: those are faded/crossfaded).");
        out ("");
        out ("| Hour | Underruns | Trims | Skips | Capture overflows | Cable gaps | Clicks: inevitable / at MicVST / other | Pitch error max (ppm) | time > 100 ppm | Latency mean / max (ms) | Safety (ms) |");
        out ("|---:|---:|---:|---:|---:|---:|---|---:|---:|---|---:|");

        AudioBridge bridge;
        bridge.setInputRate (sc.inputRate);
        bridge.setOutputRate (sc.outputRate);
        bridge.setSafety (sc.safetyMs, sc.autoSafety);
        bridge.setConsumerActive (true);
        SeparateThreadMic mic (sc);

        const int B = sc.outputBlock;
        const double rate = sc.outputRate, outPeriod = B / rate;
        const auto renderCap = (int64_t) std::lround (2.2 * B);
        const double w = juce::MathConstants<double>::twoPi * sc.toneHz / rate;
        const double limit = 3.0 * 0.5 * w * w + 2.0e-4;
        std::vector<float> inBuf ((size_t) sc.inputBlock), block ((size_t) B);
        int64_t written = renderCap, recorded = renderCap;
        double tCons = 0.0;
        float p1 = 0.0f, p2 = 0.0f;
        int64_t lastClick = -1000000;
        bool running = false;

        std::vector<int64_t> inevitable, atMicVST;   // windows (cable sample index) of explaining events
        struct Hour { uint64_t under = 0, trims = 0, skips = 0; int overflows = 0, gaps = 0, clkInev = 0, clkMic = 0, clkOther = 0;
                      double pitchMax = 0, over100 = 0, latSum = 0, latMax = 0; int latN = 0; } h;
        struct Totals { uint64_t under = 0, trims = 0, skips = 0; int overflows = 0, gaps = 0, clkInev = 0, clkMic = 0, clkOther = 0; double pitchMax = 0, over100 = 0, lostMs = 0; } tot;
        Result mr;
        int prevOverflows = 0; uint64_t prevSkips = 0;
        AudioBridge::Stats prev {};
        double hourStart = 0.0;
        int hourIndex = 0;

        auto nearAny = [] (const std::vector<int64_t>& v, int64_t at, int64_t before, int64_t after)   // ("near" is a macro in windows.h)
        {
            for (auto it = v.rbegin(); it != v.rend() && *it > at - before - 200000; ++it)
                if (at >= *it - before && at <= *it + after) return true;
            return false;
        };
        auto flushHour = [&] (double tEnd, const AudioBridge::Stats& st)
        {
            out ("| " + juce::String (hourIndex + 1) + " | " + juce::String ((int) h.under) + " | " + juce::String ((int) h.trims) + " | " + juce::String ((int) h.skips)
                 + " | " + juce::String (h.overflows) + " | " + juce::String (h.gaps) + " | " + juce::String (h.clkInev) + " / " + juce::String (h.clkMic) + " / " + juce::String (h.clkOther)
                 + " | " + juce::String (juce::roundToInt (h.pitchMax)) + " | " + juce::String (100.0 * h.over100 / std::max (1.0, tEnd - hourStart), 2) + " %"
                 + " | " + juce::String (h.latN > 0 ? h.latSum / h.latN : 0.0, 1) + " / " + juce::String (h.latMax, 1) + " | " + juce::String (st.safetyMs, 0) + " |");
            tot.under += h.under; tot.trims += h.trims; tot.skips += h.skips; tot.overflows += h.overflows; tot.gaps += h.gaps;
            tot.clkInev += h.clkInev; tot.clkMic += h.clkMic; tot.clkOther += h.clkOther; tot.pitchMax = std::max (tot.pitchMax, h.pitchMax); tot.over100 += h.over100;
            h = Hour(); hourStart = tEnd; ++hourIndex;
            // forget old windows
            auto trim = [&] (std::vector<int64_t>& v) { if (v.size() > 100000) v.erase (v.begin(), v.begin() + (ptrdiff_t) (v.size() - 1000)); };
            trim (inevitable); trim (atMicVST);
        };

        const bool trace = juce::SystemStats::getEnvironmentVariable ("MICVST_DAY_TRACE", {}).isNotEmpty();   // one line per simulated minute (diagnosis)
        double nextTrace = 0.0;
        Push nextPush;
        mic.next (nextPush, inBuf.data());
        auto playedAt = [&] (double tt) { return (int64_t) std::floor (tt * rate); };
        while (std::min (nextPush.time, tCons) < sc.seconds)
        {
            if (nextPush.time <= tCons)
            {
                const float* chans[] = { inBuf.data() };
                bridge.push (chans, 1, sc.inputBlock, nextPush.stamp, nextPush.time);
                mic.next (nextPush, inBuf.data());
                continue;
            }
            double t = tCons, stallEnd = 0.0;
            if (insideStall (sc.consumerStalls, t, stallEnd)) t = stallEnd;
            float* chans[] = { block.data() };
            bridge.pull (chans, 1, B, t);
            const auto st = bridge.getStats();
            const int minute = juce::jlimit (0, (int) sc.driftByMinutePpm.size() - 1, (int) (t / 60.0));
            const double pitch = std::abs (st.correctionPpm - sc.driftByMinutePpm[(size_t) minute]);
            if (t > 5.0)
            {
                h.pitchMax = std::max (h.pitchMax, pitch);
                if (pitch > 100.0) h.over100 += outPeriod;
            }
            mic.report (mr);
            const int64_t here = recorded;
            if (mr.captureOverflows > prevOverflows) { h.overflows += mr.captureOverflows - prevOverflows; prevOverflows = mr.captureOverflows; inevitable.push_back (here); }
            mic.reportQueue (mr);
            if (mr.inputSkips > prevSkips) { h.skips += mr.inputSkips - prevSkips; prevSkips = mr.inputSkips; atMicVST.push_back (here); }
            if (st.underruns > prev.underruns) { h.under += st.underruns - prev.underruns; atMicVST.push_back (here); }
            if (st.resyncs > prev.resyncs) { h.trims += st.resyncs - prev.resyncs; atMicVST.push_back (here); }
            prev = st;
            running = running || st.running;

            const int64_t needPlayed = written + B - renderCap;
            if (needPlayed > playedAt (t))
                t = std::ceil ((double) needPlayed / rate / outPeriod - 1.0e-9) * outPeriod;
            if (insideStall (sc.consumerStalls, t, stallEnd)) t = stallEnd;
            const int64_t played = playedAt (t);
            auto feed = [&] (float v)
            {
                if (running && std::abs ((double) v - 2.0 * p1 + p2) > limit && recorded > 48000)
                {
                    if (recorded - lastClick > (int64_t) (0.01 * rate))
                    {
                        if (nearAny (inevitable, recorded, 2400, 24000)) ++h.clkInev;
                        else if (nearAny (atMicVST, recorded, 2400, 24000)) ++h.clkMic;
                        else ++h.clkOther;
                    }
                    lastClick = recorded;
                }
                p2 = p1; p1 = v; ++recorded;
            };
            if (played > written)   // cable thread late longer than its buffer: silence was played
            {
                ++h.gaps; inevitable.push_back (recorded);
                for (int64_t i = written; i < played; ++i) feed (0.0f);
                written = played;
            }
            for (int i = 0; i < B; ++i) feed (block[(size_t) i]);
            written += B;
            tCons = t;
            const double lat = st.bufferedMs + st.inputDelayMs + 1000.0 * (double) (written - playedAt (t)) / rate;
            h.latSum += lat; ++h.latN; h.latMax = std::max (h.latMax, lat);
            if (trace && t >= nextTrace)
            {
                nextTrace += 60.0;
                std::printf ("trace t %7.0f s  true %7.1f  learned %7.1f  corr %7.1f ppm  buffered %6.1f ms  safety %3.0f  under %d trims %d\n",
                             t, sc.driftByMinutePpm[(size_t) minute], st.driftPpm, st.correctionPpm, st.bufferedMs, st.safetyMs, (int) st.underruns, (int) st.resyncs);
            }
            if (t >= hourStart + 3600.0) flushHour (t, st);
        }
        const auto st = bridge.getStats();
        if (h.latN > 0) flushHour (tCons, st);
        mic.report (mr);
        out ("");
        out ("Day total: underruns " + juce::String ((int) tot.under) + ", trims " + juce::String ((int) tot.trims) + ", skips " + juce::String ((int) tot.skips)
             + ", capture overflows " + juce::String (tot.overflows) + " (" + juce::String (juce::roundToInt (mr.lostInputMs)) + " ms lost), cable gaps " + juce::String (tot.gaps)
             + "; clicks: inevitable " + juce::String (tot.clkInev) + ", at MicVST " + juce::String (tot.clkMic) + ", other " + juce::String (tot.clkOther)
             + "; pitch error max " + juce::String (juce::roundToInt (tot.pitchMax)) + " ppm (" + juce::String (tot.pitchMax * 1.0e-6 * 1731.2, 2) + " cent), above 100 ppm for "
             + juce::String (tot.over100, 0) + " s; learned drift at the end " + juce::String (st.driftPpm, 1) + " ppm (true " + juce::String (sc.driftByMinutePpm[(size_t) juce::jmin ((int) sc.driftByMinutePpm.size() - 1, (int) ((tCons - 1.0) / 60.0))], 1)
             + " ppm), safety " + juce::String (st.safetyMs, 0) + " ms.");
        out ("");
    }
}

int main (int argc, char** argv)
{
    bool quick = false;
    double dayHours = 0.0;
    juce::File reportFile;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--quick") quick = true;
        else if (a == "--day" && i + 1 < argc) dayHours = juce::String (argv[++i]).getDoubleValue();
        else reportFile = juce::File::getCurrentWorkingDirectory().getChildFile (a);
    }

    if (dayHours > 0.0)   // Part D only (long)
    {
        out ("# MicVST: simulated day");
        out ("");
        partD (dayHours);
        if (reportFile != juce::File())
            reportFile.replaceWithText (report);
        return 0;
    }

    out ("# MicVST audio path measurements");
    out ("");
    partA (quick);
   #if JUCE_WINDOWS
    partB (quick);
   #else
    out ("(Part B needs Windows.)");
    out ("");
   #endif
    partC (quick);

    if (reportFile != juce::File())
        reportFile.replaceWithText (report);
    return 0;
}
