#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <memory>

// "Kayıt": records what goes to the cable (the processed voice, as the other side hears it - Sustur
// and Ham ses included) into a file in a folder the user picks.
//  * WAV: 16 / 24 / 32 bit PCM or 32 bit float (JUCE's WAV writer); 16 bit gets TPDF dither.
//  * FLAC: 16 / 24 bit, lossless (libFLAC inside JUCE; its writer has no 32 bit).
//  * MP3: 128 / 192 / 256 / 320 kbps constant bit rate - Windows' own MP3 encoder (Media
//    Foundation, Windows 8 and later); offered only where Windows has it (mp3Available()).
//  The sample rate is the processing rate (the microphone's, 48 kHz normally).
//
// Realtime: push() (the plug-in thread) only copies into a lock-free FIFO that was allocated by
// start(); a writer thread drains it, converts and writes; nothing on the audio thread ever waits,
// allocates, encodes or touches the disk. A full FIFO (the disk stalled for 10 s) drops audio and
// counts it. The WAV header is rewritten every second, so after a crash the file is valid up to
// the last second. A failed write or a nearly full disk stops the recording with a message.
struct RecordSettings
{
    enum class Format { wav, flac, mp3 };
    Format format = Format::wav;
    int wavBits = 24;        // 16, 24, 32 (PCM) - or 32 float (wavFloat)
    bool wavFloat = false;
    int flacBits = 24;       // 16, 24
    int mp3Kbps = 320;       // 128, 192, 256, 320 (CBR)
    juce::File folder;       // empty: defaultFolder()

    static juce::File defaultFolder();   // Documents\MicVST Kayıtları
    juce::File folderOrDefault() const { return folder == juce::File() ? defaultFolder() : folder; }
    juce::String extension() const;      // ".wav" / ".flac" / ".mp3"
    juce::String describe() const;       // "WAV, 24 bit PCM" (in the interface language)

    // config.xml: "wav:24" "wav:32f" "flac:16" "mp3:320"; anything unknown -> the default (WAV 24 bit)
    juce::String encode() const;
    static RecordSettings decode (const juce::String& text, const juce::String& folder);
    bool operator== (const RecordSettings& o) const { return encode() == o.encode() && folder == o.folder; }

    static bool mp3Available();          // Windows' MP3 encoder is installed (cached)
    static const juce::Array<int>& mp3Rates() { static const juce::Array<int> r { 128, 192, 256, 320 }; return r; }
};

class Recorder
{
public:
    Recorder();
    ~Recorder();

    // --- message thread ---
    // Starts a new file (never over an existing one). "" = recording; otherwise why not (translated).
    juce::String start (const RecordSettings&, double sampleRate, int numChannels = 2);
    struct Result
    {
        juce::File file;
        double seconds = 0.0;
        int64_t droppedFrames = 0;
        bool verified = false;          // read back: format, rate, channels and length as written
        juce::String problem;           // a write error, if any
    };
    Result stop();                      // writes the rest, closes and checks the file
    bool isRecording() const noexcept   { return session != nullptr; }
    double getSampleRate() const noexcept;
    double getSeconds() const noexcept; // audio taken so far
    juce::File getFile() const;
    int64_t getDroppedFrames() const noexcept;
    // a write error / full disk seen by the writer thread (the caller then stops)
    bool hasFailed() const noexcept;
    juce::String getFailure() const;

    // --- the realtime (plug-in) thread ---
    void push (const float* const* channels, int numChannels, int numFrames) noexcept;

    // a new file name in the folder: MicVST_2026-10-21_21-58-03.wav (+ _2, _3 ... if taken)
    static juce::File uniqueFile (const juce::File& folder, const juce::String& extension, juce::Time when);

    struct Session;
private:
    std::unique_ptr<Session> session;             // message thread
    std::atomic<Session*> active { nullptr };     // what push() writes to
    std::atomic<bool> inPush { false };
};
