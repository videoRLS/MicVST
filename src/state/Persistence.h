#pragma once
#include <juce_data_structures/juce_data_structures.h>
#include <juce_core/juce_core.h>

struct PluginEntryState
{
    juce::String fileOrId;
    bool bypassed = false;
    juce::MemoryBlock state;   // getStateInformation()-Blob
    // Identität des Plugins in seiner Datei (ab 1.2.1; fehlt in älteren config.xml):
    // PluginDescription::createIdentifierString(), Name und uniqueId.
    juce::String pluginId, pluginName;
    int pluginUid = 0;
};

struct MicVSTState
{
    juce::String inputDevice, outputDevice;
    double sampleRate = 48000.0;
    int    bufferSize = 0;   // Buffer-Wunsch in Samples; 0 = Auto (Geräte-Default)
    int    safetyBufferMs = 0;   // Sicherheitspuffer Mikro -> Kabel in ms; 0 = Auto (adaptiv)
    int    processPriority = 0;  // Prozess-Priorität: 0 Normal (Standard), 1 Normalin Üstünde, 2 Yüksek (rt::ProcessPriority)
    juce::Array<PluginEntryState> plugins;
    juce::StringArray pluginFolders;   // zusätzliche VST3-Suchordner
    juce::String windowState;          // DocumentWindow::getWindowStateAsString() (Größe/Position)
    juce::String presetName;           // the preset last loaded (Hazır ayar; empty = none)
    bool presetModified = false;       // the chain was changed after loading it
    juce::String theme;                // colour theme id (ui/Theme.h); empty = Gece (default)
    juce::String language;             // interface language id (Text.h); empty = Türkçe (default)
    juce::String uiRenderer;           // interface drawing (ui/RenderMode.h): "gpu" / "cpu"; empty = automatic (default)
    juce::String monitorDevice;        // "Kendini dinle" output (headphones); empty = Windows' default output
    float monitorVolumeDb = 0.0f;      // (monitoring itself always starts off: no feedback through speakers)
    // shortcut keys (ui/GlobalHotkeys.h) as "enabled:keyCode:modifiers"; empty = the default
    juce::String hotkeyMute, hotkeyCompare;
    // Kayıt (audio/Recorder.h): format text ("wav:24|48000|2"; empty = the default, WAV 24 bit
    // 48 kHz stereo) and the folder (empty = Documents\MicVST Kayıtları)
    juce::String recordFormat, recordFolder;
    // Bildirim sesleri (audio/CueSounds.h): "1|builtin:01_ahsap_ac|builtin:01_ahsap_kapat|80"; empty = default
    juce::String cueSounds;
    // (Kein Update-Check mehr: MicVST 1.2 hat keinerlei Netzwerkzugriff. Die alten Felder
    //  updateCheckEnabled/-Asked/lastNotifiedVersion in bestehenden config.xml werden ignoriert.)
};

juce::ValueTree  toValueTree (const MicVSTState&);
MicVSTState    fromValueTree (const juce::ValueTree&);

// The chain list alone (<plugins><plugin .../>...</plugins>) - config.xml and the preset files.
// Entries without fileOrId are skipped on reading.
juce::ValueTree               pluginsToValueTree (const juce::Array<PluginEntryState>&);
juce::Array<PluginEntryState> pluginsFromValueTree (const juce::ValueTree&);

// Datei unter %APPDATA%\MicVST\config.xml
juce::File    configFile();
bool          saveState (const MicVSTState&);
MicVSTState loadState();
