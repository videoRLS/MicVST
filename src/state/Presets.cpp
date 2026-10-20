#include "state/Presets.h"
#include "audio/BuiltInAutoGain.h"
#include "audio/BuiltInEffects.h"
#include "audio/BuiltInGain.h"

namespace presets
{
namespace
{
    const juce::Identifier presetTag ("MicVSTPreset"), versionAttr ("version"), nameAttr ("name");
    const char* const extension = ".micvstpreset";
    constexpr int maxNameLength = 60;

    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

    struct Setting { const char* id; std::vector<std::pair<const char*, float>> params; };

    PluginEntryState effectEntry (const Setting& s)
    {
        PluginEntryState e;
        e.fileOrId = s.id;
        if (juce::String (s.id) == "builtin:gain")
        {
            BuiltInGainProcessor g;
            g.setGainDb (s.params.empty() ? 0.0f : s.params[0].second);
            g.getStateInformation (e.state);
            return e;
        }
        if (juce::String (s.id) == "builtin:autogain")
        {
            BuiltInAutoGainProcessor a;
            a.setAutoGainEnabled (true);
            a.getStateInformation (e.state);
            return e;
        }
        auto fx = builtin::create (s.id);
        jassert (fx != nullptr);
        for (auto& [pid, v] : s.params)
        {
            jassert (fx->findParameter (pid) != nullptr);
            fx->setParam (pid, v);
        }
        fx->getStateInformation (e.state);
        return e;
    }

    // The five factory presets, made for the voice of the recording analysed for them (a male voice,
    // fundamental 118-138 Hz, median 126 Hz): its 300-500 Hz region is ~9 dB above 1 kHz ("boxy"),
    // narrow resonances at 630 Hz, 1.9 kHz and 3.7 kHz, a dip at 4-5 kHz, sibilants 5-10 kHz up to
    // ~16 dB above the mids. Every preset has the same elements in the same order:
    //   Low-Cut -> Yakınlık -> Çamur Temizleyici -> Parametrik EQ (the resonances) -> Gövde ->
    //   Netlik -> Hava -> Otomatik Kazanç -> De-esser -> Sıcak Saturasyon -> Kazanç
    // Otomatik Kazanç sits before the de-esser and the saturation: they then see the same level for
    // every microphone and voice (peaks around -20 dBFS), so their thresholds and drive fit everyone.
    // The last Kazanç matches the loudness of the five (switching does not jump in level).
    std::vector<Preset> build()
    {
        struct Def { const char* name; std::vector<Setting> chain; };
        auto chain = [] (float lowCutHz, float proxDb, float proxHz, float mudDb, float mudHz,
                         float r1Db, float r2Db, float r3Db,
                         float bodyDb, float bodyHz, float presDb, float presHz, float presQ,
                         float airDb, float airHz,
                         float deessHz, float deessThr, float deessRange,
                         int satType, float satDrive, float satMix, float trimDb)
        {
            return std::vector<Setting> {
                { "builtin:lowcut",    { { "freq", lowCutHz }, { "slope", 1.0f } } },
                { "builtin:proximity", { { "gain", proxDb }, { "freq", proxHz } } },
                { "builtin:mud",       { { "gain", mudDb }, { "freq", mudHz }, { "q", 1.2f } } },
                { "builtin:parameq",   { { "b1type", 0.0f }, { "b1freq", 630.0f },  { "b1gain", r1Db }, { "b1q", 4.0f },
                                         { "b2type", 0.0f }, { "b2freq", 1900.0f }, { "b2gain", r2Db }, { "b2q", 4.0f },
                                         { "b3type", 0.0f }, { "b3freq", 3700.0f }, { "b3gain", r3Db }, { "b3q", 5.0f },
                                         { "b4type", 0.0f }, { "b4freq", 8000.0f }, { "b4gain", 0.0f },  { "b4q", 1.0f } } },
                { "builtin:body",      { { "gain", bodyDb }, { "freq", bodyHz } } },
                { "builtin:presence",  { { "gain", presDb }, { "freq", presHz }, { "q", presQ } } },
                { "builtin:air",       { { "gain", airDb }, { "freq", airHz } } },
                { "builtin:autogain",  {} },
                { "builtin:deesser",   { { "freq", deessHz }, { "threshold", deessThr }, { "range", deessRange }, { "mode", 0.0f } } },
                { "builtin:saturation",{ { "type", (float) satType }, { "drive", satDrive }, { "mix", satMix } } },
                { "builtin:gain",      { { "gain", trimDb } } },
            };
        };

        //                         LC    prox      mud         630  1.9k 3.7k  body        presence          air          de-esser            sat              trim
        const std::vector<Def> defs {
            { "Balanced Pro",  chain (80,  -1.5, 150, 3.0, 400, -3.0, -1.5, -3.0,  1.0, 150,  2.5, 4600, 1.2,  2.0, 11000,  6000, -40, 6.0,  0, 4.0,  60, 0.0f) },
            { "Cinema Voice",  chain (70,   1.0, 120, 3.5, 380, -3.0, -1.5, -3.5,  3.0, 135,  2.0, 4400, 1.0,  3.0, 12000,  6200, -40, 7.0,  1, 7.0,  70, 0.0f) },
            { "Crystal Clear", chain (90,  -3.0, 160, 4.5, 420, -3.5, -2.0, -2.5,  0.0, 150,  4.0, 4800, 1.0,  4.5, 10000,  6000, -42, 8.0,  0, 0.0,   0, 0.0f) },
            { "Soft & Smooth", chain (75,  -1.0, 150, 2.5, 400, -2.5, -2.5, -4.5,  1.5, 140,  0.5, 4200, 1.0, -1.0, 12000,  5800, -44, 9.0,  0, 3.0,  50, 0.0f) },
            { "Deep & Warm",   chain (65,   2.5, 130, 2.0, 400, -2.5, -1.5, -3.0,  4.0, 130,  1.0, 4200, 1.0, -0.5, 12000,  6000, -42, 6.0,  1, 8.0,  70, 0.0f) },
        };

        std::vector<Preset> out;
        for (auto& d : defs)
        {
            Preset p;
            p.name = u8 (d.name);
            p.factory = true;
            for (auto& s : d.chain) p.elements.add (effectEntry (s));
            out.push_back (std::move (p));
        }
        return out;
    }

