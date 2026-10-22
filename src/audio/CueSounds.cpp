#include "audio/CueSounds.h"
#include "audio/DeviceFormat.h"
#include "Text.h"
#include <MicVSTSounds.h>
#include "audio/MsCounter.h"

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <mmsystem.h>
 #pragma comment (lib, "winmm.lib")
#endif

using micvst::tr;

namespace
{
    // the built-in sounds: file in resources/sounds (FLAC), name, role
    struct BuiltIn { const char* file; const char* name; int role; };   // role: 0 açılma, 1 kapanma, 2 açılma 2
    const BuiltIn builtIns[] = {
        { "01_ahsap_ac.flac", "Ahşap", 0 },       { "01_ahsap_kapat.flac", "Ahşap", 1 },
        { "02_kadife_ac.flac", "Kadife", 0 },     { "02_kadife_kapat.flac", "Kadife", 1 },
        { "03_damla_ac.flac", "Damla", 0 },       { "03_damla_kapat.flac", "Damla", 1 },
        { "04_tok_ac.flac", "Tok", 0 },           { "04_tok_kapat.flac", "Tok", 1 },
        { "05_kristal_ac.flac", "Kristal", 0 },   { "05_kristal_kapat.flac", "Kristal", 1 },
        { "06_esinti_ac.flac", "Esinti", 0 },     { "06_esinti_kapat.flac", "Esinti", 1 },
        { "07_yanki_ac.flac", "Yankı", 0 },       { "07_yanki_kapat.flac", "Yankı", 1 },
        { "08_kivilcim_ac.flac", "Kıvılcım", 0 }, { "08_kivilcim_ac2.flac", "Kıvılcım", 2 },
    };

    juce::String builtInId (const BuiltIn& b) { return "builtin:" + juce::String (b.file).upToLastOccurrenceOf (".", false, false); }

    juce::String roleText (int role)
    {
        return role == 1 ? tr ("kapanma") : role == 2 ? tr ("açılma") + " 2" : tr ("açılma");
    }

    const void* builtInData (const char* file, int& size)
    {
        for (int i = 0; i < MicVSTSoundData::namedResourceListSize; ++i)
            if (juce::String (MicVSTSoundData::originalFilenames[i]) == file)
                return MicVSTSoundData::getNamedResource (MicVSTSoundData::namedResourceList[i], size);
        size = 0;
        return nullptr;
    }

    const juce::StringArray& extensions() { static const juce::StringArray e { ".wav", ".flac", ".mp3", ".aiff", ".aif", ".ogg" }; return e; }

    bool readAll (juce::AudioFormatReader& r, juce::AudioBuffer<float>& out, double& rate)
    {
        if (r.lengthInSamples <= 0 || r.sampleRate < 8000.0 || r.sampleRate > 384000.0 || r.numChannels < 1
            || (double) r.lengthInSamples / r.sampleRate > CueLibrary::maxSeconds)
            return false;
        const int n = (int) r.lengthInSamples, ch = (int) juce::jmin (2u, r.numChannels);
        juce::AudioBuffer<float> tmp ((int) r.numChannels, n);
        if (! r.read (&tmp, 0, n, 0, true, true)) return false;
        out.setSize (2, n);
        if (r.numChannels == 1) { out.copyFrom (0, 0, tmp, 0, 0, n); out.copyFrom (1, 0, tmp, 0, 0, n); }
        else                    { out.copyFrom (0, 0, tmp, 0, 0, n); out.copyFrom (1, 0, tmp, juce::jmin (1, ch - 1), 0, n); }
        rate = r.sampleRate;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
                if (! std::isfinite (out.getSample (c, i))) return false;
        return true;
    }
}

//==============================================================================
juce::String CueSettings::encode() const
{
    return juce::String (enabled ? "1" : "0") + "|" + onId + "|" + offId + "|" + juce::String (volume);
}

CueSettings CueSettings::decode (const juce::String& text)
{
    CueSettings s;
    const auto p = juce::StringArray::fromTokens (text, "|", "");
    if (p.size() != 4) return s;
    s.enabled = p[0].trim() != "0";
    if (p[1].trim().isNotEmpty()) s.onId = p[1].trim();
    if (p[2].trim().isNotEmpty()) s.offId = p[2].trim();
    if (p[3].trim().containsOnly ("0123456789") && p[3].trim().isNotEmpty()) s.volume = juce::jlimit (0, 100, p[3].trim().getIntValue());
    return s;
}

