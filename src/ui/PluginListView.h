#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"

// Plugin-Kette als Liste. Pro Zeile (von links nach rechts):
//   - Drag-Handle (Punkte) zum Umsortieren per Drag&Drop,
//   - Plugin-Name (Doppelklick öffnet den Editor),
//   - gemeldete Latenz des Plugins (getLatencySamples),
//   - Bypass-Schalter,
//   - Mülleimer-Icon zum Entfernen.
// Oben ein "+ Plugin"-Button (öffnet den Such-Picker; enthält auch die Built-ins).
class PluginListView : public juce::Component, private juce::Timer
{
public:
    explicit PluginListView (AudioEngine& engine);
    ~PluginListView() override;
    void resized() override;

private:
    static constexpr int rowH = 30;

    void timerCallback() override;      // hält die angezeigte Plugin-Latenz aktuell

    void showPluginPicker();            // "+ Plugin": Picker mit Suchfeld (CallOutBox)
    void addFromPicker (const juce::PluginDescription&);
    void showFolderMenu();              // Custom-VST3-Ordner verwalten (hinzufügen/entfernen)
    void chooseFolder();                // VST3-Ordner per Dialog wählen + rescannen
    void openEditor (int row);
    void rebuildRows();                 // Zeilen-Components aus der aktuellen Kette neu erzeugen
    void commitChange();                // rebuildGraph + persistieren + Zeilen aktualisieren
    void requestRemove (int index);     // verzögertes Entfernen (Row löscht sich sonst selbst)
    void requestMove (int from, int to);
    void toggleBypass (int index);
    void gainChanged (int index, float gainDb);   // Kazanç-Regler: live setzen, verzögert speichern

    // Kazanç-Regler: Speichern erst 0,4-0,9 s nach der letzten Bewegung (timerCallback) - ein
    // Ziehen erzeugt viele Werte, und jedes Speichern serialisiert alle Plugin-Zustände.
    bool gainPersistPending = false;
    juce::uint32 lastGainChangeMs = 0;

    // Per-Row gezeichneter Mülleimer-Button.
    struct TrashButton : juce::Button
    {
        TrashButton();
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    // Eine Zeile: Handle + Name + Bypass + Trash. Das Handle-/Namensfeld behandelt
    // Drag (Umsortieren) und Doppelklick (Editor); Bypass/Trash sind Kind-Buttons.
    // Kazanç-Zeilen (eingebauter Gain) tragen zusätzlich einen Regler zwischen Name und Latenz.
    struct Row : juce::Component
    {
        static constexpr int latencyW  = 72;   // Latenz-Spalte links vom Bypass
        static constexpr int gainNameW = 92;   // Platz für "Kazanç" vor dem Regler

        Row (PluginListView& ownerIn, int idx);
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp   (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        bool overHandle (juce::Point<int> p) const;

        PluginListView& owner;
        int index;
        juce::TextButton bypassBtn;
        TrashButton trashBtn;
        bool isGainRow = false;
        juce::Slider gainSlider;   // nur in Kazanç-Zeilen sichtbar
        bool dragging = false;
        int  grabOffsetY = 0;
    };

    // Editor-Fenster, das sich beim Schließen selbst aus der Liste entfernt.
    struct EditorWindow : juce::DocumentWindow
    {
        EditorWindow (const juce::String& name, std::function<void (EditorWindow*)> onCloseIn)
            : juce::DocumentWindow (name, juce::Colours::black, juce::DocumentWindow::closeButton),
              onClose (std::move (onCloseIn)) {}
        void closeButtonPressed() override { if (onClose) onClose (this); }
        std::function<void (EditorWindow*)> onClose;
    };

    void updateScanUi();                 // Statuszeile/Skip-Hinweis + addBtn-Enable aktualisieren
    juce::Label scanLabel;               // "Scanning plugins... 12/34 - Name"
    double scanProgress = 0.0;           // 0..1, von ProgressBar per Referenz gelesen
    juce::ProgressBar scanBar { scanProgress };
    juce::TextButton skipScanBtn;        // "Atla": überspringt das aktuell scannende Plugin
    juce::Label skipLabel;               // "N plugin(s) skipped - hover for details"

    AudioEngine& engine;
    juce::TextButton addBtn;             // "+ Eklenti"
    juce::TextButton manageFoldersBtn;   // "VST3 klasörleri"
    juce::Viewport viewport;
    juce::Component rowsHolder;          // Inhalt des Viewports (trägt die Zeilen)
    juce::OwnedArray<Row> rows;
    juce::OwnedArray<EditorWindow> editorWindows;
};
