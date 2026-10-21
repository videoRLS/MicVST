#include "audio/Recorder.h"
#include "Text.h"
#include <cmath>
#include <thread>

using micvst::tr;

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <mfapi.h>
 #include <mfidl.h>
 #include <mfreadwrite.h>
 #include <mferror.h>
 #pragma comment (lib, "mfplat.lib")
 #pragma comment (lib, "mfreadwrite.lib")
 #pragma comment (lib, "mfuuid.lib")
 #pragma comment (lib, "ole32.lib")
#endif

//==============================================================================
juce::File RecordSettings::defaultFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile (juce::String::fromUTF8 ("MicVST Kayıtları"));
}

juce::String RecordSettings::extension() const
{
    switch (format)
    {
        case Format::flac: return ".flac";
        case Format::mp3:  return ".mp3";
        default:           return ".wav";
    }
}

juce::String RecordSettings::describe() const
{
    switch (format)
    {
        case Format::flac: return "FLAC, " + juce::String (flacBits) + " bit";
        case Format::mp3:  return "MP3, " + juce::String (mp3Kbps) + " kbps (CBR)";
        default:           return "WAV, " + juce::String (wavBits) + (wavFloat ? tr (" bit float") : juce::String (" bit PCM"));
    }
}

juce::String RecordSettings::encode() const
{
    switch (format)
    {
        case Format::flac: return "flac:" + juce::String (flacBits);
        case Format::mp3:  return "mp3:" + juce::String (mp3Kbps);
        default:           return "wav:" + juce::String (wavBits) + (wavFloat ? "f" : "");
    }
}

RecordSettings RecordSettings::decode (const juce::String& text, const juce::String& folder)
{
    RecordSettings s;
    if (folder.isNotEmpty() && juce::File::isAbsolutePath (folder)) s.folder = juce::File (folder);
    const auto kind = text.upToFirstOccurrenceOf (":", false, false).trim().toLowerCase();
    const auto q = text.fromFirstOccurrenceOf (":", false, false).trim().toLowerCase();
    if (kind == "wav")
    {
        if (q == "16" || q == "24" || q == "32") { s.wavBits = q.getIntValue(); s.wavFloat = false; }
        else if (q == "32f")                     { s.wavBits = 32; s.wavFloat = true; }
    }
    else if (kind == "flac" && (q == "16" || q == "24"))
    {
        s.format = Format::flac; s.flacBits = q.getIntValue();
    }
    else if (kind == "mp3" && mp3Rates().contains (q.getIntValue()) && q.containsOnly ("0123456789"))
    {
        s.format = Format::mp3; s.mp3Kbps = q.getIntValue();
    }
    return s;
}

//==============================================================================
namespace
{
    // what the writer thread hands the samples to
    struct Sink
    {
        virtual ~Sink() = default;
        virtual juce::String open (const juce::File&, double rate, int channels) = 0;   // "" = fine
        virtual bool write (const float* const* data, int channels, int frames) = 0;
        virtual void flush() {}
        virtual juce::String finish() { return {}; }                                    // "" = fine
    };

    // WAV / FLAC through JUCE's writers. Integer formats: clipped to full scale (no wrap-around);
    // 16 bit with TPDF dither (+-1 LSB triangular), so quiet passages keep a smooth noise floor
    // instead of distortion. 24 / 32 bit and float: written as they are.
    struct JuceSink final : Sink
    {
        JuceSink (std::unique_ptr<juce::AudioFormat> f, int bitsIn, bool isFloatIn) : fmt (std::move (f)), bits (bitsIn), isFloat (isFloatIn) {}

        juce::String open (const juce::File& file, double rate, int channels) override
        {
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
            if (! static_cast<juce::FileOutputStream&> (*out).openedOk())
                return tr ("Dosya oluşturulamadı: ") + file.getFullPathName();
            const auto opts = juce::AudioFormatWriterOptions{}
                                  .withSampleRate (rate)
                                  .withNumChannels (channels)
                                  .withBitsPerSample (bits)
                                  .withSampleFormat (isFloat ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                             : juce::AudioFormatWriterOptions::SampleFormat::integral);
            writer = fmt->createWriterFor (out, opts);
            if (writer == nullptr)
                return tr ("Bu biçimde kayıt başlatılamadı: ") + fmt->getFormatName() + " " + juce::String (bits) + " bit";
            scratch.setSize (channels, chunk);
            return {};
        }

