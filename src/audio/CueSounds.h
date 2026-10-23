#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>
#include <map>
#include <vector>
#include <cstdint>

// "Bildirim sesleri": a short sound when the microphone is muted / unmuted (Sustur - the button,
// the shortcut keys and the tray all go through AudioEngine::setMuted, which plays it in the same
// call). Played on Windows' default output (the user's headphones / speakers), never into the
// virtual cable: if the default output is the cable itself, nothing is played.
//
// Sounds: 16 built in (resources/sounds, FLAC in the exe) and every suitable file in the user's
// folder (%APPDATA%\MicVST\Sesler): WAV, FLAC, MP3, AIFF, OGG, at most 10 s - listed by themselves,
// no code change needed. Default: Ahşap (açılma / kapanma).
//
// Level (1.2.3): every sound - built in or the user's own - is measured when it is prepared and
// brought to the same loudness, so the volume slider means the same for all of them: 100 % =
// fullScaleLufs, its peaks never above peakCeilingDb (a very spiky file stays a little quieter
// rather than going over). Default volume 40 % (-8 dB). The user's file is never changed.
// Playback (Windows): MicVST mixes the sounds itself (CueMixer -> waveOut on the default output,
// opened while a sound plays): a new sound fades the one still playing out in 10 ms instead of
// cutting it - fast mute / unmute makes no click.
struct CueSound
{
    juce::String id;          // "builtin:01_ahsap_ac" or "file:<file name>"
    juce::String name;        // shown: "Ahşap - açılma" / the file name without extension
    bool builtIn = true;
    juce::File file;          // user sounds
};

struct CueSettings
{
    bool enabled = true;
    juce::String onId = defaultOnId(), offId = defaultOffId();
    int volume = 40;          // percent of the levelled sound (100 % = CuePlayer::fullScaleLufs)

    static juce::String defaultOnId()  { return "builtin:01_ahsap_ac"; }
    static juce::String defaultOffId() { return "builtin:01_ahsap_kapat"; }
    bool isDefault() const { return enabled && onId == defaultOnId() && offId == defaultOffId() && volume == 40; }

    // config.xml: "1|builtin:01_ahsap_ac|builtin:01_ahsap_kapat|40"; anything unreadable -> default
    juce::String encode() const;
    static CueSettings decode (const juce::String&);
    bool operator== (const CueSettings& o) const { return encode() == o.encode(); }
};

class CueLibrary
{
public:
    static constexpr double maxSeconds = 10.0;
    static juce::File defaultUserFolder();                 // %APPDATA%\MicVST\Sesler
    void setUserFolder (const juce::File& f) { folder = f; }
    juce::File getUserFolder() const { return folder; }

    // built-ins, then the user's suitable files (sorted by name); rescans the folder (cheap: a
    // file is decoded once, again only when its size or date changes)
    juce::Array<CueSound> list();
    // the samples of a sound (float, its own rate); false if unknown / unreadable
    bool load (const juce::String& id, juce::AudioBuffer<float>& out, double& rate);
    int skippedFiles() const { return skipped; }           // in the folder but not suitable (last scan)

private:
    struct Scanned { int64_t size = 0; juce::Time modified; bool ok = false; };
    juce::File folder = defaultUserFolder();
    std::map<juce::String, Scanned> scanned;
    int skipped = 0;
    juce::AudioFormatManager& formats();
    std::unique_ptr<juce::AudioFormatManager> fm;
};

// The sounds playing now, mixed: start() fades whatever still plays out over fadeFrames (raised
// cosine) while the new sound begins - no step in the output however fast it is called. Not
// thread-safe by itself (the Windows backend locks around it). Sounds: 16-bit stereo WAVs from
// CuePlayer::makeWav at CuePlayer::outputRate.
class CueMixer
{
public:
    static constexpr int fadeFrames = 480;                 // 10 ms at 48 kHz
    void start (const juce::MemoryBlock& wav);
    void stop();                                           // fades everything out
    // interleaved stereo 16-bit; always fills all frames (silence when idle)
    void render (int16_t* out, int frames);
    bool idle() const { return voices.empty(); }
    int voiceCount() const { return (int) voices.size(); }

private:
    struct Voice { std::vector<float> l, r; size_t pos = 0; int fade = -1; };   // fade: frames left, -1 = none
    std::vector<Voice> voices;
};

class CuePlayer
{
public:
    CuePlayer();
    ~CuePlayer();

    // message thread
    void setSettings (const CueSettings&);                 // prepares both sounds at once (no work when playing)
    const CueSettings& getSettings() const { return settings; }
    CueLibrary& library() { return lib; }

    // muted = true: the "kapanma" sound, false: "açılma". Returns at once (asynchronous playback).
    void play (bool muted);
    void preview (const juce::String& id);                  // plays this sound now (Settings)
    juce::String describe (bool muted) const;               // the name of what play() would play

    // the virtual cable's name: never play there (Windows default output = the cable)
    std::function<juce::String()> cableName;

    // where the sound goes; tests replace it. play: the bytes are a complete WAV file and stay
    // valid until the next play / stop.
    struct Backend
    {
        virtual ~Backend() = default;
        virtual bool play (const juce::MemoryBlock& wav) = 0;
        virtual void stop() = 0;
        virtual juce::String defaultOutputName() = 0;
    };
    void setBackend (std::unique_ptr<Backend>);

    // a 16-bit PCM WAV in memory, at the sound's rate, scaled by volume (0..100 %)
    static juce::MemoryBlock makeWav (const juce::AudioBuffer<float>&, double rate, int volumePercent);

    // the level every sound is brought to (100 %): loudness of 100 ms blocks (ITU-R BS.1770
    // K-weighting and gating; short sounds are judged by their loud part), peaks at most -3 dBFS
    static constexpr double outputRate = 48000.0, fullScaleLufs = -15.5, peakCeilingDb = -3.0;
    static double loudness (const juce::AudioBuffer<float>&);                    // at outputRate, LUFS
    // the sound at outputRate, at fullScaleLufs (or lower: the peak ceiling), 1 ms in / 5 ms out
    static juce::AudioBuffer<float> levelled (const juce::AudioBuffer<float>&, double rate);

    int playCount() const { return plays; }
    juce::String lastProblem() const { return problem; }

private:
    bool prepare (const juce::String& id, juce::MemoryBlock& out, juce::String& name);
    bool outputIsCable();

    CueLibrary lib;
    CueSettings settings;
    juce::MemoryBlock onWav, offWav, previewWav;
    juce::String onName, offName;
    std::unique_ptr<Backend> backend;
    juce::String cachedDefault;
    juce::uint32 defaultCheckedAt = 0;
    int plays = 0;
    juce::String problem;
    bool problemLogged = false;
};
