#pragma once
#include <juce_audio_devices/juce_audio_devices.h>

// AudioDeviceManager mit WASAPI "Low Latency Mode" (IAudioClient3, ab Win 10) als
// bevorzugtem Typ und klassischem Shared als Fallback. Low Latency meldet pro Gerät
// echte Buffer-Größen (min..max in Treiber-Schritten) -> Buffer-Dropdown im DevicePanel;
// Shared meldet genau EINE Größe (Windows-Mixer-Periode), das Dropdown bleibt dann weg.
// Exclusive bleibt bewusst außen vor (lockt Geräte gegen andere Apps).
struct MicVSTDeviceManager : juce::AudioDeviceManager
{
    static constexpr const char* lowLatencyTypeName = "Windows Audio (Low Latency Mode)";
    static constexpr const char* sharedTypeName     = "Windows Audio";

    void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override
    {
       #if JUCE_MAC
        if (auto* coreAudio = juce::AudioIODeviceType::createAudioIODeviceType_CoreAudio())
            types.add (coreAudio);
       #else
        if (auto* ll = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (
                           juce::WASAPIDeviceMode::sharedLowLatency))
            types.add (ll);
        if (auto* shared = juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (
                               juce::WASAPIDeviceMode::shared))
            types.add (shared);
       #endif
    }

    juce::String preferredTypeName()
    {
       #if JUCE_MAC
        if (auto* t = getAvailableDeviceTypes().getFirst())
            return t->getTypeName();   // "CoreAudio"
        return "CoreAudio";
       #else
        for (auto* t : getAvailableDeviceTypes())
            if (t->getTypeName() == lowLatencyTypeName)
                return lowLatencyTypeName;
        return sharedTypeName;
       #endif
    }
};
