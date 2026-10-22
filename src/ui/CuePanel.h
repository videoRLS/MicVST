#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/AudioEngine.h"

// Settings -> "Bildirim sesleri": the sound when the microphone is muted / unmuted. On or off, the
// unmute and the mute sound chosen separately (each with a listen button), volume, back to the
// default (Ahşap), and the folder for own sounds - new files there appear in the lists by
// themselves (checked every 2 s while the settings are open). Every change is saved at once.
class CuePanel final : public juce::Component, private juce::Timer
{
public:
    explicit CuePanel (AudioEngine&);
    ~CuePanel() override;
    void resized() override;
    int preferredHeight (int width) const;

private:
    void timerCallback() override;
    void refill();                            // the lists (built-in + the folder) and the selections
    void apply (const CueSettings&);

    AudioEngine& engine;
    juce::ToggleButton enabledToggle;
    juce::Label onLabel, offLabel, volumeLabel, hint;
    juce::ComboBox onBox, offBox;
    juce::TextButton onPlay, offPlay, defaultBtn, folderBtn;
    juce::Slider volume;
    juce::StringArray ids;                    // combo item i+1 -> sound id
    juce::String shownList;                   // what the lists were built from (rebuilt when it changes)
};
