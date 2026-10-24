#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"
#include "ui/VisualiserViews.h"
#include "ui/PluginListView.h"
#include "ui/DevicePanel.h"
#include "state/AutostartRegistry.h"
#include "ui/GlobalHotkeys.h"

// The big mute button of the main window: microphone icon, "Sustur" / "Sesi aç" and the shortcut
// key; red while muted.
class MuteButton final : public juce::Button
{
public:
    MuteButton();
    void setState (bool muted, const juce::String& keyText);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    bool muted = false;
    juce::String keyText;
};

// The main window (MicVST 1.3 layout): what is used every day is in front - the levels with a big
// mute button, microphone and cable, presets and the chain, A/B, monitoring, the test recording
// and Kayıt (recording to a file).
// Everything set once (buffer, safety margin, priority, headphones, shortcut keys, language, theme,
// VST3 folders) is in the settings window (SettingsComponent), opened with "Ayarlar".
// MicVST has no network code at all - no update check, no clickable web links.
class MainComponent : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (AudioEngine& engine);
    ~MainComponent() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    // Inhalt + Titel des Anleitungsfensters (auch für die Oberflächen-Schnappschüsse der Tests).
    static juce::String howToTitle();
    static std::unique_ptr<juce::Component> createHowToContent();
    void showHowTo();         // öffnet das How-To-Fenster

    // The app's shortcut keys (they outlive this window content): shown on the buttons.
    void setHotkeys (GlobalHotkeys* keys);
    PluginListView& getPluginList() { return *pluginList; }
    std::function<void()> onShowSettings;   // set by the window
    void toggleRecording();                 // Kayıt button (also the tray menu)
    // Spectrum and level meters: one analysed and drawn frame at this time (seconds, monotonic).
    // Called on the screen's refresh (VBlankAttachment) at the FPS of Ayarlar > Arayüz akıcılığı;
    // public for the UI snapshots, which have no screen.
    void updateVisuals (double timeSec);
    // drawn frames and the longest pause between two of them (the UI drawing test)
    struct VisualStats { int frames = 0; double longestGapMs = 0.0; };
    VisualStats getVisualStats() const { return visualStats; }
    void resetVisualStats()            { visualStats = {}; lastFrameAt = 0.0; }

private:
    void timerCallback() override;
    void onVBlank (double timeSec);
    bool visualsShown() const;
    void updateCableHint();   // zeigt den VB-Cable-Hinweis, wenn kein virtuelles Kabel installiert ist
    void updateActionButtons();

    AudioEngine& engine;
    micvst::viz::Feed feed;                  // input / output spectrum and levels (interface only)
    SpectrumView spectrum { feed };
    LevelMetersView meters { feed };
    juce::VBlankAttachment vblank;
    double nextFrameAt = 0.0, lastVBlank = 0.0, vblankInterval = 1.0 / 60.0;
    bool visualsIdle = false;
    VisualStats visualStats;
    double lastFrameAt = 0.0;
    juce::Rectangle<int> levelCard;
    MuteButton muteBtn;
    std::unique_ptr<DevicePanel> devicePanel;
    std::unique_ptr<PluginListView> pluginList;
    juce::TextButton compareBtn, monitorBtn, testBtn, recordBtn, helpBtn, settingsBtn;
    void showRecordingResult (const Recorder::Result&, const juce::String& why);
    std::unique_ptr<juce::BubbleMessageComponent> recordBubble;
    std::unique_ptr<juce::DocumentWindow> howToWindow;
    GlobalHotkeys* hotkeys = nullptr;
    juce::Label cableHint;               // VB-Cable-Hinweis (reiner Text, kein Link)
    juce::TooltipWindow tooltipWindow { nullptr, 100 };   // Hover-Tooltips, fast instant (100 ms)
};