    juce::String sanitize (const juce::String& name)
    {
        // a file name for any letters: keep letters/digits/space/-/_ (Turkish letters included)
        juce::String f;
        for (auto c : name.trim())
            f << (juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ' || c == '-' || c == '_' ? juce::String::charToString (c) : "_");
        f = f.trim();
        return f.isEmpty() ? juce::String ("preset") : f.substring (0, maxNameLength);
    }
}

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = build();
    return list;
}

const Preset* findFactory (const juce::String& name)
{
    for (auto& p : factoryPresets())
        if (p.name == name) return &p;
    return nullptr;
}

juce::File userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("MicVST").getChildFile ("presets");
}

std::unique_ptr<juce::XmlElement> toXml (const juce::String& name, const juce::Array<PluginEntryState>& elements)
{
    auto xml = std::make_unique<juce::XmlElement> (presetTag.toString());
    xml->setAttribute (versionAttr, 1);
    xml->setAttribute (nameAttr, name);
    if (auto list = pluginsToValueTree (elements).createXml())
        xml->addChildElement (list.release());
    return xml;
}

bool fromXml (const juce::XmlElement& xml, Preset& out)
{
    if (! xml.hasTagName (presetTag.toString())) return false;
    out.name = xml.getStringAttribute (nameAttr).trim();
    if (out.name.isEmpty()) return false;
    out.factory = false;
    out.elements.clear();
    if (auto* list = xml.getChildByName ("plugins"))
        out.elements = pluginsFromValueTree (juce::ValueTree::fromXml (*list));
    return true;
}

