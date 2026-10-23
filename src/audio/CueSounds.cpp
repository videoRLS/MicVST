#include "audio/CueSounds.h"
#include "audio/DeviceFormat.h"
#include "Text.h"
#include <MicVSTSounds.h>
#include "audio/MsCounter.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>

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
    // MicVST's own little output for the sounds: waveOut on Windows' default output (WAVE_MAPPER),
    // 48 kHz 16-bit stereo, 5 blocks of 20 ms, fed by CueMixer from a thread of its own. The device
    // starts only once all blocks are queued (paused while they are written): with 4 x 10 ms and an
    // immediate start, a sleeping onboard output (Realtek speakers) crackled on its first sound. Opened when a
    // sound starts, closed 2 s after the last one ended - nothing is open while silent, and the next
    // sound goes to whatever the default output is then. If the device cannot be opened (another
    // program holds it exclusively, it is unplugged ...), that one sound is played with PlaySound
    // and the next sound tries the device again. If the device stops taking blocks for 1 s (pulled
    // out while playing) it is closed; the sound still playing is dropped, the next one reopens.
    struct WindowsBackend final : CuePlayer::Backend, private juce::Thread
    {
        static constexpr int blockFrames = 960, numBlocks = 5;
        static constexpr juce::uint32 idleCloseMs = 2000, stallMs = 1000;

        WindowsBackend() : juce::Thread ("MicVST cue sounds") { doneEvent = CreateEventW (nullptr, FALSE, FALSE, nullptr); }
        ~WindowsBackend() override
        {
            signalThreadShouldExit();
            wake.signal();
            if (doneEvent != nullptr) SetEvent (doneEvent);
            stopThread (2000);
            PlaySoundW (nullptr, nullptr, 0);
            if (doneEvent != nullptr) CloseHandle (doneEvent);
        }

        bool play (const juce::MemoryBlock& wav) override
        {
            {
                const std::lock_guard<std::mutex> g (lock);
                mixer.start (wav);
                pending = wav;    // for PlaySound, should the device not open
            }
            if (! isThreadRunning()) startThread (juce::Thread::Priority::high);
            wake.signal();
            return true;
        }
        void stop() override
        {
            { const std::lock_guard<std::mutex> g (lock); mixer.stop(); }
            PlaySoundW (nullptr, nullptr, 0);
        }
        juce::String defaultOutputName() override { return devfmt::windowsDefaultOutputName(); }

    private:
        void run() override
        {
            while (! threadShouldExit())
            {
                bool any = false;
                { const std::lock_guard<std::mutex> g (lock); any = ! mixer.idle(); }
                if (! any) { wake.wait (-1); continue; }
                if (! open())
                {
                    {
                        const std::lock_guard<std::mutex> g (lock);
                        mixer = CueMixer{};
                        PlaySoundW (nullptr, nullptr, 0);   // before its bytes are replaced
                        fallback = pending;
                    }
                    const bool ok = fallback.getSize() > 0
                                    && PlaySoundW ((LPCWSTR) fallback.getData(), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT) != FALSE;
                    if (! ok && ! failureLogged)
                    {
                        juce::Logger::writeToLog (tr ("Bildirim sesi çalınamadı (Windows'ta ses çıkışı yok ya da meşgul)"));
                        failureLogged = true;
                    }
                    continue;   // the next sound tries the device again
                }
                failureLogged = false;
                bool started = false;
                juce::uint32 silentSince = 0, lastDone = micvst::msCounter();
                while (! threadShouldExit())
                {
                    bool wrote = false, broken = false;
                    for (auto& h : headers)
                        if ((h.dwFlags & WHDR_DONE) != 0 || (h.dwFlags & WHDR_INQUEUE) == 0)
                        {
                            bool idleNow;
                            {
                                const std::lock_guard<std::mutex> g (lock);
                                mixer.render (reinterpret_cast<int16_t*> (h.lpData), blockFrames);
                                idleNow = mixer.idle();
                            }
                            h.dwFlags &= ~(DWORD) WHDR_DONE;
                            if (waveOutWrite (out, &h, sizeof (WAVEHDR)) != MMSYSERR_NOERROR) { broken = true; break; }
                            wrote = true;
                            const auto now = micvst::msCounter();
                            lastDone = now;
                            if (! idleNow) silentSince = 0;
                            else if (silentSince == 0) silentSince = now == 0 ? 1 : now;
                        }
                    if (! started && ! broken) { waveOutRestart (out); started = true; }   // every block is queued now
                    const auto now = micvst::msCounter();
                    if (broken || now - lastDone > stallMs)   // device gone or stuck: give up what plays now
                    {
                        { const std::lock_guard<std::mutex> g (lock); mixer = CueMixer{}; }
                        break;
                    }
                    if (silentSince != 0 && now - silentSince > idleCloseMs) break;
                    if (! wrote) WaitForSingleObject (doneEvent, 20);
                }
                close();
            }
            close();
        }

        bool open()
        {
            WAVEFORMATEX f {};
            f.wFormatTag = WAVE_FORMAT_PCM; f.nChannels = 2; f.nSamplesPerSec = (DWORD) CuePlayer::outputRate;
            f.wBitsPerSample = 16; f.nBlockAlign = 4; f.nAvgBytesPerSec = f.nSamplesPerSec * 4;
            if (waveOutOpen (&out, WAVE_MAPPER, &f, (DWORD_PTR) doneEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { out = nullptr; return false; }
            for (int i = 0; i < numBlocks; ++i)
            {
                auto& h = headers[(size_t) i];
                h = {};
                h.lpData = reinterpret_cast<LPSTR> (data[(size_t) i].data());
                h.dwBufferLength = (DWORD) (blockFrames * 4);
                waveOutPrepareHeader (out, &h, sizeof (WAVEHDR));
            }
            waveOutPause (out);   // started by run() once every block is queued
            return true;
        }
        void close()
        {
            if (out == nullptr) return;
            waveOutReset (out);
            for (auto& h : headers) waveOutUnprepareHeader (out, &h, sizeof (WAVEHDR));
            waveOutClose (out);
            out = nullptr;
        }

        std::mutex lock;
        CueMixer mixer;
        juce::MemoryBlock pending, fallback;
        juce::WaitableEvent wake;
        HANDLE doneEvent = nullptr;
        HWAVEOUT out = nullptr;
        std::array<WAVEHDR, numBlocks> headers {};
        std::array<std::array<int16_t, blockFrames * 2>, numBlocks> data {};
        bool failureLogged = false;   // the cue thread only
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
    juce::String key = id;
    if (id.startsWith ("file:"))
    {
        const auto f = lib.getUserFolder().getChildFile (id.fromFirstOccurrenceOf ("file:", false, false));
        if (! f.existsAsFile()) return false;
        key << "|" << juce::String (f.getSize()) << "|" << juce::String (f.getLastModificationTime().toMilliseconds());
    }
    auto it = levelCache.find (id);
    if (it == levelCache.end() || it->second.key != key)
    {
        juce::AudioBuffer<float> buf; double rate = 0.0;
        if (! lib.load (id, buf, rate)) return false;
        levelCache[id] = Levelled { key, levelled (buf, rate) };
        for (auto e = levelCache.begin(); e != levelCache.end();)   // keep the chosen two and this one
            e = (e->first == id || e->first == settings.onId || e->first == settings.offId) ? std::next (e) : levelCache.erase (e);
        it = levelCache.find (id);
    }
    out = makeWav (it->second.buf, outputRate, settings.volume);
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

//==============================================================================
void CueMixer::start (const juce::MemoryBlock& wav)
{
    for (auto& v : voices)
        if (v.fade < 0) v.fade = fadeFrames;
    const auto* p = static_cast<const uint8_t*> (wav.getData());
    if (wav.getSize() < 44) return;
    const uint32_t bytes = juce::jmin ((uint32_t) (wav.getSize() - 44),
                                       (uint32_t) (p[40] | (p[41] << 8) | (p[42] << 16) | ((uint32_t) p[43] << 24)));
    const size_t n = bytes / 4;
    if (n == 0) return;
    Voice v;
    v.l.resize (n); v.r.resize (n);
    const auto* s = reinterpret_cast<const int16_t*> (p + 44);
    for (size_t i = 0; i < n; ++i) { v.l[i] = (float) s[2 * i]; v.r[i] = (float) s[2 * i + 1]; }
    voices.push_back (std::move (v));
}

void CueMixer::stop()
{
    for (auto& v : voices)
        if (v.fade < 0) v.fade = fadeFrames;
}

void CueMixer::render (int16_t* out, int frames)
{
    for (int i = 0; i < frames; ++i)
    {
        double l = 0.0, r = 0.0;
        for (auto& v : voices)
        {
            if (v.pos >= v.l.size() || v.fade == 0) continue;
            double g = 1.0;
            if (v.fade > 0)
            {
                g = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (double) v.fade / (double) fadeFrames);
                --v.fade;
            }
            l += g * v.l[v.pos]; r += g * v.r[v.pos];
            ++v.pos;
        }
        out[2 * i]     = (int16_t) juce::jlimit (-32768.0, 32767.0, std::floor (l + 0.5));
        out[2 * i + 1] = (int16_t) juce::jlimit (-32768.0, 32767.0, std::floor (r + 0.5));
    }
    voices.erase (std::remove_if (voices.begin(), voices.end(), [] (const Voice& v) { return v.pos >= v.l.size() || v.fade == 0; }),
                  voices.end());
}

//==============================================================================
double CuePlayer::loudness (const juce::AudioBuffer<float>& b)
{
    // ITU-R BS.1770 K-weighting at 48 kHz (both stages), blocks of 100 ms every 25 ms, absolute
    // gate -70 LUFS, relative gate -10 dB; a sound shorter than a block is one block
    const int n = b.getNumSamples(), chans = juce::jmin (2, b.getNumChannels());
    if (n <= 0 || chans <= 0) return -200.0;
    std::vector<std::vector<double>> y ((size_t) chans, std::vector<double> ((size_t) n));
    for (int c = 0; c < chans; ++c)
    {
        double s1x1 = 0, s1x2 = 0, s1y1 = 0, s1y2 = 0, s2x1 = 0, s2x2 = 0, s2y1 = 0, s2y2 = 0;
        for (int i = 0; i < n; ++i)
        {
            const double x = b.getSample (c, i);
            const double a = 1.53512485958697 * x - 2.69169618940638 * s1x1 + 1.19839281085285 * s1x2
                             + 1.69065929318241 * s1y1 - 0.73248077421585 * s1y2;
            s1x2 = s1x1; s1x1 = x; s1y2 = s1y1; s1y1 = a;
            const double o = a - 2.0 * s2x1 + s2x2 + 1.99004745483398 * s2y1 - 0.99007225036621 * s2y2;
            s2x2 = s2x1; s2x1 = a; s2y2 = s2y1; s2y1 = o;
            y[(size_t) c][(size_t) i] = o;
        }
    }
    const int block = juce::jmin (n, (int) (0.1 * outputRate)), hop = juce::jmax (1, (int) (0.025 * outputRate));
    std::vector<double> z;
    for (int s = 0; s + block <= n; s += hop)
    {
        double sum = 0.0;
        for (int c = 0; c < chans; ++c)
        {
            double e = 0.0;
            for (int i = s; i < s + block; ++i) e += y[(size_t) c][(size_t) i] * y[(size_t) c][(size_t) i];
            sum += e / block;
        }
        z.push_back (sum);
        if (block == n) break;
    }
    const auto lufs = [] (double v) { return -0.691 + 10.0 * std::log10 (juce::jmax (v, 1.0e-30)); };
    double mean = 0.0; int k = 0;
    for (auto v : z) if (lufs (v) > -70.0) { mean += v; ++k; }
    if (k == 0) return -200.0;
    const double rel = lufs (mean / k) - 10.0;
    double m2 = 0.0; int k2 = 0;
    for (auto v : z) if (lufs (v) > -70.0 && lufs (v) > rel) { m2 += v; ++k2; }
    return k2 == 0 ? -200.0 : lufs (m2 / k2);
}

juce::AudioBuffer<float> CuePlayer::levelled (const juce::AudioBuffer<float>& src, double rate)
{
    const int chans = juce::jmin (2, juce::jmax (1, src.getNumChannels()));
    juce::AudioBuffer<float> b;
    if (std::abs (rate - outputRate) < 0.5 || rate <= 0.0)
    {
        b.setSize (2, src.getNumSamples());
        for (int c = 0; c < 2; ++c) b.copyFrom (c, 0, src, juce::jmin (c, chans - 1), 0, src.getNumSamples());
    }
    else
    {
        const double ratio = rate / outputRate;   // input samples per output sample
        const int outN = (int) std::ceil (src.getNumSamples() / ratio);
        const int pad = 256;
        b.setSize (2, outN);
        for (int c = 0; c < 2; ++c)
        {
            std::vector<float> in ((size_t) src.getNumSamples() + (size_t) pad * 2 + (size_t) (outN * ratio) + 8, 0.0f);
            std::memcpy (in.data(), src.getReadPointer (juce::jmin (c, chans - 1)), sizeof (float) * (size_t) src.getNumSamples());
            juce::WindowedSincInterpolator interp;
            const int lat = (int) std::round (interp.getBaseLatency() / ratio);   // base latency is in input samples
            std::vector<float> tmp ((size_t) outN + (size_t) lat + 8);
            interp.process (ratio, in.data(), tmp.data(), (int) tmp.size());
            std::memcpy (b.getWritePointer (c), tmp.data() + juce::jmin (lat, (int) tmp.size() - outN), sizeof (float) * (size_t) outN);
        }
    }
    const int n = b.getNumSamples();
    if (n == 0) return b;
    const double l = loudness (b);
    const float peak = juce::jmax (b.getMagnitude (0, 0, n), b.getMagnitude (1, 0, n));
    double gainDb = l > -150.0 ? fullScaleLufs - l : 0.0;
    if (peak > 0.0f)
        gainDb = juce::jmin (gainDb, peakCeilingDb - 20.0 * std::log10 ((double) peak));
    b.applyGain ((float) juce::Decibels::decibelsToGain (gainDb, -200.0));
    // the ends: 1 ms in, 5 ms out (a file that starts or stops mid-wave would click)
    const int in = juce::jmin (n / 4, (int) (0.001 * outputRate)), outF = juce::jmin (n / 4, (int) (0.005 * outputRate));
    for (int c = 0; c < 2; ++c)
    {
        auto* d = b.getWritePointer (c);
        for (int i = 0; i < in; ++i)   d[i] *= (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (i + 0.5) / in));
        for (int i = 0; i < outF; ++i) d[n - 1 - i] *= (float) (0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (i + 0.5) / outF));
    }
    return b;
}
