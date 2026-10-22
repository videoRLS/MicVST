#include <juce_audio_formats/juce_audio_formats.h>
#include "audio/Recorder.h"
#include <cmath>
#include <map>
#include <vector>

// K22 - every combination the recording settings offer, each recorded for 10 s and read back:
// WAV 16 / 24 / 32 / 32 float, FLAC 16 / 24 with compression 0-8, MP3 CBR 128-320 and VBR V0/V2/V4,
// every rate the panel offers for the processing rate (44.1 / 48, and 88.2-192 kHz when the
// microphone runs that fast), stereo and mono - for processing rates 44.1, 48, 96 and 192 kHz.
// Per file: header (format, bits, rate, channels, length) as declared; WAV/FLAC content to the
// LSB (24 bit: every sample exactly the rounded source value) or, when converted, signal/error
// > 130 dB in time; 16 bit TPDF dither statistics; MP3 length exact (gapless, LAME tag), CBR bit
// rate, decoded in order. Plus: settings the panel does not offer fall back to safe values.
// Long (about 760 files): runs only with MICVST_TEST_K22=1 (CI step "K22"), a table per file
// goes to MICVST_TEST_K22_REPORT when set.
struct RecorderSweepTest : juce::UnitTest
{
    RecorderSweepTest() : juce::UnitTest ("Recorder K22") {}

    static double sig (int ch, int64_t n, double rate)
    {
        const double t = (double) n / rate, w = 2.0 * juce::MathConstants<double>::pi;
        return ch == 0 ? 0.4 * std::sin (w * 997.0 * t) + 0.1 * std::sin (w * 1500.0 * t + 30.0 * std::sin (w * 0.7 * t))
                       : 0.25 * std::sin (w * 440.0 * t) + 0.1 * std::sin (w * 2500.0 * t + 20.0 * std::sin (w * 1.3 * t));
    }
    // what the file should hold at sample n (mono = (L+R)/2, the recorder's mix)
    static double expected (int ch, int64_t n, double rate, int channels)
    {
        return channels == 1 ? 0.5 * (sig (0, n, rate) + sig (1, n, rate)) : sig (ch, n, rate);
    }

    // the float sample the recorder gets for file channel 'ch' (mono: its own float mix 0.5f * (L + R))
    static float refF (int ch, int64_t n, double rate, int channels)
    {
        const float l = (float) sig (0, n, rate), r = (float) sig (1, n, rate);
        return channels == 1 ? 0.5f * (l + r) : (ch == 0 ? l : r);
    }

    static int64_t feed (Recorder& rec, double rate, double seconds)
    {
        const int block = (int) std::lround (rate / 100.0);
        const int64_t total = (int64_t) std::llround (seconds * rate);
        std::vector<float> l ((size_t) block), r ((size_t) block);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        const double speed = 60.0;
        for (int64_t pos = 0; pos < total; pos += block)
        {
            const int n = (int) juce::jmin<int64_t> (block, total - pos);
            for (int i = 0; i < n; ++i) { l[(size_t) i] = (float) sig (0, pos + i, rate); r[(size_t) i] = (float) sig (1, pos + i, rate); }
            const float* ch[2] = { l.data(), r.data() };
            rec.push (ch, 2, n);
            const double due = (double) (pos + n) / rate * 1000.0 / speed;
            while (juce::Time::getMillisecondCounterHiRes() - t0 < due) juce::Thread::sleep (1);
        }
        return total;
    }

