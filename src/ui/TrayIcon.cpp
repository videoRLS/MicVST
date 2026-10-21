#include "ui/TrayIcon.h"
#include "BinaryData.h"
#include "Text.h"

using micvst::tr;

TrayIcon::TrayIcon()
{
    updateIcon();                 // erzeugt erst das Icon (pimpl) ...
    setIconTooltip ("MicVST");    // ... sonst verpufft der Tooltip (pimpl wäre noch null)
    juce::Desktop::getInstance().addDarkModeSettingListener (this);
    startTimer (400);
}

void TrayIcon::timerCallback()
{
    const bool m = isMuted && isMuted();
    if (m == shownMuted) return;
    shownMuted = m;
    setIconTooltip (m ? "MicVST - " + tr ("Sessiz") : juce::String ("MicVST"));
}

TrayIcon::~TrayIcon()
{
    juce::Desktop::getInstance().removeDarkModeSettingListener (this);
}

void TrayIcon::updateIcon()
{
    // Dunkler Modus -> helles Icon (iconDarkMode), heller Modus -> dunkles Icon.
    const bool dark = juce::Desktop::getInstance().isDarkModeActive();
    const void* data = dark ? BinaryData::iconDarkMode_png  : BinaryData::iconLightMode_png;
    const int   size = dark ? BinaryData::iconDarkMode_pngSize : BinaryData::iconLightMode_pngSize;

    auto img = juce::ImageCache::getFromMemory (data, size);
    if (img.isValid())
        setIconImage (img, img);
}

void TrayIcon::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isRightButtonDown())
    {
        static constexpr int presetOffset = 1000;
        juce::PopupMenu m;
        if (toggleMute)
            m.addItem (3, tr ("Sustur"), true, isMuted && isMuted());
        if (addPresetMenus)
        {
            juce::PopupMenu presetsMenu;
            addPresetMenus (presetsMenu, presetOffset);
            m.addSubMenu (tr ("Hazır ayar"), presetsMenu);
        }
        m.addSeparator();
        m.addItem (1, tr ("Windows açılışında başlat"),
                   true, isAutostartOn && isAutostartOn());
        m.addSeparator();
        m.addItem (2, tr ("MicVST'yi kapat"));
        m.showMenuAsync (juce::PopupMenu::Options(), [this] (int result)
        {
            if      (result == 1 && setAutostart && isAutostartOn) setAutostart (! isAutostartOn());
            else if (result == 2 && onQuit)                        onQuit();
            else if (result == 3 && toggleMute)                    toggleMute();
            else if (result > presetOffset && loadPreset)          loadPreset (result - presetOffset);
        });
    }
    else if (onToggleWindow)
    {
        onToggleWindow();
    }
}
