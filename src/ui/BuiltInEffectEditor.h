#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

class BuiltInEffect;

// Settings window of a built-in effect: its description, an optional live display (meters), then
// one row per parameter (name + slider / list / switch, all bound to the JUCE parameters, so the
// plug-in thread sees every change at once and glides to it) and "Varsayılanlara dön".
// Double-click on a slider returns it to its default.
class BuiltInEffectEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BuiltInEffectEditor (BuiltInEffect&);
    ~BuiltInEffectEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int width = 460, rowH = 30, labelW = 170;

private:
    struct Row;
    BuiltInEffect& effect;
    juce::Label description;
    std::unique_ptr<juce::Component> display;
    juce::OwnedArray<Row> rows;
    juce::TextButton defaultsBtn;
    int descriptionH = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BuiltInEffectEditor)
};
