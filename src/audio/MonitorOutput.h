#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include "audio/AudioBridge.h"
#include "audio/MicVSTDeviceManager.h"

// "Kendini dinle" (monitoring): a third, optional audio device - usually headphones - that gets the
// same processed audio as the cable (after the mute and the A/B switch), or the test recording while
// it plays back. It has its own device thread and its own AudioBridge (lock-free FIFO + drift-
// compensating resampler, as for the cable), so a slow or missing headphone device never disturbs
// the cable. Opened in WASAPI shared mode only: other programs keep using the headphones.
//
// The device is open only while it is needed (monitoring on, or a test recording playing back);
// the volume and on/off glide (20 ms), so switching never clicks.
class MonitorOutput final : private juce::AudioIODeviceCallback
{
public:
    MonitorOutput();
    ~MonitorOutput() override;

    // Message thread. Empty name = Windows' default output device.
    void setDeviceName (const juce::String& name);
    const juce::String& getDeviceName() const { return deviceName; }
    // Opens / closes the device (message thread). Returns an error text ("" = fine).
    juce::String open();
    void close();
    bool isOpen() const;
    juce::String getOpenDeviceName() const;
    juce::StringArray getDeviceNames();   // output devices of this computer (shared mode)

    void setVolumeDb (float db) noexcept { volumeDb.store (db); }
    float getVolumeDb() const noexcept   { return volumeDb.load(); }
    void setAudible (bool on) noexcept   { audible.store (on); }   // false: glides to silence

    // Plug-in thread.
    void setInputRate (double hz) { bridge.setInputRate (hz); }
    void push (const float* const* data, int numFrames, double captureTime, double now) noexcept;

    static constexpr float minVolumeDb = -40.0f, maxVolumeDb = 6.0f;

private:
    void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override;
    void audioDeviceAboutToStart (juce::AudioIODevice*) override;
    void audioDeviceStopped() override;

    juce::AudioIODeviceType* type();   // WASAPI shared (the first type if there is none)
    MicVSTDeviceManager devices;
    AudioBridge bridge;
    juce::String deviceName;
    std::atomic<float> volumeDb { 0.0f };
    std::atomic<bool> audible { false };
    std::atomic<bool> running { false };
    // device thread: on/off fade (0..1, raised-cosine shaped, 20 ms) and the volume as a plain gain
    // factor that glides (10 ms) - kept apart, so the volume is exactly the dB value set
    double fade = 0.0, fadeStep = 0.001;
    double level = 1.0, levelCoeff = 0.99;
};