        bool write (const float* const* data, int channels, int frames) override
        {
            if (isFloat)
                return writer->writeFromFloatArrays (data, channels, frames);
            for (int start = 0; start < frames; start += chunk)
            {
                const int n = juce::jmin (chunk, frames - start);
                const double lsb = bits == 16 ? 1.0 / 32768.0 : 0.0;
                for (int c = 0; c < channels; ++c)
                {
                    const float* src = data[c] + start;
                    float* dst = scratch.getWritePointer (c);
                    for (int i = 0; i < n; ++i)
                    {
                        double v = src[i];
                        if (lsb > 0.0) v += lsb * (uniform() - uniform());   // TPDF dither
                        dst[i] = (float) juce::jlimit (-1.0, 1.0 - lsbFor (bits), v);
                    }
                }
                if (! writer->writeFromAudioSampleBuffer (scratch, 0, n)) return false;
            }
            return true;
        }

        void flush() override { if (writer != nullptr) writer->flush(); }   // WAV: the header now covers everything written
        juce::String finish() override { writer.reset(); return {}; }      // writes the final header and closes

        static double lsbFor (int b) { return std::ldexp (1.0, -(b - 1)); }
        double uniform() noexcept
        {
            rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;   // xorshift64
            return (double) (rng >> 11) * (1.0 / 9007199254740992.0);
        }

        static constexpr int chunk = 4096;
        std::unique_ptr<juce::AudioFormat> fmt;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        juce::AudioBuffer<float> scratch;
        const int bits;
        const bool isFloat;
        uint64_t rng = 0x9E3779B97F4A7C15ull;
    };

   #if JUCE_WINDOWS
    template <typename T> void release (T*& p) { if (p != nullptr) { p->Release(); p = nullptr; } }

    // MP3 with Windows' own encoder: a Media Foundation sink writer, 16-bit PCM in, MP3 (CBR) out
    // into an .mp3 file. Created, used and finished on the writer thread (COM: multithreaded).
    struct Mp3Sink final : Sink
    {
        explicit Mp3Sink (int kbpsIn) : kbps (kbpsIn) {}
        ~Mp3Sink() override { close(); }

        juce::String open (const juce::File& file, double rateIn, int channelsIn) override
        {
            rate = rateIn; channels = channelsIn;
            comInit = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED));
            if (FAILED (MFStartup (MF_VERSION, MFSTARTUP_LITE))) return tr ("MP3: Windows Media Foundation başlatılamadı");
            mfStarted = true;

