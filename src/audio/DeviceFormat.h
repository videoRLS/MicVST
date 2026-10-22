#pragma once
#include <juce_core/juce_core.h>

// The format Windows itself runs an audio endpoint in ("Varsayılan biçim" in the Windows sound
// settings, e.g. "24 bit, 48000 Hz"). MicVST opens its streams in shared mode as 32-bit float
// (JUCE tries float32 first; in shared mode the Windows audio engine mixes in float32), so between
// MicVST and the device Windows converts: float32 -> the endpoint's PCM format on the cable, the
// microphone's PCM format -> float32 on the input. This reads the endpoint format so the log shows
// what really happens on the user's machine (e.g. QuadCast 32 bit in, VB-Cable 24 bit out).
namespace devfmt
{
    struct Format
    {
        bool valid = false;
        bool isFloat = false;
        int containerBits = 0, validBits = 0, sampleRate = 0, channels = 0;
    };

    // A WAVEFORMATEX / WAVEFORMATEXTENSIBLE blob (little-endian bytes, as stored by Windows).
    Format parseWaveFormat (const void* data, size_t size);
    // "24 bit PCM, 48000 Hz, 2 kanal" / "32 bit kayan nokta, ..."
    juce::String describe (const Format&);
    // Windows: the endpoint with this friendly name (as JUCE lists WASAPI devices) and direction.
    // Elsewhere, or not found: invalid.
    Format windowsEndpointFormat (const juce::String& friendlyName, bool capture);
    // Windows: the friendly name of the default output (what Windows plays system sounds on).
    // Elsewhere or none: "".
    juce::String windowsDefaultOutputName();
}
