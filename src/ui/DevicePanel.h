#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"
#include "ui/Theme.h"

// Kleines "?"-Icon, das bei Hover einen Hilfetext zeigt (via TooltipWindow im MainComponent).
struct InfoIcon : juce::Component, juce::SettableTooltipClient
{
    void paint (juce::Graphics& g) override
    {
        auto b = getLocalBounds().toFloat();
        const float d = juce::jmin (b.getWidth(), b.getHeight()) - 1.5f;   // Durchmesser = kleinere Seite
        auto circle = juce::Rectangle<float> (d, d).withCentre (b.getCentre());
        g.setColour (micvst::theme::textDim);
        g.drawEllipse (circle, 1.2f);
        g.setColour (micvst::theme::text);
        g.setFont (d * 0.66f);
        g.drawText ("?", getLocalBounds(), juce::Justification::centred);
    }
};

// Kompakte Geräteauswahl: Input zuerst, dann Output (Output kennt "(none)").
// Darunter optional eine Buffer-Zeile: nur sichtbar, wenn das Mikrofon im
// Low-Latency-Modus mehr als eine Buffer-Größe meldet ("Auto" = Geräte-Default).
// Dann die Safety-Zeile (Sicherheitspuffer Mikrofon -> Kabel, "Auto" = adaptiv) und (nur Windows)
// die Zeile für die Prozess-Priorität (Normal / Normalin Üstünde / Yüksek).
// Zuunterst zwei dauerhaft sichtbare Info-Zeilen (Active / Geräte / Latenz und
// CPU-Last / Safety / Drift / Aussetzer).
// Bewusst KEIN Input-Pegelmeter (oben gibt es den großen) und keine Kanal-Listen
// (MicVST verwaltet die Kanäle selbst auf Stereo).
class DevicePanel : public juce::Component,
                    private juce::ChangeListener,
                    private juce::Timer
{
public:
    explicit DevicePanel (AudioEngine&);
    ~DevicePanel() override;
    void resized() override;

    int preferredHeight() const;   // Geräte-/Einstellungszeilen + 2 Info-Zeilen

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void refresh();        // Input/Output-Combos aus dem aktuellen Setup neu füllen
    void updateStatus();   // nur die Info-Zeilen (Geräte / Latenz, CPU-Last / Aussetzer)
    void apply();          // aktuelle Auswahl -> AudioEngine
    void applySafety();    // Safety-Combo -> AudioEngine (ohne Geräte-Neustart)
    void applyPriority();  // Prioritäts-Combo -> AudioEngine (sofort, aus Windows zurückgelesen)
    void syncPriority();   // Combo = die Klasse, die der Prozess gerade wirklich hat (auch nach Task-Manager)

    AudioEngine& engine;
    juce::Label    inLabel, outLabel;     // Texte im Konstruktor (UTF-8, siehe Text.h)
    InfoIcon       inInfo, outInfo;
    juce::ComboBox inBox, outBox;
    juce::Label    bufLabel;
    InfoIcon       bufInfo;
    juce::ComboBox bufBox;
    juce::Label    safetyLabel;
    InfoIcon       safetyInfo;
    juce::ComboBox safetyBox;
    juce::Label    priorityLabel;   // İşlem önceliği (Windows): Normal / Normalin Üstünde / Yüksek
    InfoIcon       priorityInfo;
    juce::ComboBox priorityBox;
    int            shownPriorityProblems = 0;
    juce::Label    monitorLabel;  // Kulaklık: output for "Kendini dinle" and the test recording + volume
    InfoIcon       monitorInfo;
    juce::ComboBox monitorBox;
    juce::Slider   monitorVol;
    juce::StringArray monitorNames;
    juce::Label    statusLabel;   // Active / Geräte / Latenz (immer sichtbar)
    juce::Label    statsLabel;    // CPU-Last / Safety / Drift / Aussetzer (immer sichtbar)

    bool updating = false;        // Re-entrancy-Guard beim Befüllen
};