//==============================================================================
juce::File CueLibrary::defaultUserFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("MicVST").getChildFile ("Sesler");
}

juce::AudioFormatManager& CueLibrary::formats()
{
    if (fm == nullptr)
    {
        fm = std::make_unique<juce::AudioFormatManager>();
        fm->registerBasicFormats();
       #if ! JUCE_USE_MP3AUDIOFORMAT
        // MP3 needs JUCE_USE_MP3AUDIOFORMAT (set for MicVSTCore in CMakeLists.txt)
       #endif
    }
    return *fm;
}

juce::Array<CueSound> CueLibrary::list()
{
    juce::Array<CueSound> result;
    for (auto& b : builtIns)
        result.add ({ builtInId (b), juce::String::fromUTF8 (b.name) + " - " + roleText (b.role), true, {} });

    skipped = 0;
    if (! folder.isDirectory()) return result;
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*");
    files.sort();
    for (auto& f : files)
    {
        if (! extensions().contains (f.getFileExtension().toLowerCase())) continue;
        auto& s = scanned[f.getFullPathName()];
        if (s.size != f.getSize() || s.modified != f.getLastModificationTime())
        {
            s.size = f.getSize(); s.modified = f.getLastModificationTime(); s.ok = false;
            if (auto r = std::unique_ptr<juce::AudioFormatReader> (formats().createReaderFor (f)))
            {
                juce::AudioBuffer<float> b; double rate = 0.0;
                s.ok = readAll (*r, b, rate);
            }
        }
        if (s.ok) result.add ({ "file:" + f.getFileName(), f.getFileNameWithoutExtension(), false, f });
        else      ++skipped;
    }
    return result;
}

bool CueLibrary::load (const juce::String& id, juce::AudioBuffer<float>& out, double& rate)
{
    if (id.startsWith ("builtin:"))
    {
        for (auto& b : builtIns)
            if (builtInId (b) == id)
            {
                int size = 0;
                const void* data = builtInData (b.file, size);
                if (data == nullptr) return false;
                juce::FlacAudioFormat flac;
                std::unique_ptr<juce::AudioFormatReader> r (flac.createReaderFor (new juce::MemoryInputStream (data, (size_t) size, false), true));
                return r != nullptr && readAll (*r, out, rate);
            }
        return false;
    }
    if (id.startsWith ("file:"))
    {
        const auto f = folder.getChildFile (id.fromFirstOccurrenceOf ("file:", false, false));
        if (! f.existsAsFile()) return false;
        std::unique_ptr<juce::AudioFormatReader> r (formats().createReaderFor (f));
        return r != nullptr && readAll (*r, out, rate);
    }
    return false;
}

//==============================================================================
namespace
{
   #if JUCE_WINDOWS
    // PlaySound: asynchronous, from memory, on Windows' default output; a new sound stops the one
    // still playing (fast mute / unmute never piles up). Nothing is opened while silent.
    struct WindowsBackend final : CuePlayer::Backend
    {
        bool play (const juce::MemoryBlock& wav) override
        {
            return PlaySoundW ((LPCWSTR) wav.getData(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT) != FALSE;
        }
        void stop() override { PlaySoundW (nullptr, nullptr, 0); }
        juce::String defaultOutputName() override { return devfmt::windowsDefaultOutputName(); }
    };
   #endif

