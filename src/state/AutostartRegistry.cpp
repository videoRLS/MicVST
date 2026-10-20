#include "state/AutostartRegistry.h"

#if JUCE_WINDOWS

namespace
{
    const juce::String keyPath =
        "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run\\MicVST";
}

namespace AutostartRegistry
{
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
}

#elif JUCE_MAC

// macOS: a LaunchAgent in ~/Library/LaunchAgents starts MicVST at login (in the menu bar, --tray).
namespace
{
    juce::File agentFile()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
                   .getChildFile ("Library/LaunchAgents/com.micvst.MicVST.plist");
    }

    juce::String xmlEscaped (const juce::String& s)
    {
        return s.replace ("&", "&amp;").replace ("<", "&lt;").replace (">", "&gt;");
    }
}

namespace AutostartRegistry
{
    bool isEnabled()
    {
        return agentFile().existsAsFile();
    }

    void setEnabled (bool shouldRun)
    {
        const auto file = agentFile();
        if (! shouldRun)
        {
            file.deleteFile();
            return;
        }
        const auto exe = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName();
        juce::String plist;
        plist << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
              << "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
              << "<plist version=\"1.0\">\n<dict>\n"
              << "    <key>Label</key><string>com.micvst.MicVST</string>\n"
              << "    <key>ProgramArguments</key>\n    <array>\n"
              << "        <string>" << xmlEscaped (exe) << "</string>\n"
              << "        <string>--tray</string>\n"
              << "    </array>\n"
              << "    <key>RunAtLoad</key><true/>\n"
              << "</dict>\n</plist>\n";
        file.getParentDirectory().createDirectory();
        file.replaceWithText (plist);
    }
}

#else

namespace AutostartRegistry
{
    bool isEnabled()           { return false; }
    void setEnabled (bool)     {}
}

#endif
