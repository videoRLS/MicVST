// Included by EngineTestMain.cpp (after Vst3Suite.inc). Mode "priority": MicVST's process priority
// option (Normal / Normalin Üstünde / Yüksek), Windows only.
//  1. each class through MicVST's own code (AudioEngine::setProcessPriority), read back from Windows
//  2. under full CPU load from another program (a child process with one normal-priority busy
//     thread per CPU, plus a thread that measures how late it wakes = how responsive that program
//     stays): audio quality and continuity, MicVST's window thread (timer lateness), and how much
//     MicVST takes from the other program when one of its non-audio threads is busy (a heavy
//     plug-in window / analyser) - for each class
//  3. switching between the classes every second while audio plays
// Mode "burn <threads> <seconds>": the load program itself (prints its results).
namespace prio
{
   #if JUCE_WINDOWS
    #pragma comment (lib, "winmm.lib")
    struct Lateness { std::vector<double> ms; double p (double q) const { if (ms.empty()) return 0; auto v = ms; std::sort (v.begin(), v.end()); return v[std::min (v.size() - 1, (size_t) (q * (double) v.size()))]; }
                      double max() const { return ms.empty() ? 0 : *std::max_element (ms.begin(), ms.end()); } };

    // "burn": N busy threads (normal priority) + a probe thread sleeping 1 ms in a loop
    int burn (int threads, double seconds)
    {
        std::atomic<bool> stop { false };
        std::vector<std::thread> pool;
        std::vector<uint64_t> counts ((size_t) threads, 0);
        for (int i = 0; i < threads; ++i)
            pool.emplace_back ([&stop, &counts, i] { volatile double x = 1.0; uint64_t n = 0; while (! stop.load (std::memory_order_relaxed)) { for (int k = 0; k < 2000; ++k) x = x * 1.0000001 + 1e-9; ++n; } counts[(size_t) i] = n; });
        Lateness probe;
        std::thread pr2 ([&stop, &probe]
        {
            timeBeginPeriod (1);
            while (! stop.load())
            {
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                Sleep (1);
                probe.ms.push_back (juce::Time::getMillisecondCounterHiRes() - t0 - 1.0);
            }
            timeEndPeriod (1);
        });
        Sleep ((DWORD) (seconds * 1000.0));
        stop = true;
        for (auto& t : pool) t.join();
        pr2.join();
        uint64_t total = 0; for (auto c : counts) total += c;
        std::printf ("burn: throughput %.0f units/s, probe wake-up lateness p50 %.2f ms, p99 %.2f ms, max %.2f ms, %d wake-ups\n",
                     (double) total / seconds, probe.p (0.5), probe.p (0.99), probe.max(), (int) probe.ms.size());
        return 0;
    }

    struct TimerProbe final : juce::Timer
    {
        Lateness l; double last = 0;
        void timerCallback() override { const double t = juce::Time::getMillisecondCounterHiRes(); if (last > 0) l.ms.push_back (t - last - 10.0); last = t; }
    };

    juce::String className (DWORD c)
    {
        switch (c)
        {
            case NORMAL_PRIORITY_CLASS: return "NORMAL_PRIORITY_CLASS"; case ABOVE_NORMAL_PRIORITY_CLASS: return "ABOVE_NORMAL_PRIORITY_CLASS";
            case HIGH_PRIORITY_CLASS: return "HIGH_PRIORITY_CLASS"; case IDLE_PRIORITY_CLASS: return "IDLE_PRIORITY_CLASS";
            case BELOW_NORMAL_PRIORITY_CLASS: return "BELOW_NORMAL_PRIORITY_CLASS"; case REALTIME_PRIORITY_CLASS: return "REALTIME_PRIORITY_CLASS";
            default: return "0x" + juce::String::toHexString ((int) c);
        }
    }

