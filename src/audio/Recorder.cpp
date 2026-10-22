#include "audio/Recorder.h"
#include "audio/Resampler.h"
#include "Text.h"
#include <lame.h>
#include <cmath>
#include <cstring>
#include <thread>

using micvst::tr;

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

juce::Array<int> RecordSettings::allowedRates (Format f, double processRate)
{
    juce::Array<int> r { 44100, 48000 };
    if (f != Format::mp3)
        for (int x : allRates())
            if (x > 48000 && x <= (int) std::lround (processRate)) r.add (x);
    return r;
}

int RecordSettings::effectiveRate (double processRate) const
{
    const auto allowed = allowedRates (format, processRate);
    if (allowed.contains (sampleRate)) return sampleRate;
    const int p = (int) std::lround (processRate);
    if (allowed.contains (p)) return p;
    return 48000;
}

static juce::String khz (int rate)
{
    return rate % 1000 == 0 ? juce::String (rate / 1000) + " kHz" : juce::String (rate / 1000.0, 1) + " kHz";
}

juce::String RecordSettings::describe() const
{
    juce::String q;
    switch (format)
    {
        case Format::flac: q = "FLAC " + juce::String (flacBits) + " bit"; break;
        case Format::mp3:  q = mp3Mode == Mp3Mode::cbr ? "MP3 " + juce::String (mp3Kbps) + " kbps CBR"
                                                       : "MP3 VBR V" + juce::String (mp3Vbr); break;
        default:           q = "WAV " + juce::String (wavBits) + (wavFloat ? tr (" bit float") : juce::String (" bit PCM")); break;
    }
    return q + ", " + khz (sampleRate) + ", " + (channels == 1 ? tr ("mono") : tr ("stereo"));
}

juce::String RecordSettings::encode() const
{
    juce::String q;
    switch (format)
    {
        case Format::flac: q = "flac:" + juce::String (flacBits) + ":" + juce::String (flacLevel); break;
        case Format::mp3:  q = mp3Mode == Mp3Mode::cbr ? "mp3:cbr" + juce::String (mp3Kbps) : "mp3:v" + juce::String (mp3Vbr); break;
        default:           q = "wav:" + juce::String (wavBits) + (wavFloat ? "f" : ""); break;
    }
    return q + "|" + juce::String (sampleRate) + "|" + juce::String (channels);
}

RecordSettings RecordSettings::decode (const juce::String& text, const juce::String& folder)
{
    RecordSettings s;
    if (folder.isNotEmpty() && juce::File::isAbsolutePath (folder)) s.folder = juce::File (folder);
    auto parts = juce::StringArray::fromTokens (text, "|", "");
    const auto fmt = parts[0].trim().toLowerCase();
    const auto kind = fmt.upToFirstOccurrenceOf (":", false, false);
    auto q = fmt.fromFirstOccurrenceOf (":", false, false);
    const auto isNum = [] (const juce::String& t) { return t.isNotEmpty() && t.containsOnly ("0123456789"); };
    if (kind == "wav")
    {
        if (q == "16" || q == "24" || q == "32") { s.wavBits = q.getIntValue(); s.wavFloat = false; }
        else if (q == "32f")                     { s.wavBits = 32; s.wavFloat = true; }
    }
    else if (kind == "flac")
    {
        const auto bits = q.upToFirstOccurrenceOf (":", false, false);
        const auto level = q.fromFirstOccurrenceOf (":", false, false);
        if (bits == "16" || bits == "24")
        {
            s.format = Format::flac; s.flacBits = bits.getIntValue();
            if (isNum (level) && level.getIntValue() <= 8) s.flacLevel = level.getIntValue();
        }
    }
    else if (kind == "mp3")
    {
        if (q.startsWith ("cbr")) q = q.substring (3);
        if (isNum (q) && mp3CbrRates().contains (q.getIntValue()))
        { s.format = Format::mp3; s.mp3Mode = Mp3Mode::cbr; s.mp3Kbps = q.getIntValue(); }
        else if (q.startsWith ("v") && isNum (q.substring (1)) && mp3VbrLevels().contains (q.substring (1).getIntValue()))
        { s.format = Format::mp3; s.mp3Mode = Mp3Mode::vbr; s.mp3Vbr = q.substring (1).getIntValue(); }
    }
    if (parts.size() > 1 && isNum (parts[1].trim()) && allRates().contains (parts[1].trim().getIntValue()))
        s.sampleRate = parts[1].trim().getIntValue();
    if (parts.size() > 2 && (parts[2].trim() == "1" || parts[2].trim() == "2"))
        s.channels = parts[2].trim().getIntValue();
    if (s.format == Format::mp3 && ! allowedRates (Format::mp3, 0.0).contains (s.sampleRate))
        s.sampleRate = 48000;
    return s;
}

