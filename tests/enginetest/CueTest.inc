// "cues": Bildirim sesleri in the real engine. Sustur (setMuted - what the button, the shortcut keys
// and the tray call) plays the sound in the same call, exactly once per real change, never at
// start-up; the cable is muted as before (no clicks); switched off: silent; the cable never gets it.
namespace cuetest
{
    int failures = 0;
    int hiccupsNow() { return vdev::recorder().deviceGaps.load() + (int) vdev::stats().lostPackets.load(); }
    void check (bool ok, const juce::String& what) { pr (juce::String ("  ") + (ok ? "PASS " : "FAIL ") + what); if (! ok) ++failures; }

    struct Recording final : CuePlayer::Backend
    {
        std::vector<double>* when; std::vector<size_t>* sizes;
        Recording (std::vector<double>* w, std::vector<size_t>* s) : when (w), sizes (s) {}
        bool play (const juce::MemoryBlock& wav) override { when->push_back (juce::Time::getMillisecondCounterHiRes()); sizes->push_back (wav.getSize()); return true; }
        void stop() override {}
        juce::String defaultOutputName() override { return "Kulaklik"; }
    };

    void run()
    {
        auto& cfg = vdev::config();
        sineConfig (997.0, 0.1, cfg.micRate);
        pr ("## Bildirim sesleri: Sustur in the real engine (997 Hz -20 dBFS, virtual devices)");
        std::vector<double> when; std::vector<size_t> sizes;
        {
            EngineRun run (480, 40.0);
            auto& e = *run.engine;
            e.getCues().setBackend (std::make_unique<Recording> (&when, &sizes));
            pump (2.0);
            check (when.empty(), "no sound at start-up");

            // 40 mute / unmute changes, as the button / F9 / tray do; plus repeated calls without a change
            std::vector<double> calls;
            int64_t cableAt = vdev::recorder().available.load();
            for (int i = 0; i < 40; ++i)
            {
                const bool m = (i % 2) == 0;
                calls.push_back (juce::Time::getMillisecondCounterHiRes());
                e.setMuted (m);
                e.setMuted (m);   // the same state again: no second sound
                pump (0.35);
            }
            const int64_t cableEnd = vdev::recorder().available.load();
            check ((int) when.size() == 40, "one sound per real change (40 changes, 40 sounds; repeats silent): " + juce::String ((int) when.size()));
            double worst = 0.0;
            for (size_t i = 0; i < std::min (when.size(), calls.size()); ++i) worst = std::max (worst, when[i] - calls[i]);
            pr ("    from the mute call to the sound handed to Windows: at most " + juce::String (worst, 3) + " ms");
            check (worst < 5.0, "the sound starts with the mute itself (< 5 ms; it is the same call)");
            bool alternate = true;
            const size_t onSize = sizes.size() > 1 ? sizes[1] : 0, offSize = sizes.empty() ? 0 : sizes[0];
            for (size_t i = 0; i < sizes.size(); ++i) alternate = alternate && sizes[i] == (i % 2 == 0 ? offSize : onSize);
            check (alternate && onSize != offSize, "mute -> kapanma, unmute -> açılma, in order");

            // the cable: muted / unmuted as before, without clicks
            const auto& L = vdev::recorder().left;
            const auto clk = clicks (L, (size_t) cableAt, (size_t) cableEnd, 0.1, 997.0 / cfg.cableRate);
            pr ("    cable during the 40 changes: clicks " + juce::String (clk.events) + (hiccupsNow() > 0 ? ", machine hiccups " + juce::String (hiccupsNow()) : juce::String()));
            check (clk.events == 0 || hiccupsNow() > 0, "Sustur on the cable: no clicks (15 ms fades as before)");

            // switched off (the tray switch / Settings): silent, the mute itself unchanged
            auto s = e.getCues().getSettings(); s.enabled = false; e.getCues().setSettings (s);
            const size_t before = when.size();
            for (int i = 0; i < 6; ++i) { e.setMuted (i % 2 == 0); pump (0.1); }
            check (when.size() == before, "switched off: mute / unmute silent");
            check (! e.isMuted(), "the mute itself still works");

            // the default output is the cable: nothing
            s.enabled = true; e.getCues().setSettings (s);
            struct CableOut final : CuePlayer::Backend
            {
                int* n; explicit CableOut (int* p) : n (p) {}
                bool play (const juce::MemoryBlock&) override { ++*n; return true; }
                void stop() override {}
                juce::String defaultOutputName() override { return "VCable"; }
            };
            int toCable = 0;
            e.getCues().setBackend (std::make_unique<CableOut> (&toCable));
            for (int i = 0; i < 4; ++i) { e.setMuted (i % 2 == 0); pump (0.1); }
            check (toCable == 0, "Windows' default output = the cable: no sound (the other side would hear it)");

            // saved with the state and back after a restart
            s.onId = "builtin:03_damla_ac"; s.offId = "builtin:07_yanki_kapat"; s.volume = 55; s.enabled = false;
            e.getCues().setSettings (s);
            const auto st = fromValueTree (juce::ValueTree::fromXml (*juce::parseXML (toValueTree (e.captureState()).createXml()->toString())));
            check (CueSettings::decode (st.cueSounds) == s, "settings saved and read back (restart): " + st.cueSounds);
            e.getCues().setSettings (CueSettings{});
            check (fromValueTree (toValueTree (e.captureState())).cueSounds.isEmpty(), "the default writes nothing (older config.xml stay as they are)");
        }
       #if JUCE_WINDOWS
        {
            // the real Windows backend (MicVST's own mixer on waveOut; PlaySound only if no device opens):
            // 200 plays 5 ms apart - far faster than anyone presses - then the player is destroyed.
            // No crash, no hang; whether this machine has an output device is only logged.
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            juce::String problem;
            {
                CuePlayer p;
                p.cableName = [] { return juce::String(); };
                p.setSettings (CueSettings{});
                for (int i = 0; i < 200; ++i) { p.play ((i & 1) != 0); juce::Thread::sleep (5); }
                juce::Thread::sleep (300);
                problem = p.lastProblem();
            }
            const double beyond = juce::Time::getMillisecondCounterHiRes() - t0 - 200 * 5 - 300;
            pr ("  real backend: " + (problem.isEmpty() ? juce::String ("no problem reported") : "reported: " + problem));
            check (beyond < 3000.0, "the real Windows backend: 200 plays 5 ms apart and shut-down without a hang ("
                                    + juce::String (beyond, 0) + " ms beyond the waits)");
        }
       #endif
        pr ("");
        pr (failures == 0 ? "cues: all checks passed" : "cues: " + juce::String (failures) + " failures");
    }
}
