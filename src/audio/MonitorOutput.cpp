#include "audio/MonitorOutput.h"
#include "audio/RealtimeSupport.h"
#include "Text.h"
#include <cmath>

MonitorOutput::MonitorOutput()
{
    // nothing is opened here: the device type is chosen (and a device opened) only in open()
    bridge.setSafety (AudioBridge::autoStartMs, true);
}

juce::AudioIODeviceType* MonitorOutput::type()
{
    auto& types = devices.getAvailableDeviceTypes();
    for (auto* t : types)
        if (t->getTypeName() == MicVSTDeviceManager::sharedTypeName)   // shared: never exclusive
            return t;
    return types.getFirst();
}

MonitorOutput::~MonitorOutput()
{
    close();
}

void MonitorOutput::setDeviceName (const juce::String& name)
{
    if (name == deviceName) return;
    deviceName = name;
    if (isOpen()) { close(); open(); }
}

juce::StringArray MonitorOutput::getDeviceNames()
{
    if (auto* t = type())
    {
        t->scanForDevices();
        return t->getDeviceNames (false);
    }
    return {};
}

juce::String MonitorOutput::open()
{
    if (isOpen()) return {};
    auto* t = type();
    if (t == nullptr) return micvst::tr ("Ses cihazı açılamadı");
    t->scanForDevices();
    const auto names = t->getDeviceNames (false);
    juce::String name = deviceName;
    if (name.isEmpty() || ! names.contains (name))
    {
        if (name.isNotEmpty())
            return micvst::tr ("Cihaz bulunamadı: ") + name;
        const int def = t->getDefaultDeviceIndex (false);
        name = juce::isPositiveAndBelow (def, names.size()) ? names[def] : names[0];
    }
    if (name.isEmpty()) return micvst::tr ("Cihaz bulunamadı: ") + micvst::tr ("(yok)");
    if (devices.getCurrentAudioDeviceType() != t->getTypeName())
        devices.setCurrentAudioDeviceType (t->getTypeName(), true);

    auto setup = devices.getAudioDeviceSetup();
    setup.inputDeviceName = {};
    setup.outputDeviceName = name;
    setup.useDefaultInputChannels = false;
    setup.useDefaultOutputChannels = false;
    setup.inputChannels.clear();
    setup.outputChannels.clear();
    setup.outputChannels.setRange (0, 2, true);
    setup.sampleRate = 0.0;   // the device's own rate: the bridge resamples
    setup.bufferSize = 0;
    const auto err = devices.setAudioDeviceSetup (setup, true);
    if (err.isNotEmpty())
    {
        devices.closeAudioDevice();
        juce::Logger::writeToLog (micvst::tr ("Kulaklık çıkışı açılamadı: ") + micvst::deviceErrorText (err));
        return micvst::deviceErrorText (err);
    }
    devices.addAudioCallback (this);
    juce::Logger::writeToLog (micvst::tr ("Kulaklık çıkışı açıldı: ") + name);
    return {};
}

void MonitorOutput::close()
{
    devices.removeAudioCallback (this);
    devices.closeAudioDevice();
}

bool MonitorOutput::isOpen() const
{
    auto* d = devices.getCurrentAudioDevice();
    return d != nullptr && d->isOpen();
}

juce::String MonitorOutput::getOpenDeviceName() const
{
    auto* d = devices.getCurrentAudioDevice();
    return d != nullptr ? d->getName() : juce::String();
}

void MonitorOutput::push (const float* const* data, int numFrames, double captureTime, double now) noexcept
{
    if (running.load (std::memory_order_relaxed))
        bridge.push (data, 2, numFrames, captureTime, now);
}

void MonitorOutput::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    const double sr = d->getCurrentSampleRate();
    bridge.setOutputRate (sr);
    bridge.setConsumerActive (true);
    fade = 0.0;
    fadeStep = 1.0 / std::max (1.0, 0.02 * sr);   // 20 ms for the whole way
    level = std::pow (10.0, (double) juce::jlimit (minVolumeDb, maxVolumeDb, volumeDb.load()) / 20.0);
    levelCoeff = std::exp (-1.0 / std::max (1.0, 0.01 * sr));
    running.store (true);
}

void MonitorOutput::audioDeviceStopped()
{
    running.store (false);
    bridge.setConsumerActive (false);
}

void MonitorOutput::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut,
                                                      int numSamples, const juce::AudioIODeviceCallbackContext&)
{
    rt::configureAudioThread();
    const juce::ScopedNoDenormals noDenormals;
    bridge.pull (out, numOut, numSamples, juce::Time::getMillisecondCounterHiRes() * 0.001);
    const double fadeTarget = audible.load (std::memory_order_relaxed) ? 1.0 : 0.0;
    const double levelTarget = std::pow (10.0, (double) juce::jlimit (minVolumeDb, maxVolumeDb, volumeDb.load (std::memory_order_relaxed)) / 20.0);
    for (int i = 0; i < numSamples; ++i)
    {
        if (fade < fadeTarget)      fade = std::min (fadeTarget, fade + fadeStep);
        else if (fade > fadeTarget) fade = std::max (fadeTarget, fade - fadeStep);
        level = levelTarget + (level - levelTarget) * levelCoeff;
        // on/off: raised-cosine shaped (no corner at the ends); the volume itself is applied exactly
        const double g = (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * fade)) * level;
        for (int c = 0; c < numOut; ++c)
            if (out[c] != nullptr) out[c][i] = (float) (out[c][i] * g);
    }
}
