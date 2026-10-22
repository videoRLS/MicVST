#include <juce_audio_formats/juce_audio_formats.h>
#include "audio/Recorder.h"
#include "audio/Resampler.h"
#include "Text.h"
#include <cmath>
#include <vector>

// The recorder ("Kayıt"): settings and their config.xml text, the rates offered, unique file
// names, and real recordings in every format and quality read back - format, bit depth, rate,
// channels, exact length and content (WAV / FLAC: to the LSB; MP3: decoded, lined up, signal-to-
// error ratio) - plus clipping, dither, mono, rate conversion, a crash in the middle (the WAV is
// valid up to the last second), a FIFO overflow (counted), errors, and a long recording.
// MP3 decoding: JUCE's decoder (JUCE_USE_MP3AUDIOFORMAT, tests only).
struct RecorderTest : juce::UnitTest
{
    RecorderTest() : juce::UnitTest ("Recorder") {}

    juce::File dir;

    // the test signal (peaks about -6 / -9 dBFS): L 997 Hz + a frequency-modulated 1.5 kHz,
    // R 440 Hz + a frequency-modulated 2.5 kHz - different per channel (channel order) and not
    // periodic over seconds (so an MP3 decoder's delay can be found by correlation)
    static float sig (int ch, int64_t n, double rate)
    {
        const double t = (double) n / rate, w = 2.0 * juce::MathConstants<double>::pi;
        return ch == 0 ? (float) (0.4 * std::sin (w * 997.0 * t) + 0.1 * std::sin (w * 1500.0 * t + 30.0 * std::sin (w * 0.7 * t)))
                       : (float) (0.25 * std::sin (w * 440.0 * t) + 0.1 * std::sin (w * 2500.0 * t + 20.0 * std::sin (w * 1.3 * t)));
    }

    // feeds 'seconds' of the signal in 480-frame blocks (10 ms at 48 kHz) like the plug-in thread;
    // paced so the FIFO never fills (speed = times real time)
    static int64_t feed (Recorder& rec, double rate, double seconds, double speed = 50.0, bool mono = false, float gain = 1.0f)
    {
        const int block = (int) std::lround (rate / 100.0);
        const int64_t total = (int64_t) std::llround (seconds * rate);
        std::vector<float> l ((size_t) block), r ((size_t) block);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        for (int64_t pos = 0; pos < total; pos += block)
        {
            const int n = (int) juce::jmin<int64_t> (block, total - pos);
            for (int i = 0; i < n; ++i)
            {
                l[(size_t) i] = gain * sig (0, pos + i, rate);
                r[(size_t) i] = mono ? l[(size_t) i] : gain * sig (1, pos + i, rate);
            }
            const float* ch[2] = { l.data(), r.data() };
            rec.push (ch, 2, n);
            const double due = (double) (pos + n) / rate * 1000.0 / speed;
            while (juce::Time::getMillisecondCounterHiRes() - t0 < due) juce::Thread::sleep (1);
        }
        return total;
    }

