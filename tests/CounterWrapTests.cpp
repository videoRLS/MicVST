// R1 (test task docs/TEST_PROMPTU_WIN11.md): the 32-bit millisecond counter wraps every 49.7 days.
// The program's timers run here with the counter started just before the wrap (micvst::msCounterOffset):
//  * ReopenBackoff (device retry): due exactly when its pause is over, before and across the wrap,
//    and also when it was scheduled weeks ago (the PC asleep meanwhile)
//  * Recorder: the WAV header is still brought up to date every second after the wrap
//  * CuePlayer: Windows' default output is still looked up every 2 s (not on every call, not never)
//  * the other timers (watchdog back-off, Deneme kaydı play-out, Kazanç save delay, scan time-out)
//    use the same unsigned form: checked for every interval they use, across the wrap
#include <juce_audio_formats/juce_audio_formats.h>
#include "audio/MsCounter.h"
#include "audio/Recorder.h"
#include "audio/CueSounds.h"
#include <cmath>
#include <random>
#include <thread>

namespace
{
    struct OffsetSetter
    {
        // the counter reads (2^32 - msBeforeWrap) now
        explicit OffsetSetter (juce::uint32 msBeforeWrap)
        {
            micvst::msCounterOffset().store ((juce::uint32) (0u - msBeforeWrap) - juce::Time::getMillisecondCounter());
        }
        ~OffsetSetter() { micvst::msCounterOffset().store (0); }
    };

    struct CountingBackend final : CuePlayer::Backend
    {
        std::atomic<int>* lookups;
        explicit CountingBackend (std::atomic<int>* l) : lookups (l) {}
        bool play (const juce::MemoryBlock&) override { return true; }
        void stop() override {}
        juce::String defaultOutputName() override { ++*lookups; return "Kulaklık (USB)"; }
    };
}

class CounterWrapTest final : public juce::UnitTest
{
public:
    CounterWrapTest() : juce::UnitTest ("Counters R1", "MicVST") {}