//==============================================================================
namespace
{
    // what the writer thread hands the (converted) samples to
    struct Sink
    {
        virtual ~Sink() = default;
        virtual juce::String open (const juce::File&, int rate, int channels, juce::Time started) = 0;   // "" = fine
        virtual bool write (const float* const* data, int channels, int frames) = 0;
        virtual void flush() {}
        virtual juce::String finish() { return {}; }                                                     // "" = fine
    };

    // WAV / FLAC through JUCE's writers. Integer formats: rounded, limited to full scale (no wrap-around);
    // 16 bit with TPDF dither (+-1 LSB triangular), so quiet passages keep a smooth noise floor
    // instead of distortion. 24 / 32 bit and float: written as they are.
    struct JuceSink final : Sink
    {
        JuceSink (bool flacIn, int bitsIn, bool isFloatIn, int levelIn) : flac (flacIn), bits (bitsIn), isFloat (isFloatIn), level (levelIn) {}

        juce::String open (const juce::File& file, int rate, int channels, juce::Time started) override
        {
            auto fos = std::make_unique<juce::FileOutputStream> (file);
            if (! fos->openedOk())
                return tr ("Dosya oluşturulamadı: ") + file.getFullPathName();
            std::unique_ptr<juce::OutputStream> out = std::move (fos);
            auto opts = juce::AudioFormatWriterOptions{}
                            .withSampleRate (rate)
                            .withNumChannels (channels)
                            .withBitsPerSample (bits)
                            .withSampleFormat (isFloat ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                                       : juce::AudioFormatWriterOptions::SampleFormat::integral);
            if (flac)
            {
                fmt = std::make_unique<juce::FlacAudioFormat>();
                opts = opts.withQualityOptionIndex (level);
            }
            else
            {
                fmt = std::make_unique<juce::WavAudioFormat>();
                // Broadcast WAV: who made it and when it started (what DAWs show and sort by)
                const auto history = "A=" + juce::String (isFloat ? "PCM_FLOAT" : "PCM") + ",F=" + juce::String (rate)
                                     + ",W=" + juce::String (bits) + ",M=" + (channels == 1 ? "mono" : "stereo") + ",T=MicVST\r\n";
                const auto bwav = juce::WavAudioFormat::createBWAVMetadata ("MicVST", "MicVST", {}, started, 0, history);
                std::unordered_map<juce::String, juce::String> meta;
                for (const auto& k : bwav.getAllKeys()) meta[k] = bwav[k];
                opts = opts.withMetadataValues (meta);
            }
            writer = fmt->createWriterFor (out, opts);
            if (writer == nullptr)
                return tr ("Bu biçimde kayıt başlatılamadı: ") + fmt->getFormatName() + " " + juce::String (bits) + " bit";
            for (auto& v : ints) v.assign ((size_t) chunk, 0);
            return {};
        }

        bool write (const float* const* data, int channels, int frames) override
        {
            if (isFloat)
                return writer->writeFromFloatArrays (data, channels, frames);
            // Rounded to the file's bit depth here (JUCE's own float path would truncate: up to
            // 1 LSB off and a -0.5 LSB offset), then handed over left-justified so the writer's
            // shift is exact.
            const double scale = std::ldexp (1.0, bits - 1);
            const double top = scale - 1.0;
            const int shift = 32 - bits;
            const bool dither = bits == 16;
            const int* ptrs[3] = { ints[0].data(), ints[1].data(), nullptr };
            for (int start = 0; start < frames; start += chunk)
            {
                const int n = juce::jmin (chunk, frames - start);
                for (int c = 0; c < channels; ++c)
                {
                    const float* src = data[c] + start;
                    int* dst = ints[(size_t) c].data();
                    for (int i = 0; i < n; ++i)
                    {
                        double v = (double) src[i] * scale;
                        if (dither) v += uniform() - uniform();   // TPDF dither, +-1 LSB
                        // nearest, ties to even: a float above |0.5| has only 24 significant bits, so many
                        // samples sit exactly between two steps - rounding those always up would add a DC offset
                        const double q = juce::jlimit (-scale, top, std::nearbyint (v));
                        dst[i] = (int) ((int64_t) q * ((int64_t) 1 << shift));
                    }
                }
                ptrs[channels] = nullptr;
                if (! writer->write (ptrs, n)) return false;
            }
            return true;
        }