            // the encoder's own output types: the one with this rate, channels and bit rate
            IMFCollection* types = nullptr;
            IMFMediaType* outType = nullptr;
            if (SUCCEEDED (MFTranscodeGetAudioOutputAvailableTypes (MFAudioFormat_MP3, MFT_ENUM_FLAG_ALL, nullptr, &types)) && types != nullptr)
            {
                DWORD n = 0;
                types->GetElementCount (&n);
                for (DWORD i = 0; i < n && outType == nullptr; ++i)
                {
                    IUnknown* u = nullptr;
                    if (FAILED (types->GetElement (i, &u)) || u == nullptr) continue;
                    IMFMediaType* t = nullptr;
                    if (SUCCEEDED (u->QueryInterface (IID_PPV_ARGS (&t))))
                    {
                        UINT32 sr = 0, ch = 0, bytes = 0;
                        t->GetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, &sr);
                        t->GetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, &ch);
                        t->GetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, &bytes);
                        if ((double) sr == rate && (int) ch == channels && (int) bytes * 8 == kbps * 1000) { outType = t; t = nullptr; }
                    }
                    release (t);
                    u->Release();
                }
            }
            release (types);
            if (outType == nullptr)
                return tr ("MP3: Windows'un kodlayıcısı bu ayarı desteklemiyor: ") + juce::String (kbps) + " kbps, "
                       + juce::String ((int) rate) + " Hz, " + juce::String (channels) + tr (" kanal");

            IMFAttributes* attrs = nullptr;
            MFCreateAttributes (&attrs, 1);
            if (attrs != nullptr) attrs->SetGUID (MF_TRANSCODE_CONTAINERTYPE, MFTranscodeContainerType_MP3);
            const HRESULT hr = MFCreateSinkWriterFromURL (file.getFullPathName().toWideCharPointer(), nullptr, attrs, &writer);
            release (attrs);
            if (FAILED (hr) || writer == nullptr) { release (outType); return tr ("Dosya oluşturulamadı: ") + file.getFullPathName(); }

            const bool streamOk = SUCCEEDED (writer->AddStream (outType, &stream));
            release (outType);
            if (! streamOk) return tr ("MP3: kodlayıcı akışı eklenemedi");

            IMFMediaType* inType = nullptr;
            MFCreateMediaType (&inType);
            if (inType == nullptr) return tr ("MP3: kodlayıcı akışı eklenemedi");
            inType->SetGUID (MF_MT_MAJOR_TYPE, MFMediaType_Audio);
            inType->SetGUID (MF_MT_SUBTYPE, MFAudioFormat_PCM);
            inType->SetUINT32 (MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
            inType->SetUINT32 (MF_MT_AUDIO_SAMPLES_PER_SECOND, (UINT32) rate);
            inType->SetUINT32 (MF_MT_AUDIO_NUM_CHANNELS, (UINT32) channels);
            inType->SetUINT32 (MF_MT_AUDIO_BLOCK_ALIGNMENT, (UINT32) (2 * channels));
            inType->SetUINT32 (MF_MT_AUDIO_AVG_BYTES_PER_SECOND, (UINT32) (2 * channels * (int) rate));
            inType->SetUINT32 (MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
            const bool inOk = SUCCEEDED (writer->SetInputMediaType (stream, inType, nullptr));
            release (inType);
            if (! inOk) return tr ("MP3: kodlayıcı 16 bit PCM girişini kabul etmedi");
            if (FAILED (writer->BeginWriting())) return tr ("MP3: kodlama başlatılamadı");
            return {};
        }

        bool write (const float* const* data, int ch, int frames) override
        {
            if (writer == nullptr) return false;
            IMFMediaBuffer* buffer = nullptr;
            const DWORD bytes = (DWORD) (frames * ch * 2);
            if (FAILED (MFCreateMemoryBuffer (bytes, &buffer)) || buffer == nullptr) return false;
            BYTE* p = nullptr;
            bool ok = SUCCEEDED (buffer->Lock (&p, nullptr, nullptr));
            if (ok)
            {
                auto* s = reinterpret_cast<int16_t*> (p);
                for (int i = 0; i < frames; ++i)
                    for (int c = 0; c < ch; ++c)
                        *s++ = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround ((double) data[c][i] * 32767.0));
                buffer->Unlock();
                buffer->SetCurrentLength (bytes);
                IMFSample* sample = nullptr;
                ok = SUCCEEDED (MFCreateSample (&sample)) && sample != nullptr;
                if (ok)
                {
                    sample->AddBuffer (buffer);
                    sample->SetSampleTime ((LONGLONG) std::llround ((double) framesWritten * 1.0e7 / rate));
                    sample->SetSampleDuration ((LONGLONG) std::llround ((double) frames * 1.0e7 / rate));
                    ok = SUCCEEDED (writer->WriteSample (stream, sample));
                    sample->Release();
                }
            }
            buffer->Release();
            if (ok) framesWritten += frames;
            return ok;
        }

        juce::String finish() override
        {
            juce::String err;
            if (writer != nullptr && FAILED (writer->Finalize())) err = tr ("MP3: dosya kapatılamadı");
            close();
            return err;
        }

        void close()
        {
            release (writer);
            if (mfStarted) { MFShutdown(); mfStarted = false; }
            if (comInit) { CoUninitialize(); comInit = false; }
        }

        const int kbps;
        double rate = 48000.0;
        int channels = 2;
        IMFSinkWriter* writer = nullptr;
        DWORD stream = 0;
        int64_t framesWritten = 0;
        bool comInit = false, mfStarted = false;
    };
   #endif

    std::unique_ptr<Sink> makeSink (const RecordSettings& s)
    {
        switch (s.format)
        {
            case RecordSettings::Format::flac:
                return std::make_unique<JuceSink> (std::make_unique<juce::FlacAudioFormat>(), s.flacBits, false);
            case RecordSettings::Format::mp3:
               #if JUCE_WINDOWS
                return std::make_unique<Mp3Sink> (s.mp3Kbps);
               #else
                return nullptr;
               #endif
            default:
                return std::make_unique<JuceSink> (std::make_unique<juce::WavAudioFormat>(), s.wavBits, s.wavFloat && s.wavBits == 32);
        }
    }
}