    struct Read { bool ok = false; int bits = 0; bool isFloat = false; double rate = 0; int ch = 0; int64_t len = 0; juce::AudioBuffer<float> audio; };
    static Read readBack (const juce::File& f)
    {
        Read r;
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        fm.registerFormat (new juce::MP3AudioFormat(), false);
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (f));
        if (reader == nullptr) return r;
        r.ok = true; r.bits = (int) reader->bitsPerSample; r.isFloat = reader->usesFloatingPointData;
        r.rate = reader->sampleRate; r.ch = (int) reader->numChannels; r.len = reader->lengthInSamples;
        r.audio.setSize (r.ch, (int) r.len);
        reader->read (&r.audio, 0, (int) r.len, 0, true, true);
        return r;
    }

    // largest difference from the test signal, in LSBs of 'bits' (skip: samples left out at the ends)
    static double maxErrorLsb (const Read& r, int bits, int skip = 0)
    {
        double worst = 0.0;
        const double lsb = std::ldexp (1.0, -(bits - 1));
        for (int c = 0; c < r.ch; ++c)
            for (int i = skip; i < r.audio.getNumSamples() - skip; ++i)
                worst = std::max (worst, std::abs ((double) r.audio.getSample (c, i) - sig (c, i, r.rate)) / lsb);
        return worst;
    }

    // MP3: decoder delay unknown to JUCE's decoder -> find the offset, then signal / error in dB
    static double mp3Snr (const Read& r, int channel, int& offsetOut)
    {
        const int n = r.audio.getNumSamples();
        double best = 1e30; int bestOff = 0;
        for (int off = 0; off < 4000; ++off)
        {
            double err = 0.0;
            for (int i = 20000; i < 40000 && i + off < n; ++i)
            {
                const double d = r.audio.getSample (channel, i + off) - sig (channel, i, r.rate);
                err += d * d;
            }
            if (err < best) { best = err; bestOff = off; }
        }
        offsetOut = bestOff;
        double s = 0.0, e = 0.0;
        for (int i = 10000; i + bestOff < n - 10000; ++i)
        {
            const double ref = sig (channel, i, r.rate);
            const double d = r.audio.getSample (channel, i + bestOff) - ref;
            s += ref * ref; e += d * d;
        }
        return 10.0 * std::log10 (s / std::max (e, 1e-30));
    }

    // MICVST_RECORDER_KEEP=<folder>: a copy of each test recording, for checks with other players
    static void keep (const juce::File& f, const juce::String& cfg)
    {
        const auto where = juce::SystemStats::getEnvironmentVariable ("MICVST_RECORDER_KEEP", {});
        if (where.isEmpty()) return;
        const juce::File d (where);
        d.createDirectory();
        f.copyFileTo (d.getChildFile (cfg.replaceCharacters (":|", "__") + f.getFileExtension()));
    }

    juce::File only (const juce::String& ext)
    {
        auto files = dir.findChildFiles (juce::File::findFiles, false, "*" + ext);
        return files.size() == 1 ? files[0] : juce::File();
    }

    void runTest() override
    {
        dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("micvst-recorder-test-" + juce::String (juce::Random::getSystemRandom().nextInt64()));
        dir.deleteRecursively();

        beginTest ("settings: defaults, config.xml text, the rates offered");
        {
            RecordSettings d;
            expectEquals (d.encode(), juce::String ("wav:24|48000|2"));
            expect (d.format == RecordSettings::Format::wav && d.wavBits == 24 && ! d.wavFloat && d.sampleRate == 48000 && d.channels == 2);
            expectEquals (RecordSettings::decode ("", {}).encode(), d.encode());
            expectEquals (RecordSettings::decode ("wav:24", {}).encode(), d.encode());           // the first, short form
            expectEquals (RecordSettings::decode ("ogg:7|1|9", {}).encode(), d.encode());
            expectEquals (RecordSettings::decode ("wav:20|47000|3", {}).encode(), d.encode());
            expectEquals (RecordSettings::decode ("mp3:cbr999", {}).encode(), d.encode());
            expectEquals (RecordSettings::decode ("mp3:v3", {}).encode(), d.encode());
            expectEquals (RecordSettings::decode ("flac:32:5", {}).encode(), d.encode());
            const juce::StringArray all { "wav:16|44100|2", "wav:24|48000|1", "wav:32|96000|2", "wav:32f|192000|2",
                                          "flac:16:0|48000|2", "flac:24:8|88200|1", "flac:24:5|176400|2",
                                          "mp3:cbr128|44100|2", "mp3:cbr160|48000|2", "mp3:cbr192|48000|1", "mp3:cbr224|44100|2",
                                          "mp3:cbr256|48000|2", "mp3:cbr320|48000|2", "mp3:v0|48000|2", "mp3:v2|44100|1", "mp3:v4|48000|2" };
            for (auto& t : all) expectEquals (RecordSettings::decode (t, {}).encode(), t);
            expectEquals (RecordSettings::decode ("mp3:cbr320|96000|2", {}).sampleRate, 48000);   // MPEG-1 has no 96 kHz
            expectEquals (RecordSettings::decode ("flac:24|48000|2", {}).flacLevel, 5);
            const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("x");
            expect (RecordSettings::decode ("wav:24", folder.getFullPathName()).folder == folder);
            expect (RecordSettings::decode ("wav:24", "relative/path").folder == juce::File());
            expect (RecordSettings{}.folderOrDefault() == RecordSettings::defaultFolder());

            using F = RecordSettings::Format;
            expect (RecordSettings::allowedRates (F::wav, 48000.0) == juce::Array<int> { 44100, 48000 });
            expect (RecordSettings::allowedRates (F::flac, 96000.0) == juce::Array<int> { 44100, 48000, 88200, 96000 });
            expect (RecordSettings::allowedRates (F::wav, 192000.0) == juce::Array<int> { 44100, 48000, 88200, 96000, 176400, 192000 });
            expect (RecordSettings::allowedRates (F::mp3, 192000.0) == juce::Array<int> { 44100, 48000 });
            RecordSettings hi; hi.sampleRate = 96000;
            expectEquals (hi.effectiveRate (96000.0), 96000);
            expectEquals (hi.effectiveRate (48000.0), 48000);     // no up-sampling to "hi-res": the processing rate
            expectEquals (hi.effectiveRate (44100.0), 44100);
            RecordSettings cd; cd.sampleRate = 44100;
            expectEquals (cd.effectiveRate (48000.0), 44100);
            expect (RecordSettings{}.describe().startsWith ("WAV 24 bit PCM, 48 kHz"));
        }

        beginTest ("file names: date and time, never over an existing file");
        {
            dir.createDirectory();
            const juce::Time t (2026, 9, 21, 21, 58, 3);
            const auto a = Recorder::uniqueFile (dir, ".wav", t);
            expectEquals (a.getFileName(), juce::String ("MicVST_2026-10-21_21-58-03.wav"));
            a.create();
            const auto b = Recorder::uniqueFile (dir, ".wav", t);
            expectEquals (b.getFileName(), juce::String ("MicVST_2026-10-21_21-58-03_2.wav"));
            b.create();
            expectEquals (Recorder::uniqueFile (dir, ".wav", t).getFileName(), juce::String ("MicVST_2026-10-21_21-58-03_3.wav"));
            expectEquals (Recorder::uniqueFile (dir, ".flac", t).getFileName(), juce::String ("MicVST_2026-10-21_21-58-03.flac"));
            dir.deleteRecursively();
        }

        beginTest ("WAV and FLAC: every bit depth, read back to the LSB");
        {
            struct Case { const char* cfg; const char* ext; int bits; bool isFloat; double maxLsb; };
            const Case cases[] = {
                { "wav:16|48000|2", ".wav", 16, false, 1.5 },    // TPDF dither: +-1 LSB + rounding
                { "wav:24|48000|2", ".wav", 24, false, 0.51 },
                { "wav:32|48000|2", ".wav", 32, false, 0.51 },   // 32 bit PCM: every float value exactly
                { "wav:32f|48000|2", ".wav", 32, true, 0.0 },
                { "flac:16:5|48000|2", ".flac", 16, false, 1.5 },
                { "flac:24:5|48000|2", ".flac", 24, false, 0.51 },
                { "flac:24:0|48000|2", ".flac", 24, false, 0.51 },
                { "flac:24:8|48000|2", ".flac", 24, false, 0.51 },
            };
            for (auto& c : cases)
            {
                dir.deleteRecursively();
                auto s = RecordSettings::decode (c.cfg, dir.getFullPathName());
                Recorder rec;
                const auto err = rec.start (s, 48000.0);
                expectEquals (err, juce::String(), c.cfg);
                expect (rec.isRecording());
                const auto pushed = feed (rec, 48000.0, 3.0);
                const auto res = rec.stop();
                expect (! rec.isRecording());
                expect (res.verified, c.cfg);
                expectEquals (res.problem, juce::String(), c.cfg);
                expectEquals ((int64_t) res.droppedFrames, (int64_t) 0);
                expectEquals ((int64_t) res.framesWritten, pushed);
                expect (res.file == only (c.ext), c.cfg);
                keep (res.file, c.cfg);
                const auto r = readBack (res.file);
                expect (r.ok, c.cfg);
                expectEquals (r.bits, c.bits, c.cfg);
                expect (r.isFloat == c.isFloat, c.cfg);
                expectEquals (r.rate, 48000.0);
                expectEquals (r.ch, 2);
                expectEquals (r.len, pushed, c.cfg);
                const double e = maxErrorLsb (r, c.isFloat ? 24 : c.bits);
                if (c.maxLsb == 0.0) expectEquals (e, 0.0, c.cfg);
                else expect (e <= c.maxLsb, juce::String (c.cfg) + ": max error " + juce::String (e, 3) + " LSB");
                logMessage (juce::String (c.cfg) + ": " + juce::String (r.len) + " samples, max error " + juce::String (e, 3)
                            + " LSB, file " + juce::String (res.file.getSize() / 1024) + " KB");
            }
        }

        beginTest ("WAV: Broadcast WAV chunk (originator, date)");
        {
            dir.deleteRecursively();
            Recorder rec;
            expectEquals (rec.start (RecordSettings::decode ("wav:24", dir.getFullPathName()), 48000.0), juce::String());
            feed (rec, 48000.0, 0.5);
            const auto res = rec.stop();
            juce::WavAudioFormat wav;
            std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (res.file.createInputStream().release(), true));
            expect (reader != nullptr);
            if (reader != nullptr)
            {
                expectEquals (reader->metadataValues[juce::WavAudioFormat::bwavOriginator], juce::String ("MicVST"));
                expect (reader->metadataValues[juce::WavAudioFormat::bwavOriginationDate].length() == 10);
            }
        }

        beginTest ("integer formats: overs limited to full scale, never wrapped round");
        {
            for (auto cfg : { "wav:16", "wav:24", "wav:32", "flac:24:5" })
            {
                dir.deleteRecursively();
                Recorder rec;
                expectEquals (rec.start (RecordSettings::decode (cfg, dir.getFullPathName()), 48000.0), juce::String());
                feed (rec, 48000.0, 0.5, 50.0, false, 3.0f);   // peaks up to x1.5 (+3.5 dBFS)
                const auto res = rec.stop();
                const auto r = readBack (res.file);
                float mx = -2.0f, mn = 2.0f; int signFlips = 0;
                for (int i = 0; i < r.audio.getNumSamples(); ++i)
                {
                    const float v = r.audio.getSample (0, i);
                    mx = std::max (mx, v); mn = std::min (mn, v);
                    if (std::abs (sig (0, i, 48000.0) * 3.0f) > 1.0f && (v > 0) != (sig (0, i, 48000.0) > 0)) ++signFlips;
                }
                expect (mx <= 1.0f && mx > 0.99f && mn >= -1.0f && mn < -0.99f, juce::String (cfg) + " range " + juce::String (mn) + " .. " + juce::String (mx));
                expectEquals (signFlips, 0, cfg);
            }
        }

        beginTest ("16 bit dither: TPDF, unbiased, no sign of truncation");
        {
            dir.deleteRecursively();
            Recorder rec;
            expectEquals (rec.start (RecordSettings::decode ("wav:16", dir.getFullPathName()), 48000.0), juce::String());
            feed (rec, 48000.0, 2.0);
            const auto r = readBack (rec.stop().file);
            const double lsb = 1.0 / 32768.0;
            double sum = 0.0, sq = 0.0; int n = 0;
            for (int i = 0; i < r.audio.getNumSamples(); ++i)
            {
                const double e = (r.audio.getSample (0, i) - sig (0, i, 48000.0)) / lsb;
                sum += e; sq += e * e; ++n;
            }
            const double mean = sum / n, rms = std::sqrt (sq / n - mean * mean);
            // TPDF (+-1 LSB) + rounding: rms = sqrt (1/6 + 1/12) = 0.5 LSB
            expect (std::abs (mean) < 0.02, "mean " + juce::String (mean, 4));
            expect (rms > 0.45 && rms < 0.55, "rms " + juce::String (rms, 4) + " LSB");
            logMessage ("16 bit dither: error mean " + juce::String (mean, 4) + " LSB, rms " + juce::String (rms, 4) + " LSB (TPDF: 0.5)");
        }

        beginTest ("mono: (L+R)/2 - a mono voice exactly");
        {
            dir.deleteRecursively();
            Recorder rec;
            expectEquals (rec.start (RecordSettings::decode ("wav:32f|48000|1", dir.getFullPathName()), 48000.0), juce::String());
            const auto pushed = feed (rec, 48000.0, 1.0, 50.0, true);
            const auto res = rec.stop();
            expect (res.verified);
            const auto r = readBack (res.file);
            expectEquals (r.ch, 1);
            expectEquals (r.len, pushed);
            double worst = 0.0;
            for (int i = 0; i < r.audio.getNumSamples(); ++i) worst = std::max (worst, (double) std::abs (r.audio.getSample (0, i) - sig (0, i, 48000.0)));
            expectEquals (worst, 0.0);
        }

        beginTest ("sample rates: 48 -> 44.1 kHz and 96 -> 48 / 88.2 kHz, converted cleanly and in time");
        {
            struct Case { const char* cfg; double in; int out; };
            for (auto c : { Case { "wav:32f|44100|2", 48000.0, 44100 }, Case { "wav:24|44100|2", 48000.0, 44100 },
                            Case { "flac:24:5|48000|2", 96000.0, 48000 }, Case { "wav:32f|88200|2", 96000.0, 88200 },
                            Case { "wav:32f|48000|2", 44100.0, 48000 } })
            {
                dir.deleteRecursively();
                Recorder rec;
                expectEquals (rec.start (RecordSettings::decode (c.cfg, dir.getFullPathName()), c.in), juce::String());
                const auto pushed = feed (rec, c.in, 2.0);
                const auto res = rec.stop();
                expect (res.verified, c.cfg);
                keep (res.file, juce::String (c.cfg) + "_from" + juce::String ((int) c.in));
                const auto r = readBack (res.file);
                expectEquals (r.rate, (double) c.out, c.cfg);
                const int64_t expectLen = (pushed * c.out + (int64_t) c.in - 1) / (int64_t) c.in;
                expectEquals (r.len, expectLen, c.cfg);
                // against the signal computed at the new rate (same times): converter error
                double e = 0.0, s = 0.0;
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 2000; i < r.audio.getNumSamples() - 2000; ++i)
                    {
                        const double ref = sig (ch, i, c.out);
                        const double d = r.audio.getSample (ch, i) - ref;
                        e += d * d; s += ref * ref;
                    }
                const double snr = 10.0 * std::log10 (s / std::max (e, 1e-30));
                expect (snr > 130.0, juce::String (c.cfg) + " from " + juce::String (c.in) + ": " + juce::String (snr, 1) + " dB");
                logMessage (juce::String (c.cfg) + " from " + juce::String ((int) c.in) + " Hz: signal/error " + juce::String (snr, 1) + " dB, length exact");
            }
            // a 96 kHz setting while the microphone runs at 48 kHz: 48 kHz, not up-sampled
            dir.deleteRecursively();
            Recorder rec;
            expectEquals (rec.start (RecordSettings::decode ("wav:24|96000|2", dir.getFullPathName()), 48000.0), juce::String());
            feed (rec, 48000.0, 0.3);
            expectEquals (readBack (rec.stop().file).rate, 48000.0);
        }

        beginTest ("the converter: flat pass band, 140 dB stop band");
        {
            for (auto [in, out] : { std::pair<int, int> { 48000, 44100 }, { 96000, 48000 }, { 192000, 44100 } })
            {
                for (double f : { 100.0, 1000.0, 10000.0, 19900.0, 0.53 * std::min (in, out) })
                {
                    Resampler rs; rs.prepare (in, out, 1);
                    const int n = in;
                    std::vector<float> x ((size_t) n);
                    for (int i = 0; i < n; ++i) x[(size_t) i] = (float) (0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * f * i / in));
                    std::vector<std::vector<float>> y;
                    const float* p = x.data();
                    rs.process (&p, n, y); rs.finish (y);
                    expectEquals ((int64_t) y[0].size(), rs.outputsFor (n));
                    double e = 0.0;
                    const bool stop = f >= 0.5 * std::min (in, out);
                    for (size_t i = (size_t) out / 4; i < y[0].size() - (size_t) out / 4; ++i)
                    {
                        const double ref = stop ? 0.0 : 0.5 * std::sin (2.0 * juce::MathConstants<double>::pi * f * (double) i / out);
                        e += (y[0][i] - ref) * (y[0][i] - ref);
                    }
                    const double db = 10.0 * std::log10 (e / (double) (y[0].size() - (size_t) out / 2) / 0.125);
                    expect (db < -135.0, juce::String (in) + " -> " + juce::String (out) + " at " + juce::String (f) + " Hz: " + juce::String (db, 1) + " dB");
                }
            }
        }

        beginTest ("MP3: CBR 128-320 and VBR V0/V2/V4, stereo and mono, 44.1 and 48 kHz");
        {
            for (auto cfg : { "mp3:cbr128|48000|2", "mp3:cbr160|48000|2", "mp3:cbr192|44100|2", "mp3:cbr224|48000|2",
                              "mp3:cbr256|48000|2", "mp3:cbr320|48000|2", "mp3:cbr320|44100|1",
                              "mp3:v0|48000|2", "mp3:v2|48000|2", "mp3:v4|44100|2", "mp3:v0|48000|1" })
            {
                dir.deleteRecursively();
                const auto s = RecordSettings::decode (cfg, dir.getFullPathName());
                expectEquals (s.encode(), juce::String (cfg));
                Recorder rec;
                expectEquals (rec.start (s, 48000.0), juce::String(), cfg);
                const auto pushed = feed (rec, 48000.0, 4.0, 50.0, s.channels == 1);
                const auto res = rec.stop();
                expect (res.verified, cfg);
                expectEquals (res.problem, juce::String(), cfg);
                expect (res.file == only (".mp3"), cfg);
                keep (res.file, cfg);
                Recorder::Mp3Info info;
                expect (Recorder::inspectMp3 (res.file, info), cfg);
                expectEquals (info.sampleRate, s.sampleRate, cfg);
                expectEquals (info.channels, s.channels, cfg);
                const int64_t expectLen = (pushed * s.sampleRate + 47999) / 48000;
                expectEquals (info.samples, expectLen, juce::String (cfg) + " samples (LAME tag)");
                if (s.mp3Mode == RecordSettings::Mp3Mode::cbr) { expectEquals (info.kbps, s.mp3Kbps, cfg); expect (! info.vbr); }
                else expect (info.vbr, cfg);
                const auto r = readBack (res.file);
                expect (r.ok && r.ch == s.channels && (int) r.rate == s.sampleRate, cfg);
                int off = 0;
                const double snrL = mp3Snr (r, 0, off);
                const double kbps = (double) res.file.getSize() * 8.0 / 1000.0 / ((double) expectLen / s.sampleRate);
                // waveform error of a perceptual coder (it hides noise under the signal, so this is
                // no quality score): 20+ dB = decoded right, in order; garbage / wrong channel /
                // wrong offset gives < 3 dB
                expect (snrL > 20.0, juce::String (cfg) + " L signal/error " + juce::String (snrL, 1) + " dB");
                if (s.channels == 2)
                {
                    int offR = 0;
                    const double snrR = mp3Snr (r, 1, offR);
                    expect (snrR > 15.0, juce::String (cfg) + " R signal/error " + juce::String (snrR, 1) + " dB");
                    expectEquals (offR, off, "same offset both channels");
                    logMessage ("   R signal/error " + juce::String (snrR, 1) + " dB");
                }
                logMessage (juce::String (cfg) + ": " + juce::String (info.frames) + " frames, " + juce::String (info.samples)
                            + " samples (= written), " + juce::String (kbps, 0) + " kbps average, L signal/error "
                            + juce::String (snrL, 1) + " dB, decoder offset " + juce::String (off));
            }
        }

        beginTest ("MP3 check rejects damaged files");
        {
            dir.deleteRecursively();
            Recorder rec;
            expectEquals (rec.start (RecordSettings::decode ("mp3:cbr192", dir.getFullPathName()), 48000.0), juce::String());
            feed (rec, 48000.0, 1.0);
            const auto f = rec.stop().file;
            juce::MemoryBlock mb; f.loadFileAsData (mb);
            Recorder::Mp3Info info;
            const auto cut = dir.getChildFile ("cut.mp3");
            cut.replaceWithData (mb.getData(), mb.getSize() - 100);
            expect (! Recorder::inspectMp3 (cut, info));
            const auto junk = dir.getChildFile ("junk.mp3");
            junk.replaceWithText ("this is not an mp3 file at all, just text that is long enough for the check to look at it......................................................");
            expect (! Recorder::inspectMp3 (junk, info));
        }

        beginTest ("a crash while recording: the WAV is valid up to the last second");
        {
            dir.deleteRecursively();
            Recorder rec;
            expectEquals (rec.start (RecordSettings::decode ("wav:24", dir.getFullPathName()), 48000.0), juce::String());
            const auto pushed = feed (rec, 48000.0, 3.0, 1.0);   // real time
            juce::Thread::sleep (1300);                          // one header rewrite after the last push
            // what is on the disk now, as if the process ended here
            const auto snap = dir.getChildFile ("snapshot.wav");
            rec.getFile().copyFileTo (snap);
            const auto r = readBack (snap);
            expect (r.ok, "readable after a crash");
            expect (r.len >= pushed - 48000 && r.len <= pushed, "length " + juce::String (r.len) + " of " + juce::String (pushed));
            const double crashErr = maxErrorLsb (r, 24);
            expect (crashErr <= 0.51, "max error " + juce::String (crashErr) + " LSB");
            rec.stop();
        }

        beginTest ("FIFO overflow (disk stalled > 30 s): counted, the file stays consistent");
        {
            dir.deleteRecursively();
            Recorder rec;
            expectEquals (rec.start (RecordSettings::decode ("wav:24", dir.getFullPathName()), 48000.0), juce::String());
            const int n = (int) (48000 * (Recorder::fifoSeconds + 2.0));   // more than the FIFO at once
            std::vector<float> l ((size_t) n, 0.1f);
            const float* ch[2] = { l.data(), l.data() };
            rec.push (ch, 2, n);
            expect (rec.getDroppedFrames() >= 48000 * 2 - 1, "dropped " + juce::String (rec.getDroppedFrames()));
            const auto res = rec.stop();
            expect (res.verified);
            expectEquals (res.framesWritten + res.droppedFrames, (int64_t) n);
        }

        beginTest ("errors: no audio, folder that cannot exist, two starts");
        {
            dir.deleteRecursively();
            Recorder rec;
            expect (rec.start (RecordSettings::decode ("wav:24", dir.getFullPathName()), 0.0).isNotEmpty());
            expect (! rec.isRecording());
            dir.createDirectory();
            const auto blocker = dir.getChildFile ("file");
            blocker.replaceWithText ("x");
            auto s = RecordSettings::decode ("wav:24", blocker.getChildFile ("sub").getFullPathName());
            const auto err = rec.start (s, 48000.0);
            expect (err.isNotEmpty(), "a folder inside a file");
            expect (! rec.isRecording());
            expectEquals (rec.start (RecordSettings::decode ("wav:24", dir.getFullPathName()), 48000.0), juce::String());
            expect (rec.start (RecordSettings::decode ("wav:24", dir.getFullPathName()), 48000.0).isNotEmpty(), "second start refused");
            const auto r1 = rec.stop();
            expect (r1.verified);
            const auto r2 = rec.stop();   // nothing to stop
            expect (r2.file == juce::File());
        }

        beginTest ("start / stop many times: every file whole, no two the same");
        {
            dir.deleteRecursively();
            juce::StringArray names;
            for (int i = 0; i < 12; ++i)
            {
                Recorder rec;
                const juce::StringArray cfgs { "wav:24", "flac:16:5", "mp3:cbr320", "wav:32f|44100|1" };
                expectEquals (rec.start (RecordSettings::decode (cfgs[i % 4], dir.getFullPathName()), 48000.0), juce::String());
                feed (rec, 48000.0, 0.2, 100.0);
                const auto res = rec.stop();
                expect (res.verified, cfgs[i % 4]);
                expect (! names.contains (res.file.getFileName()));
                names.add (res.file.getFileName());
            }
            expectEquals (dir.findChildFiles (juce::File::findFiles, false).size(), 12);
        }

        beginTest ("long recording: 20 minutes, WAV 24 bit and FLAC, no loss");
        {
            for (auto cfg : { "wav:24|48000|2", "flac:24:5|48000|2", "mp3:v0|48000|2" })
            {
                dir.deleteRecursively();
                Recorder rec;
                expectEquals (rec.start (RecordSettings::decode (cfg, dir.getFullPathName()), 48000.0), juce::String());
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                // 10x real time: a margin of 10 over real use; the CI runners' CPU varies too much for more
                // (the encoder's own speed is measured below: FLAC 68-300x, MP3 27-120x)
                const auto pushed = feed (rec, 48000.0, 20.0 * 60.0, 10.0);
                const auto res = rec.stop();
                const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
                expect (res.verified, cfg);
                expectEquals ((int64_t) res.droppedFrames, (int64_t) 0);
                expectEquals (res.framesWritten, pushed, cfg);
                expectEquals (res.problem, juce::String());
                if (! juce::String (cfg).startsWith ("mp3"))
                {
                    const auto r = readBack (res.file);
                    expectEquals (r.len, pushed);
                    expect (maxErrorLsb (r, 24) <= 0.51, cfg);
                }
                logMessage (juce::String (cfg) + ": 20 min recorded in " + juce::String (secs, 1) + " s (" + juce::String (1200.0 / secs, 0)
                            + "x real time incl. the test signal; real use: 1x), " + juce::String (res.file.getSize() / (1024 * 1024)) + " MB, verified");
            }
        }

        beginTest ("encoder speed (writer thread, one core): times real time");
        {
            for (auto cfg : { "wav:24|48000|2", "wav:16|48000|2", "flac:24:5|48000|2", "flac:24:8|48000|2", "wav:24|44100|2",
                              "mp3:cbr320|48000|2", "mp3:v0|48000|2", "mp3:cbr320|44100|2" })
            {
                dir.deleteRecursively();
                Recorder rec;
                expectEquals (rec.start (RecordSettings::decode (cfg, dir.getFullPathName()), 48000.0), juce::String());
                const int n = 48000 * 9;
                std::vector<float> l ((size_t) n), r ((size_t) n);
                for (int i = 0; i < n; ++i) { l[(size_t) i] = sig (0, i, 48000.0); r[(size_t) i] = sig (1, i, 48000.0); }
                const float* ch[2] = { l.data(), r.data() };
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                rec.push (ch, 2, n);
                const auto res = rec.stop();
                const double secs = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
                expect (res.verified && res.droppedFrames == 0, cfg);
                const double speed = 9.0 / secs;
                expect (speed > 5.0, juce::String (cfg) + ": " + juce::String (speed, 1) + "x");
                logMessage (juce::String (cfg) + ": " + juce::String (speed, 0) + "x real time (" + juce::String (100.0 / speed, 2) + " % of one core)");
            }
        }

        if (juce::SystemStats::getEnvironmentVariable ("MICVST_TEST_RF64", {}) == "1")
        {
            beginTest ("past 4 GB: the WAV becomes RF64 and reads back whole");
            dir.deleteRecursively();
            Recorder rec;
            expectEquals (rec.start (RecordSettings::decode ("wav:32f|192000|2", dir.getFullPathName()), 192000.0), juce::String());
            const int block = 192000;
            std::vector<float> l ((size_t) block), r ((size_t) block);
            for (int i = 0; i < block; ++i) { l[(size_t) i] = sig (0, i, 192000.0); r[(size_t) i] = sig (1, i, 192000.0); }
            const float* ch[2] = { l.data(), r.data() };
            const int64_t target = (4500ll * 1024 * 1024) / 8;   // 4.5 GB of 2 x 32 bit
            int64_t pushed = 0;
            // as fast as the disk takes it: at most 10 s ahead of what is in the file
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            while (pushed < target)
            {
                while (pushed - rec.getFramesWritten() > 10 * block) juce::Thread::sleep (5);
                rec.push (ch, 2, block);
                pushed += block;
            }
            logMessage ("RF64: disk took " + juce::String ((double) pushed * 8.0 / 1.048576e6 / ((juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0), 0) + " MB/s");
            const auto res = rec.stop();
            expect (res.verified, "RF64 read back");
            expectEquals ((int64_t) res.droppedFrames, (int64_t) 0);
            juce::FileInputStream in (res.file);
            char id[4] = {}; in.read (id, 4);
            expect (juce::String (id, 4) == "RF64", "header " + juce::String (id, 4));
            logMessage ("RF64: " + juce::String (res.file.getSize() / (1024 * 1024)) + " MB, " + juce::String (res.seconds, 0) + " s");
        }

        dir.deleteRecursively();
    }
};

static RecorderTest recorderTest;