        void flush() override { if (writer != nullptr) writer->flush(); }   // WAV: the header now covers everything written
        juce::String finish() override { writer.reset(); return {}; }      // writes the final header and closes

        double uniform() noexcept
        {
            rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;   // xorshift64
            return (double) (rng >> 11) * (1.0 / 9007199254740992.0);
        }

        static constexpr int chunk = 4096;
        const bool flac;
        const int bits;
        const bool isFloat;
        const int level;
        std::unique_ptr<juce::AudioFormat> fmt;
        std::unique_ptr<juce::AudioFormatWriter> writer;
        std::vector<int> ints[2];
        uint64_t rng = 0x9E3779B97F4A7C15ull;
    };

    // MP3 with LAME 4.0 (built into MicVST, third_party/lame-4.0): float in, MPEG-1 Layer III out,
    // joint stereo (mono for mono). CBR uses LAME's high-quality search (-q 2), VBR LAME's own
    // presets V0 / V2 / V4. At the end the LAME/Xing tag frame (length, encoder delay and padding
    // for gapless playback, seek table) is written over the placeholder frame at the start.
    struct LameSink final : Sink
    {
        LameSink (RecordSettings::Mp3Mode modeIn, int kbpsIn, int vbrIn) : mode (modeIn), kbps (kbpsIn), vbr (vbrIn) {}
        ~LameSink() override { if (gfp != nullptr) lame_close (gfp); }

        juce::String open (const juce::File& file, int rate, int channelsIn, juce::Time) override
        {
            channels = channelsIn;
            out = std::make_unique<juce::FileOutputStream> (file);
            if (! out->openedOk()) return tr ("Dosya oluşturulamadı: ") + file.getFullPathName();
            gfp = lame_init();
            if (gfp == nullptr) return tr ("MP3 kodlayıcı başlatılamadı");
            lame_set_in_samplerate (gfp, rate);
            lame_set_out_samplerate (gfp, rate);     // no resampling inside LAME (done before, 140 dB)
            lame_set_num_channels (gfp, channels);
            lame_set_mode (gfp, channels == 1 ? MONO : JOINT_STEREO);
            lame_set_bWriteVbrTag (gfp, 1);
            lame_set_findReplayGain (gfp, 0);
            if (mode == RecordSettings::Mp3Mode::cbr)
            {
                lame_set_VBR (gfp, vbr_off);
                lame_set_brate (gfp, kbps);
                lame_set_quality (gfp, 2);
            }
            else
            {
                lame_set_VBR (gfp, vbr_default);
                lame_set_VBR_quality (gfp, (float) vbr);
            }
            if (lame_init_params (gfp) < 0) return tr ("MP3 kodlayıcı bu ayarı kabul etmedi: ") + juce::String (rate) + " Hz";
            return {};
        }

        bool write (const float* const* data, int ch, int frames) override
        {
            for (int start = 0; start < frames; start += chunk)
            {
                const int n = juce::jmin (chunk, frames - start);
                const float* l = data[0] + start;
                const float* r = ch > 1 ? data[1] + start : l;
                mp3.resize ((size_t) (n + n / 4 + 7200));
                const int bytes = lame_encode_buffer_ieee_float (gfp, l, r, n, mp3.data(), (int) mp3.size());
                if (bytes < 0 || ! out->write (mp3.data(), (size_t) bytes)) return false;
            }
            return true;
        }

        void flush() override { out->flush(); }

