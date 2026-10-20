#include "state/AutostartRegistry.h"

namespace
{
    const juce::String keyPath =
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\MicVST";
}

namespace AutostartRegistry
{
   #if JUCE_WINDOWS
    bool isEnabled()
    {
        return juce::WindowsRegistry::valueExists (keyPath);
    }

    void setEnabled (bool shouldRun)
    {
        if (shouldRun)
        {
            auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile)
                          .getFullPathName();
            juce::WindowsRegistry::setValue (keyPath, "\"" + exe + "\" --tray");
        }
        else
        {
            juce::WindowsRegistry::deleteValue (keyPath);
        }
    }
   #else
    // (only the Linux build of the tests and UI snapshots gets here: there is no autostart)
    bool isEnabled() { return false; }
    void setEnabled (bool) {}
   #endif
}
