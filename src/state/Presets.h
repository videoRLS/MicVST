#pragma once
#include "state/Persistence.h"
#include <vector>

// Hazır ayarlar (presets): a whole chain - every element with its settings (built-in effects, Kazanç,
// Otomatik Kazanç, Mono/Stereo and VST3 plug-ins with their full state blob, exactly as config.xml
// keeps them).
//
//  * Factory presets: two folders, "Erkek Sesi Ayarları" and "Kadın Sesi Ayarları", ten each
//    (everyday, podcast, radio, narration, singing, streaming ...), for average adult Turkish men's
//    and women's voices. Built-in effects only, so they work for everyone; built in code
//    (factoryPresets()). All twenty have the same elements in the same order - switching between
//    them only moves parameters (each glides in 50 ms), the chain is not rebuilt.
//  * User presets: one file per preset in %APPDATA%\MicVST\presets (*.micvstpreset, XML), written
//    through a temporary file (a crash never leaves half a preset). Names may contain any letters
//    (Turkish too); the file name is derived from the name, the name itself is kept inside the file.
//    A damaged file is skipped (and named in the log), never a reason to fail.
namespace presets
{
    struct Preset
    {
        juce::String name;            // unique: factory presets "Erkek Sesi - Radyo Spikeri"
        juce::String group, shortName;   // factory presets: folder ("Erkek Sesi Ayarları") and name in it
        bool factory = false;
        juce::Array<PluginEntryState> elements;
        juce::File file;   // user presets only
    };

    const std::vector<Preset>& factoryPresets();
    const Preset* findFactory (const juce::String& name);

    juce::File userFolder();                               // %APPDATA%\MicVST\presets
    std::vector<Preset> loadUserPresets (const juce::File& folder = userFolder());   // sorted by name

    // Name checks for the save dialog: empty / too long / taken by a factory preset -> error text.
    juce::String nameProblem (const juce::String& name);
    juce::File fileFor (const juce::String& name, const juce::File& folder = userFolder());

    // Writes (or overwrites) a user preset; false + error text on failure.
    bool saveUserPreset (const juce::String& name, const juce::Array<PluginEntryState>& elements,
                         juce::String& error, const juce::File& folder = userFolder());
    bool deleteUserPreset (const juce::String& name, const juce::File& folder = userFolder());
    bool renameUserPreset (const juce::String& from, const juce::String& to, juce::String& error,
                           const juce::File& folder = userFolder());

    // File format (also for tests): <MicVSTPreset version="1" name="..."><plugins>...</plugins></MicVSTPreset>
    std::unique_ptr<juce::XmlElement> toXml (const juce::String& name, const juce::Array<PluginEntryState>& elements);
    bool fromXml (const juce::XmlElement&, Preset& out);
}
