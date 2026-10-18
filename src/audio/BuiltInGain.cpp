#include "audio/BuiltInGain.h"

namespace
{
    const juce::Identifier stateTag ("MicVSTGain"), versionAttr ("version"), gainAttr ("gainDb");
}

BuiltInGainProcessor::BuiltInGainProcessor()
    : juce::AudioProcessor (BusesProperties()
          .withInput  ("In",  juce::AudioChannelSet::stereo(), true)
          .withOutput ("Out", juce::AudioChannelSet::stereo(), true))
{
}

const juce::String BuiltInGainProcessor::getName() const
{
    return juce::String::fromUTF8 ("Kazanç");
}

void BuiltInGainProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    juce::XmlElement xml (stateTag.toString());
    xml.setAttribute (versionAttr, 1);
    xml.setAttribute (gainAttr, (double) stage.getGainDb());
    copyXmlToBinary (xml, dest);
}

void BuiltInGainProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Unknown or damaged data keeps the current value (0 dB for a new element).
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (stateTag.toString()) && xml->hasAttribute (gainAttr.toString()))
            stage.setGainDb ((float) xml->getDoubleAttribute (gainAttr), true);
}
