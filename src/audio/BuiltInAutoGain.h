#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/AutoGain.h"

// Built-in chain element "Otomatik Kazanç" (Auto Gain), next to "Kazanç" in the picker group
// "Dahili". See AutoGain.h for the DSP. PluginChain keeps it at the very front of the chain (before
// every effect and before the manual Kazanç), allows only one, and wires it as a pass-through
// element (it keeps the channel width of the microphone).
//
// State (config.xml): <MicVSTAutoGain version="1" enabled="1"/> - only the on/off switch; the gain
// is learned again from the voice after every start.
class BuiltInAutoGainProcessor final : public juce::AudioProcessor
{
public:
    BuiltInAutoGainProcessor();

    const juce::String getName() const override;

    void prepareToPlay (double sampleRate, int) override { stage.prepare (sampleRate); }
    void releaseResources() override {}
    void reset() override { stage.reset(); }   // PluginChain calls it while the node is suspended
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        stage.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples());
    }

    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }   // controls sit in the chain row
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Message thread (lock-free): on/off; switched off, the gain glides back to 0 dB.
    void setAutoGainEnabled (bool on) noexcept { stage.setEnabled (on); }
    bool isAutoGainEnabled() const noexcept    { return stage.isEnabled(); }
    AutoGainStage::Meters takeMeters() noexcept { return stage.takeMeters(); }

private:
    AutoGainStage stage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltInAutoGainProcessor)
};
