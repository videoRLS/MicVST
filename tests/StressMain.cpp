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
//
// Usage: MicVSTStress [report.md] [--quick]
#include <juce_core/juce_core.h>
#include "audio/AudioBridge.h"
#include "audio/RealtimeSupport.h"
#include "BridgeSim.h"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <thread>

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
        add ("6. System freeze (DPC storm) 30 ms every 6 s, both threads", [] (Scenario& s)
             { s.seconds = 120; s.inputJitterMs = 1.0;
               for (double t = 5; t < s.seconds; t += 6) { s.stalls.push_back ({ t, 0.030 }); s.consumerStalls.push_back ({ t, 0.030 }); } });
        add ("7. Mic 44.1 kHz, cable 48 kHz", [] (Scenario& s) { s.inputRate = 44100; s.inputBlock = 441; s.inputJitterMs = 1.0; s.seconds = 120; });

        out ("## Part A - simulation: old single-thread design (1.1.1) vs. new bridge (1.2)");
        out ("");
        out ("Same input signal (997 Hz sine), same timing per scenario, both designs with the same WASAPI render");
        out ("buffer (2.2 periods). 'Audible glitches' = output samples where the waveform jumps (clicks, gaps,");
        out ("zero-padding). A faded gap of the new design is not a click but counts under 'gaps'. Latency =");
        out ("audio queued between microphone and cable (input queue/bridge + render buffer), device latency excluded.");
        out ("");
        out ("| Scenario | Design | Audible glitches | Gaps / underruns | Mean added latency |");
        out ("|---|---|---:|---:|---:|");

        for (auto& c : cases)
        {
            const auto oldR = runOldCoupled (c.sc);
            const auto newR = runBridge (c.sc);

            if (oldR.cannotOpen)
                out ("| " + juce::String (c.name) + " | 1.1.1 | device does not open (rate mismatch) | - | - |");
            else
                out ("| " + juce::String (c.name) + " | 1.1.1 | " + juce::String (oldR.clicksAfterStart) + " | "
                     + juce::String (oldR.gapEvents) + " | " + juce::String (oldR.meanLatencyMs, 1) + " ms |");

            out ("| | **1.2** | **" + juce::String (newR.clicksAfterStart) + "** | " + juce::String (newR.gapEvents)
                 + (newR.resyncs > 0 ? " (+" + juce::String ((int) newR.resyncs) + " trims)" : juce::String())
                 + " | " + juce::String (newR.meanLatencyMs, 1) + " ms (safety " + juce::String (newR.finalSafetyMs, 0) + " ms) |");
        }
        out ("");
    }

   #if JUCE_WINDOWS
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
   #endif

    if (reportFile != juce::File())
        reportFile.replaceWithText (report);
    return 0;
}
