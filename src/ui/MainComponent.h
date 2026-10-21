#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"
#include "ui/LevelMeterComponent.h"
#include "ui/PluginListView.h"
#include "ui/DevicePanel.h"
#include "state/AutostartRegistry.h"
#include "ui/GlobalHotkeys.h"

// MicVST 1.2: keinerlei Netzwerkcode - kein Update-Check, keine klickbaren Web-Links
// (URLs nur noch als reiner Text).
class MainComponent : public juce::Component, private juce::Timer
{
public:
    explicit MainComponent (AudioEngine& engine);
    ~MainComponent() override;
    void resized() override;

    // Inhalt + Titel des Anleitungsfensters (auch für die Oberflächen-Schnappschüsse der Tests).
    static juce::String howToTitle();
    static std::unique_ptr<juce::Component> createHowToContent();

    // The app's shortcut keys (they outlive this window content); changes are saved by the app.
    void setHotkeys (GlobalHotkeys* keys, std::function<bool (GlobalHotkeys::Action, const GlobalHotkeys::Binding&)> setter);
    PluginListView& getPluginList() { return *pluginList; }

private:
    void timerCallback() override;
    void updateCableHint();   // zeigt den VB-Cable-Hinweis, wenn kein virtuelles Kabel installiert ist
    void showHowTo();         // öffnet das How-To-Fenster
    AudioEngine& engine;
    std::unique_ptr<DevicePanel> devicePanel;
    LevelMeterComponent inMeter, outMeter;
    DbScaleComponent dbScale;
    juce::Label inLabel, outLabel;       // "Giriş" / "Çıkış" (Texte im Konstruktor, UTF-8)
    std::unique_ptr<PluginListView> pluginList;
    juce::Label versionLabel;            // Versionsnummer (reiner Text, kein Link)
    juce::TextButton howToBtn;           // "Kullanım"
    std::unique_ptr<juce::DocumentWindow> howToWindow;
    juce::ComboBox languageBox;          // interface language (Text.h); the app rebuilds the window
    juce::ComboBox themeBox;             // colour theme (ui/Theme.h); the app rebuilds the window
public:
    std::function<void (const juce::String& themeId)> onThemeChosen;      // set by the window
    std::function<void (const juce::String& languageId)> onLanguageChosen;
private:
    juce::ToggleButton autostartToggle;  // "Açılışta başlat"

    // Action row under the devices: Sustur, Ham ses (A/B), Kendini dinle, Deneme kaydı, Kısayollar
    juce::TextButton muteBtn, compareBtn, monitorBtn, testBtn, hotkeysBtn;
    void updateActionButtons();
    void showHotkeys();
    GlobalHotkeys* hotkeys = nullptr;
    std::function<bool (GlobalHotkeys::Action, const GlobalHotkeys::Binding&)> setHotkey;
    juce::Label cableHint;               // VB-Cable-Hinweis (reiner Text, kein Link)
    juce::TooltipWindow tooltipWindow { nullptr, 100 };   // Hover-Tooltips, fast instant (100 ms)
    int timerTicks = 0;                  // Registry-Abgleich nur jede Sekunde
};
