#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/Visualiser.h"

// The main window's two views of micvst::viz::Feed (interface only, see Visualiser.h). Both are
// opaque, keep their still parts (screen, grid, labels, frame) in cached images rebuilt only on a
// size, scale, theme or drawing-engine change, and draw only the moving parts per frame.

// Spectrum in the level card: logarithmic 20 Hz - 20 kHz, the input (before the chain, muted grey)
// under the output (what goes to the cable, the theme's accent), drawn with a 4.5 dB/octave tilt
// around 1 kHz so speech and music look balanced.
class SpectrumView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    explicit SpectrumView (const micvst::viz::Feed&);
    void paint (juce::Graphics&) override;
    void resized() override;
    int pointsWanted() const;     // one point every 2 px of the plot

    static constexpr float topDb = -12.0f, bottomDb = -100.0f, tiltDbPerOctave = 4.5f;
    static constexpr double fMin = 20.0, fMax = 20000.0;

private:
    float xOf (double hz) const;
    float yOf (float db, double hz) const;
    void buildImages (float scale, int engine);
    void drawCurve (juce::Graphics&, const std::vector<float>&, juce::Colour fill, juce::Colour line, float lineWidth, bool glow);

    const micvst::viz::Feed& feed;
    juce::Rectangle<float> plot;
    juce::Image under, over;      // screen + grid / corner masks, frame, labels, legend
    float imageScale = 0.0f;
    int imageEngine = -2;
    juce::uint32 imageTheme = 0;
    std::vector<juce::Point<float>> pts;
};

// Input and output levels as vertical meters (left / right each) under the chain: peak and RMS
// bars, the held peak, the clip light, the held peak in dB.
class LevelMetersView final : public juce::Component
{
public:
    explicit LevelMetersView (const micvst::viz::Feed&);
    void paint (juce::Graphics&) override;
    void resized() override;
    static constexpr int preferredHeight = 96;
    static constexpr float minDb = -60.0f;

private:
    struct Group { juce::Rectangle<int> title, value, bars[2], clip[2], scale; };
    void buildImage (float scale, int engine);
    float yOf (float db, const juce::Rectangle<int>& bar) const;

    const micvst::viz::Feed& feed;
    Group groups[2];
    juce::Image back;
    float imageScale = 0.0f;
    int imageEngine = -2;
    juce::uint32 imageTheme = 0;
};
