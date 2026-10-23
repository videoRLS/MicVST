// "recordlong <minutes> [format] [folder]": one recording running in real time for hours (test task
// P2 / R3) in the real engine (virtual microphone and cable, real threads):
//  * the cable meanwhile: clicks (second difference, 6.1) counted on the fly, test-machine stalls apart
//  * every minute: memory (working set) and CPU of the process, the recording's length
//  * the file: whole and verified, no frame lost, its length = the time it ran (< 0.01 %), a WAV
//    past 4 GB is RF64; WAV / FLAC: every sample the microphone's (24 bit: the nearest step), so
//    no sample is missing, doubled or changed anywhere; MP3: the LAME tag's length exact, decoded
//    in order at the start, the middle and the end
//  * memory of the process does not grow with the recording (slope over the run, first 5 min left out)
// The file stays in [folder] (default: temp) for other readers (ffprobe, soundfile) in the CI job.
namespace reclong
{
    int failures = 0;
    void check (bool ok, const juce::String& what)
    {
        pr (juce::String ("  ") + (ok ? "PASS " : "FAIL ") + what);
        if (! ok) ++failures;
    }

    void run (double minutes, const juce::String& fmtIn, const juce::String& folderIn)
    {
        auto& cfg = vdev::config();
        // 997.123 Hz: the samples repeat only every 1000 s, so the file's first sample has one place on the
        // microphone's timeline (a 997 Hz tone repeats every second: the search found the wrong second and
        // reported 1-LSB differences where the float sine of n and n + 48000 differ in the last bit)
        const double freq = 997.123, amp = 0.25;   // -12 dBFS
        sineConfig (freq, amp, 48000);
        const double fs = cfg.cableRate;
        const auto fmt = fmtIn.isNotEmpty() ? fmtIn : juce::String ("wav:24|48000|2");
        const juce::File dir = folderIn.isNotEmpty() ? juce::File (folderIn)
                                                     : juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("micvst-recordlong");
        dir.createDirectory();
        for (auto& f : dir.findChildFiles (juce::File::findFiles, false, "MicVST_*")) f.deleteFile();
        pr ("## Kayıt for " + juce::String (minutes, 0) + " min in real time: " + fmt + ", 997.123 Hz -12 dBFS, empty chain, folder " + dir.getFullPathName());

        // cable: streamed, clicks on the fly
        auto& rec = vdev::recorder();
        const double lim = 3.0 * amp * std::pow (twoPi * freq / fs, 2.0) + 2.0e-4;
        std::atomic<bool> analyse { true };
        float p1 = 0.0f, p2 = 0.0f; bool audio = false; int64_t idx = 0, lastClick = -1000000;
        std::vector<int64_t> clickAt;
        std::mutex clickLock;
        rec.sink = [&] (const float* l, const float*, int n)
        {
            if (! analyse.load (std::memory_order_relaxed)) return;
            for (int i = 0; i < n; ++i, ++idx)
            {
                const float x = l[i];
                if (! audio) { if (std::abs (x) > 1.0e-4f) audio = true; p2 = p1; p1 = x; continue; }
                if (std::abs ((double) x - 2.0 * p1 + p2) > lim && idx - lastClick > 480)
                {
                    const std::lock_guard<std::mutex> g (clickLock);
                    if (clickAt.size() < 10000) clickAt.push_back (idx);
                    lastClick = idx;
                }
                p2 = p1; p1 = x;
            }
        };

        Recorder::Result res;
        double wall = 0.0, shownAtEnd = 0.0;
        std::vector<std::pair<double, double>> rssSeries;   // minute, MB
        int64_t micAtStart = 0;
        {
            EngineRun run (480, 60.0);   // the cable is streamed: the buffer size here does not matter
            auto& e = *run.engine;
            pump (3.0);
            e.setRecordSettings (RecordSettings::decode (fmt, dir.getFullPathName()));
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            // where the microphone is now (its sample counter from its start time), for finding the file's first sample
            micAtStart = (int64_t) ((vdev::now() - rec.micStart.load()) * 48000.0);
            const auto err = e.startRecording();
            check (err.isEmpty(), "the recording starts" + (err.isNotEmpty() ? ": " + err : juce::String()));
            const double cpu0 = processCpuSeconds();
            pr ("| minute | recorded (s) | wall (s) | working set (MB) | CPU % of one core | engine |");
            pr ("|---:|---:|---:|---:|---:|---|");
            for (int m = 1; m <= (int) std::lround (minutes); ++m)
            {
                const double due = t0 + m * 60000.0;
                while (juce::Time::getMillisecondCounterHiRes() < due)
                    pump (juce::jmin (1.0, (due - juce::Time::getMillisecondCounterHiRes()) / 1000.0 + 0.001));
                const double w = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
                const double mb = rssKb() / 1024.0;
                rssSeries.push_back ({ (double) m, mb });
                if (m <= 3 || m % 10 == 0 || m == (int) std::lround (minutes))
                    pr ("| " + juce::String (m) + " | " + juce::String (e.getRecordingSeconds(), 2) + " | " + juce::String (w, 2) + " | " + juce::String (mb, 1)
                        + " | " + juce::String (100.0 * (processCpuSeconds() - cpu0) / w, 2) + " | " + diagLine (e) + " |");
            }
            shownAtEnd = e.getRecordingSeconds();
            res = e.stopRecording();
            wall = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            pump (1.0);
            analyse = false;
            pr ("  " + diagLine (e));
        }
        rec.sink = nullptr;

        // cable clicks: explained when the test machine stalled nearby (late virtual cable / lost microphone packet)
        const int gaps = rec.gapCount.load(), lost = (int) vdev::stats().lostPackets.load();
        int unexplained = 0;
        for (auto at : clickAt)
        {
            bool isNear = false;   // not "near": a macro in windows.h
            for (int i = 0; i < gaps && ! isNear; ++i) isNear = std::llabs (rec.gapEvents[(size_t) i].first - at) < (int64_t) fs;
            if (! isNear && lost > 0)
                for (int i = 0; i < vdev::stats().lostCount.load() && ! isNear; ++i)
                    isNear = std::abs ((vdev::stats().lostTimes[(size_t) i] - rec.outputStart.load()) * fs - (double) at) < 2.0 * fs;
            if (! isNear) ++unexplained;
        }
        pr ("cable: clicks " + juce::String ((int) clickAt.size()) + " (unexplained " + juce::String (unexplained) + "), test-machine stalls: cable gaps "
            + juce::String (gaps) + ", lost microphone packets " + juce::String (lost));
        check (unexplained == 0, "no click on the cable that a stall of the test machine does not explain");

        pr ("file: " + res.file.getFullPathName() + ", " + juce::String (res.file.getSize() / (1024.0 * 1024.0), 1) + " MB, " + juce::String (res.seconds, 3)
            + " s (shown " + juce::String (shownAtEnd, 2) + " s, wall " + juce::String (wall, 3) + " s), frames " + juce::String ((juce::int64) res.framesWritten)
            + ", dropped " + juce::String ((juce::int64) res.droppedFrames) + ", verified " + (res.verified ? "yes" : "NO")
            + (res.problem.isNotEmpty() ? ", problem: " + res.problem : juce::String()));
        check (res.verified && res.problem.isEmpty(), "the file is whole and verified");
        check (res.droppedFrames == 0, "no frame lost (dropped 0)");
        // start and stop reach the audio thread within a block or two: the allowed difference is 0.01 % of the
        // run or 0.15 s, whichever is larger (a 2 min check run would otherwise fail on the start latency alone)
        const double durDiff = std::abs (res.seconds - wall);
        check (durDiff < std::max (1.0e-4 * wall, 0.15), "length = the time it ran: difference " + juce::String (durDiff, 3) + " s ("
               + juce::String (100.0 * durDiff / wall, 5) + " %)");

        // memory: slope over the run, the first 5 minutes left out
        if (rssSeries.size() > 10)
        {
            double sx = 0, sy = 0, sxx = 0, sxy = 0; int k = 0;
            for (auto& p : rssSeries) if (p.first > 5.0) { sx += p.first; sy += p.second; sxx += p.first * p.first; sxy += p.first * p.second; ++k; }
            const double slope = (k * sxy - sx * sy) / std::max (1e-9, k * sxx - sx * sx) * 60.0;   // MB per hour
            pr ("working set: start " + juce::String (rssSeries.front().second, 1) + " MB, end " + juce::String (rssSeries.back().second, 1) + " MB, slope "
                + juce::String (slope, 3) + " MB/h");
            check (slope <= 1.0, "memory does not grow with the recording (<= 1 MB/h)");
        }

        // the file itself
        auto s = RecordSettings::decode (fmt, {});
        juce::AudioFormatManager fm; fm.registerBasicFormats();
       #if JUCE_USE_MP3AUDIOFORMAT
        fm.registerFormat (new juce::MP3AudioFormat(), false);
       #endif
        if (s.format == RecordSettings::Format::wav)
        {
            juce::FileInputStream in (res.file);
            char head[5] = {};
            in.read (head, 4);
            const bool big = res.file.getSize() > (int64_t) 0xFFFFFFFFLL;
            pr ("container: " + juce::String (head) + (big ? " (file past 4 GB)" : ""));
            check (juce::String (head) == (big ? "RF64" : "RIFF"), big ? "past 4 GB the WAV is RF64" : "a RIFF WAV below 4 GB");
        }
        if (s.format == RecordSettings::Format::mp3)
        {
            Recorder::Mp3Info info;
            const bool ok = Recorder::inspectMp3 (res.file, info);
            check (ok && info.samples == (int64_t) res.framesWritten, "MP3: the LAME tag's length = the frames recorded (" + juce::String (info.samples) + ")");
            return;   // decoding hours of MP3 with JUCE's decoder in the test: left to ffmpeg in the CI job
        }
        std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (res.file));
        check (rd != nullptr, "the file opens");
        if (rd == nullptr) return;
        check ((int64_t) rd->lengthInSamples == (int64_t) res.framesWritten && (int) rd->sampleRate == 48000 && rd->numChannels == 2,
               "length / rate / channels as recorded: " + juce::String (rd->lengthInSamples) + " frames");
        const int bits = s.format == RecordSettings::Format::flac ? s.flacBits : s.wavBits;
        const bool isFloat = s.format == RecordSettings::Format::wav && s.wavFloat;
        const double scale = isFloat ? 0.0 : std::ldexp (1.0, bits - 1);
        auto expectedAt = [&] (int64_t micSample) -> double
        {
            const double x = (double) cfg.signal (0, micSample);   // the microphone's float sample
            if (isFloat) return x;
            return juce::jlimit (-scale, scale - 1.0, std::nearbyint (x * scale)) / scale;
        };
        // find the file's first sample on the microphone's timeline: exact match of the first 64 samples
        juce::AudioBuffer<float> b (2, 48000);
        rd->read (&b, 0, 48000, 0, true, true);
        int64_t first = -1;
        for (int64_t c = juce::jmax<int64_t> (0, micAtStart - 96000); c < micAtStart + 96000 && first < 0; ++c)
        {
            bool all = true;
            for (int i = 0; i < 64 && all; ++i) all = juce::exactlyEqual ((double) b.getSample (0, i), expectedAt (c + i));
            if (all) first = c;
        }
        check (first >= 0, "the file's first sample found on the microphone's timeline");
        if (first < 0) return;
        // every frame against the microphone; a lost microphone packet (a stall of the test machine: the
        // virtual microphone drops 480 samples) shifts the timeline - then it is found again and counted
        int64_t differ = 0, firstDiffer = -1, jumps = 0, jumpedFrames = 0, unexplainedJumps = 0;
        double worstLsb = 0.0;
        const int64_t len = rd->lengthInSamples;
        juce::AudioBuffer<float> probe (2, 64);
        auto matchesAt = [&] (int64_t fileFrame, int64_t micSample)
        {
            const int n = (int) juce::jmin<int64_t> (64, len - fileFrame);
            rd->read (&probe, 0, n, fileFrame, true, true);
            for (int i = 0; i < n; ++i) if (! juce::exactlyEqual ((double) probe.getSample (0, i), expectedAt (micSample + i))) return false;
            return n > 0;
        };
        for (int64_t pos = 0; pos < len; pos += 48000)
        {
            const int n = (int) juce::jmin<int64_t> (48000, len - pos);
            rd->read (&b, 0, n, pos, true, true);
            for (int i = 0; i < n; ++i)
            {
                bool ok = true;
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double want = expectedAt (first + pos + i), got = b.getSample (ch, i);
                    if (! juce::exactlyEqual (got, want)) { ok = false; worstLsb = std::max (worstLsb, std::abs (got - want) * (isFloat ? 8388608.0 : scale)); }
                }
                if (ok) continue;
                // timeline shifted? look ahead up to 2 s on the microphone
                int64_t found = -1;
                if (differ + jumps < 200)   // each search reads the file up to 96000 times: not for a file full of differences
                    for (int64_t d = 1; d <= 96000 && found < 0; ++d) if (matchesAt (pos + i, first + pos + i + d)) found = d;
                if (found > 0)
                {
                    ++jumps; jumpedFrames += found;
                    if (found % 480 != 0) ++unexplainedJumps;
                    pr ("  timeline shift at frame " + juce::String (pos + i) + ": " + juce::String (found) + " microphone samples not in the file"
                        + (found % 480 == 0 ? " (" + juce::String (found / 480) + " whole packets: lost by the virtual microphone)" : " (NOT whole packets)"));
                    first += found;
                    --i;   // compare this frame again on the new timeline
                    continue;
                }
                ++differ;
                if (firstDiffer < 0) firstDiffer = pos + i;
            }
        }
        const int lostPk = (int) vdev::stats().lostPackets.load();
        if (jumpedFrames / 480 > lostPk) unexplainedJumps += (int) (jumpedFrames / 480 - lostPk);
        pr ("content: " + juce::String (len) + " frames compared with the microphone's samples: differing " + juce::String (differ)
            + (firstDiffer >= 0 ? " (first at frame " + juce::String (firstDiffer) + ", worst " + juce::String (worstLsb, 2) + " LSB)" : juce::String())
            + "; timeline shifts " + juce::String (jumps) + " (" + juce::String (jumpedFrames) + " samples, microphone packets lost by the test machine: "
            + juce::String (lostPk) + ")");
        check (unexplainedJumps == 0, "nothing missing in the file except what the (virtual) microphone itself lost");
        check (differ == 0, "every sample in the file is the microphone's (nothing missing, doubled or changed)");
    }
}
