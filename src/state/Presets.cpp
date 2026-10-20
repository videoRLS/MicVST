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

    // The factory presets, for average adult Turkish voices, not for one particular voice: every
    // move is broad (low Q) and moderate, so it suits the great majority of voices and microphones
    // (USB and condenser microphones at 5-20 cm).
    //  Men (fundamental about 85-155 Hz): boom below 100 Hz, close-microphone build-up around 150 Hz,
    //  mud around 300 Hz, a boxy 500 Hz, a nasal 1 kHz, a hard edge around 3 kHz.
    //  Women (fundamental about 165-255 Hz): everything about a third to a half octave higher - low
    //  cut 90-115 Hz, body around 200-230 Hz, mud 350 Hz, box 600 Hz, nasal 1.2 kHz, edge 3.5 kHz,
    //  presence 5 kHz, sibilants higher (de-esser 6.5-7 kHz).
    //  Turkish sibilants (s, ş, ç, z) are frequent: every preset de-esses relative to the voice.
    // Every preset has the same elements in the same order:
    //   Low-Cut -> Yakınlık -> Çamur Temizleyici -> Parametrik EQ (kutu, genizlik, sertlik) -> Gövde ->
    //   Netlik -> Hava -> Otomatik Kazanç -> De-esser -> Sıcak Saturasyon -> Oda Yankısı -> Kazanç
    // (the room is open only in the singing presets). Otomatik Kazanç sits before the de-esser and the
    // saturation: they then see the same level for every microphone and voice. The last Kazanç
    // matches the loudness of all twenty (measured on synthetic men's and women's speech), so
    // switching does not jump in level.
    struct V
    {
        float lowCut, proxDb, proxHz, mudDb, mudHz, boxHz, boxDb, nasalHz, nasalDb, hardHz, hardDb,
              bodyDb, bodyHz, presDb, presHz, airDb, airHz, deessHz, deessThr, deessRange;
        int satType; float satDrive, satMix, roomSize, roomMix, trimDb;
    };

    std::vector<Setting> chainFor (const V& v)
    {
        return {
            { "builtin:lowcut",    { { "freq", v.lowCut }, { "slope", 1.0f } } },
            { "builtin:proximity", { { "gain", v.proxDb }, { "freq", v.proxHz } } },
            { "builtin:mud",       { { "gain", v.mudDb }, { "freq", v.mudHz }, { "q", 1.0f } } },
            { "builtin:parameq",   { { "b1type", 0.0f }, { "b1freq", v.boxHz },   { "b1gain", v.boxDb },   { "b1q", 1.4f },
                                     { "b2type", 0.0f }, { "b2freq", v.nasalHz }, { "b2gain", v.nasalDb }, { "b2q", 1.8f },
                                     { "b3type", 0.0f }, { "b3freq", v.hardHz },  { "b3gain", v.hardDb },  { "b3q", 1.8f },
                                     { "b4type", 0.0f }, { "b4freq", 8000.0f },   { "b4gain", 0.0f },      { "b4q", 1.0f } } },
            { "builtin:body",      { { "gain", v.bodyDb }, { "freq", v.bodyHz } } },
            { "builtin:presence",  { { "gain", v.presDb }, { "freq", v.presHz }, { "q", 1.0f } } },
            { "builtin:air",       { { "gain", v.airDb }, { "freq", v.airHz } } },
            { "builtin:autogain",  {} },
            { "builtin:deesser",   { { "freq", v.deessHz }, { "threshold", v.deessThr }, { "range", v.deessRange }, { "mode", 0.0f }, { "detect", 0.0f } } },
            { "builtin:saturation",{ { "type", (float) v.satType }, { "drive", v.satDrive }, { "mix", v.satMix } } },
            { "builtin:reverb",    { { "size", v.roomSize }, { "damping", 45.0f }, { "width", 100.0f }, { "mix", v.roomMix } } },
            { "builtin:gain",      { { "gain", v.trimDb } } },
        };
    }

    std::vector<Preset> build()
    {
        struct Def { const char* group; const char* name; V v; };
        static const char* const men = "Erkek Sesi Ayarları";
        static const char* const women = "Kadın Sesi Ayarları";
        //                                                  LC   prox       mud        kutu      genizlik   sertlik     gövde      netlik     hava        de-esser          sat           oda     trim
        const std::vector<Def> defs {
            { men,   "Dengeli Profesyonel",   { 80, -1.5, 150, 3.0, 300,  500, -1.5, 1000, -1.0, 3000, -1.5,  1.0, 130,  2.5, 4500,  2.0, 11000,  5500, -14, 6,  0, 3, 50,  30,  0, 0.6f } },
            { men,   "Podcast / Sohbet",      { 80,  1.0, 130, 2.5, 300,  500, -1.5, 1000, -1.0, 3000, -2.0,  2.0, 125,  1.5, 4000,  0.5, 12000,  5500, -14, 7,  1, 3, 40,  30,  0, 0.2f } },
            { men,   "Radyo Spikeri",         { 75,  1.5, 120, 3.0, 300,  500, -2.0, 1000, -1.5, 3000, -1.0,  3.0, 115,  3.0, 4000,  2.5, 11000,  5500, -15, 8,  1, 8, 80,  30,  0, 0.7f } },
            { men,   "Seslendirme / Belgesel",{ 70,  0.5, 130, 3.0, 300,  500, -2.0, 1000, -1.0, 3000, -3.0,  2.0, 120,  1.0, 4200,  1.5, 12000,  5500, -16, 9,  1, 5, 60,  30,  0, 0.7f } },
            { men,   "Sinema Sesi",           { 70,  1.0, 120, 3.5, 300,  500, -2.0, 1000, -1.0, 3000, -2.0,  2.5, 120,  2.0, 4200,  4.0, 12000,  5500, -15, 8,  1, 6, 70,  30,  0, 0.9f } },
            { men,   "Kristal Netlik",        { 90, -3.0, 160, 4.0, 320,  500, -2.0, 1000,  0.0, 3000, -1.0,  0.0, 130,  4.0, 4800,  5.0,  9000,  5500, -14, 8,  0, 0,  0,  30,  0, 1.2f } },
            { men,   "Yumuşak ve Pürüzsüz",   { 75, -1.0, 150, 2.5, 300,  500, -1.0, 1000, -1.5, 3000, -3.0,  1.5, 130,  0.0, 4200, -1.5, 11000,  5000, -16, 9,  0, 3, 50,  30,  0, 0.3f } },
            { men,   "Derin ve Sıcak",        { 70,  2.0, 130, 2.0, 300,  500, -1.5, 1000, -1.0, 3000, -2.0,  3.0, 120,  1.0, 4200, -0.5, 12000,  5500, -15, 7,  1, 7, 70,  30,  0, -0.1f } },
            { men,   "Şarkı / Vokal",         { 90, -1.5, 150, 3.0, 300,  500, -1.5, 1000, -1.0, 3000, -2.0,  1.0, 140,  2.5, 4500,  4.0, 12000,  5500, -15, 8,  0, 5, 60,  45, 18, 0.7f } },
            { men,   "Oyun / Canlı Yayın",    { 95, -2.5, 160, 4.0, 320,  500, -2.0, 1000, -1.5, 3000, -1.0,  0.5, 140,  4.0, 4600,  3.0, 10000,  5500, -14, 8,  0, 4, 50,  30,  0, 1.2f } },

            { women, "Dengeli Profesyonel",   { 100, -1.0, 200, 2.5, 350, 600, -1.5, 1200, -1.0, 3500, -1.5,  1.0, 220,  2.0, 5000,  2.5, 12000,  6500, -14, 7,  0, 2, 40,  30,  0, -0.3f } },
            { women, "Podcast / Sohbet",      { 100,  0.5, 190, 2.0, 350, 600, -1.0, 1200, -1.0, 3500, -2.0,  2.0, 210,  1.0, 4800,  1.0, 12000,  6500, -14, 8,  1, 2, 40,  30,  0, -1.3f } },
            { women, "Radyo Spikeri",         { 95,   1.0, 180, 2.5, 350, 600, -2.0, 1200, -1.5, 3500, -1.5,  3.0, 195,  3.0, 4600,  2.5, 12000,  6500, -15, 9,  1, 6, 70,  30,  0, -1.3f } },
            { women, "Seslendirme / Anlatıcı",{ 90,   0.0, 190, 2.5, 350, 600, -1.5, 1200, -1.5, 3500, -3.0,  2.5, 210,  0.5, 4500,  1.5, 12500,  6500, -16, 9,  1, 4, 50,  30,  0, -1.1f } },
            { women, "Sinema Sesi",           { 90,   0.0, 180, 3.0, 350, 600, -2.0, 1200, -1.0, 3500, -2.0,  2.0, 205,  1.0, 5000,  4.0, 13000,  6500, -15, 8,  1, 5, 60,  30,  0, -0.4f } },
            { women, "Kristal Netlik",        { 110, -2.5, 220, 3.5, 380, 600, -2.0, 1200,  0.0, 3500, -1.0,  0.0, 220,  3.0, 5400,  5.0, 11000,  6500, -14, 9,  0, 0,  0,  30,  0, 0.6f } },
            { women, "Yumuşak ve Pürüzsüz",   { 100, -0.5, 200, 2.0, 350, 600, -1.0, 1200, -1.5, 3500, -3.0,  1.5, 220,  0.0, 4800, -1.0, 12000,  6000, -16, 10, 0, 2, 40,  30,  0, -0.9f } },
            { women, "Sıcak ve Dolgun",       { 90,   1.5, 180, 1.5, 350, 600, -1.5, 1200, -1.0, 3500, -2.0,  3.0, 200,  0.5, 4800,  0.0, 12000,  6500, -15, 8,  1, 6, 60,  30,  0, -2.0f } },
            { women, "Şarkı / Vokal",         { 110, -1.5, 200, 2.5, 350, 600, -1.5, 1200, -1.0, 3500, -2.0,  0.5, 230,  2.0, 5200,  4.5, 12500,  7000, -15, 9,  0, 4, 50,  45, 20, 0.0f } },
            { women, "Oyun / Canlı Yayın",    { 120, -3.0, 230, 3.0, 380, 600, -2.0, 1200, -1.5, 3500, -1.0, -0.5, 230,  4.5, 4600,  2.5, 10000,  6500, -14, 9,  0, 3, 40,  30,  0, 0.4f } },
        };

        std::vector<Preset> out;
        for (auto& d : defs)
        {
            Preset p;
            p.group = u8 (d.group);
            p.shortName = u8 (d.name);
            p.name = u8 (d.group == men ? "Erkek Sesi - " : "Kadın Sesi - ") + p.shortName;
            p.factory = true;
            for (auto& s : chainFor (d.v)) p.elements.add (effectEntry (s));
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
