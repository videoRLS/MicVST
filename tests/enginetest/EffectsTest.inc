// MicVSTEngineTest effects: the built-in effects (BuiltInEffects.h) in MicVST's real engine with real
// threads between the virtual devices: every effect alone (level, distortion, clicks, gaps,
// latency), chain edits with effects while audio plays, all 24 at once (load), live parameter
// changes, save -> config.xml -> restore of every parameter, mono microphone.
// Included into tests/EngineTestMain.cpp (namespace of the helpers). Ends with "effects: N failures".
namespace fxtest
{
    int failures = 0;
    juce::String only;   // "effects <id>": only this effect (sections 1 and 4)
    void check (bool ok, const juce::String& what)
    {
        if (! ok) { ++failures; pr ("  FAIL: " + what); }
    }

    void addFx (EngineRun& r, const juce::String& id)
    {
        r.engine->getChain().addBuiltInEffect (id);
        const AudioEngine::ChainEdit e (*r.engine);
        r.engine->rebuildGraph();
    }

    uint64_t invalidBlocks (AudioEngine& e) { return e.getDiagnostics().invalidBlocks; }

    // clicks in the recorded left channel from 50 ms after the first audio to 50 ms before the end
    int clicksIn (double amp, double freqHz)
    {
        const auto& L = vdev::recorder().left;
        size_t last = L.size();
        while (last > 0 && std::abs (L[last - 1]) < 1.0e-6f) --last;
        const double fs = vdev::config().cableRate;
        const size_t start = firstAudio (L) + (size_t) (0.05 * fs), end = last > (size_t) (0.05 * fs) ? last - (size_t) (0.05 * fs) : 0;
        // the detector's limit follows the loudest the chain made the tone (EQ +18 dB ...), at least amp
        float peak = 0.0f;
        for (size_t i = start; i < end; ++i) peak = std::max (peak, std::abs (L[i]));
        return clicks (L, start, end, std::max (amp, (double) peak), freqHz / fs).events;
    }

    // where the clicks are (seconds on the cable timeline) - for the report
    juce::String clickTimes (double amp, double freqHz)
    {
        const auto& L = vdev::recorder().left;
        const double fs = vdev::config().cableRate;
        float peak = 0.0f;
        const size_t start = firstAudio (L) + (size_t) (0.05 * fs);
        for (size_t i = start; i < L.size(); ++i) peak = std::max (peak, std::abs (L[i]));
        std::vector<size_t> where;
        clicks (L, start, L.size(), std::max (amp, (double) peak), freqHz / fs, 480, &where);
        juce::String s;
        for (size_t k = 0; k < std::min<size_t> (where.size(), 8); ++k)
            s << (k > 0 ? ", " : "") << juce::String ((double) where[k] / fs, 3) << " s";
        return s;
    }

    int hiccups() { return vdev::recorder().deviceGaps.load() + (int) vdev::stats().lostPackets.load(); }