    struct SilentBackend final : CuePlayer::Backend
    {
        bool play (const juce::MemoryBlock&) override { return false; }
        void stop() override {}
        juce::String defaultOutputName() override { return {}; }
    };
}

CuePlayer::CuePlayer()
{
   #if JUCE_WINDOWS
    backend = std::make_unique<WindowsBackend>();
   #else
    backend = std::make_unique<SilentBackend>();
   #endif
    setSettings (settings);
}

CuePlayer::~CuePlayer()
{
    if (backend != nullptr) backend->stop();   // PlaySound must not read our memory after this
}

void CuePlayer::setBackend (std::unique_ptr<Backend> b)
{
    if (backend != nullptr) backend->stop();
    backend = std::move (b);
    defaultCheckedAt = 0;
}

juce::MemoryBlock CuePlayer::makeWav (const juce::AudioBuffer<float>& buf, double rate, int volumePercent)
{
    const int n = buf.getNumSamples(), ch = 2;
    const double gain = juce::jlimit (0, 100, volumePercent) / 100.0;
    juce::MemoryOutputStream o;
    const auto u32 = [&] (juce::uint32 v) { o.writeInt ((int) v); };
    const auto u16 = [&] (int v) { o.writeShort ((short) v); };
    o.write ("RIFF", 4); u32 ((juce::uint32) (36 + n * ch * 2)); o.write ("WAVE", 4);
    o.write ("fmt ", 4); u32 (16); u16 (1); u16 (ch); u32 ((juce::uint32) rate); u32 ((juce::uint32) rate * ch * 2); u16 (ch * 2); u16 (16);
    o.write ("data", 4); u32 ((juce::uint32) (n * ch * 2));
    juce::uint64 rng = 0x2545F4914F6CDD1Dull;
    const auto uni = [&rng] { rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17; return (double) (rng >> 11) * (1.0 / 9007199254740992.0); };
    for (int i = 0; i < n; ++i)
        for (int c = 0; c < ch; ++c)
        {
            const double v = buf.getSample (juce::jmin (c, buf.getNumChannels() - 1), i) * gain * 32768.0;
            const double d = gain > 0.0 ? uni() - uni() : 0.0;   // TPDF dither (silence stays exact 0)
            u16 ((int) juce::jlimit (-32768.0, 32767.0, std::floor (v + d + 0.5)));
        }
    return o.getMemoryBlock();
}

bool CuePlayer::prepare (const juce::String& id, juce::MemoryBlock& out, juce::String& name)
{
    juce::AudioBuffer<float> buf; double rate = 0.0;
    if (! lib.load (id, buf, rate)) return false;
    out = makeWav (buf, rate, settings.volume);
    name.clear();
    for (auto& s : lib.list()) if (s.id == id) name = s.name;
    return true;
}

void CuePlayer::setSettings (const CueSettings& s)
{
    if (backend != nullptr) backend->stop();   // the old bytes may still be playing
    settings = s;
    // a sound that is gone (file deleted, renamed) plays the default instead
    if (! prepare (settings.onId, onWav, onName))   prepare (CueSettings::defaultOnId(), onWav, onName);
    if (! prepare (settings.offId, offWav, offName)) prepare (CueSettings::defaultOffId(), offWav, offName);
}

bool CuePlayer::outputIsCable()
{
    if (! cableName) return false;
    const auto cable = cableName();
    if (cable.isEmpty()) return false;
    const auto now = micvst::msCounter();
    if (defaultCheckedAt == 0 || now - defaultCheckedAt > 2000)   // the default output may change: checked at most every 2 s
    {
        cachedDefault = backend->defaultOutputName();
        defaultCheckedAt = now == 0 ? 1 : now;
    }
    return cachedDefault == cable;
}

void CuePlayer::play (bool muted)
{
    if (! settings.enabled || backend == nullptr) return;
    if (outputIsCable())
    {
        problem = tr ("Bildirim sesi çalınmadı: Windows'un varsayılan çıkışı sanal kablo (karşı taraf duyardı)");
        if (! problemLogged) { juce::Logger::writeToLog (problem); problemLogged = true; }
        return;
    }
    auto& wav = muted ? offWav : onWav;
    if (wav.getSize() == 0) return;
    ++plays;
    if (! backend->play (wav))
    {
        problem = tr ("Bildirim sesi çalınamadı (Windows'ta ses çıkışı yok ya da meşgul)");
        if (! problemLogged) { juce::Logger::writeToLog (problem); problemLogged = true; }
    }
    else
        problem.clear();
}

void CuePlayer::preview (const juce::String& id)
{
    if (backend == nullptr) return;
    backend->stop();
    juce::String name;
    if (prepare (id, previewWav, name) && ! outputIsCable())
        backend->play (previewWav);
}

juce::String CuePlayer::describe (bool muted) const
{
    return muted ? offName : onName;
}