        juce::String finish() override
        {
            mp3.resize (16384);
            const int bytes = lame_encode_flush (gfp, mp3.data(), (int) mp3.size());
            bool ok = bytes >= 0 && out->write (mp3.data(), (size_t) bytes);
            if (ok)
            {
                std::vector<unsigned char> tag (8192);
                const size_t n = lame_get_lametag_frame (gfp, tag.data(), tag.size());
                if (n > 0 && n <= tag.size())
                {
                    out->flush();
                    ok = out->setPosition (0) && out->write (tag.data(), n);
                }
            }
            out->flush();
            ok = ok && out->getStatus().wasOk();
            out.reset();
            lame_close (gfp);
            gfp = nullptr;
            return ok ? juce::String() : tr ("MP3 dosyası tamamlanamadı (disk dolu olabilir)");
        }

        static constexpr int chunk = 4096;
        const RecordSettings::Mp3Mode mode;
        const int kbps, vbr;
        int channels = 2;
        lame_global_flags* gfp = nullptr;
        std::unique_ptr<juce::FileOutputStream> out;
        std::vector<unsigned char> mp3;
    };

    std::unique_ptr<Sink> makeSink (const RecordSettings& s)
    {
        switch (s.format)
        {
            case RecordSettings::Format::flac: return std::make_unique<JuceSink> (true, s.flacBits, false, s.flacLevel);
            case RecordSettings::Format::mp3:  return std::make_unique<LameSink> (s.mp3Mode, s.mp3Kbps, s.mp3Vbr);
            default:                           return std::make_unique<JuceSink> (false, s.wavBits, s.wavFloat && s.wavBits == 32, 0);
        }
    }
}

//==============================================================================
struct Recorder::Session final : juce::Thread
{
    Session() : juce::Thread ("MicVST kayit") {}
    ~Session() override { stopThread (60000); }

    // set before the thread starts
    std::unique_ptr<Sink> sink;
    juce::File file;
    juce::Time started;
    double inRate = 48000.0;    // the processing rate (what push() gets)
    int outRate = 48000;
    int inChannels = 2, outChannels = 2;
    juce::AbstractFifo fifo { 1 };
    juce::AudioBuffer<float> ring;

    // audio thread -> others
    std::atomic<int64_t> pushed { 0 }, dropped { 0 };
    std::atomic<bool> rateChanged { false };
    // writer thread -> others
    std::atomic<int64_t> written { 0 };
    std::atomic<bool> failed { false };
    juce::String failure;                      // written before 'failed' is set
    juce::WaitableEvent opened;
    juce::String openError;                    // written before 'opened' is signalled
    juce::String finishError;

    // writer thread only
    Resampler resampler;
    std::vector<float> monoMix;
    std::vector<std::vector<float>> converted;

    void fail (const juce::String& why)
    {
        if (failed.load()) return;
        failure = why;
        failed.store (true, std::memory_order_release);
    }

    void writeOut()
    {
        if (converted.empty() || converted[0].empty()) return;
        const int n = (int) converted[0].size();
        const float* ptrs[2] = { converted[0].data(), converted.size() > 1 ? converted[1].data() : converted[0].data() };
        if (! failed.load())
        {
            if (sink->write (ptrs, outChannels, n)) written.fetch_add (n);
            else fail (tr ("Kayıt dosyasına yazılamadı (disk dolu ya da çıkarılmış olabilir): ") + file.getFullPathName());
        }
        for (auto& v : converted) v.clear();
    }

    void take (int start, int n)
    {
        const float* src[2] = { ring.getReadPointer (0, start), ring.getReadPointer (juce::jmin (1, inChannels - 1), start) };
        if (outChannels == 1)
        {
            monoMix.resize ((size_t) n);
            for (int i = 0; i < n; ++i) monoMix[(size_t) i] = 0.5f * (src[0][i] + src[1][i]);   // L = R: exact
            const float* m = monoMix.data();
            resampler.process (&m, n, converted);
        }
        else
            resampler.process (src, n, converted);
        writeOut();
    }