    void runTest() override
    {
        beginTest ("settings the panel does not offer fall back to safe values");
        {
            struct Case { const char* text; const char* expect; };
            for (auto c : { Case { "mp3:cbr320|96000|2", "mp3:cbr320|48000|2" },   // MP3: 44.1 / 48 kHz only
                            Case { "mp3:cbr320|192000|1", "mp3:cbr320|48000|1" },
                            Case { "mp3:cbr100|48000|2", "wav:24|48000|2" },       // no such bit rate: the default format
                            Case { "mp3:v1|48000|2", "wav:24|48000|2" },
                            Case { "wav:20|48000|2", "wav:24|48000|2" },
                            Case { "wav:24|22050|2", "wav:24|48000|2" },           // rate not offered
                            Case { "wav:24|48000|6", "wav:24|48000|2" },           // channels 1 or 2
                            Case { "flac:16:12|48000|2", "flac:16:5|48000|2" },    // level 0-8
                            Case { "flac:20:5|48000|2", "wav:24|48000|2" },
                            Case { "ogg:5|48000|2", "wav:24|48000|2" },
                            Case { "", "wav:24|48000|2" },
                            Case { "|||", "wav:24|48000|2" } })
                expectEquals (RecordSettings::decode (c.text, {}).encode(), juce::String (c.expect), c.text);
            // a rate above the microphone's is never up-sampled: the file gets the microphone's rate
            auto s = RecordSettings::decode ("wav:24|192000|2", {});
            expectEquals (s.effectiveRate (48000.0), 48000);
            expectEquals (s.effectiveRate (96000.0), 96000);
            expect (! RecordSettings::allowedRates (RecordSettings::Format::mp3, 192000.0).contains (96000));
        }

        beginTest ("24 / 32 bit: rounded to nearest, ties to even - no bias (K2: |mean| < 0.01 LSB)");
        {
            // A loud signal: above |0.5| a float has only 24 significant bits, so half of the samples
            // sit exactly between two 24 bit steps. Rounding those ties always up adds a DC offset
            // (+0.07 LSB on this signal); ties to even does not.
            auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("micvst-k2-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
            for (auto cfg : { "wav:24|48000|2", "flac:24:5|48000|2", "wav:24|44100|2" })
            {
                dir.deleteRecursively();
                auto s = RecordSettings::decode (cfg, dir.getFullPathName());
                const double rate = s.sampleRate;
                Recorder rec;
                expectEquals (rec.start (s, rate), juce::String(), cfg);
                const int block = (int) std::lround (rate / 100.0);
                const int64_t total = (int64_t) (3.0 * rate);
                std::vector<float> l ((size_t) block), r ((size_t) block);
                for (int64_t pos = 0; pos < total; pos += block)
                {
                    for (int i = 0; i < block; ++i) { l[(size_t) i] = (float) (1.8 * sig (0, pos + i, rate)); r[(size_t) i] = (float) (2.5 * sig (1, pos + i, rate)); }
                    const float* ch[2] = { l.data(), r.data() };
                    rec.push (ch, 2, block);
                    juce::Thread::sleep (1);
                }
                const auto res = rec.stop();
                juce::AudioFormatManager fm; fm.registerBasicFormats();
                std::unique_ptr<juce::AudioFormatReader> rd (fm.createReaderFor (res.file));
                expect (rd != nullptr, cfg);
                if (rd == nullptr) continue;
                juce::AudioBuffer<float> a (2, (int) rd->lengthInSamples);
                rd->read (&a, 0, (int) rd->lengthInSamples, 0, true, true);
                double sum = 0.0, worst = 0.0; int64_t n = 0, notNearestEven = 0, ties = 0;
                for (int c = 0; c < 2; ++c)
                    for (int i = 0; i < a.getNumSamples(); ++i)
                    {
                        const double ref = (double) (float) ((c == 0 ? 1.8 : 2.5) * sig (c, i, rate));
                        const double v = ref * 8388608.0;
                        if (juce::exactlyEqual (v - std::floor (v), 0.5)) ++ties;
                        const double e = (double) a.getSample (c, i) * 8388608.0 - v;
                        sum += e; worst = std::max (worst, std::abs (e)); ++n;
                        if (! juce::exactlyEqual ((double) a.getSample (c, i) * 8388608.0, std::nearbyint (v))) ++notNearestEven;
                    }
                const double mean = sum / (double) n;
                logMessage (juce::String (cfg) + ": mean error " + juce::String (mean, 4) + " LSB, max " + juce::String (worst, 3) + " LSB, exact ties "
                            + juce::String (ties) + " of " + juce::String (n) + ", samples not rounded to nearest/even " + juce::String (notNearestEven));
                expect (std::abs (mean) < 0.01, juce::String (cfg) + ": mean error " + juce::String (mean, 4) + " LSB (biased rounding)");
                expect (worst <= 0.5, cfg);
                expectEquals (notNearestEven, (int64_t) 0, cfg);
            }
            dir.deleteRecursively();
        }

        if (juce::SystemStats::getEnvironmentVariable ("MICVST_TEST_K22", {}).isEmpty())
        {
            beginTest ("full sweep (skipped: set MICVST_TEST_K22=1)");
            logMessage ("K22 full sweep not run in this pass (MICVST_TEST_K22 not set)");
            return;
        }

        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("micvst-k22-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        juce::StringArray table;
        table.add ("| processing Hz | setting | file Hz | ch | samples | header | content | result |");
        table.add ("|---:|---|---:|---:|---:|---|---|---|");
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        fm.registerFormat (new juce::MP3AudioFormat(), false);

        juce::StringArray formats { "wav:16", "wav:24", "wav:32", "wav:32f" };
        for (int b : { 16, 24 }) for (int lv = 0; lv <= 8; ++lv) formats.add ("flac:" + juce::String (b) + ":" + juce::String (lv));
        for (int k : RecordSettings::mp3CbrRates()) formats.add ("mp3:cbr" + juce::String (k));
        for (int v : RecordSettings::mp3VbrLevels()) formats.add ("mp3:v" + juce::String (v));

        int files = 0, failed = 0;
        std::map<juce::String, int> failByKind;
        for (double proc : { 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            beginTest ("processing rate " + juce::String ((int) proc) + " Hz: every format, rate and channel count");
            for (auto& fmt : formats)
            {
                const auto kindProbe = RecordSettings::decode (fmt + "|48000|2", {});
                for (int rate : RecordSettings::allowedRates (kindProbe.format, proc))
                    for (int ch : { 2, 1 })
                    {
                        const juce::String cfg = fmt + "|" + juce::String (rate) + "|" + juce::String (ch);
                        dir.deleteRecursively();
                        auto s = RecordSettings::decode (cfg, dir.getFullPathName());
                        const juce::String label = juce::String ((int) proc) + " " + cfg;
                        if (s.encode() != cfg) { expect (false, label + ": the panel's own setting did not survive config.xml (" + s.encode() + ")"); ++failed; continue; }
                        ++files;
                        juce::StringArray problems;
                        Recorder rec;
                        const auto err = rec.start (s, proc);
                        if (err.isNotEmpty()) { expect (false, label + ": " + err); ++failed; continue; }
                        const auto pushed = feed (rec, proc, 10.0);
                        const auto res = rec.stop();
                        if (! res.verified) problems.add ("not verified: " + res.problem);
                        if (res.droppedFrames != 0) problems.add ("dropped " + juce::String ((juce::int64) res.droppedFrames));
                                                const int out = s.effectiveRate (proc);
                        const int64_t expectLen = (pushed * out + (int64_t) proc - 1) / (int64_t) proc;
                        if ((int64_t) res.framesWritten != expectLen) problems.add ("written " + juce::String ((juce::int64) res.framesWritten) + " of " + juce::String (expectLen));
                        juce::String header = "ok", content = "-";
                        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (res.file));
                        if (reader == nullptr) { problems.add ("unreadable"); header = "unreadable"; }
                        else
                        {
                            const bool isMp3 = s.format == RecordSettings::Format::mp3;
                            const int bits = s.format == RecordSettings::Format::flac ? s.flacBits : s.wavBits;
                            if ((int) reader->sampleRate != out) problems.add ("rate " + juce::String (reader->sampleRate));
                            if ((int) reader->numChannels != ch) problems.add ("channels " + juce::String ((int) reader->numChannels));
                            if (! isMp3)
                            {
                                if ((int) reader->bitsPerSample != bits) problems.add ("bits " + juce::String ((int) reader->bitsPerSample));
                                if (reader->usesFloatingPointData != (s.format == RecordSettings::Format::wav && s.wavFloat)) problems.add ("float flag");
                                if ((int64_t) reader->lengthInSamples != expectLen) problems.add ("length " + juce::String (reader->lengthInSamples) + " != " + juce::String (expectLen));
                            }
                            else
                            {
                                Recorder::Mp3Info info;
                                if (! Recorder::inspectMp3 (res.file, info)) problems.add ("MP3 frames / tag unreadable");
                                else
                                {
                                    if (info.samples != expectLen) problems.add ("MP3 samples " + juce::String (info.samples) + " != " + juce::String (expectLen));
                                    if (info.sampleRate != out || info.channels != ch) problems.add ("MP3 header rate/channels");
                                    if (s.mp3Mode == RecordSettings::Mp3Mode::cbr && (info.kbps != s.mp3Kbps || info.vbr)) problems.add ("CBR " + juce::String (info.kbps) + " kbps");
                                    if (s.mp3Mode == RecordSettings::Mp3Mode::vbr && ! info.vbr) problems.add ("not VBR");
                                }
                            }
                            if (problems.isEmpty()) header = "ok";
                            else header = "BAD";

                            juce::AudioBuffer<float> a ((int) reader->numChannels, (int) reader->lengthInSamples);
                            reader->read (&a, 0, (int) reader->lengthInSamples, 0, true, true);
                            const int n = a.getNumSamples();
                            if (isMp3)
                            {
                                // decoder delay unknown to JUCE's decoder: find it, then signal/error of channel 0
                                double best = 1e30; int off = 0;
                                const int from = juce::jmin (n / 4, (int) (2 * out)), to = juce::jmin (n, from + out / 2);
                                for (int o = 0; o < 4000; ++o)
                                {
                                    double e = 0.0;
                                    for (int i = from; i < to && i + o < n; ++i) { const double d = a.getSample (0, i + o) - expected (0, i, out, ch); e += d * d; }
                                    if (e < best) { best = e; off = o; }
                                }
                                double sp = 0.0, ep = 0.0;
                                for (int i = out / 4; i + off < n - out / 4; ++i) { const double r = expected (0, i, out, ch); const double d = a.getSample (0, i + off) - r; sp += r * r; ep += d * d; }
                                const double snr = 10.0 * std::log10 (sp / std::max (ep, 1e-30));
                                content = "signal/error " + juce::String (snr, 1) + " dB";
                                if (snr < 15.0) problems.add ("MP3 decoded content " + juce::String (snr, 1) + " dB");
                            }
                            else if (out == (int) proc)
                            {
                                const double lsb = std::ldexp (1.0, -((s.format == RecordSettings::Format::wav && s.wavFloat ? 24 : bits) - 1));
                                double worst = 0.0, sum = 0.0, sq = 0.0; int64_t cnt = 0, notExact = 0;
                                for (int c = 0; c < ch; ++c)
                                    for (int i = 0; i < n; ++i)
                                    {
                                        const double ref = (double) refF (c, i, out, ch);
                                        const double e = (a.getSample (c, i) - ref) / lsb;
                                        worst = std::max (worst, std::abs (e)); sum += e; sq += e * e; ++cnt;
                                        // 24 bit: exactly the source rounded to nearest, ties to even
                                        if (bits == 24 && ! (s.format == RecordSettings::Format::wav && s.wavFloat))
                                            if (! juce::exactlyEqual ((double) a.getSample (c, i) * 8388608.0, std::nearbyint (ref * 8388608.0))) ++notExact;
                                    }
                                const double mean = sum / (double) cnt, rms = std::sqrt (sq / (double) cnt - mean * mean);
                                content = "max " + juce::String (worst, 3) + " LSB, mean " + juce::String (mean, 4) + ", rms " + juce::String (rms, 3);
                                if (s.format == RecordSettings::Format::wav && s.wavFloat)
                                {
                                    if (! juce::exactlyEqual (worst, 0.0)) problems.add ("32 float not bit-exact: " + juce::String (worst, 4) + " LSB24");
                                }
                                else if (bits == 16)
                                {
                                    if (worst > 1.5 || std::abs (mean) > 0.02 || rms < 0.45 || rms > 0.55) problems.add ("16 bit dither: " + content);
                                }
                                else
                                {
                                    if (worst > 0.51 || std::abs (mean) > 0.01) problems.add ("rounding: " + content);
                                    if (bits == 24 && notExact > 0) problems.add (juce::String (notExact) + " samples not the rounded source value");
                                    if (bits == 24) content << ", not exact " << juce::String (notExact);
                                }
                            }
                            else
                            {
                                double sp = 0.0, ep = 0.0; int64_t cnt = 0;
                                for (int c = 0; c < ch; ++c)
                                    for (int i = out / 20; i < n - out / 20; ++i)
                                    {
                                        const double r = expected (c, i, out, ch);
                                        const double d = a.getSample (c, i) - r;
                                        sp += r * r; ep += d * d; ++cnt;
                                    }
                                const double snr = 10.0 * std::log10 (sp / std::max (ep, 1e-30));
                                // 16 bit: the file's own dither noise (TPDF + rounding, 0.5 LSB rms) bounds what can
                                // be seen - the converter must stay within 1 dB of that; 24 / 32 bit and float: 130 dB
                                const double ditherFloor = 10.0 * std::log10 ((sp / (double) cnt) / std::pow (0.5 / 32768.0, 2.0));
                                const double need = bits == 16 ? ditherFloor - 1.0 : 130.0;
                                content = "converted, signal/error " + juce::String (snr, 1) + " dB";
                                if (snr < need) problems.add ("conversion " + juce::String (snr, 1) + " dB < " + juce::String (need, 0));
                            }
                        }
                        const bool ok = problems.isEmpty();
                        if (! ok) { ++failed; failByKind[fmt] += problems.size(); }
                        expect (ok, label + ": " + problems.joinIntoString ("; "));
                        table.add ("| " + juce::String ((int) proc) + " | " + cfg + " | " + juce::String (out) + " | " + juce::String (ch) + " | "
                                   + juce::String (expectLen) + " | " + header + " | " + content + " | " + (ok ? "PASS" : "FAIL: " + problems.joinIntoString ("; ")) + " |");
                    }
            }
        }
        dir.deleteRecursively();
        logMessage ("K22: " + juce::String (files) + " files, " + juce::String (failed) + " failed");
        const auto report = juce::SystemStats::getEnvironmentVariable ("MICVST_TEST_K22_REPORT", {});
        if (report.isNotEmpty())
        {
            table.insert (0, "# K22 - every recording setting, 10 s each\n\nfiles " + juce::String (files) + ", failed " + juce::String (failed) + "\n");
            juce::File (report).replaceWithText (table.joinIntoString ("\n") + "\n");
        }
    }
};

static RecorderSweepTest recorderSweepTest;

// K14 - a real small disk (CI: a 260 MB NTFS VHD made with diskpart): the recording runs until the
// disk is nearly full, stops by itself with a message, the file is valid up to the end and exact
// (32 bit float); with less than 100 MB free nothing starts; the disk taken away while recording:
// an error, no crash, no hang. Runs only with MICVST_TEST_SMALLDISK=<folder on that disk>;
// MICVST_TEST_SMALLDISK_DETACH=<command> takes the disk away (diskpart script).
struct RecorderSmallDiskTest : juce::UnitTest
{
    RecorderSmallDiskTest() : juce::UnitTest ("Recorder K14") {}

    void runTest() override
    {
        const auto where = juce::SystemStats::getEnvironmentVariable ("MICVST_TEST_SMALLDISK", {});
        if (where.isEmpty())
        {
            beginTest ("small disk (skipped: set MICVST_TEST_SMALLDISK)");
            logMessage ("K14 small disk not run (MICVST_TEST_SMALLDISK not set)");
            return;
        }
        const juce::File folder (where);
        folder.createDirectory();
        const double rate = 192000.0;
        auto feedUntil = [rate] (Recorder& rec, double maxSeconds, double speed, int64_t& pushed)
        {
            const int block = 1920;
            std::vector<float> l ((size_t) block), r ((size_t) block);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const int64_t total = (int64_t) (maxSeconds * rate);
            for (int64_t pos = 0; pos < total; pos += block)
            {
                if (rec.hasFailed()) return;
                for (int i = 0; i < block; ++i) { l[(size_t) i] = (float) RecorderSweepTest::sig (0, pos + i, rate); r[(size_t) i] = (float) RecorderSweepTest::sig (1, pos + i, rate); }
                const float* ch[2] = { l.data(), r.data() };
                rec.push (ch, 2, block);
                pushed += block;
                const double due = (double) (pos + block) / rate * 1000.0 / speed;
                while (juce::Time::getMillisecondCounterHiRes() - t0 < due) juce::Thread::sleep (1);
            }
        };

        beginTest ("the disk fills up: the recording stops by itself, the file is whole and exact");
        {
            const auto freeMb = folder.getBytesFreeOnVolume() / (1024 * 1024);
            logMessage ("free on the small disk: " + juce::String (freeMb) + " MB");
            expect (freeMb >= 100, "the test disk needs at least 100 MB free");
            Recorder rec;
            expectEquals (rec.start (RecordSettings::decode ("wav:32f|192000|2", folder.getFullPathName()), rate), juce::String());
            int64_t pushed = 0;
            feedUntil (rec, 400.0, 2.0, pushed);   // 1.5 MB/s of audio, 3 MB/s on the disk
            expect (rec.hasFailed(), "the recording should have stopped itself");
            const auto why = rec.getFailure();
            const auto res = rec.stop();
            logMessage ("stopped after " + juce::String ((double) pushed / rate, 1) + " s of audio: '" + why + "' (problem: '" + res.problem + "'), file "
                        + juce::String (res.file.getSize() / (1024 * 1024)) + " MB, written " + juce::String ((juce::int64) res.framesWritten)
                        + " frames, dropped " + juce::String ((juce::int64) res.droppedFrames));
            expect (why.isNotEmpty(), "a message for the user");
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> rd (wav.createReaderFor (res.file.createInputStream().release(), true));
            expect (rd != nullptr, "the file opens");
            if (rd != nullptr)
            {
                const int64_t len = rd->lengthInSamples;
                expect (len > 0 && len <= (int64_t) res.framesWritten + 1, "length " + juce::String (len) + " vs written " + juce::String ((juce::int64) res.framesWritten));
                expect ((double) len >= (double) res.framesWritten - rate, "readable up to the last second: " + juce::String (len));
                int64_t bad = 0;
                juce::AudioBuffer<float> b (2, 192000);
                for (int64_t pos = 0; pos < len; pos += 192000)
                {
                    const int n = (int) juce::jmin<int64_t> (192000, len - pos);
                    rd->read (&b, 0, n, pos, true, true);
                    for (int c = 0; c < 2; ++c)
                        for (int i = 0; i < n; ++i)
                            if (! juce::exactlyEqual (b.getSample (c, i), (float) RecorderSweepTest::sig (c, pos + i, rate))) ++bad;
                }
                expectEquals (bad, (int64_t) 0, "samples that differ from what was recorded");
                logMessage ("file readable: " + juce::String ((double) len / rate, 2) + " s, differing samples " + juce::String (bad));
            }
            res.file.moveFileTo (folder.getChildFile ("dolu.wav"));   // kept for the next part (fills the disk)
        }

        beginTest ("less than 100 MB free: the recording does not start, with a message");
        {
            // the disk is full now (< 30 MB): no new recording
            Recorder rec;
            const auto err = rec.start (RecordSettings::decode ("wav:24|48000|2", folder.getFullPathName()), 48000.0);
            logMessage ("start with " + juce::String (folder.getBytesFreeOnVolume() / (1024 * 1024)) + " MB free: '" + err + "'");
            expect (err.containsIgnoreCase ("100 MB"), "message: " + err);
            expect (! rec.isRecording());
            folder.getChildFile ("dolu.wav").deleteFile();
        }

        const auto detach = juce::SystemStats::getEnvironmentVariable ("MICVST_TEST_SMALLDISK_DETACH", {});
        if (detach.isNotEmpty())
        {
            beginTest ("the disk taken away while recording: an error message, no crash, no hang");
            Recorder rec;
            expectEquals (rec.start (RecordSettings::decode ("wav:24|48000|2", folder.getFullPathName()), 48000.0), juce::String());
            int64_t pushed = 0;
            const int block = 480;
            std::vector<float> l ((size_t) block, 0.1f), r ((size_t) block, -0.1f);
            const float* ch[2] = { l.data(), r.data() };
            for (int i = 0; i < 300; ++i) { rec.push (ch, 2, block); pushed += block; juce::Thread::sleep (10); }   // 3 s
            juce::ChildProcess cp;
            const bool started = cp.start (detach);
            expect (started, "detach command: " + detach);
            if (started) { cp.waitForProcessToFinish (60000); logMessage ("detach: " + cp.readAllProcessOutput().trim().replace ("\r\n", " / ").substring (0, 300)); }
            for (int i = 0; i < 800 && ! rec.hasFailed(); ++i) { rec.push (ch, 2, block); pushed += block; juce::Thread::sleep (10); }   // up to 8 s more
            expect (rec.hasFailed(), "the lost disk is noticed");
            const auto why = rec.getFailure();
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            const auto res = rec.stop();
            const double stopMs = juce::Time::getMillisecondCounterHiRes() - t0;
            logMessage ("message: '" + why + "', stop took " + juce::String (stopMs, 0) + " ms, problem: '" + res.problem + "'");
            expect (why.isNotEmpty());
            expect (stopMs < 15000.0, "stop() returned (no hang)");
        }
    }
};

static RecorderSmallDiskTest recorderSmallDiskTest;
