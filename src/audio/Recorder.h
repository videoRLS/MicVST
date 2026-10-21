#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>

// "Kayıt": records what goes to the cable (the processed voice, as the other side hears it - Sustur
// and Ham ses included) into a file in a folder the user picks. The option set follows what
// professional recorders/DAWs offer (REAPER, Audition, Audacity), minus what cannot add anything
// to a 32-bit float voice signal:
//  * WAV: 16 / 24 / 32 bit PCM, 32 bit float. 16 bit: TPDF dither. Broadcast WAV (bext chunk:
//    originator MicVST, date and time); past 4 GB it becomes RF64 by itself (no 4 GB limit).
//  * FLAC: 16 / 24 bit, lossless, compression level 0-8 (5 = the FLAC default).
//  * MP3 (LAME 4.0): CBR 128 / 160 / 192 / 224 / 256 / 320 kbps, VBR V0 / V2 / V4 (LAME's
//    "extreme" / "standard" / "medium"); joint stereo; LAME/Xing tag (exact length, gapless).
//  * Sample rate: 44.1 or 48 kHz (the delivery standards), or 88.2 / 96 / 176.4 / 192 kHz when
//    the microphone itself runs that fast (no up-sampling to "hi-res": it adds nothing). Converted
//    with a 140 dB linear-phase converter (Resampler.h) when it differs from the processing rate.
//    MP3: 44.1 / 48 kHz only (MPEG-1).
//  * Channels: stereo, or mono (L+R)/2 - a voice is mono, the file half the size.
//
// Realtime: push() (the plug-in thread) only copies into a lock-free FIFO that start() allocated;
// a writer thread (above normal priority, below the audio) drains it, converts and writes - nothing on the audio thread ever waits,
// allocates, encodes or touches the disk. A full FIFO (the disk stalled for 30 s) drops audio and
// counts it. WAV: the header is rewritten every second, so after a crash the file is valid up to
// the last second. A failed write or a nearly full disk stops the recording with a message.
struct RecordSettings
{
    enum class Format { wav, flac, mp3 };
    enum class Mp3Mode { cbr, vbr };
    Format format = Format::wav;
    int wavBits = 24;          // 16, 24, 32 (PCM) - or 32 float (wavFloat)
    bool wavFloat = false;
    int flacBits = 24;         // 16, 24
    int flacLevel = 5;         // 0 (fastest) .. 8 (smallest)
    Mp3Mode mp3Mode = Mp3Mode::cbr;
    int mp3Kbps = 320;         // CBR
    int mp3Vbr = 0;            // VBR quality: 0, 2, 4
    int sampleRate = 48000;    // see allowedRates()
    int channels = 2;          // 2 or 1
    juce::File folder;         // empty: defaultFolder()

    static juce::File defaultFolder();   // Documents\MicVST Kayıtları
    juce::File folderOrDefault() const { return folder == juce::File() ? defaultFolder() : folder; }
    juce::String extension() const;      // ".wav" / ".flac" / ".mp3"
    juce::String describe() const;       // "WAV 24 bit PCM, 48 kHz, stereo" (interface language)

    // rates offered while processing runs at 'processRate'; the rate a recording really gets
    static juce::Array<int> allowedRates (Format, double processRate);
    int effectiveRate (double processRate) const;

    // config.xml: "wav:24|48000|2" "wav:32f|.." "flac:24:5|.." "mp3:cbr320|.." "mp3:v0|.."
    // (older "wav:24" accepted); anything unknown -> the default (WAV 24 bit, 48 kHz, stereo)
    juce::String encode() const;
    static RecordSettings decode (const juce::String& text, const juce::String& folder);
    bool operator== (const RecordSettings& o) const { return encode() == o.encode() && folder == o.folder; }

    static const juce::Array<int>& mp3CbrRates() { static const juce::Array<int> r { 128, 160, 192, 224, 256, 320 }; return r; }
    static const juce::Array<int>& mp3VbrLevels() { static const juce::Array<int> r { 0, 2, 4 }; return r; }
    static const juce::Array<int>& allRates() { static const juce::Array<int> r { 44100, 48000, 88200, 96000, 176400, 192000 }; return r; }
};

class Recorder
{
public:
    Recorder();
    ~Recorder();
    static constexpr double fifoSeconds = 30.0;   // what the disk may lag behind before audio is dropped

    // --- message thread ---
    // Starts a new file (never over an existing one). "" = recording; otherwise why not (translated).
    juce::String start (const RecordSettings&, double processRate, int numChannels = 2);
    struct Result
    {
        juce::File file;
        double seconds = 0.0;
        int64_t droppedFrames = 0;
        int64_t framesWritten = 0;      // at the file's rate
        bool verified = false;          // read back: format, rate, channels and length as written
        juce::String problem;           // a write error, if any
    };
    Result stop();                      // writes the rest, closes and checks the file
    bool isRecording() const noexcept   { return session != nullptr; }
    double getSeconds() const noexcept; // audio taken so far
    juce::File getFile() const;
    int64_t getDroppedFrames() const noexcept;
    // a write error / full disk seen by the writer thread (the caller then stops)
    bool hasFailed() const noexcept;
    juce::String getFailure() const;

    // --- the realtime (plug-in) thread ---
    // processRate: the rate of these samples; if it is not the one the recording started with
    // (the microphone was switched to another rate), nothing is taken and hasFailed() says so.
    void push (const float* const* channels, int numChannels, int numFrames, double processRate = 0.0) noexcept;

    // a new file name in the folder: MicVST_2026-10-21_21-58-03.wav (+ _2, _3 ... if taken)
    static juce::File uniqueFile (const juce::File& folder, const juce::String& extension, juce::Time when);

    // MP3 check: walks the frames; rate, channels and the number of samples the LAME tag says
    // (encoder delay and padding removed). false if it is not a whole, consistent MP3 file.
    struct Mp3Info { int sampleRate = 0, channels = 0, frames = 0; int64_t samples = -1; bool vbr = false; int kbps = 0; };
    static bool inspectMp3 (const juce::File&, Mp3Info&);

    struct Session;
private:
    std::unique_ptr<Session> session;             // message thread
    std::atomic<Session*> active { nullptr };     // what push() writes to
    std::atomic<bool> inPush { false };
};