bool RecordSettings::mp3Available()
{
   #if JUCE_WINDOWS
    static const bool available = []
    {
        bool found = false;
        // on a thread of its own: COM multithreaded, whatever the caller's apartment is
        std::thread t ([&found]
        {
            const bool com = SUCCEEDED (CoInitializeEx (nullptr, COINIT_MULTITHREADED));
            if (SUCCEEDED (MFStartup (MF_VERSION, MFSTARTUP_LITE)))
            {
                IMFCollection* types = nullptr;
                if (SUCCEEDED (MFTranscodeGetAudioOutputAvailableTypes (MFAudioFormat_MP3, MFT_ENUM_FLAG_ALL, nullptr, &types)) && types != nullptr)
                {
                    DWORD n = 0;
                    types->GetElementCount (&n);
                    found = n > 0;
                    types->Release();
                }
                MFShutdown();
            }
            if (com) CoUninitialize();
        });
        t.join();
        return found;
    }();
    return available;
   #else
    return false;
   #endif
}

//==============================================================================
struct Recorder::Session final : juce::Thread
{
    Session() : juce::Thread ("MicVST kayit") {}
    ~Session() override { stopThread (30000); }

    // set before the thread starts
    std::unique_ptr<Sink> sink;
    juce::File file;
    double rate = 48000.0;
    int channels = 2;
    juce::AbstractFifo fifo { 1 };
    juce::AudioBuffer<float> ring;

    // audio thread -> others
    std::atomic<int64_t> pushed { 0 }, dropped { 0 };
    // writer thread -> others
    std::atomic<int64_t> written { 0 };
    std::atomic<bool> failed { false };
    juce::String failure;                      // written before 'failed' is set
    juce::WaitableEvent opened;
    juce::String openError;                    // written before 'opened' is signalled
    juce::String finishError;

    void fail (const juce::String& why)
    {
        if (failed.load()) return;
        failure = why;
        failed.store (true, std::memory_order_release);
    }

    void run() override
    {
        openError = sink->open (file, rate, channels);
        opened.signal();
        if (openError.isNotEmpty()) return;

        std::vector<const float*> ptrs ((size_t) channels);
        auto lastFlush = juce::Time::getMillisecondCounter(), lastSpace = lastFlush;
        for (;;)
        {
            const bool exiting = threadShouldExit();
            // drain what is there (everything, when finishing)
            for (;;)
            {
                int s1, n1, s2, n2;
                fifo.prepareToRead (fifo.getNumReady(), s1, n1, s2, n2);
                if (n1 + n2 == 0) break;
                for (auto [start, n] : { std::pair<int, int> { s1, n1 }, std::pair<int, int> { s2, n2 } })
                {
                    if (n <= 0) continue;
                    for (int c = 0; c < channels; ++c) ptrs[(size_t) c] = ring.getReadPointer (c, start);
                    if (! failed.load() && ! sink->write (ptrs.data(), channels, n))
                        fail (tr ("Kayıt dosyasına yazılamadı (disk dolu ya da çıkarılmış olabilir): ") + file.getFullPathName());
                    if (! failed.load()) written.fetch_add (n);
                }
                fifo.finishedRead (n1 + n2);
            }
            if (exiting) break;

            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastFlush >= 1000) { sink->flush(); lastFlush = now; }   // WAV header up to date every second
            if (now - lastSpace >= 5000)
            {
                lastSpace = now;
                const auto freeBytes = file.getBytesFreeOnVolume();
                if (freeBytes > 0 && freeBytes < 30 * 1024 * 1024)
                    fail (tr ("Diskte yer kalmadı (30 MB'tan az): kayıt durduruldu."));
            }
            wait (50);
        }
        finishError = sink->finish();
    }
};

Recorder::Recorder() = default;

Recorder::~Recorder()
{
    if (session != nullptr) stop();
}

juce::File Recorder::uniqueFile (const juce::File& folder, const juce::String& extension, juce::Time when)
{
    const auto base = "MicVST_" + when.formatted ("%Y-%m-%d_%H-%M-%S");
    auto f = folder.getChildFile (base + extension);
    for (int k = 2; f.exists() && k < 10000; ++k)
        f = folder.getChildFile (base + "_" + juce::String (k) + extension);
    return f;
}

