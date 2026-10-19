#include "audio/BuiltInAutoGain.h"

namespace
{
    const juce::Identifier stateTag ("MicVSTAutoGain"), versionAttr ("version"), enabledAttr ("enabled");
}

BuiltInAutoGainProcessor::BuiltInAutoGainProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("In",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Out", juce::AudioChannelSet::stereo(), true))
{
    setLatencySamples (AutoGainStage::latencyFor (48000.0));   // until prepareToPlay knows the real rate
}

const juce::String BuiltInAutoGainProcessor::getName() const
{
    return juce::String::fromUTF8 ("Otomatik Kazanç");
}

void BuiltInAutoGainProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml (stateTag.toString());
    xml.setAttribute (versionAttr, 1);
    xml.setAttribute (enabledAttr, stage.isEnabled() ? 1 : 0);
    copyXmlToBinary (xml, dest);
}

void BuiltInAutoGainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Unknown or damaged data keeps the current state (on for a new element).
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (stateTag.toString()) && xml->hasAttribute (enabledAttr.toString()))
            stage.setEnabled (xml->getIntAttribute (enabledAttr) != 0);
}
