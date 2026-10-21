#include <juce_core/juce_core.h>
#include "audio/DeviceFormat.h"
#include <vector>

// The endpoint formats Windows reports (PKEY_AudioEngine_DeviceFormat): WAVEFORMATEX and
// WAVEFORMATEXTENSIBLE blobs as Windows stores them, read without the Windows headers.
struct DeviceFormatTest : juce::UnitTest
{
    DeviceFormatTest() : juce::UnitTest ("Device formats") {}

    static void put16 (std::vector<juce::uint8>& b, int v) { b.push_back ((juce::uint8) v); b.push_back ((juce::uint8) (v >> 8)); }
    static void put32 (std::vector<juce::uint8>& b, juce::uint32 v) { for (int i = 0; i < 4; ++i) b.push_back ((juce::uint8) (v >> (8 * i))); }
    static std::vector<juce::uint8> wave (int tag, int ch, int rate, int bits, int validBits = 0, int subTag = 0, bool goodGuid = true)
    {
        std::vector<juce::uint8> b;
        put16 (b, tag); put16 (b, ch); put32 (b, (juce::uint32) rate);
        put32 (b, (juce::uint32) (rate * ch * bits / 8)); put16 (b, ch * bits / 8); put16 (b, bits);
        if (tag == 0xFFFE)
        {
            put16 (b, 22); put16 (b, validBits); put32 (b, 3);
            put32 (b, (juce::uint32) subTag);
            const juce::uint8 tail[12] = { 0x00, 0x00, 0x10, 0x00, 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, goodGuid ? (juce::uint8) 0x71 : (juce::uint8) 0x72 };
            b.insert (b.end(), tail, tail + 12);
        }
        else put16 (b, 0);
        return b;
    }

    void runTest() override
    {
        using namespace devfmt;
        beginTest ("the usual endpoint formats");
        {
            auto cable = wave (0xFFFE, 2, 48000, 24, 24, 1);              // VB-Cable set to 24 bit, 48000 Hz
            auto f = parseWaveFormat (cable.data(), cable.size());
            expect (f.valid && ! f.isFloat && f.validBits == 24 && f.containerBits == 24 && f.sampleRate == 48000 && f.channels == 2);
            expectEquals (describe (f), juce::String ("24 bit PCM, 48000 Hz, 2") + micvst::tr (" kanal"));

            auto mic = wave (0xFFFE, 2, 48000, 32, 32, 1);                // a microphone in 32 bit PCM
            f = parseWaveFormat (mic.data(), mic.size());
            expect (f.valid && ! f.isFloat && f.validBits == 32 && f.sampleRate == 48000);

            auto padded = wave (0xFFFE, 2, 96000, 32, 24, 1);             // 24 valid bits in 32-bit containers
            f = parseWaveFormat (padded.data(), padded.size());
            expect (f.valid && f.validBits == 24 && f.containerBits == 32 && f.sampleRate == 96000);
            expect (describe (f).contains ("24 bit PCM") && describe (f).contains ("32"));

            auto flt = wave (0xFFFE, 2, 48000, 32, 32, 3);                // 32 bit float
            f = parseWaveFormat (flt.data(), flt.size());
            expect (f.valid && f.isFloat && f.validBits == 32);

            auto plain16 = wave (1, 1, 44100, 16);                        // WAVE_FORMAT_PCM, mono
            f = parseWaveFormat (plain16.data(), plain16.size());
            expect (f.valid && ! f.isFloat && f.validBits == 16 && f.channels == 1 && f.sampleRate == 44100);

            auto plainFloat = wave (3, 2, 48000, 32);                     // WAVE_FORMAT_IEEE_FLOAT
            f = parseWaveFormat (plainFloat.data(), plainFloat.size());
            expect (f.valid && f.isFloat);
        }

        beginTest ("damaged or unknown blobs are never misread");
        {
            auto ok = wave (0xFFFE, 2, 48000, 24, 24, 1);
            expect (! parseWaveFormat (ok.data(), 10).valid, "truncated");
            expect (! parseWaveFormat (ok.data(), 30).valid, "extensible without its tail");
            expect (! parseWaveFormat (nullptr, 40).valid);
            auto badGuid = wave (0xFFFE, 2, 48000, 24, 24, 1, false);
            expect (! parseWaveFormat (badGuid.data(), badGuid.size()).valid, "unknown sub-format GUID");
            auto otherTag = wave (0x0055, 2, 48000, 16);                  // MP3 tag
            expect (! parseWaveFormat (otherTag.data(), otherTag.size()).valid);
            auto validOver = wave (0xFFFE, 2, 48000, 16, 24, 1);          // 24 valid bits in 16-bit containers
            expect (! parseWaveFormat (validOver.data(), validOver.size()).valid);
            expectEquals (describe (Format()), micvst::tr ("bilinmiyor"));
        }

       #if ! JUCE_WINDOWS
        beginTest ("no Windows endpoints elsewhere");
        expect (! windowsEndpointFormat ("CABLE Input (VB-Audio Virtual Cable)", false).valid);
       #endif
    }
};

static DeviceFormatTest deviceFormatTest;