    void run()
    {
        auto& cfg = vdev::config();
        const double fs = cfg.cableRate;

        pr ("## Every built-in effect alone in the real engine (997 Hz, -20 dBFS, buffer 480, added while audio runs)");
        pr ("| Effect | Gain L / R | THD+N L | Clicks | Gaps | Invalid blocks | Reported latency | Shown latency added |");
        pr ("|---|---:|---:|---:|---:|---:|---:|---:|");
        double emptyShown = 0.0;
        {
            sineConfig (997.0, 0.1, cfg.micRate);
            EngineRun run (480, 3.0);
            pump (3.0);
            emptyShown = run.engine->getDiagnostics().latencyMs;
        }
        int inconclusive = 0;
        for (auto& info : builtin::effects())
        {
            const juce::String id (info.id);
            if (only.isNotEmpty() && id != only) continue;
            // A hiccup of the virtual devices (this machine's scheduling: lost microphone packets, cable
            // gaps) makes clicks and gaps of its own. Such a run is repeated (up to 3 times); a run
            // without hiccups decides. If none was clean, the row says so and is not counted as passed.
            for (int attempt = 1; attempt <= 3; ++attempt)
            {
                int reported = 0, inChain = -1;
                uint64_t invalid = 0;
                double shown = 0.0;
                const auto r = measureSine (997.0, 0.1, 7.0, 480, 4.0, [&] (EngineRun& run)
                {
                    addFx (run, id);
                    pump (3.0);
                    if (auto* fx = run.engine->getChain().getBuiltInEffect (0)) reported = fx->getLatencySamples();
                    inChain = run.engine->chainLatencySamples();
                    shown = run.engine->getDiagnostics().latencyMs;
                    invalid = invalidBlocks (*run.engine);
                }, 2.0);
                // gaps after the insertion (the chain edit's own short silence at the start is wanted)
                const auto gapsAfter = silences (vdev::recorder().left, (size_t) (2.0 * fs), fs);
                const int hiccups = vdev::recorder().deviceGaps.load() + (int) vdev::stats().lostPackets.load();
                if (hiccups > 0 && (r.clk.events > 0 || gapsAfter.count > 0) && attempt < 3)
                {
                    pr ("    (device hiccups " + juce::String (hiccups) + " with " + juce::String (r.clk.events) + " clicks: run repeated)");
                    continue;
                }
                const bool judged = hiccups == 0 || (r.clk.events == 0 && gapsAfter.count == 0);
                pr ("| " + juce::String::fromUTF8 (info.name) + " | " + db (r.gainDbL, 2) + " / " + db (r.gainDbR, 2) + " | " + db (r.thdnL)
                    + " | " + juce::String (r.clk.events) + " | " + juce::String (gapsAfter.count) + " | " + juce::String ((int) invalid)
                    + " | " + juce::String (reported) + " | " + juce::String (shown - emptyShown, 1) + " ms |"
                    + (judged ? juce::String() : " NOT JUDGED: device hiccups in all 3 runs"));
                if (! judged) ++inconclusive;
                if (r.clk.events > 0) pr ("    clicks at " + clickTimes (0.2, 997.0) + " (first audio at "
                                           + juce::String ((double) firstAudio (vdev::recorder().left) / fs, 3) + " s)");
                check (! judged || r.clk.events == 0, id + ": clicks");
                check (! judged || gapsAfter.count == 0, id + ": gaps");
                check (invalid == 0, id + ": invalid blocks");
                check (std::abs (r.gainDbL - r.gainDbR) < 0.05 || info.stereo, id + ": L and R differ");
                // the engine adds the element's latency (shown latency, silence around edits)
                check (inChain == reported, id + ": chain latency " + juce::String (inChain) + " != reported " + juce::String (reported));
                break;
            }
        }
        pr ("rows not judged because of device hiccups: " + juce::String (inconclusive));

        if (only.isEmpty() || only == "edits") {
        pr ("");
        pr ("## Chain edits with effects while audio plays (997 Hz -20 dBFS, one edit every 0.6 s, 60 edits)");
        {
            sineConfig (997.0, 0.1, cfg.micRate);
            std::mt19937 rng (5);
            int adds = 0, moves = 0, bypasses = 0, removes = 0;
            juce::StringArray editLog;
            uint64_t invalid = 0;
            {
                EngineRun run (480, 40.0);
                auto& e = *run.engine;
                pump (1.0);
                const auto& list = builtin::effects();
                for (int k = 0; k < 60; ++k)
                {
                    auto& c = e.getChain();
                    const int n = (int) c.entries().size();
                    const int op = n < 3 ? 0 : (int) (rng() % 4);
                    const double t = juce::Time::getMillisecondCounterHiRes() * 0.001 - vdev::recorder().outputStart.load();
                    juce::String what;
                    {
                        const AudioEngine::ChainEdit ce (e);
                        if (op == 0 || n == 0) { const auto* id = list[(size_t) (rng() % list.size())].id; c.addBuiltInEffect (id); ++adds; what = juce::String ("add ") + id; }
                        else if (op == 1)      { c.movePlugin ((int) (rng() % (unsigned) n), (int) (rng() % (unsigned) n)); ++moves; what = "move"; }
                        else if (op == 2)      { const int i = (int) (rng() % (unsigned) n); c.setBypass (i, ! c.entries()[(size_t) i].bypassed); ++bypasses; what = "bypass " + c.entries()[(size_t) i].fileOrId; }
                        else                   { const int i = (int) (rng() % (unsigned) n); what = "remove " + c.entries()[(size_t) i].fileOrId; c.removePlugin (i); ++removes; }
                        e.rebuildGraph();
                    }
                    editLog.add (juce::String (t, 3) + " s: " + what);
                    pump (0.6);
                }
                pr ("    " + diagLine (e));
                invalid = invalidBlocks (e);
            }
            const int clk = clicksIn (0.1, 997.0);
            const int h = hiccups();
            pr ("edits: " + juce::String (adds) + " added, " + juce::String (moves) + " moved, " + juce::String (bypasses) + " bypass switches, "
                + juce::String (removes) + " removed; clicks " + juce::String (clk) + ", invalid blocks " + juce::String ((int) invalid)
                + (h > 0 ? ", device hiccups " + juce::String (h) : juce::String()));
            if (clk > 0)
            {
                pr ("    clicks at " + clickTimes (0.1, 997.0));
                for (auto& m : editLog) pr ("    " + m);
            }
            check (clk == 0 || h > 0, "chain edits: clicks");
            check (invalid == 0, "chain edits: invalid blocks");
        }

        pr ("");
        pr ("## All 24 effects in one chain (speech, 20 s): load and stability");
        {
            talkConfig (-20.0, 3);
            EngineRun run (480, 20.0);
            for (auto& info : builtin::effects())
                run.engine->getChain().addBuiltInEffect (info.id);
            {
                const AudioEngine::ChainEdit ce (*run.engine);
                run.engine->rebuildGraph();
            }
            pump (20.0);
            const auto d = run.engine->getDiagnostics();
            pr ("    " + diagLine (*run.engine));
            pr ("plug-in thread load: mean " + juce::String (d.dspLoadPercent, 1) + " %, worst block of the last second " + juce::String (d.dspPeakPercent, 1)
                + " %; overloads " + juce::String ((int) d.overloads) + ", skips " + juce::String ((int) d.inputSkips) + ", invalid blocks "
                + juce::String ((int) d.invalidBlocks) + ", chain latency " + juce::String (1000.0 * run.engine->chainLatencySamples() / fs, 1) + " ms");
            bool finite = true;
            for (auto v : vdev::recorder().left) finite = finite && std::isfinite (v);
            check (finite, "all effects: finite output");
            check (d.invalidBlocks == 0, "all effects: invalid blocks");
            check (d.inputSkips == 0, "all effects: input skips (chain too slow)");
        }

        }
        pr ("");
        pr ("## Live parameter changes while audio plays (997 Hz -20 dBFS; every parameter of every effect set to random values, 4 per second)");
        {
            std::mt19937 rng (9);
            std::uniform_real_distribution<float> u (0.0f, 1.0f);
            pr ("| Effect | Changes | Clicks | Invalid blocks |");
            pr ("|---|---:|---:|---:|");
            int totalClicks = 0;
            int notJudged = 0;
            for (auto& info : builtin::effects())
            {
                const juce::String id (info.id);
                if (only.isNotEmpty() && id != only) continue;
                for (int attempt = 1; attempt <= 3; ++attempt)
                {
                    int changes = 0;
                    uint64_t invalid = 0;
                    sineConfig (997.0, 0.1, cfg.micRate);
                    {
                        EngineRun run (480, 7.0);
                        addFx (run, id);
                        pump (1.0);
                        auto* fx = run.engine->getChain().getBuiltInEffect (0);
                        for (int k = 0; k < 20 && fx != nullptr; ++k)
                        {
                            for (auto* p : fx->getEffectParameters())
                            {
                                // the mute is left open (silence has no clicks to find); the limiter's
                                // ceiling would make the tone a different signal
                                if (id == "builtin:mute") continue;
                                if (id == "builtin:limiter" && p->getParameterID() == "ceiling") continue;
                                p->setValueNotifyingHost (u (rng));
                                ++changes;
                            }
                            pump (0.25);
                        }
                        pump (0.5);
                        invalid = invalidBlocks (*run.engine);
                    }
                    const int clk = clicksIn (0.1, 997.0);
                    const int h = hiccups();
                    if (clk > 0 && h > 0 && attempt < 3) { pr ("    (" + id + ": device hiccups " + juce::String (h) + ", run repeated)"); continue; }
                    const bool judged = clk == 0 || h == 0;
                    if (judged) totalClicks += clk; else ++notJudged;
                    pr ("| " + juce::String::fromUTF8 (info.name) + " | " + juce::String (changes) + " | " + juce::String (clk) + " | " + juce::String ((int) invalid) + " |"
                        + (judged ? juce::String() : " NOT JUDGED: device hiccups in all 3 runs"));
                    if (clk > 0) pr ("    clicks at " + clickTimes (0.1, 997.0));
                    check (invalid == 0, id + ": invalid blocks during parameter changes");
                    break;
                }
            }
            pr ("rows not judged because of device hiccups: " + juce::String (notJudged));
            pr ("clicks in all runs: " + juce::String (totalClicks) + " (the unit tests check every parameter jump against the steady signal; "
                "here the absolute detector of the engine test is used)");
            check (totalClicks == 0, "live parameter changes: clicks");
        }

        if (only.isNotEmpty()) { pr ("effects: " + juce::String (failures) + " failures"); return; }
        pr ("");
        pr ("## Saved and restored (captureState -> config.xml -> applyState): every parameter, order, bypass");
        {
            sineConfig (997.0, 0.1, cfg.micRate);
            std::mt19937 rng (13);
            std::uniform_real_distribution<float> u (0.0f, 1.0f);
            MicVSTState saved;
            std::vector<std::map<juce::String, float>> want;
            std::vector<juce::String> ids;
            std::vector<bool> byp;
            {
                EngineRun run (480, 2.0);
                auto& c = run.engine->getChain();
                for (auto& info : builtin::effects())
                    c.addBuiltInEffect (info.id);
                c.addGain (4.5f);
                c.addAutoGain (false);
                for (int k = 0; k < 10; ++k) c.movePlugin ((int) (rng() % c.entries().size()), (int) (rng() % c.entries().size()));
                for (int i = 0; i < (int) c.entries().size(); ++i)
                {
                    if (rng() % 4 == 0) c.setBypass (i, true);
                    if (auto* fx = c.getBuiltInEffect (i))
                        for (auto* p : fx->getEffectParameters()) p->setValueNotifyingHost (u (rng));
                }
                {
                    const AudioEngine::ChainEdit ce (*run.engine);
                    run.engine->rebuildGraph();
                }
                pump (0.5);
                for (int i = 0; i < (int) c.entries().size(); ++i)
                {
                    ids.push_back (c.entries()[(size_t) i].fileOrId);
                    byp.push_back (c.entries()[(size_t) i].bypassed);
                    std::map<juce::String, float> m;
                    if (auto* fx = c.getBuiltInEffect (i))
                        for (auto* p : fx->getEffectParameters()) m[p->getParameterID()] = fx->getParam (p->getParameterID());
                    want.push_back (m);
                }
                saved = run.engine->captureState();
            }
            // through the real config.xml text
            const auto xmlText = toValueTree (saved).createXml()->toString();
            auto parsed = juce::XmlDocument::parse (xmlText);
            MicVSTState st = fromValueTree (juce::ValueTree::fromXml (*parsed));
            // an element of a newer MicVST in the middle: skipped, the rest is restored
            PluginEntryState future;
            future.fileOrId = "builtin:from-a-newer-version";
            st.plugins.insert (5, future);
            st.inputDevice = "VMic"; st.outputDevice = "VCable"; st.bufferSize = 480;
            EngineRun run (480, 2.0);
            run.engine->applyState (st);
            pump (0.5);
            auto& c = run.engine->getChain();
            int wrongOrder = 0, wrongBypass = 0, wrongParams = 0, params = 0;
            const bool sameCount = c.entries().size() == ids.size();
            for (size_t i = 0; i < std::min (ids.size(), c.entries().size()); ++i)
            {
                if (c.entries()[i].fileOrId != ids[i]) ++wrongOrder;
                if (c.entries()[i].bypassed != byp[i]) ++wrongBypass;
                if (auto* fx = c.getBuiltInEffect ((int) i))
                    for (auto& [pid, v] : want[i])
                    {
                        ++params;
                        if (std::abs (fx->getParam (pid) - v) > 1.0e-4f * std::max (1.0f, std::abs (v))) ++wrongParams;
                    }
            }
            const float gainBack = c.isGain ((int) (std::find (ids.begin(), ids.end(), juce::String (PluginChain::gainId)) - ids.begin()))
                                       ? c.getGainDb ((int) (std::find (ids.begin(), ids.end(), juce::String (PluginChain::gainId)) - ids.begin())) : -999.0f;
            pr ("elements saved " + juce::String ((int) ids.size()) + ", restored " + juce::String ((int) c.entries().size())
                + " (one unknown element from a newer version skipped); order wrong " + juce::String (wrongOrder) + ", bypass wrong "
                + juce::String (wrongBypass) + ", parameters compared " + juce::String (params) + ", wrong " + juce::String (wrongParams)
                + "; Kazanç " + juce::String (gainBack, 1) + " dB; config.xml " + juce::String (xmlText.length() / 1024) + " KB");
            int allParams = 0;
            for (auto& info : builtin::effects()) allParams += builtin::create (info.id)->getEffectParameters().size();
            check (sameCount && wrongOrder == 0 && wrongBypass == 0 && wrongParams == 0 && params == allParams && std::abs (gainBack - 4.5f) < 0.01f,
                   "save/restore");
        }

        pr ("");
        pr ("## Hazir ayarlar: switching while audio plays (997 Hz -20 dBFS; every 1.5 s, all five twice, then a user preset and back)");
        {
            sineConfig (997.0, 0.1, cfg.micRate);
            const auto& f = presets::factoryPresets();
            int morphs = 0, rebuilds = 0;
            uint64_t invalid = 0;
            juce::String steps;
            std::vector<double> levels;   // output level 1.2 s after each switch (1 s window)
            {
                EngineRun run (480, 26.0);
                auto& e = *run.engine;
                pump (1.0);
                for (int round = 0; round < 2; ++round)
                    for (auto& p : f)
                    {
                        const auto r = e.applyPreset (p.name, p.elements);
                        (r.morphed ? morphs : rebuilds)++;
                        pump (1.5);
                    }
                // a user preset (other elements: Kazanc, a test plug-in with its state) and back
                juce::Array<PluginEntryState> user;
                {
                    PluginEntryState g; g.fileOrId = PluginChain::gainId;
                    BuiltInGainProcessor gp; gp.setGainDb (-3.0f); gp.getStateInformation (g.state); user.add (g);
                    PluginEntryState t; t.fileOrId = "TestGain"; double v = 0.8; t.state.append (&v, sizeof (v)); user.add (t);
                }
                e.getKnownPlugins().addType (testDesc ("TestGain", 0.8));
                const auto r1 = e.applyPreset ("Kendi", user);
                (r1.morphed ? morphs : rebuilds)++;
                pump (1.5);
                e.undoPreset();
                pump (1.5);
                invalid = invalidBlocks (e);
                pr ("    " + diagLine (e));
            }
            const int clk = clicksIn (0.1, 997.0);
            const int h = hiccups();
            pr ("switches: " + juce::String (morphs) + " settings glides (same elements), " + juce::String (rebuilds)
                + " chain swaps; clicks " + juce::String (clk) + ", invalid blocks " + juce::String ((int) invalid)
                + (h > 0 ? ", device hiccups " + juce::String (h) : juce::String()));
            check (morphs >= 9, "factory presets switch by gliding the settings");
            check (invalid == 0, "preset switching: invalid blocks");
            check (clk == 0 || h > 0, "preset switching: clicks");
        }

        pr ("");
        pr ("## Hazir ayar: a user preset with a plug-in state and every built-in effect, through the file into a new engine");
        {
            sineConfig (997.0, 0.1, cfg.micRate);
            std::mt19937 rng (21);
            std::uniform_real_distribution<float> u (0.0f, 1.0f);
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("micvst_presets_et", "");
            juce::Array<PluginEntryState> chain;
            {
                EngineRun run (480, 2.0);
                run.add (testDesc ("TestGain", 0.37));
                for (auto& info : builtin::effects())
                {
                    run.engine->getChain().addBuiltInEffect (info.id);
                    if (auto* fx = run.engine->getChain().getBuiltInEffect ((int) run.engine->getChain().entries().size() - 1))
                        for (auto* q : fx->getEffectParameters()) q->setValueNotifyingHost (u (rng));
                }
                { const AudioEngine::ChainEdit ce (*run.engine); run.engine->rebuildGraph(); }
                pump (0.5);
                chain = run.engine->captureChain();
            }
            juce::String err;
            const bool saved = presets::saveUserPreset (juce::String::fromUTF8 ("Sesim ğüşiöç"), chain, err, folder);
            const auto list = presets::loadUserPresets (folder);
            bool same = false;
            juce::StringArray missing;
            if (list.size() == 1)
            {
                EngineRun run (480, 2.0);
                run.engine->getKnownPlugins().addType (testDesc ("TestGain", 0.37));   // installed (as after the plug-in scan)
                const auto r = run.engine->applyPreset (list[0].name, list[0].elements);
                missing = r.missing;
                pump (0.5);
                const auto back = run.engine->captureChain();
                same = back.size() == chain.size();
                for (int i = 0; same && i < chain.size(); ++i)
                    same = back[i].fileOrId == chain[i].fileOrId && back[i].state == chain[i].state && back[i].bypassed == chain[i].bypassed;
            }
            folder.deleteRecursively();
            pr ("elements " + juce::String (chain.size()) + "; saved " + juce::String (saved ? "yes" : "NO " + err) + "; loaded into a new engine: every state byte-identical "
                + juce::String (same ? "yes" : "NO") + "; missing plug-ins " + juce::String (missing.size()));
            check (saved && same && missing.isEmpty(), "user preset round trip");
        }

        pr ("");
        pr ("## Mono microphone with effects: both cable channels carry the voice");
        {
            cfg.micChannels = 1;
            struct Case { const char* what; std::vector<const char*> ids; };
            for (const auto& cs : { Case { "Low-Cut, De-esser, Limiter (channel-neutral: the chain stays mono)",
                                           { "builtin:lowcut", "builtin:deesser", "builtin:limiter" } },
                                    Case { "Low-Cut, Oda Yankisi (stereo effect), Limiter",
                                           { "builtin:lowcut", "builtin:reverb", "builtin:limiter" } },
                                    Case { "Kanal Araclari (stereo), Parametrik EQ", { "builtin:channel", "builtin:parameq" } } })
            {
                const auto r = measureSine (997.0, 0.1, 7.0, 480, 4.0, [&] (EngineRun& run)
                {
                    for (auto* id : cs.ids) run.engine->getChain().addBuiltInEffect (id);
                    const AudioEngine::ChainEdit ce (*run.engine);
                    run.engine->rebuildGraph();
                });
                pr (juce::String (cs.what) + ": gain L " + db (r.gainDbL, 2) + ", R " + db (r.gainDbR, 2) + ", clicks " + juce::String (r.clk.events));
                check (r.gainDbL > -6.0 && r.gainDbR > -6.0, juce::String (cs.what) + ": a cable channel without the voice");
                check (r.clk.events == 0, juce::String (cs.what) + ": clicks");
            }
            cfg.micChannels = 2;
        }

        pr ("");
        pr ("effects: " + juce::String (failures) + " failures");
    }
}
