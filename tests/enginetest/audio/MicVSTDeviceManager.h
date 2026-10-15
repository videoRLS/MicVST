#pragma once
// Engine-test stand-in for src/audio/MicVSTDeviceManager.h (tests/enginetest comes first on the
// include path of MicVSTEngineTest): same interface, virtual devices with WASAPI-like timing
// (VirtualDevices.h) instead of WASAPI - CI machines have no audio hardware.
#include <juce_audio_devices/juce_audio_devices.h>
#include "../VirtualDevices.h"
struct MicVSTDeviceManager : juce::AudioDeviceManager
{
    static constexpr const char* lowLatencyTypeName = "Windows Audio (Low Latency Mode)";
    static constexpr const char* sharedTypeName     = "Windows Audio";
    void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override
    {
        types.add (new vdev::VirtualType());
    }
    juce::String preferredTypeName() { return "Virtual"; }
};
