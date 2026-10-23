#include <juce_audio_formats/juce_audio_formats.h>
#include "audio/CueSounds.h"
#include "Text.h"
#include <cmath>

// Bildirim sesleri: the 16 built-in sounds, the user's folder (new files listed by themselves,
// unsuitable ones skipped), the settings text, the WAV handed to Windows, and the player (the
// right sound for mute / unmute, off means silent, never into the cable, a missing file falls
// back to the default).
struct CueTest : juce::UnitTest
{
    CueTest() : juce::UnitTest ("Cue sounds") {}

    struct FakeBackend final : CuePlayer::Backend
    {
        juce::Array<juce::MemoryBlock>* played; int* stops; juce::String output;
        FakeBackend (juce::Array<juce::MemoryBlock>* p, int* s, juce::String o) : played (p), stops (s), output (std::move (o)) {}
        bool play (const juce::MemoryBlock& w) override { played->add (w); return true; }
        void stop() override { ++*stops; }
        juce::String defaultOutputName() override { return output; }
    };

    static void writeWav (const juce::File& f, double seconds, double rate = 48000.0, int ch = 2, double freq = 440.0)
    {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (f);
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (ch).withBitsPerSample (24));
        const int n = (int) (seconds * rate);
        juce::AudioBuffer<float> b (ch, n);
        for (int c = 0; c < ch; ++c)
            for (int i = 0; i < n; ++i) b.setSample (c, i, (float) (0.3 * std::sin (2.0 * juce::MathConstants<double>::pi * freq * i / rate)));
        w->writeFromAudioSampleBuffer (b, 0, n);
    }

    // the samples of a 16-bit PCM WAV made by makeWav
    static std::vector<int16_t> pcm (const juce::MemoryBlock& m, int& rate, int& ch)
    {
        const auto* p = static_cast<const uint8_t*> (m.getData());
        ch = p[22] | (p[23] << 8);
        rate = (int) (p[24] | (p[25] << 8) | (p[26] << 16) | ((uint32_t) p[27] << 24));
        const uint32_t bytes = p[40] | (p[41] << 8) | (p[42] << 16) | ((uint32_t) p[43] << 24);
        std::vector<int16_t> s (bytes / 2);
        std::memcpy (s.data(), p + 44, bytes);
        return s;
    }

    // what the player hands to Windows at the default volume
    static juce::MemoryBlock wavOf (const juce::AudioBuffer<float>& b, double rate, int volume = 40)
    {
        return CuePlayer::makeWav (CuePlayer::levelled (b, rate), CuePlayer::outputRate, volume);
    }

    // the largest step between neighbouring samples (a cut sound shows up as a step)
    static int maxStep (const std::vector<int16_t>& s, int from, int to)
    {
        int m = 0;
        for (int i = juce::jmax (2, from); i < to && i < (int) s.size(); ++i) m = juce::jmax (m, std::abs ((int) s[(size_t) i] - (int) s[(size_t) i - 2]));
        return m;
    }

    void runTest() override
    {
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("micvst-cue-test");
        dir.deleteRecursively();

        beginTest ("16 built-in sounds: all load, 48 kHz stereo, short, start and end silent");
        {
            CueLibrary lib;
            lib.setUserFolder (dir);   // missing: built-ins only
            const auto list = lib.list();
            expectEquals (list.size(), 16);
            juce::StringArray ids;
            for (auto& s : list)
            {
                expect (s.builtIn);
                expect (! ids.contains (s.id), "unique id " + s.id);
                ids.add (s.id);
                juce::AudioBuffer<float> b; double rate = 0.0;
                expect (lib.load (s.id, b, rate), s.id);
                expectEquals (rate, 48000.0, s.id);
                expectEquals (b.getNumChannels(), 2);
                const double secs = b.getNumSamples() / rate;
                expect (secs > 0.05 && secs < 2.0, s.id + " " + juce::String (secs));
                const float first = std::max (std::abs (b.getSample (0, 0)), std::abs (b.getSample (1, 0)));
                const float last = std::max (std::abs (b.getSample (0, b.getNumSamples() - 1)), std::abs (b.getSample (1, b.getNumSamples() - 1)));
                expect (first < 1.0e-4f && last < 1.0e-4f, s.id + ": no click at the ends (" + juce::String (first) + ", " + juce::String (last) + ")");
                const float peak = b.getMagnitude (0, b.getNumSamples());
                expect (peak > 0.1f && peak < 0.9f, s.id + " peak " + juce::String (peak));
            }
            expect (ids.contains (CueSettings::defaultOnId()) && ids.contains (CueSettings::defaultOffId()), "the defaults are built in");
            expect (list[0].name.startsWith (juce::String::fromUTF8 ("Ahşap")));
        }

        beginTest ("settings: default Ahşap, config.xml text, unreadable -> default");
        {
            CueSettings d;
            expect (d.enabled && d.onId == "builtin:01_ahsap_ac" && d.offId == "builtin:01_ahsap_kapat" && d.volume == 40 && d.isDefault());
            CueSettings s; s.enabled = false; s.onId = "file:Benim sesim.wav"; s.offId = "builtin:05_kristal_kapat"; s.volume = 35;
            expect (CueSettings::decode (s.encode()) == s);
            expect (CueSettings::decode ("") == d);
            expect (CueSettings::decode ("rubbish") == d);
            expectEquals (CueSettings::decode ("1|a|b|250").volume, 100);
        }

        beginTest ("user folder: suitable files listed by themselves, others skipped");
        {
            dir.createDirectory();
            CueLibrary lib;
            lib.setUserFolder (dir);
            expectEquals (lib.list().size(), 16);
            writeWav (dir.getChildFile ("Benim açılma sesim.wav"), 0.4);
            writeWav (dir.getChildFile ("mono.wav"), 0.3, 44100.0, 1);
            writeWav (dir.getChildFile ("uzun.wav"), 12.0);                     // longer than 10 s
            dir.getChildFile ("not.txt").replaceWithText ("not audio");
            dir.getChildFile ("bozuk.wav").replaceWithText ("RIFF garbage");
            {
                // a FLAC too
                std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (dir.getChildFile ("cam.flac"));
                juce::FlacAudioFormat flac;
                auto w = flac.createWriterFor (out, juce::AudioFormatWriterOptions{}.withSampleRate (48000).withNumChannels (2).withBitsPerSample (24));
                juce::AudioBuffer<float> b (2, 9600); b.clear(); b.setSample (0, 100, 0.5f);
                w->writeFromAudioSampleBuffer (b, 0, 9600);
            }
            auto list = lib.list();
            juce::StringArray names;
            for (auto& s : list) if (! s.builtIn) names.add (s.name);
            expect (names == juce::StringArray { juce::String::fromUTF8 ("Benim açılma sesim"), "cam", "mono" }, names.joinIntoString (", "));
            expectEquals (lib.skippedFiles(), 2, "too long + broken (the .txt is not even looked at)");
            juce::AudioBuffer<float> b; double rate = 0.0;
            expect (lib.load ("file:mono.wav", b, rate) && rate == 44100.0 && b.getNumChannels() == 2, "mono played on both sides");
            // a new file appears, a removed one disappears - no code, no restart
            writeWav (dir.getChildFile ("yeni.wav"), 0.2);
            dir.getChildFile ("cam.flac").deleteFile();
            names.clear();
            for (auto& s : lib.list()) if (! s.builtIn) names.add (s.name);
            expect (names.contains ("yeni") && ! names.contains ("cam"), names.joinIntoString (", "));
        }

        beginTest ("the WAV for Windows: 16-bit PCM, its rate, the volume applied");
        {
            juce::AudioBuffer<float> b (2, 4800);
            for (int i = 0; i < 4800; ++i) { b.setSample (0, i, 0.5f * (float) std::sin (0.06 * i)); b.setSample (1, i, b.getSample (0, i)); }
            int rate = 0, ch = 0;
            const auto full = pcm (CuePlayer::makeWav (b, 44100.0, 100), rate, ch);
            expectEquals (rate, 44100); expectEquals (ch, 2); expectEquals ((int) full.size(), 9600);
            const auto half = pcm (CuePlayer::makeWav (b, 44100.0, 50), rate, ch);
            const auto zero = pcm (CuePlayer::makeWav (b, 44100.0, 0), rate, ch);
            int pf = 0, ph = 0, pz = 0;
            for (size_t i = 0; i < full.size(); ++i) { pf = std::max (pf, std::abs ((int) full[i])); ph = std::max (ph, std::abs ((int) half[i])); pz = std::max (pz, std::abs ((int) zero[i])); }
            expect (std::abs (pf - 16384) <= 2 && std::abs (ph - 8192) <= 2, juce::String (pf) + " / " + juce::String (ph));
            expectEquals (pz, 0, "volume 0: silence");
        }

        beginTest ("player: unmute -> açılma, mute -> kapanma, off -> nothing, never into the cable");
        {
            juce::Array<juce::MemoryBlock> played; int stops = 0;
            CuePlayer p;
            p.library().setUserFolder (dir);
            p.setBackend (std::make_unique<FakeBackend> (&played, &stops, "Kulaklık (USB)"));
            p.cableName = [] { return juce::String ("CABLE Input (VB-Audio Virtual Cable)"); };
            p.setSettings (CueSettings{});
            p.play (false);   // unmuted
            p.play (true);    // muted
            expectEquals (played.size(), 2);
            CueLibrary lib; juce::AudioBuffer<float> on, off; double r = 0;
            lib.load (CueSettings::defaultOnId(), on, r); lib.load (CueSettings::defaultOffId(), off, r);
            expect (played[0] == wavOf (on, r), "unmute plays Ahşap açılma");
            expect (played[1] == wavOf (off, r), "mute plays Ahşap kapanma");
            expect (p.describe (true).startsWith (juce::String::fromUTF8 ("Ahşap")));

            auto s = CueSettings{}; s.enabled = false; p.setSettings (s);
            p.play (true); p.play (false);
            expectEquals (played.size(), 2, "switched off: silent mute / unmute");

            s.enabled = true; s.onId = "builtin:05_kristal_ac"; s.offId = "file:yeni.wav"; p.setSettings (s);
            p.play (false); p.play (true);
            CueLibrary l2; l2.setUserFolder (dir); juce::AudioBuffer<float> k, y; l2.load ("builtin:05_kristal_ac", k, r); double ry = 0; l2.load ("file:yeni.wav", y, ry);
            expect (played[2] == wavOf (k, r), "chosen built-in");
            expect (played[3] == wavOf (y, ry), "chosen own file");

            dir.getChildFile ("yeni.wav").deleteFile();          // the chosen file is gone
            p.setSettings (s);
            p.play (true);
            expect (played[4] == wavOf (off, 48000.0), "missing file: the default kapanma");

            // Windows' default output is the cable: nothing, ever (the other side would hear it)
            juce::Array<juce::MemoryBlock> played2; int stops2 = 0;
            CuePlayer q;
            q.setBackend (std::make_unique<FakeBackend> (&played2, &stops2, "CABLE Input (VB-Audio Virtual Cable)"));
            q.cableName = [] { return juce::String ("CABLE Input (VB-Audio Virtual Cable)"); };
            q.play (true); q.play (false); q.preview (CueSettings::defaultOnId());
            expectEquals (played2.size(), 0);
            expect (q.lastProblem().isNotEmpty());
        }

        beginTest ("player: play returns at once (the sound starts with the mute itself)");
        {
            juce::Array<juce::MemoryBlock> played; int stops = 0;
            CuePlayer p;
            p.setBackend (std::make_unique<FakeBackend> (&played, &stops, "Hoparlör"));
            p.setSettings (CueSettings{});
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 1000; ++i) p.play ((i & 1) != 0);
            const double each = (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            expectEquals (played.size(), 1000);
            expect (each < 1.0, "per play " + juce::String (each, 3) + " ms");
            logMessage ("play(): " + juce::String (each * 1000.0, 1) + " us per call (nothing decoded or allocated there; the sounds are prepared when chosen)");
        }

        beginTest ("level: every built-in sound and any own file at the same loudness, peaks under the ceiling");
        {
            CueLibrary lib; lib.setUserFolder (dir);
            double lo = 0.0, hi = -100.0;
            for (auto& s : lib.list())
            {
                juce::AudioBuffer<float> b; double rate = 0;
                if (! s.builtIn || ! lib.load (s.id, b, rate)) continue;
                const auto lv = CuePlayer::levelled (b, rate);
                const double l = CuePlayer::loudness (lv);
                const double pk = juce::Decibels::gainToDecibels (juce::jmax (lv.getMagnitude (0, 0, lv.getNumSamples()), lv.getMagnitude (1, 0, lv.getNumSamples())));
                expect (pk <= CuePlayer::peakCeilingDb + 0.01, s.id + " peak " + juce::String (pk, 2));
                lo = juce::jmin (lo, l); hi = juce::jmax (hi, l);
                logMessage (s.id + ": " + juce::String (l, 2) + " LUFS, peak " + juce::String (pk, 2) + " dBFS at 100 %");
            }
            expect (hi - lo < 0.5 && std::abs (hi - CuePlayer::fullScaleLufs) < 0.1, "built-ins " + juce::String (lo, 2) + " .. " + juce::String (hi, 2));

            // own files: a sine at 0 dBFS, one at -40 dBFS, one at 44.1 kHz - all come out the same
            for (auto [amp, rate] : { std::pair<double, double> { 1.0, 48000.0 }, { 0.01, 48000.0 }, { 0.3, 44100.0 } })
            {
                juce::AudioBuffer<float> b (2, (int) (0.5 * rate));
                for (int i = 0; i < b.getNumSamples(); ++i)
                    for (int c = 0; c < 2; ++c) b.setSample (c, i, (float) (amp * std::sin (2.0 * juce::MathConstants<double>::pi * 1000.0 * i / rate)));
                const auto lv = CuePlayer::levelled (b, rate);
                const double l = CuePlayer::loudness (lv);
                expect (std::abs (l - CuePlayer::fullScaleLufs) < 0.15, juce::String (amp) + " @ " + juce::String (rate) + ": " + juce::String (l, 2));
                expect (std::abs (lv.getNumSamples() - 24000) <= 2, "at 48 kHz: " + juce::String (lv.getNumSamples()));
                int crossings = 0;
                for (int i = 1; i < lv.getNumSamples(); ++i) if ((lv.getSample (0, i - 1) < 0.0f) != (lv.getSample (0, i) < 0.0f)) ++crossings;
                expect (std::abs (crossings - 1000) <= 4, "still 1 kHz: " + juce::String (crossings) + " crossings in 0.5 s");
            }
            // a spiky file: held under the ceiling, quieter rather than over
            juce::AudioBuffer<float> sp (2, 24000); sp.clear();
            for (int i = 0; i < 24000; i += 4800) { sp.setSample (0, i + 100, 0.9f); sp.setSample (1, i + 100, 0.9f); }
            const auto lsp = CuePlayer::levelled (sp, 48000.0);
            expect (lsp.getMagnitude (0, 0, 24000) <= juce::Decibels::decibelsToGain (CuePlayer::peakCeilingDb) + 1.0e-4f);
        }

        beginTest ("level: a 10 s own file at 44.1 kHz is levelled once; moving the volume only rescales");
        {
            juce::AudioBuffer<float> b (2, 441000);
            for (int i = 0; i < b.getNumSamples(); ++i)
                for (int c = 0; c < 2; ++c) b.setSample (c, i, (float) (0.2 * std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * i / 44100.0)));
            const double t0 = juce::Time::getMillisecondCounterHiRes();
            const auto lv = CuePlayer::levelled (b, 44100.0);
            const double t1 = juce::Time::getMillisecondCounterHiRes();
            const auto w = CuePlayer::makeWav (lv, CuePlayer::outputRate, 40);
            const double t2 = juce::Time::getMillisecondCounterHiRes();
            logMessage ("10 s at 44.1 kHz: levelled " + juce::String (t1 - t0, 1) + " ms, WAV " + juce::String (t2 - t1, 1) + " ms");
            expect (w.getSize() > 0);

            // the player: chosen once (levelled), then the volume slider moves - only rescaled
            dir.createDirectory();
            writeWav (dir.getChildFile ("uzun.wav"), 10.0, 44100.0);
            juce::Array<juce::MemoryBlock> played; int stops = 0;
            CuePlayer p;
            p.library().setUserFolder (dir);
            p.setBackend (std::make_unique<FakeBackend> (&played, &stops, "Kulaklik"));
            CueSettings s; s.onId = s.offId = "file:uzun.wav";
            p.setSettings (s);
            double worst = 0.0;
            for (int v = 10; v <= 100; v += 10)
            {
                s.volume = v;
                const double a0 = juce::Time::getMillisecondCounterHiRes();
                p.setSettings (s);
                worst = juce::jmax (worst, juce::Time::getMillisecondCounterHiRes() - a0);
            }
            logMessage ("volume change with a 10 s own file: at most " + juce::String (worst, 1) + " ms");
            expect (worst < 150.0, "a volume change does not level again: " + juce::String (worst, 1) + " ms");
            p.play (true);
            CueLibrary l; l.setUserFolder (dir); juce::AudioBuffer<float> y; double ry = 0;
            expect (l.load ("file:uzun.wav", y, ry) && played.size() == 1 && played[0] == wavOf (y, ry, 100), "the cached sound is the same sound");
            dir.getChildFile ("uzun.wav").deleteFile();
        }

        beginTest ("mixer: fast mute / unmute (10 per second, 1 s) makes no step bigger than the sounds' own");
        {
            CueLibrary lib; juce::AudioBuffer<float> a, b; double r = 0;
            lib.load (CueSettings::defaultOnId(), a, r); lib.load (CueSettings::defaultOffId(), b, r);
            const auto wa = CuePlayer::makeWav (CuePlayer::levelled (a, r), CuePlayer::outputRate, 100);
            const auto wb = CuePlayer::makeWav (CuePlayer::levelled (b, r), CuePlayer::outputRate, 100);
            int rate = 0, ch = 0;
            const int own = juce::jmax (maxStep (pcm (wa, rate, ch), 0, 1 << 30), maxStep (pcm (wb, rate, ch), 0, 1 << 30));

            CueMixer m;
            std::vector<int16_t> outBuf;
            std::vector<int16_t> block (480 * 2);
            for (int k = 0; k < 10; ++k)
            {
                m.start ((k & 1) ? wb : wa);
                for (int j = 0; j < 10; ++j) { m.render (block.data(), 480); outBuf.insert (outBuf.end(), block.begin(), block.end()); }
            }
            while (! m.idle()) { m.render (block.data(), 480); outBuf.insert (outBuf.end(), block.begin(), block.end()); }
            const int mixed = maxStep (outBuf, 0, 1 << 30);
            expect (mixed <= own + 64, "mixed " + juce::String (mixed) + ", the sounds themselves " + juce::String (own));

            // the old way for comparison: cut at 100 ms
            const auto pa = pcm (wa, rate, ch);
            int cut = 0;
            for (int i = 4800; i < 4800 + 48; ++i) cut = juce::jmax (cut, std::abs ((int) pa[(size_t) i * 2]));
            logMessage ("largest step: sounds " + juce::String (own) + ", mixed fast " + juce::String (mixed)
                        + "; cutting Ahşap at 100 ms (PlaySound) would step " + juce::String (cut));
            expect (m.idle() && m.voiceCount() == 0);
        }

        dir.deleteRecursively();
    }
};

static CueTest cueTest;
