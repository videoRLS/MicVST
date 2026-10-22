#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <functional>

// Tray-Icon mit Rechtsklick-Menü. Aktionen werden via std::function injiziert,
// damit der Tray nichts über App-Interna weiß.
class TrayIcon : public juce::SystemTrayIconComponent,
                public juce::DarkModeSettingListener,
                private juce::Timer
{
public:
    TrayIcon();
    ~TrayIcon() override;

    std::function<void()>         onToggleWindow;
    std::function<void()>         onQuit;

    std::function<bool()>         isAutostartOn;
    std::function<void(bool)>     setAutostart;

    // Sustur and the presets right from the tray (the window may be hidden)
    std::function<bool()>         isMuted;
    std::function<void()>         toggleMute;
    std::function<bool()>         isCueOn;          // Bildirim sesleri (quick switch)
    std::function<void()>         toggleCue;
    std::function<bool()>         isRecording;      // Kayıt
    std::function<void()>         toggleRecording;
    std::function<void (juce::PopupMenu&, int idOffset)> addPresetMenus;
    std::function<void (int id)>  loadPreset;

    void mouseDown (const juce::MouseEvent&) override;

    // Tray-Icon passend zum aktuellen Windows-Hell/Dunkel-Modus setzen (stumm: roter Punkt).
    void updateIcon();
    void darkModeSettingChanged() override { updateIcon(); }   // Live-Umschaltung
private:
    void timerCallback() override;   // tooltip "MicVST - Sessiz" while muted
    bool shownMuted = false;
};
