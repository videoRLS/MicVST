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
            expect (d.enabled && d.onId == "builtin:01_ahsap_ac" && d.offId == "builtin:01_ahsap_kapat" && d.volume == 80 && d.isDefault());
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
            expect (played[0] == CuePlayer::makeWav (on, r, 80), "unmute plays Ahşap açılma");
            expect (played[1] == CuePlayer::makeWav (off, r, 80), "mute plays Ahşap kapanma");
            expect (p.describe (true).startsWith (juce::String::fromUTF8 ("Ahşap")));

            auto s = CueSettings{}; s.enabled = false; p.setSettings (s);
            p.play (true); p.play (false);
            expectEquals (played.size(), 2, "switched off: silent mute / unmute");

            s.enabled = true; s.onId = "builtin:05_kristal_ac"; s.offId = "file:yeni.wav"; p.setSettings (s);
            p.play (false); p.play (true);
            CueLibrary l2; l2.setUserFolder (dir); juce::AudioBuffer<float> k, y; l2.load ("builtin:05_kristal_ac", k, r); double ry = 0; l2.load ("file:yeni.wav", y, ry);
            expect (played[2] == CuePlayer::makeWav (k, r, 80), "chosen built-in");
            expect (played[3] == CuePlayer::makeWav (y, ry, 80), "chosen own file");

            dir.getChildFile ("yeni.wav").deleteFile();          // the chosen file is gone
            p.setSettings (s);
            p.play (true);
            expect (played[4] == CuePlayer::makeWav (off, 48000.0, 80), "missing file: the default kapanma");

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

        dir.deleteRecursively();
    }
};

static CueTest cueTest;
