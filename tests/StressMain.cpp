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
// Usage: MicVSTStress [report.md] [--quick]
#include <juce_core/juce_core.h>
#include "audio/AudioBridge.h"
#include "audio/InputQueue.h"
#include "audio/RealtimeSupport.h"
#include "BridgeSim.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
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

int main (int argc, char** argv)
{
    bool quick = false;
    juce::File reportFile;
    for (int i = 1; i < argc; ++i)
    {
        const juce::String a (argv[i]);
        if (a == "--quick") quick = true;
        else reportFile = juce::File::getCurrentWorkingDirectory().getChildFile (a);
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
