#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_core/juce_core.h>

struct PluginEntryState
{
    juce::String fileOrId;
    bool bypassed = false;
    juce::MemoryBlock state;   // getStateInformation()-Blob
};

struct MicVSTState
{
    juce::String inputDevice, outputDevice;
    double sampleRate = 48000.0;
    int    bufferSize = 0;   // Buffer-Wunsch in Samples; 0 = Auto (Geräte-Default)
    juce::Array<PluginEntryState> plugins;
    juce::StringArray pluginFolders;   // zusätzliche VST3-Suchordner
    juce::String windowState;          // DocumentWindow::getWindowStateAsString() (Größe/Position)
    // (Kein Update-Check mehr: MicVST 1.2 hat keinerlei Netzwerkzugriff. Die alten Felder
    //  updateCheckEnabled/-Asked/lastNotifiedVersion in bestehenden config.xml werden ignoriert.)
};

juce::ValueTree  toValueTree (const MicVSTState&);
MicVSTState    fromValueTree (const juce::ValueTree&);

// Datei unter %APPDATA%\MicVST\config.xml
juce::File    configFile();
bool          saveState (const MicVSTState&);
MicVSTState loadState();
