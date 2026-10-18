#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/GainStage.h"

// Built-in chain element "Kazanç" (gain), next to "Mono -> Stereo" / "Stereo -> Mono". See
// GainStage.h for the DSP and its thread-safety rules.
//
// Channels: the processor has two inputs and two outputs, but PluginChain wires it as a
// pass-through element (NodeChannels::passThrough): it takes the width of whatever feeds it, so a
// mono part of the chain stays mono behind it and a stereo part stays stereo.
//
// State (config.xml, the plug-in's state blob): <MicVSTGain version="1" gainDb="6.0"/>.
class BuiltInGainProcessor final : public juce::AudioProcessor
{
public:
    BuiltInGainProcessor();

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

    juce::AudioProcessorEditor* createEditor() override { return nullptr; }   // the slider sits in the chain row
    bool hasEditor() const override { return false; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void* data, int sizeInBytes) override;   // jumps (no glide): restore only

    // Live change from the UI (message thread): lock-free; the plug-in thread glides to it.
    void  setGainDb (float db) noexcept { stage.setGainDb (db); }
    float getGainDb() const noexcept    { return stage.getGainDb(); }

private:
    GainStage stage;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltInGainProcessor)
};