    void run() override
    {
        resampler.prepare ((int) std::lround (inRate), outRate, outChannels);
        openError = sink->open (file, outRate, outChannels, started);
        opened.signal();
        if (openError.isNotEmpty()) return;

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
                if (n1 > 0) take (s1, n1);
                if (n2 > 0) take (s2, n2);
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
        resampler.finish (converted);   // the converter's last samples
        writeOut();
        const auto err = sink->finish();
        if (finishError.isEmpty()) finishError = err;
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

juce::String Recorder::start (const RecordSettings& settings, double processRate, int numChannels)
{
    if (session != nullptr) return tr ("Kayıt zaten sürüyor");
    if (processRate <= 0.0) return tr ("Ses çalışmıyor: önce mikrofon ve kablo açık olmalı");

    const auto folder = settings.folderOrDefault();
    if (! folder.isDirectory() && ! folder.createDirectory())
        return tr ("Kayıt klasörü oluşturulamadı: ") + folder.getFullPathName();
    const auto freeBytes = folder.getBytesFreeOnVolume();
    if (freeBytes > 0 && freeBytes < 100 * 1024 * 1024)
        return tr ("Kayıt klasörünün diskinde yer yok (100 MB'tan az): ") + folder.getFullPathName();

    auto s = std::make_unique<Session>();
    s->sink = makeSink (settings);
    s->started = juce::Time::getCurrentTime();
    s->file = uniqueFile (folder, settings.extension(), s->started);
    s->inRate = processRate;
    s->outRate = settings.effectiveRate (processRate);
    s->inChannels = juce::jlimit (1, 2, numChannels);
    s->outChannels = settings.channels == 1 ? 1 : 2;
    const int frames = (int) std::ceil (fifoSeconds * processRate);   // room for a stalled disk (USB drive, virus scan)
    s->ring.setSize (s->inChannels, frames);
    s->ring.clear();
    s->fifo.setTotalSize (frames);
    // above normal (Windows: THREAD_PRIORITY_ABOVE_NORMAL, performance cores): a game or a render at
    // 100 % CPU must not starve the file - and still far below the audio threads (MMCSS / realtime)
    s->startThread (juce::Thread::Priority::high);
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

void Recorder::push (const float* const* channels, int numChannels, int numFrames, double processRate) noexcept
{
    inPush.store (true);
    if (auto* s = active.load(); s != nullptr && numChannels > 0)
    {
        if (processRate > 0.0 && std::abs (processRate - s->inRate) > 0.5)
        {
            s->rateChanged.store (true, std::memory_order_relaxed);
            inPush.store (false);
            return;
        }
        int s1, n1, s2, n2;
        s->fifo.prepareToWrite (numFrames, s1, n1, s2, n2);
        for (int c = 0; c < s->inChannels; ++c)
        {
            const float* src = channels[juce::jmin (c, numChannels - 1)];
            if (n1 > 0) juce::FloatVectorOperations::copy (s->ring.getWritePointer (c, s1), src, n1);
            if (n2 > 0) juce::FloatVectorOperations::copy (s->ring.getWritePointer (c, s2), src + n1, n2);
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
    session->stopThread (120000);                      // writes the rest and closes the file
    r.file = session->file;
    r.framesWritten = session->written.load();
    r.seconds = (double) r.framesWritten / session->outRate;
    r.droppedFrames = session->dropped.load();
    r.problem = session->failed.load (std::memory_order_acquire) ? session->failure : session->finishError;
    const int rate = session->outRate, ch = session->outChannels;
    session.reset();

    // read back: format, rate, channels and the exact length
    if (r.file.hasFileExtension ("mp3"))
    {
        Mp3Info info;
        r.verified = inspectMp3 (r.file, info) && info.sampleRate == rate && info.channels == ch && info.samples == r.framesWritten;
    }
    else
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        if (auto reader = std::unique_ptr<juce::AudioFormatReader> (fm.createReaderFor (r.file)))
            r.verified = reader->lengthInSamples == r.framesWritten && (int) reader->numChannels == ch
                         && (int) std::lround (reader->sampleRate) == rate;
    }
    return r;
}

double Recorder::getSeconds() const noexcept { return session != nullptr ? (double) session->pushed.load() / session->inRate : 0.0; }
int64_t Recorder::getFramesWritten() const noexcept { return session != nullptr ? session->written.load() : 0; }
juce::File Recorder::getFile() const { return session != nullptr ? session->file : juce::File(); }
int64_t Recorder::getDroppedFrames() const noexcept { return session != nullptr ? session->dropped.load() : 0; }
bool Recorder::hasFailed() const noexcept
{
    return session != nullptr && (session->failed.load (std::memory_order_acquire) || session->rateChanged.load());
}

juce::String Recorder::getFailure() const
{
    if (session == nullptr) return {};
    if (session->failed.load (std::memory_order_acquire)) return session->failure;
    if (session->rateChanged.load()) return tr ("Mikrofonun örnekleme hızı değişti: kayıt bu noktada bitirildi.");
    return {};
}

//==============================================================================
bool Recorder::inspectMp3 (const juce::File& file, Mp3Info& info)
{
    info = {};
    juce::MemoryBlock data;
    if (! file.loadFileAsData (data) || data.getSize() < 128) return false;
    const auto* p = static_cast<const uint8_t*> (data.getData());
    const size_t size = data.getSize();
    size_t pos = 0;
    if (size > 10 && p[0] == 'I' && p[1] == 'D' && p[2] == '3')   // ID3v2 in front
        pos = 10 + (size_t) (((p[6] & 0x7f) << 21) | ((p[7] & 0x7f) << 14) | ((p[8] & 0x7f) << 7) | (p[9] & 0x7f));

    static const int brTable[16] = { 0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, -1 };
    static const int srTable[4] = { 44100, 48000, 32000, -1 };
    int tagFrames = -1, delay = 0, padding = 0, audioFrames = 0, firstKbps = 0;
    bool constantRate = true, first = true;
    while (pos + 4 <= size)
    {
        const uint32_t h = (uint32_t) p[pos] << 24 | (uint32_t) p[pos + 1] << 16 | (uint32_t) p[pos + 2] << 8 | p[pos + 3];
        if ((h & 0xFFE00000u) != 0xFFE00000u) break;
        const int version = (int) ((h >> 19) & 3), layer = (int) ((h >> 17) & 3);
        const int br = brTable[(h >> 12) & 15], sr = srTable[(h >> 10) & 3];
        const int pad = (int) ((h >> 9) & 1), chMode = (int) ((h >> 6) & 3);
        if (version != 3 || layer != 1 || br <= 0 || sr <= 0) return false;   // MPEG-1 Layer III only
        const size_t len = (size_t) (144000 * br / sr + pad);
        if (pos + len > size) return false;                                     // cut off
        const int ch = chMode == 3 ? 1 : 2;
        if (first)
        {
            first = false;
            info.sampleRate = sr; info.channels = ch;
            const size_t x = pos + 4 + (ch == 1 ? 17 : 32);
            if (x + 8 <= size && (std::memcmp (p + x, "Xing", 4) == 0 || std::memcmp (p + x, "Info", 4) == 0))
            {
                info.vbr = std::memcmp (p + x, "Xing", 4) == 0;
                const uint32_t flags = (uint32_t) p[x + 4] << 24 | (uint32_t) p[x + 5] << 16 | (uint32_t) p[x + 6] << 8 | p[x + 7];
                size_t y = x + 8;
                if ((flags & 1) && y + 4 <= size) { tagFrames = (int) ((uint32_t) p[y] << 24 | (uint32_t) p[y + 1] << 16 | (uint32_t) p[y + 2] << 8 | p[y + 3]); y += 4; }
                if (flags & 2) y += 4;
                if (flags & 4) y += 100;
                if (flags & 8) y += 4;
                // LAME extension: version string (9), rev (1), lowpass (1), peak (4), 2 gains (4),
                // flags (1), bit rate (1), then 12 bits delay + 12 bits padding
                if (y + 24 <= size && std::memcmp (p + y, "LAME", 4) == 0)
                {
                    const size_t d = y + 21;
                    delay = (p[d] << 4) | (p[d + 1] >> 4);
                    padding = ((p[d + 1] & 0x0f) << 8) | p[d + 2];
                }
                pos += len;
                continue;
            }
        }
        if (sr != info.sampleRate || ch != info.channels) return false;
        if (firstKbps == 0) firstKbps = br; else if (br != firstKbps) constantRate = false;
        ++audioFrames;
        pos += len;
    }
    const size_t rest = size - juce::jmin (pos, size);
    if (rest != 0 && ! (rest == 128 && p[pos] == 'T' && p[pos + 1] == 'A' && p[pos + 2] == 'G')) return false;   // junk after the frames
    info.frames = audioFrames;
    info.kbps = constantRate ? firstKbps : 0;
    if (tagFrames >= 0 && tagFrames != audioFrames) return false;
    info.samples = audioFrames > 0 ? (int64_t) audioFrames * 1152 - delay - padding : -1;
    return audioFrames > 0;
}