juce::String Recorder::start (const RecordSettings& settings, double sampleRate, int numChannels)
{
    if (session != nullptr) return tr ("Kayıt zaten sürüyor");
    if (sampleRate <= 0.0) return tr ("Ses çalışmıyor: önce mikrofon ve kablo açık olmalı");
    if (settings.format == RecordSettings::Format::mp3 && ! RecordSettings::mp3Available())
        return tr ("Bu bilgisayarda Windows'un MP3 kodlayıcısı yok; WAV ya da FLAC seçin");

    const auto folder = settings.folderOrDefault();
    if (! folder.isDirectory() && ! folder.createDirectory())
        return tr ("Kayıt klasörü oluşturulamadı: ") + folder.getFullPathName();
    const auto freeBytes = folder.getBytesFreeOnVolume();
    if (freeBytes > 0 && freeBytes < 100 * 1024 * 1024)
        return tr ("Kayıt klasörünün diskinde yer yok (100 MB'tan az): ") + folder.getFullPathName();

    auto s = std::make_unique<Session>();
    s->sink = makeSink (settings);
    if (s->sink == nullptr) return tr ("Bu biçim bu sistemde desteklenmiyor: ") + settings.describe();
    s->file = uniqueFile (folder, settings.extension(), juce::Time::getCurrentTime());
    s->rate = sampleRate;
    s->channels = juce::jlimit (1, 8, numChannels);
    const int frames = (int) std::ceil (10.0 * sampleRate);   // 10 s: room for a stalled disk
    s->ring.setSize (s->channels, frames);
    s->ring.clear();
    s->fifo.setTotalSize (frames);
    s->startThread (juce::Thread::Priority::normal);
    if (! s->opened.wait (15000))
        return tr ("Kayıt dosyası açılamadı (zaman aşımı): ") + s->file.getFullPathName();   // ~Session waits for the thread
    if (s->openError.isNotEmpty())
    {
        const auto err = s->openError;
        const auto file = s->file;
        s.reset();
        if (file.existsAsFile() && file.getSize() < 1024) file.deleteFile();   // nothing of use in it
        return err;
    }
    session = std::move (s);
    active.store (session.get());
    return {};
}

void Recorder::push (const float* const* channels, int numChannels, int numFrames) noexcept
{
    inPush.store (true);
    if (auto* s = active.load())
    {
        int s1, n1, s2, n2;
        s->fifo.prepareToWrite (numFrames, s1, n1, s2, n2);
        for (auto [start, n, from] : { std::tuple<int, int, int> { s1, n1, 0 }, std::tuple<int, int, int> { s2, n2, n1 } })
        {
            if (n <= 0) continue;
            for (int c = 0; c < s->channels; ++c)
                juce::FloatVectorOperations::copy (s->ring.getWritePointer (c, start), channels[juce::jmin (c, numChannels - 1)] + from, n);
        }
        s->fifo.finishedWrite (n1 + n2);
        s->pushed.fetch_add (n1 + n2, std::memory_order_relaxed);
        if (n1 + n2 < numFrames) s->dropped.fetch_add (numFrames - (n1 + n2), std::memory_order_relaxed);
    }
    inPush.store (false);
}

Recorder::Result Recorder::stop()
{
    Result r;
    if (session == nullptr) return r;
    active.store (nullptr);
    while (inPush.load()) std::this_thread::yield();   // a push that saw the session ends within microseconds
    session->signalThreadShouldExit();
    session->notify();
    session->stopThread (60000);                       // writes the rest and closes the file
    r.file = session->file;
    r.seconds = (double) session->written.load() / session->rate;
    r.droppedFrames = session->dropped.load();
    r.problem = session->failed.load (std::memory_order_acquire) ? session->failure : session->finishError;
    const auto writtenFrames = session->written.load();
    const bool isMp3 = r.file.hasFileExtension ("mp3");
    session.reset();

    // read back: format, rate, channels and length as written (MP3: the file exists and has data)
    if (isMp3)
        r.verified = r.file.existsAsFile() && r.file.getSize() > 0;
    else
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (r.file)))
            r.verified = reader->lengthInSamples == writtenFrames && reader->numChannels > 0;
    }
    return r;
}

double Recorder::getSampleRate() const noexcept { return session != nullptr ? session->rate : 0.0; }
double Recorder::getSeconds() const noexcept { return session != nullptr ? (double) session->pushed.load() / session->rate : 0.0; }
juce::File Recorder::getFile() const { return session != nullptr ? session->file : juce::File(); }
int64_t Recorder::getDroppedFrames() const noexcept { return session != nullptr ? session->dropped.load() : 0; }
bool Recorder::hasFailed() const noexcept { return session != nullptr && session->failed.load (std::memory_order_acquire); }
juce::String Recorder::getFailure() const { return hasFailed() ? session->failure : juce::String(); }
