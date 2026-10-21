#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/AudioEngine.h"
#include "ui/DevicePanel.h"
#include "ui/GlobalHotkeys.h"
#include "ui/HotkeyDialog.h"

// "Ayarlar": everything that is set once and then left alone, out of the main window - sound
// (buffer, safety margin, process priority, headphones), shortcut keys, language, theme and
// start with Windows, the VST3 folders and the about line with the Relios signature. A list of
// sections on the left scrolls the page to that section.
class SettingsComponent final : public juce::Component, private juce::Timer
{
public:
    struct Callbacks
    {
        std::function<void (const juce::String&)> chooseTheme, chooseLanguage;
        std::function<void (juce::Component* target)> showFolderMenu;
        std::function<void()> showHowTo;
        HotkeyDialog::Setter setHotkey;
    };
    SettingsComponent (AudioEngine&, GlobalHotkeys*, Callbacks);
    ~SettingsComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static juce::String windowTitle();

    // Where the page is scrolled to: the section at the top and how far into it. Kept when the
    // window is rebuilt (theme or language changed: the section positions move with the texts),
    // so a change never throws the viewer to another section.
    struct ViewState { int section = 0; int offset = 0; };
    ViewState getViewState() const;
    void setViewState (ViewState);

private:
    void timerCallback() override;   // keeps "start with Windows" in step with the registry
    void scrollTo (int section);
    void updateNav();   // the section button of what is shown is lit
    int  sectionAt (int y) const;

    struct Page : juce::Component { void paint (juce::Graphics&) override; std::vector<juce::Rectangle<int>> lines; };

    AudioEngine& engine;
    Callbacks cb;
    juce::OwnedArray<juce::TextButton> navButtons;
    juce::Viewport viewport;
    Page page;
    juce::OwnedArray<juce::Label> headings;
    DevicePanel soundPanel;
    std::unique_ptr<HotkeyDialog> hotkeys;
    juce::Label noHotkeys;
    juce::Label languageLabel, themeLabel;
    juce::ComboBox languageBox, themeBox;
    juce::ToggleButton autostartToggle;
    juce::TextButton foldersBtn;
    juce::Label foldersHint;
    juce::Label aboutLine, signature;
    juce::TextButton howToBtn;
    std::vector<int> sectionY;
};
