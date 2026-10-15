#pragma once
// MicVSTSnapshot only: stands in for src/audio/MicVSTDeviceManager.h with two simple virtual
// devices ("Mikrofon (Test)" / "CABLE Input (Test)"), so the rendered window shows a running
// setup with live status lines on a CI machine without audio hardware.
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <chrono>
#include <cmath>
#include <thread>
#include <vector>

namespace snapshotdevices
{
    class Device final : public juce::AudioIODevice
    {
    public:
        Device (const juce::String& name, bool isInput) : juce::AudioIODevice (name, "Test"), input (isInput) {}
        ~Device() override { close(); }

        juce::StringArray getOutputChannelNames() override { return input ? juce::StringArray() : juce::StringArray { "1", "2" }; }
        juce::StringArray getInputChannelNames() override  { return input ? juce::StringArray { "1", "2" } : juce::StringArray(); }
        juce::Array<double> getAvailableSampleRates() override { return { 48000.0 }; }
        juce::Array<int> getAvailableBufferSizes() override { return { 240, 480, 960 }; }
        int getDefaultBufferSize() override { return 480; }
        juce::String open (const juce::BigInteger& ins, const juce::BigInteger& outs, double, int bs) override
        {
            activeIns = input ? ins : juce::BigInteger();
            activeOuts = input ? juce::BigInteger() : outs;
            block = bs > 0 ? bs : 480;
            opened = true;
            return {};
        }
        void close() override { stop(); opened = false; }
        bool isOpen() override { return opened; }
        void start (juce::AudioIODeviceCallback* cb) override
        {
            if (! opened || cb == nullptr || running) return;
            cb->audioDeviceAboutToStart (this);
            callback = cb;
            running = true;
            thread = std::thread ([this]
            {
                std::vector<float> a ((size_t) block), b ((size_t) block);
                auto next = std::chrono::steady_clock::now();
                int64_t n = 0;
                while (running)
                {
                    next += std::chrono::microseconds ((int64_t) (1.0e6 * block / 48000.0));
                    std::this_thread::sleep_until (next);
                    if (input)
                    {
                        for (int i = 0; i < block; ++i, ++n)
                            a[(size_t) i] = b[(size_t) i] = (float) (0.3 * std::sin (0.13 * (double) n));
                        const float* ins[] = { a.data(), b.data() };
                        callback->audioDeviceIOCallbackWithContext (ins, 2, nullptr, 0, block, {});
                    }
                    else
                    {
                        float* outs[] = { a.data(), b.data() };
                        callback->audioDeviceIOCallbackWithContext (nullptr, 0, outs, 2, block, {});
                    }
                }
            });
        }
        void stop() override
        {
            if (! running) return;
            running = false;
            thread.join();
            if (callback != nullptr) callback->audioDeviceStopped();
            callback = nullptr;
        }
        bool isPlaying() override { return running; }
        juce::String getLastError() override { return {}; }
        int getCurrentBufferSizeSamples() override { return block; }
        double getCurrentSampleRate() override { return 48000.0; }
        int getCurrentBitDepth() override { return 32; }
        juce::BigInteger getActiveOutputChannels() const override { return activeOuts; }
        juce::BigInteger getActiveInputChannels() const override  { return activeIns; }
        int getOutputLatencyInSamples() override { return input ? 0 : 2 * block; }
        int getInputLatencyInSamples() override  { return input ? block : 0; }

    private:
        const bool input;
        bool opened = false;
        std::atomic<bool> running { false };
        juce::BigInteger activeIns, activeOuts;
        int block = 480;
        juce::AudioIODeviceCallback* callback = nullptr;
        std::thread thread;
    };

    class Type final : public juce::AudioIODeviceType
    {
    public:
        Type() : juce::AudioIODeviceType ("Test") {}
        void scanForDevices() override {}
        juce::StringArray getDeviceNames (bool wantInputs) const override
        {
            return wantInputs ? juce::StringArray { juce::String::fromUTF8 ("Mikrofon (HyperX QuadCast 2 S)") }
                              : juce::StringArray { "CABLE Input (VB-Audio Virtual Cable)" };
        }
        int getDefaultDeviceIndex (bool) const override { return 0; }
        int getIndexOfDevice (juce::AudioIODevice* d, bool asInput) const override
        {
            return d == nullptr ? -1 : getDeviceNames (asInput).indexOf (d->getName());
        }
        bool hasSeparateInputsAndOutputs() const override { return true; }
        juce::AudioIODevice* createDevice (const juce::String& outputName, const juce::String& inputName) override
        {
            if (inputName.isNotEmpty() && outputName.isEmpty())  return new Device (inputName, true);
            if (outputName.isNotEmpty() && inputName.isEmpty())  return new Device (outputName, false);
            return nullptr;
        }
    };
}

struct MicVSTDeviceManager : juce::AudioDeviceManager
{
    static constexpr const char* lowLatencyTypeName = "Windows Audio (Low Latency Mode)";
    static constexpr const char* sharedTypeName     = "Windows Audio";
    void createAudioDeviceTypes (juce::OwnedArray<juce::AudioIODeviceType>& types) override { types.add (new snapshotdevices::Type()); }
    juce::String preferredTypeName() { return "Test"; }
};
