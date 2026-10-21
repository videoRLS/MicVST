// "record": Kayıt in the real engine (virtual microphone and cable, tests/enginetest/VirtualDevices.h).
//  * what the cable gets with and without a recording: clicks, THD+N, underruns, latency - the same
//  * the file: exactly the processed audio (32-bit float file: the sine at its level, no clicks,
//    no gaps, as long as the recording ran)
//  * start / stop many times while audio runs, every format: every file whole and verified
//  * CPU and memory of the process: recording off vs on, and after 30 recordings
namespace rectest
{
    int failures = 0;
    void check (bool ok, const juce::String& what)
    {
        pr (juce::String ("  ") + (ok ? "PASS " : "FAIL ") + what);
        if (! ok) ++failures;
    }
    int hiccups() { return vdev::recorder().deviceGaps.load() + (int) vdev::stats().lostPackets.load(); }

    struct CableStats { Discontinuities clk; double thdn = 0.0, gainDb = 0.0; };
    CableStats cable (int64_t from, int64_t to, double amp, double freq)
    {
        CableStats c;
        const auto& L = vdev::recorder().left;
        const double fs = vdev::config().cableRate;
        c.clk = clicks (L, (size_t) from, (size_t) to, amp, freq / fs);
        std::vector<double> t, g;
        const int win = 8192;
        for (int64_t s0 = from; s0 + win < to; s0 += win * 2)
        {
            const auto f = fitSine (L, (size_t) s0, win, freq / fs);
            t.push_back (f.residualDb);
            g.push_back (juce::Decibels::gainToDecibels (f.amp, -200.0));
        }
        std::sort (t.begin(), t.end()); std::sort (g.begin(), g.end());
        if (! t.empty()) { c.thdn = t[t.size() / 2]; c.gainDb = g[g.size() / 2]; }
        return c;
    }

