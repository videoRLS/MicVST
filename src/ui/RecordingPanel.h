#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/AudioEngine.h"

// Settings -> "Kayıt": format, quality, (FLAC) compression, sample rate, channels and the folder
// of the recordings (audio/Recorder.h). Every change is saved at once (config.xml). While a
// recording runs the controls are locked: the file being written keeps its format.
class RecordingPanel final : public juce::Component, private juce::Timer
{
public:
    explicit RecordingPanel (AudioEngine&);
    ~RecordingPanel() override;
    void resized() override;
    int preferredHeight (int width) const;

private:
    void timerCallback() override;
    void refill();                 // the lists for the current format (and rate) and the selections
    void apply (const RecordSettings&);
    void chooseFolder();

    AudioEngine& engine;
    juce::Label formatLabel, qualityLabel, levelLabel, rateLabel, channelLabel, folderLabel;
    juce::ComboBox formatBox, qualityBox, levelBox, rateBox, channelBox;
    juce::Label folderPath, info;
    juce::TextButton folderBtn, openBtn, defaultBtn;
    std::unique_ptr<juce::FileChooser> chooser;
    double shownRate = 0.0;
    bool shownRecording = false;
    juce::Array<int> rateItems;    // rateBox item i+1 -> rate
};