    void run()
    {
        auto& cfg = vdev::config();
        const double fs = cfg.cableRate;
        int fails = 0;
        auto check = [&fails] (bool ok, const juce::String& what) { pr (juce::String ("  ") + (ok ? "PASS " : "FAIL ") + what); if (! ok) ++fails; };
        const int cpus = juce::SystemStats::getNumCpus();
        pr ("## Process priority option (" + juce::String (cpus) + " logical CPUs, " + juce::SystemStats::getOperatingSystemName() + ")");
        pr ("start: " + className (GetPriorityClass (GetCurrentProcess())));

        // ---- 1. applied through MicVST's code, read back ----
        pr ("");
        pr ("### 1. Each choice applied through AudioEngine::setProcessPriority, read back with GetPriorityClass");
        {
            AudioEngine e;
            for (int choice : { 2, 1, 0, 2, 0 })
            {
                const auto r = e.setProcessPriority (choice);
                const DWORD c = GetPriorityClass (GetCurrentProcess());
                const DWORD want = choice == 2 ? HIGH_PRIORITY_CLASS : choice == 1 ? ABOVE_NORMAL_PRIORITY_CLASS : NORMAL_PRIORITY_CLASS;
                check (r.ok && c == want && e.getProcessPriority() == choice && rt::currentProcessPriority() == choice,
                       AudioEngine::priorityName (choice) + " -> Windows reports " + className (c) + ", the setting holds " + juce::String (e.getProcessPriority()));
            }
            // saved in config.xml and applied again at start (applyState)
            MicVSTState st; st.processPriority = 1;
            const auto back = fromValueTree (juce::ValueTree::fromXml (*toValueTree (st).createXml()));
            AudioEngine e2;
            e2.applyState (back);
            check (back.processPriority == 1 && GetPriorityClass (GetCurrentProcess()) == ABOVE_NORMAL_PRIORITY_CLASS,
                   "config.xml round trip (Normalin Ustunde) and applied at start: " + className (GetPriorityClass (GetCurrentProcess())));
            MicVSTState old;   // a config.xml without the setting (older versions) -> Normal
            const auto oldBack = fromValueTree (juce::ValueTree::fromXml (*juce::parseXML ("<MicVST inputDevice=\"x\" outputDevice=\"y\"/>")));
            e2.applyState (oldBack);
            check (oldBack.processPriority == 0 && GetPriorityClass (GetCurrentProcess()) == NORMAL_PRIORITY_CLASS,
                   "older config.xml without the setting -> " + className (GetPriorityClass (GetCurrentProcess())));
            juce::ignoreUnused (old);
            // a class set from outside (Task Manager) is reported as "another level"
            SetPriorityClass (GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
            check (rt::currentProcessPriority() == -1, "below normal set from outside -> shown as another level (" + juce::String (rt::currentProcessPriority()) + ")");
            SetPriorityClass (GetCurrentProcess(), NORMAL_PRIORITY_CLASS);
        }

        // ---- 2. under full CPU load from another program ----
        pr ("");
        pr ("### 2. Under full CPU load: another program with " + juce::String (cpus) + " busy threads (normal priority) + a wake-up probe; MicVST plays 997 Hz -6 dBFS "
            "through Otomatik Kazanc + a plug-in with 30 % load; MicVST's window thread runs a 10 ms timer; with and without a busy non-audio MicVST thread "
            "(a heavy plug-in window: 8 ms busy / 8 ms idle). 40 s per row.");
        pr ("| MicVST priority | busy MicVST window thread | audio: clicks / silent gaps / MicVST events | THD+N (median, 43 ms windows) | MicVST timer lateness p99 / max (ms) "
            "| busy thread work done | other program: throughput | other program: wake-up lateness p99 / max (ms) |");
        pr ("|---|---|---|---:|---:|---:|---:|---:|");
        const auto self = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
        double baseThroughput = 0;
        {   // reference: the load program alone (MicVST not running)
            juce::ChildProcess cp;
            juce::StringArray a; a.add (self); a.add ("burn"); a.add (juce::String (cpus)); a.add ("20");
            if (cp.start (a))
            {
                const auto out = cp.readAllProcessOutput();
                cp.waitForProcessToFinish (60000);
                const auto line = juce::StringArray::fromLines (out).joinIntoString (" ").fromFirstOccurrenceOf ("burn:", false, false);
                baseThroughput = line.fromFirstOccurrenceOf ("throughput", false, false).getDoubleValue();
                pr ("| (MicVST not running) | - | - | - | - | - | " + juce::String (baseThroughput, 0) + " /s | " + line.fromFirstOccurrenceOf ("p99", false, false).upToFirstOccurrenceOf (",", false, false).trim()
                    + " / " + line.fromFirstOccurrenceOf ("max", false, false).upToFirstOccurrenceOf (",", false, false).trim() + " |");
            }
        }
        struct Row { int choice; bool busy; };
        for (const Row row : { Row { 0, false }, Row { 1, false }, Row { 2, false }, Row { 0, true }, Row { 1, true }, Row { 2, true } })
        {
            sineConfig (997.0, 0.5, 48000);
            suite::Counters c0, c1;
            Lateness timer;
            uint64_t busyWork = 0;
            juce::String loadLine;
            {
                EngineRun run (480, 44.0);
                auto& e = *run.engine;
                e.setProcessPriority (row.choice);
                run.add (testDesc ("TestLoad", 0.3));
                e.getChain().addAutoGain (true);
                { const AudioEngine::ChainEdit ce (e); e.rebuildGraph(); }
                pump (2.0);
                std::atomic<bool> stop { false };
                std::atomic<uint64_t> work { 0 };
                std::thread busy;
                if (row.busy)
                    busy = std::thread ([&stop, &work]
                    {
                        while (! stop.load())
                        {
                            const double t0 = juce::Time::getMillisecondCounterHiRes();
                            volatile double x = 1.0;
                            while (juce::Time::getMillisecondCounterHiRes() - t0 < 8.0) { for (int k = 0; k < 500; ++k) x = x * 1.0000001 + 1e-9; work.fetch_add (1, std::memory_order_relaxed); }
                            Sleep (8);
                        }
                    });
                juce::ChildProcess cp;
                juce::StringArray a; a.add (self); a.add ("burn"); a.add (juce::String (cpus)); a.add ("38");
                const bool started = cp.start (a);
                TimerProbe tp;
                c0 = suite::counters (e);
                tp.startTimer (10);
                pump (39.0);
                tp.stopTimer();
                c1 = suite::counters (e);
                timer = tp.l;
                if (started) { loadLine = juce::StringArray::fromLines (cp.readAllProcessOutput()).joinIntoString (" "); cp.waitForProcessToFinish (30000); }
                stop = true;
                if (busy.joinable()) busy.join();
                busyWork = work.load();
                e.setProcessPriority (0);
            }
            const auto& L = vdev::recorder().left;
            suite::Window w; w.from = (size_t) (4.0 * fs); w.to = std::min (L.size(), (size_t) (41.0 * fs));
            const int clk = suite::sineClicks (L, w, 997.0), gaps = suite::zeroRuns (L, w);
            const double thd = suite::thdnDb (L, w, 997.0);
            const auto ll = loadLine.fromFirstOccurrenceOf ("burn:", false, false);
            const double thr = ll.fromFirstOccurrenceOf ("throughput", false, false).getDoubleValue();
            pr ("| " + AudioEngine::priorityName (row.choice) + " | " + (row.busy ? "yes" : "no") + " | " + juce::String (clk) + " / " + juce::String (gaps) + " / "
                + (suite::countersClean (c0, c1) ? juce::String ("0") : suite::countersDiff (c0, c1)) + " | " + juce::String (thd, 1) + " dB | "
                + juce::String (timer.p (0.99), 2) + " / " + juce::String (timer.max(), 2) + " | " + (row.busy ? juce::String ((juce::int64) busyWork) : juce::String ("-"))
                + " | " + juce::String (thr, 0) + " /s (" + juce::String (baseThroughput > 0 ? 100.0 * thr / baseThroughput : 0.0, 1) + " % of alone) | "
                + ll.fromFirstOccurrenceOf ("p99", false, false).upToFirstOccurrenceOf (",", false, false).trim() + " / "
                + ll.fromFirstOccurrenceOf ("max", false, false).upToFirstOccurrenceOf (",", false, false).trim() + " |");
            check (clk == 0 && gaps == 0 && suite::countersClean (c0, c1),
                   AudioEngine::priorityName (row.choice) + (row.busy ? " + busy window thread" : "") + ": audio continuous under full CPU load (clicks " + juce::String (clk)
                   + ", gaps " + juce::String (gaps) + ", " + suite::countersDiff (c0, c1) + ")");
        }

        // ---- 3. switching while audio plays ----
        pr ("");
        pr ("### 3. Switching Normal -> Normalin Ustunde -> Yuksek -> ... every second while audio plays (30 s)");
        {
            sineConfig (997.0, 0.5, 48000);
            suite::Counters c0, c1;
            int wrong = 0;
            {
                EngineRun run (480, 34.0);
                auto& e = *run.engine;
                e.getChain().addAutoGain (true);
                { const AudioEngine::ChainEdit ce (e); e.rebuildGraph(); }
                pump (2.0);
                c0 = suite::counters (e);
                for (int k = 0; k < 30; ++k)
                {
                    const int choice = k % 3;
                    const auto r = e.setProcessPriority (choice);
                    if (! r.ok || rt::currentProcessPriority() != choice) ++wrong;
                    pump (1.0);
                }
                c1 = suite::counters (e);
                e.setProcessPriority (0);
            }
            const auto& L = vdev::recorder().left;
            suite::Window w; w.from = (size_t) (2.5 * fs); w.to = std::min (L.size(), (size_t) (32.0 * fs));
            const int clk = suite::sineClicks (L, w, 997.0), gaps = suite::zeroRuns (L, w);
            check (wrong == 0, "30 switches applied and read back correctly (" + juce::String (wrong) + " wrong)");
            check (clk == 0 && gaps == 0 && suite::countersClean (c0, c1), "no click, gap or MicVST event while switching (clicks " + juce::String (clk) + ", gaps "
                   + juce::String (gaps) + ", " + suite::countersDiff (c0, c1) + "), THD+N " + juce::String (suite::thdnDb (L, w, 997.0), 1) + " dB");
        }
        pr ("");
        pr ("priority test: " + juce::String (fails) + " failed");
    }
   #else
    int burn (int, double) { return 0; }
    void run() { pr ("process priority: Windows only (not tested on this system)"); }
   #endif
}