    void runTest() override
    {
        beginTest ("ReopenBackoff: due exactly when the pause is over - before, across and long after the wrap");
        {
            using B = micvst::ReopenBackoff;
            int wrong = 0;
            std::mt19937 rng (7);
            for (int k = 0; k < 20000; ++k)
            {
                // scheduled at 'at' (around the wrap, or anywhere), pause 1..10 s
                const juce::uint32 at = (k % 2 == 0) ? (juce::uint32) (0u - (juce::uint32) (rng() % 20000)) : (juce::uint32) rng();
                const juce::uint32 pause = 1000u + (juce::uint32) (rng() % 9001);
                B b; b.failures = 1; b.nextMs = at + pause;
                for (juce::uint32 later : { 0u, 1u, pause - 1u, pause, pause + 1u, pause + 5000u, 3600000u, 86400000u * 30u, 0xFFFFF000u })
                {
                    const bool want = later >= pause && later - pause < 0u - B::maxWaitMs;   // until 49.7 days - 10 s after it was due
                    if (b.due (at + later) != want) ++wrong;
                }
            }
            expectEquals (wrong, 0);
            B b; b.failures = 2; b.nextMs = 0xFFFFFF00u + 4000u;   // scheduled 4 s ahead, 256 ms before the wrap
            expect (! b.due (0xFFFFFF00u) && ! b.due (0xFFFFFF00u + 3999u) && b.due (0xFFFFFF00u + 4000u) && b.due (0xFFFFFF00u + 9000u),
                    "4 s pause across the wrap: not before, at once after");
            // the old signed difference: a pause scheduled 30 days earlier looked like one 19 days ahead
            const juce::uint32 now = b.nextMs + 86400000u * 30u;
            expect (b.due (now), "scheduled 30 days ago: due (1.2.1: (int32) (now - next) >= 0 said no for 19 more days)");
            expect ((juce::int32) (now - b.nextMs) < 0, "the 1.2.1 test indeed failed here");
            B fresh;
            expect (fresh.due (12345u), "no failure yet: due at once");
        }

        beginTest ("the unsigned interval checks of the other timers, across the wrap");
        {
            // watchdog back-off (10..60 s), Deneme kaydı play-out (400 ms), Kazanç save delay (400 ms),
            // Recorder (1 s, 5 s), Bildirim sesi (2 s), plug-in scan time-out (120 s): elapsed = now - then
            int wrong = 0;
            for (juce::uint32 interval : { 400u, 1000u, 2000u, 5000u, 10000u, 60000u, 120000u })
                for (juce::uint32 k = 0; k * 97u < 6u * interval; ++k)   // 'then' from 3 intervals before the wrap to 3 after
                    for (juce::uint32 dt : { 0u, interval - 1u, interval, interval + 1u, 2u * interval })
                    {
                        const juce::uint32 then = 0u - 3u * interval + k * 97u, now = then + dt;
                        if ((now - then >= interval) != (dt >= interval)) ++wrong;
                    }
            expectEquals (wrong, 0);
        }

        beginTest ("Recorder: the WAV header up to date every second after the wrap");
        {
            const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("micvst-r1-rec");
            dir.deleteRecursively();
            dir.createDirectory();
            const OffsetSetter wrapSoon (1500);   // wraps 1.5 s into the recording
            Recorder rec;
            const auto err = rec.start (RecordSettings::decode ("wav:24|48000|2", dir.getFullPathName()), 48000.0, 2);
            expect (err.isEmpty(), err);
            if (err.isEmpty())
            {
                std::vector<float> l (480), r (480);
                const float* ch[] = { l.data(), r.data() };
                int64_t n = 0;
                const auto t0 = juce::Time::getMillisecondCounterHiRes();
                double headerAt45 = -1.0;
                for (int block = 0; block < 600; ++block)   // 6 s in real time
                {
                    for (int i = 0; i < 480; ++i, ++n) l[(size_t) i] = r[(size_t) i] = (float) (0.25 * std::sin (0.13 * (double) n));
                    rec.push (ch, 2, 480, 48000.0);
                    const double due = t0 + (block + 1) * 10.0;
                    while (juce::Time::getMillisecondCounterHiRes() < due) juce::Thread::sleep (1);
                    if (block == 449)   // 4.5 s: the header must hold at least 3.5 s (refreshed after the wrap)
                    {
                        juce::WavAudioFormat wav;
                        if (auto* in = new juce::FileInputStream (rec.getFile()); in->openedOk())
                        {
                            std::unique_ptr<juce::AudioFormatReader> rd (wav.createReaderFor (in, true));
                            if (rd != nullptr) headerAt45 = (double) rd->lengthInSamples / 48000.0;
                        }
                        else delete in;
                    }
                }
                const auto res = rec.stop();
                logMessage ("header at 4.5 s (wrap at 1.5 s): " + juce::String (headerAt45, 2) + " s; file " + juce::String (res.seconds, 3) + " s, verified "
                            + (res.verified ? "yes" : "no"));
                expect (headerAt45 >= 3.5, "header refreshed after the wrap: " + juce::String (headerAt45, 2) + " s");
                expect (res.verified && res.droppedFrames == 0 && std::abs (res.seconds - 6.0) < 0.001, "the file whole");
            }
            dir.deleteRecursively();
        }

        beginTest ("CuePlayer: Windows' default output looked up every 2 s across the wrap");
        {
            std::atomic<int> lookups { 0 };
            CuePlayer p;
            p.setBackend (std::make_unique<CountingBackend> (&lookups));
            p.cableName = [] { return juce::String ("CABLE Input (VB-Audio Virtual Cable)"); };
            p.setSettings (CueSettings{});
            const OffsetSetter wrapSoon (2900);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            int calls = 0;
            while (juce::Time::getMillisecondCounterHiRes() - t0 < 7000.0)   // 7 s, a call every 50 ms
            {
                p.play ((calls & 1) != 0);
                ++calls;
                juce::Thread::sleep (50);
            }
            logMessage (juce::String (calls) + " plays in 7 s across the wrap: " + juce::String (lookups.load()) + " look-ups of the default output");
            expect (lookups.load() >= 3 && lookups.load() <= 4, "every 2 s: " + juce::String (lookups.load()) + " look-ups (3..4)");
        }
    }
};

static CounterWrapTest counterWrapTest;
