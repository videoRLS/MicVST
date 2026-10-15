#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"
#include "ui/LevelMeterComponent.h"
#include "ui/PluginListView.h"
#include "ui/DevicePanel.h"
#include "state/AutostartRegistry.h"

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
    juce::ToggleButton autostartToggle;  // "Açılışta başlat"
    juce::Label cableHint;               // VB-Cable-Hinweis (reiner Text, kein Link)
    juce::TooltipWindow tooltipWindow { nullptr, 100 };   // Hover-Tooltips, fast instant (100 ms)
    int timerTicks = 0;                  // Registry-Abgleich nur jede Sekunde
};