    void run()
    {
        auto& cfg = vdev::config();
        const double freq = 997.0, amp = 0.1;           // -20 dBFS, chain Kazanc -6 dB -> -26 dBFS
        const double outAmp = amp * 0.5011872336;
        sineConfig (freq, amp, cfg.micRate);
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("micvst-enginetest-record");
        dir.deleteRecursively();
        dir.createDirectory();

        pr ("## Kayıt (recording) in the engine: 997 Hz -20 dBFS, chain Kazanc -6 dB, virtual microphone and cable");
        {
            EngineRun run (480, 150.0);
            auto& e = *run.engine;
            e.getChain().addGain (-6.0f);
            { const AudioEngine::ChainEdit ce (e); e.rebuildGraph(); }
            pump (3.0);
            const auto diag0 = e.getDiagnostics();

            // A: no recording
            const int hiccups0 = hiccups();
            const int64_t a0 = vdev::recorder().available.load();
            const double cpuA0 = processCpuSeconds(), wallA0 = juce::Time::getMillisecondCounterHiRes();
            const long rssA = rssKb();
            pump (12.0);
            const double cpuA = (processCpuSeconds() - cpuA0) / ((juce::Time::getMillisecondCounterHiRes() - wallA0) / 1000.0);
            const int64_t a1 = vdev::recorder().available.load();
            const double latencyA = e.getDiagnostics().latencyMs;

            // B: recording, WAV 32 bit float (the file must be the processed audio itself)
            e.setRecordSettings (RecordSettings::decode ("wav:32f|48000|2", dir.getFullPathName()));
            const int64_t b0 = vdev::recorder().available.load();
            const double cpuB0 = processCpuSeconds(), wallB0 = juce::Time::getMillisecondCounterHiRes();
            const auto startErr = e.startRecording();
            check (startErr.isEmpty(), "recording starts" + (startErr.isNotEmpty() ? ": " + startErr : juce::String()));
            check (e.isRecording(), "the engine says it is recording");
            pump (12.0);
            const double latencyB = e.getDiagnostics().latencyMs;
            const double shownSeconds = e.getRecordingSeconds();
            const auto res = e.stopRecording();
            const double wallB = (juce::Time::getMillisecondCounterHiRes() - wallB0) / 1000.0;
            const double cpuB = (processCpuSeconds() - cpuB0) / wallB;
            const int64_t b1 = vdev::recorder().available.load();
            const long rssB = rssKb();
            const int hiccupsAB = hiccups() - hiccups0;   // stalls of the test machine itself (2 cores here)

            pr ("    " + diagLine (e));
            const auto ca = cable (a0 + 4800, a1, outAmp, freq), cb = cable (b0, b1 + 4800, outAmp, freq);
            pr ("cable without recording: clicks " + juce::String (ca.clk.events) + ", THD+N " + db (ca.thdn) + ", level " + db (ca.gainDb, 2)
                + "; with recording (start and stop inside): clicks " + juce::String (cb.clk.events) + ", THD+N " + db (cb.thdn) + ", level " + db (cb.gainDb, 2));
            pr ("process CPU: without " + juce::String (cpuA * 100.0, 2) + " %, with recording " + juce::String (cpuB * 100.0, 2)
                + " % of one core; memory " + juce::String (rssA / 1024) + " -> " + juce::String (rssB / 1024) + " MB; shown latency "
                + juce::String (latencyA, 1) + " / " + juce::String (latencyB, 1) + " ms");
            pr ("file: " + res.file.getFileName() + ", " + juce::String (res.seconds, 3) + " s (shown while running " + juce::String (shownSeconds, 2)
                + " s, wall " + juce::String (wallB, 2) + " s), verified " + (res.verified ? "yes" : "NO") + ", dropped " + juce::String (res.droppedFrames)
                + (res.problem.isNotEmpty() ? ", problem: " + res.problem : juce::String()));

            const auto diag1 = e.getDiagnostics();
            if (hiccupsAB == 0)
            {
                check (ca.clk.events == 0 && cb.clk.events == 0, "the cable has no clicks, with or without a recording (start and stop included)");
                // worse by less than 3 dB (the fit's own spread between two 12 s windows); better is fine
                check (cb.thdn - ca.thdn < 3.0 && std::abs (cb.gainDb - ca.gainDb) < 0.01, "the cable's quality and level do not get worse while recording");
                check (diag1.bridge.underruns == diag0.bridge.underruns && diag1.bridge.droppedBlocks == diag0.bridge.droppedBlocks,
                       "no underruns or dropped blocks on the cable while recording");
                check (std::abs (latencyB - latencyA) < 1.0, "the latency does not change while recording");
            }
            else
                pr ("  SKIP cable comparison: the test machine stalled " + juce::String (hiccupsAB) + " times in this part (device gaps / lost packets of the "
                    "virtual devices), which makes its own clicks; cable clicks " + juce::String (ca.clk.events) + " / " + juce::String (cb.clk.events));
            check (res.verified && res.droppedFrames == 0 && res.problem.isEmpty(), "the recording is whole and verified");
            check (std::abs (res.seconds - wallB) < 0.5, "the file is as long as the recording ran");

            // the file itself: the processed sine, exactly
            {
                juce::AudioFormatManager fm; fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (res.file));
                check (reader != nullptr && reader->usesFloatingPointData && reader->bitsPerSample == 32 && reader->sampleRate == 48000.0 && reader->numChannels == 2,
                       "WAV 32-bit float, 48000 Hz, 2 channels");
                if (reader != nullptr)
                {
                    const int n = (int) reader->lengthInSamples;
                    juce::AudioBuffer<float> buf (2, n);
                    reader->read (&buf, 0, n, 0, true, true);
                    std::vector<float> l (buf.getReadPointer (0), buf.getReadPointer (0) + n), r (buf.getReadPointer (1), buf.getReadPointer (1) + n);
                    const size_t start = firstAudio (l);
                    const auto clk = clicks (l, start, l.size(), outAmp, freq / 48000.0);
                    const auto gaps = silences (l, start + 10, 48000.0);
                    std::vector<double> thd, lvl;
                    for (size_t s0 = start + 4800; s0 + 8192 < l.size(); s0 += 8192 * 2)
                    {
                        const auto f = fit3 (l.data() + s0, 8192, freq / 48000.0);   // no resampler before the file: the exact frequency
                        thd.push_back (f.residualDb); lvl.push_back (juce::Decibels::gainToDecibels (f.amp, -200.0));
                    }
                    std::sort (thd.begin(), thd.end()); std::sort (lvl.begin(), lvl.end());
                    const double fileThd = thd.empty() ? 0.0 : thd.back(), fileLvl = lvl.empty() ? 0.0 : lvl[lvl.size() / 2];
                    bool same = true;
                    for (int i = 0; i < n; ++i) same = same && l[(size_t) i] == r[(size_t) i];
                    pr ("file content: level " + db (fileLvl, 3) + " (expected -26.000), worst THD+N " + db (fileThd) + ", clicks " + juce::String (clk.events)
                        + ", gaps " + juce::String (gaps.count) + ", L = R " + (same ? "yes" : "no"));
                    check (std::abs (fileLvl - (-26.0)) < 0.01, "the file has the processed level (-26 dBFS)");
                    check (fileThd < -120.0, "the file is clean (THD+N < -120 dB: float, no conversion)");
                    // the file is a copy of what the cable gets: a stall of the test machine (the virtual
                    // microphone losing packets) is in both - then it is reported, not counted
                    if (hiccupsAB == 0) check (clk.events == 0 && gaps.count == 0, "no clicks or gaps in the file");
                    else pr ("  SKIP file clicks/gaps (" + juce::String (clk.events) + " / " + juce::String (gaps.count) + "): the test machine stalled "
                             + juce::String (hiccupsAB) + " times while recording; the cable got the same");
                    check (same, "both channels as the cable gets them (mono microphone: L = R)");
                }
            }

            // C: start / stop 30 times while audio runs, all formats
            const juce::StringArray cfgs { "wav:16|48000|2", "wav:24|48000|2", "wav:32|48000|2", "wav:32f|48000|1", "flac:16:5|48000|2",
                                           "flac:24:8|44100|2", "mp3:cbr320|48000|2", "mp3:cbr128|44100|1", "mp3:v0|48000|2", "mp3:v4|48000|2" };
            const int64_t c0 = vdev::recorder().available.load();
            const int hiccupsC0 = hiccups();
            int bad = 0;
            juce::StringArray badList;
            std::vector<int64_t> marks;   // cable positions of every start and stop
            for (int i = 0; i < 30; ++i)
            {
                e.setRecordSettings (RecordSettings::decode (cfgs[i % cfgs.size()], dir.getFullPathName()));
                marks.push_back (vdev::recorder().available.load());
                const auto err = e.startRecording();
                pump (0.25 + 0.05 * (i % 4));
                marks.push_back (vdev::recorder().available.load());
                const auto r = e.stopRecording();
                if (err.isNotEmpty() || ! r.verified || r.droppedFrames > 0 || r.problem.isNotEmpty())
                {
                    ++bad;
                    badList.add (cfgs[i % cfgs.size()] + " " + err + r.problem);
                }
                pump (0.05);
            }
            const int64_t c1 = vdev::recorder().available.load();
            const auto cc = cable (c0, c1, outAmp, freq);
            const int hiccupsC = hiccups() - hiccupsC0;
            {
                // where the clicks are: next to a start / stop (within 100 ms after the call), or elsewhere
                std::vector<size_t> where;
                clicks (vdev::recorder().left, (size_t) c0, (size_t) c1, outAmp, freq / vdev::config().cableRate, 480, &where);
                juce::String w;
                for (auto p : where)
                {
                    int64_t nearest = INT64_MAX; int idx = -1;
                    for (size_t k = 0; k < marks.size(); ++k)
                        if (std::llabs ((int64_t) p - marks[k]) < std::llabs (nearest)) { nearest = (int64_t) p - marks[k]; idx = (int) k; }
                    w << juce::String ((double) p / vdev::config().cableRate, 3) << " s (" << juce::String ((double) nearest / 48.0, 1) << " ms from "
                      << ((idx % 2) == 0 ? "start" : "stop") << " " << idx / 2 << ") ";
                }
                if (w.isNotEmpty()) pr ("    cable clicks at: " + w + "; machine hiccups in this part: " + juce::String (hiccupsC));
            }
            const long rssC = rssKb();
            pr ("30 recordings started and stopped during audio (" + cfgs.joinIntoString (", ") + "): " + juce::String (30 - bad) + " verified; cable clicks "
                + juce::String (cc.clk.events) + "; memory " + juce::String (rssB / 1024) + " -> " + juce::String (rssC / 1024) + " MB"
                + (badList.isEmpty() ? juce::String() : "; failed: " + badList.joinIntoString ("; ")));
            check (bad == 0, "30 start / stop cycles in every format: every file whole and verified");
            // a stall of the test machine itself (device gaps / lost packets of the virtual devices) is
            // its own click; then the clicks are reported, not counted
            check (cc.clk.events == 0 || (hiccupsC > 0 && cc.clk.events <= 2 * hiccupsC), "no clicks on the cable from starting and stopping"
                   + (cc.clk.events > 0 ? juce::String (" (") + juce::String (cc.clk.events) + " clicks with " + juce::String (hiccupsC) + " machine hiccups)" : juce::String()));
            check (rssC - rssB < 30 * 1024, "memory stays (no leak per recording)");
            check (dir.findChildFiles (juce::File::findFiles, false).size() == 31, "31 files, none overwritten");

            // D: the CPU cost of the heaviest options, while the engine runs
            for (auto heavy : { "mp3:cbr320|44100|2", "flac:24:8|44100|2" })
            {
                e.setRecordSettings (RecordSettings::decode (heavy, dir.getFullPathName()));
                const double c0s = processCpuSeconds(), w0 = juce::Time::getMillisecondCounterHiRes();
                e.startRecording();
                pump (8.0);
                const auto r = e.stopRecording();
                const double cpu = (processCpuSeconds() - c0s) / ((juce::Time::getMillisecondCounterHiRes() - w0) / 1000.0);
                pr (juce::String (heavy) + " (converted from 48 kHz while recording): process CPU " + juce::String (cpu * 100.0, 2) + " % of one core (without recording "
                    + juce::String (cpuA * 100.0, 2) + " %), verified " + (r.verified ? "yes" : "NO"));
                check (r.verified && r.droppedFrames == 0, juce::String (heavy) + ": verified, nothing dropped");
            }
            // the settings go into config.xml with the rest of the state and come back the same
            {
                const auto chosen = RecordSettings::decode ("mp3:v2|44100|1", dir.getChildFile (juce::String::fromUTF8 ("Kayıtlarım")).getFullPathName());
                e.setRecordSettings (chosen);
                const auto st = fromValueTree (juce::ValueTree::fromXml (*juce::parseXML (toValueTree (e.captureState()).createXml()->toString())));
                const auto back = RecordSettings::decode (st.recordFormat, st.recordFolder);
                pr ("config.xml: recordFormat=\"" + st.recordFormat + "\" recordFolder=\"" + st.recordFolder + "\"");
                check (back == chosen, "Kayıt settings saved with the state and read back the same (restart)");
            }
            pr ("    " + diagLine (e) + (hiccups() > 0 ? ", machine hiccups " + juce::String (hiccups()) : juce::String()));
        }
        dir.deleteRecursively();
        pr ("");
        pr (failures == 0 ? "record: all checks passed" : "record: " + juce::String (failures) + " failures");
    }
}