std::vector<Preset> loadUserPresets (const juce::File& folder)
{
    std::vector<Preset> out;
    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension))
    {
        Preset p;
        auto xml = juce::XmlDocument::parse (f);
        if (xml != nullptr && fromXml (*xml, p))
        {
            p.file = f;
            out.push_back (std::move (p));
        }
        else
        {
            juce::Logger::writeToLog (u8 ("Hazır ayar dosyası okunamadı, atlandı: ") + f.getFullPathName());
        }
    }
    std::sort (out.begin(), out.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
    return out;
}

juce::String nameProblem (const juce::String& raw)
{
    const auto name = raw.trim();
    if (name.isEmpty()) return u8 ("Bir ad yazın.");
    if (name.length() > maxNameLength) return u8 ("Ad en fazla ") + juce::String (maxNameLength) + u8 (" karakter olabilir.");
    for (auto& f : factoryPresets())
        if (f.name.equalsIgnoreCase (name)) return u8 ("Bu ad hazır bir ayarın adı; başka bir ad seçin.");
    return {};
}

juce::File fileFor (const juce::String& name, const juce::File& folder)
{
    // the same name always gives the same file; different names that sanitize alike get a number
    for (auto& p : loadUserPresets (folder))
        if (p.name == name.trim()) return p.file;
    auto base = sanitize (name);
    auto f = folder.getChildFile (base + extension);
    for (int k = 2; f.exists(); ++k)
        f = folder.getChildFile (base + " (" + juce::String (k) + ")" + extension);
    return f;
}

bool saveUserPreset (const juce::String& raw, const juce::Array<PluginEntryState>& elements, juce::String& error,
                     const juce::File& folder)
{
    const auto name = raw.trim();
    if (const auto problem = nameProblem (name); problem.isNotEmpty()) { error = problem; return false; }
    if (! folder.createDirectory()) { error = u8 ("Klasör oluşturulamadı: ") + folder.getFullPathName(); return false; }
    const auto file = fileFor (name, folder);
    auto xml = toXml (name, elements);
    // writeTo goes through a temporary file that replaces the old one at once
    if (! xml->writeTo (file)) { error = u8 ("Dosya yazılamadı: ") + file.getFullPathName(); return false; }
    // read back: what is on disk must be what was meant
    Preset check;
    auto back = juce::XmlDocument::parse (file);
    if (back == nullptr || ! fromXml (*back, check) || check.name != name || check.elements.size() != elements.size())
    {
        error = u8 ("Kaydedilen dosya doğrulanamadı: ") + file.getFullPathName();
        return false;
    }
    for (int i = 0; i < elements.size(); ++i)
        if (check.elements[i].state != elements[i].state || check.elements[i].fileOrId != elements[i].fileOrId)
        {
            error = u8 ("Kaydedilen dosya doğrulanamadı: ") + file.getFullPathName();
            return false;
        }
    return true;
}

bool deleteUserPreset (const juce::String& name, const juce::File& folder)
{
    for (auto& p : loadUserPresets (folder))
        if (p.name == name) return p.file.deleteFile();
    return false;
}

bool renameUserPreset (const juce::String& from, const juce::String& to, juce::String& error, const juce::File& folder)
{
    const auto newName = to.trim();
    if (const auto problem = nameProblem (newName); problem.isNotEmpty()) { error = problem; return false; }
    for (auto& p : loadUserPresets (folder))
        if (p.name == newName && newName != from) { error = u8 ("Bu adda bir ayar zaten var."); return false; }
    for (auto& p : loadUserPresets (folder))
        if (p.name == from)
        {
            const auto oldFile = p.file;
            if (! saveUserPreset (newName, p.elements, error, folder)) return false;
            if (fileFor (newName, folder) != oldFile) oldFile.deleteFile();
            return true;
        }
    error = u8 ("Ayar bulunamadı.");
    return false;
}
}
