#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include "ui/Theme.h"

namespace fx
{
    // One line of live text in an effect's settings window (e.g. the limiter's current reduction),
    // refreshed 8 times a second on the message thread.
    class Readout final : public juce::Component, private juce::Timer
    {
    public:
        explicit Readout (std::function<juce::String()> textIn) : text (std::move (textIn))
        {
            setSize (300, 24);
            startTimerHz (8);
            timerCallback();
        }
        void paint (juce::Graphics& g) override
        {
            g.setColour (micvst::theme::textBright);
            g.setFont (juce::Font (juce::FontOptions (15.0f)));
            g.drawText (shown, getLocalBounds(), juce::Justification::centredLeft);
        }
    private:
        void timerCallback() override
        {
            auto t = text();
            if (t != shown) { shown = t; repaint(); }
        }
        std::function<juce::String()> text;
        juce::String shown;
    };
}
