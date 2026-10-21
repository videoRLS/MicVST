#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/GlobalHotkeys.h"

// "Kısayollar": one line per shortcut - on/off, the key (click, then press the new key; Esc
// cancels) and "Varsayılan". Changes apply at once (setBinding registers the key with Windows and
// saves; false = Windows refused the key, shown in red).
class HotkeyDialog final : public juce::Component
{
public:
    using Setter = std::function<bool (GlobalHotkeys::Action, const GlobalHotkeys::Binding&)>;
    HotkeyDialog (GlobalHotkeys& keys, Setter setBinding);
    ~HotkeyDialog() override;

    void resized() override;
    void paint (juce::Graphics&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static juce::String actionName (GlobalHotkeys::Action);

private:
    struct Line
    {
        juce::ToggleButton enabled;
        juce::Label name;
        juce::TextButton key, reset;
        juce::Label problem;
    };
    void refresh();
    void endCapture();
    void apply (GlobalHotkeys::Action, const GlobalHotkeys::Binding&);

    GlobalHotkeys& keys;
    Setter setBinding;
    Line lines[GlobalHotkeys::numActions];
    juce::Label hint;
    int capturing = -1;   // action waiting for a key
};
