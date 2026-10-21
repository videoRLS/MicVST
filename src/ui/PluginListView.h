#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "audio/AudioEngine.h"
#include "state/Presets.h"
#include "ui/DarkTitleBar.h"
#include "ui/Theme.h"

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
public:
    // Custom-VST3-Ordner verwalten (hinzufügen/entfernen, neu scannen, zurücksetzen); the menu opens
    // at target (the settings window's button).
    void showFolderMenu (juce::Component* target);
private:
    void chooseFolder();                // VST3-Ordner per Dialog wählen + rescannen
    void openEditor (int row);
    void rebuildRows();                 // Zeilen-Components aus der aktuellen Kette neu erzeugen
    void commitChange();                // rebuildGraph + persistieren + Zeilen aktualisieren
    void requestRemove (int index);     // verzögertes Entfernen (Row löscht sich sonst selbst)
    void requestMove (int from, int to);
    void toggleBypass (int index);
    void gainChanged (int index, float gainDb);   // Kazanç-Regler: live setzen, verzögert speichern
    void autoGainToggled (int index, bool on);     // Otomatik Kazanç ein/aus (sofort speichern)

    // Kazanç-Regler: Speichern erst 0,4-0,9 s nach der letzten Bewegung (timerCallback) - ein
    // Ziehen erzeugt viele Werte, und jedes Speichern serialisiert alle Plugin-Zustände.
    bool gainPersistPending = false;
    juce::uint32 lastGainChangeMs = 0;

    // On/off switch of a row (on = the element works; off = bypassed). Toggle state = bypassed.
    struct SwitchButton : juce::Button
    {
        SwitchButton() : juce::Button ("switch") {}
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    // Per-Row gezeichneter Mülleimer-Button.
    struct TrashButton : juce::Button
    {
        TrashButton();
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;
    };

    // Eine Zeile: Nummer (Verarbeitungsreihenfolge, 1 = erstes Glied im Signalweg) + Handle + Name +
    // Bypass + Trash. Ohne Glieder gibt es keine Zeilen, also auch keine Nummernspalte. Das Handle-/Namensfeld behandelt
    // Drag (Umsortieren) und Doppelklick (Editor); Bypass/Trash sind Kind-Buttons.
    // Kazanç-Zeilen (eingebauter Gain) tragen zusätzlich einen Regler zwischen Name und Latenz;
    // die Otomatik-Kazanç-Zeile einen Ein/Aus-Schalter und eine Live-Anzeige (10 Hz).
    struct Row : juce::Component, private juce::Timer
    {
        static constexpr int numberW   = 18;   // Nummernspalte ganz links (rechtsbündig, bis "99")
        static constexpr int handleX   = numberW + 4;   // Drag-Handle (zwei Punktspalten)
        static constexpr int nameX     = numberW + 22;  // Name beginnt hier
        static constexpr int latencyW  = 60;   // Latenz-Spalte links vom Bypass ("106,7 ms" passt)
        static constexpr int gainNameW = 92;   // Platz für "Kazanç" vor dem Regler
        static constexpr int autoNameW = 128;  // Platz für "Otomatik Kazanç" vor dem Schalter
        static constexpr int autoToggleW = 64;

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
        SwitchButton bypassBtn;
        TrashButton trashBtn;
        bool isGainRow = false;
        juce::Slider gainSlider;   // nur in Kazanç-Zeilen sichtbar

        // Otomatik Kazanç: Schalter + Anzeige "Verstärkung (Eingang -> Ausgang, Spitze dBFS)"
        // Sessize Alma (Dahili): Schalter "Sessiz" direkt in der Zeile
        static constexpr int muteNameW = 160;
        bool isMuteRow = false;
        juce::ToggleButton muteToggle;

        bool isAutoGainRow = false;
        juce::ToggleButton autoToggle;
        float shownInDb = -120.0f, shownOutDb = -120.0f, shownGainDb = 0.0f;   // Anzeige-Werte (Spitzen fallen langsam)
        bool  shownSpeech = false;
        void timerCallback() override;   // nur Otomatik-Kazanç-Zeilen: Messwerte holen, neu zeichnen
        juce::Rectangle<int> autoInfoArea() const;
        bool dragging = false;
        int  grabOffsetY = 0;
    };

    // Editor-Fenster, das sich beim Schließen selbst aus der Liste entfernt.
    struct EditorWindow : juce::DocumentWindow
    {
        EditorWindow (const juce::String& name, std::function<void (EditorWindow*)> onCloseIn)
            : juce::DocumentWindow (name, micvst::theme::background, juce::DocumentWindow::closeButton),
              onClose (std::move (onCloseIn)) {}
        void visibilityChanged() override
        {
            juce::DocumentWindow::visibilityChanged();
            if (isVisible()) micvst::useDarkTitleBar (*this, micvst::theme::dark);   // title bar like the theme
        }
        void closeButtonPressed() override { if (onClose) onClose (this); }
        std::function<void (EditorWindow*)> onClose;
    };

    void updateScanUi();                 // Statuszeile/Skip-Hinweis + addBtn-Enable aktualisieren
    juce::Label scanLabel;               // "Scanning plugins... 12/34 - Name"
    double scanProgress = 0.0;           // 0..1, von ProgressBar per Referenz gelesen
    juce::ProgressBar scanBar { scanProgress };
    juce::TextButton skipScanBtn;        // "Atla": überspringt das aktuell scannende Plugin
    juce::Label skipLabel;               // "N plugin(s) skipped - hover for details"

    // Hazır ayarlar (next to "+ Eklenti"): the list (dahili + kendi ayarlarım) and its menu
    void refreshPresetList();            // re-read the user presets, refill the list
    void presetChosen();
    void showPresetMenu();
    void savePresetAs (const juce::String& name, bool overwrite = false);
    void showPresetError (const juce::String&);
    void askPresetName (const juce::String& title, const juce::String& message, const juce::String& initial,
                        std::function<void (const juce::String&)> onOk);
    bool currentIsUserPreset() const;
    void exportPreset();                 // the chain as a .micvstpreset file (to give to someone)
    void importPreset();                 // a .micvstpreset file -> own presets, loaded at once
    std::unique_ptr<juce::FileChooser> fileChooser;
public:
    void updatePresetBox();              // the box shows the loaded preset ("*" = changed since)
    // The three preset folders as sub-menus (ids offset + 1 ...; the loaded preset ticked) and
    // loading by such an id - for the list here and the tray icon's menu.
    void addPresetMenus (juce::PopupMenu& root, int idOffset);
    void loadPresetById (int id);
private:
    static constexpr int factoryIdBase = 1, emptyUserId = 99, userIdBase = 100;
    juce::ComboBox presetBox;
    juce::TextButton presetMenuBtn;
    std::vector<presets::Preset> userPresets;
    juce::String shownPresetName;
    bool shownPresetModified = false;

    AudioEngine& engine;
    juce::TextButton addBtn;             // "+ Eklenti"
    juce::TextButton manageFoldersBtn;   // "VST3 klasörleri"
    juce::Viewport viewport;
    juce::Component rowsHolder;          // Inhalt des Viewports (trägt die Zeilen)
    juce::OwnedArray<Row> rows;
    juce::OwnedArray<EditorWindow> editorWindows;
};
