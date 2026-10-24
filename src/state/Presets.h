#pragma once
#include "state/Persistence.h"
#include <vector>

// Hazır ayarlar (presets): a whole chain - every element with its settings (built-in effects, Kazanç,
// Otomatik Kazanç, Mono/Stereo and VST3 plug-ins with their full state blob, exactly as config.xml
// keeps them).
//
//  * Factory presets: two folders, "Erkek Sesi Ayarları" and "Kadın Sesi Ayarları", fifteen each
//    (the same fifteen purposes: everyday, podcast, streaming, meetings, radio, news, narration,
//    audiobook, warm, bright, smooth, close microphone, noisy room, headset / laptop microphone,
//    singing), for the great majority of adult men's and women's voices. Built-in effects only, so
//    they work for everyone; built in code (factoryPresets(), from a Voicing each). All thirty have
//    the same elements in the same order - switching between them only moves parameters (each
//    glides in 50 ms), the chain is not rebuilt.
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

    // The recipe of the factory presets (and of any chain made the same way, e.g. personal presets
    // given as files): always the same fifteen elements in the same order, so switching between two
    // such chains glides - nothing is rebuilt:
    //   Low-Cut -> Yakınlık -> Çamur Temizleyici -> Parametrik EQ (kutu, genizlik, sertlik, cızırtı) ->
    //   Gövde -> Netlik -> Hava -> Otomatik Kazanç -> Gürültü Kapısı -> Kompresör -> De-esser ->
    //   Sıcak Saturasyon -> Oda Yankısı -> Kazanç -> Çıkış Limiteri
    // The defaults change nothing but the Low-Cut, Otomatik Kazanç and a limiter at -1 dBTP.
    struct Voicing
    {
        float lowCut = 80.0f;                                   // Hz, 24 dB/octave
        float proxDb = 0.0f, proxHz = 150.0f;                   // Yakınlık: low shelf (- = less close-microphone boom)
        float mudDb = 0.0f, mudHz = 300.0f, mudQ = 1.0f;        // Çamur Temizleyici: cut (dB >= 0)
        float boxDb = 0.0f, boxHz = 500.0f, boxQ = 1.4f;        // Parametrik EQ band 1: bell ("kutu")
        float nasalDb = 0.0f, nasalHz = 1000.0f, nasalQ = 1.8f; // band 2: bell (genizlik)
        float hardDb = 0.0f, hardHz = 3000.0f, hardQ = 1.8f;    // band 3: bell (sertlik)
        float fizzDb = 0.0f, fizzHz = 8000.0f, fizzQ = 1.0f;    // band 4: bell (cızırtı / keskin tizler)
        float bodyDb = 0.0f, bodyHz = 130.0f;                   // Gövde: bell, Q 0.8
        float presDb = 0.0f, presHz = 4000.0f, presQ = 1.0f;    // Netlik
        float airDb = 0.0f, airHz = 12000.0f;                   // Hava: high shelf
        float gateThr = -80.0f, gateRange = 0.0f, gateHold = 150.0f, gateRelease = 250.0f;
        float compThr = -20.0f, compRatio = 1.0f, compAtk = 10.0f, compRel = 150.0f, compKnee = 6.0f, compMix = 100.0f,
              compMakeup = 0.0f;
        float deessHz = 6000.0f, deessThr = -15.0f, deessRange = 0.0f;
        int satType = 0; float satDrive = 0.0f, satMix = 0.0f;  // 0 tube, 1 tape
        float roomSize = 30.0f, roomMix = 0.0f;
        float trimDb = 0.0f;                                    // Kazanç: the loudness match
        float ceiling = -1.0f;                                  // Çıkış Limiteri, dBTP
    };
    juce::Array<PluginEntryState> buildChain (const Voicing&);

    const std::vector<Preset>& factoryPresets();
    const Preset* findFactory (const juce::String& name);
    // A preset name as shown: a factory preset in the user's language, an own preset as it is.
    juce::String displayName (const juce::String& name);

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
    // Sharing: a chain as a .micvstpreset file anywhere (the same format as the own presets), and
    // such a file added to the own presets - under its name, or "name (2)" if that is taken or is a
    // factory preset's name. importPreset gives the name it got.
    juce::String fileNameFor (const juce::String& name);   // "Benim sesim.micvstpreset"
    bool exportPreset (const juce::String& name, const juce::Array<PluginEntryState>& elements,
                       const juce::File& file, juce::String& error);
    bool importPreset (const juce::File& file, juce::String& nameOut, juce::String& error,
                       const juce::File& folder = userFolder());

    std::unique_ptr<juce::XmlElement> toXml (const juce::String& name, const juce::Array<PluginEntryState>& elements);
    bool fromXml (const juce::XmlElement&, Preset& out);
}
