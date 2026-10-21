// Colour and ambience effects: Sıcak Saturasyon, Telefon / Radyo / Megafon, Oda Yankısı,
// Gecikme / Eko, Chorus / Stereo Genişletici.
#include "audio/fx/EffectFactories.h"
#include "audio/fx/FxDsp.h"
#include "Text.h"
#include <juce_dsp/juce_dsp.h>
#include <vector>

namespace fx
{
namespace
{
    using U = BuiltInEffect::Unit;
    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
    float valueOf (const juce::RangedAudioParameter* p) noexcept { return p->convertFrom0to1 (p->getValue()); }

    // Fractional read from a circular buffer (linear interpolation); delay in samples >= 1.
    inline float readFrac (const float* buf, int size, int writePos, double delay) noexcept
    {
        double rp = (double) writePos - delay;
        while (rp < 0.0) rp += size;
        const int i0 = (int) rp;
        const double f = rp - i0;
        const int i1 = i0 + 1 < size ? i0 + 1 : 0;
        return (float) (buf[i0] + f * (buf[i1] - buf[i0]));
    }

    //==============================================================================
    // Saturation, 4x oversampled (polyphase IIR half-band filters, integer latency). Both curves are
    // normalised to a small-signal gain of 1, so quiet parts keep their level and only the louder
    // parts gain harmonics (and are rounded softly):
    //   Tüp:  (tanh(k x + b) - tanh(b)) / (k (1 - tanh(b)^2)), b = 0.25 - asymmetric, even harmonics
    //   Bant: tanh(k x) / k, then a gentle 1st-order roll-off at 14 kHz - symmetric, odd harmonics
    // k = drive. The dry signal is delayed by the oversampling latency, so "Karışım" adds no comb
    // filter. A 10 Hz high-pass removes the DC the asymmetric curve creates.
    class Saturation final : public BuiltInEffect
    {
    public:
        static constexpr int chunk = 256;
        static constexpr double bias = 0.25;

        explicit Saturation (const Info& i) : BuiltInEffect (i)
        {
            type  = addChoice ("type", "Tür", { u8 ("Tüp"), u8 ("Bant") }, 0);
            drive = addFloat ("drive", "Sürüş", 0.0f, 24.0f, 6.0f, U::decibel, 0.0f, 0.1f);
            mix   = addFloat ("mix", "Karışım", 0.0f, 100.0f, 100.0f, U::percent, 0.0f, 1.0f);
            // the latency is fixed by the filter design; known before prepareToPlay
            juce::dsp::Oversampling<float> probe (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
            latency = (int) std::lround (probe.getLatencyInSamples());
            updateLatency();
        }

        int computeLatency() const override { return latency; }

        static double tube (double x, double k) noexcept
        {
            static const double tb = std::tanh (bias), norm = 1.0 - tb * tb;
            return (std::tanh (k * x + bias) - tb) / (k * norm);
        }
        static double tape (double x, double k) noexcept { return std::tanh (k * x) / k; }

    private:
        void prepareEffect() override
        {
            os = std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
                                                                   true, true);
            os->initProcessing ((size_t) chunk);
            latency = (int) std::lround (os->getLatencyInSamples());
            dry.assign ((size_t) (2 * (latency + 1)), 0.0f);
            const double hiRate = getRate() * 4.0;
            k.prepare (hiRate, 0.05);
            k.snap (dbToGain (valueOf (drive)));
            tapeW.prepare (hiRate, 0.03);
            tapeW.snap (type->getIndex() == 1 ? 1.0 : 0.0);
            wet.prepare (getRate(), 0.05);
            wet.snap (valueOf (mix) / 100.0);
            for (auto& f : tapeLp) f.k = BiquadCoeffs::lowPass1 (hiRate, 14000.0);
            for (auto& f : dcHp) f.k = BiquadCoeffs::highPass1 (getRate(), 10.0);
        }
        void resetEffect() override
        {
            if (os != nullptr) os->reset();
            std::fill (dry.begin(), dry.end(), 0.0f);
            dryPos = 0;
            for (auto& f : tapeLp) f.reset();
            for (auto& f : dcHp) f.reset();
        }
        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            k.setTarget (dbToGain (valueOf (drive)));
            tapeW.setTarget (type->getIndex() == 1 ? 1.0 : 0.0);
            wet.setTarget (valueOf (mix) / 100.0);
            const int D = latency + 1;

            for (int start = 0; start < n; start += chunk)
            {
                const int len = std::min (chunk, n - start);
                float* ptrs[2] = { ch[0] + start, numCh > 1 ? ch[1] + start : nullptr };

                // dry copy, delayed by the oversampling latency
                for (int i = 0; i < len; ++i)
                {
                    for (int c = 0; c < numCh; ++c)
                    {
                        float* d = dry.data() + c * D;
                        d[dryPos] = ptrs[c][i];
                        dryOut[(size_t) c][(size_t) i] = d[(dryPos + 1) % D];   // written latency samples ago
                    }
                    dryPos = dryPos + 1 < D ? dryPos + 1 : 0;
                }

                juce::dsp::AudioBlock<float> block (ptrs, (size_t) numCh, (size_t) len);
                auto up = os->processSamplesUp (block);
                const size_t m = up.getNumSamples();
                for (size_t j = 0; j < m; ++j)
                {
                    const double kk = k.next(), tw = tapeW.next();
                    for (int c = 0; c < numCh; ++c)
                    {
                        float* u = up.getChannelPointer ((size_t) c);
                        const double x = u[j];
                        const double yTube = tube (x, kk);
                        const double yTape = tapeLp[(size_t) c].process (tape (x, kk));
                        u[j] = (float) (yTube + tw * (yTape - yTube));
                    }
                }
                os->processSamplesDown (block);

                for (int i = 0; i < len; ++i)
                {
                    const double w = wet.next();
                    for (int c = 0; c < numCh; ++c)
                    {
                        const double y = dcHp[(size_t) c].process (ptrs[c][i]);
                        const double d = dryOut[(size_t) c][(size_t) i];
                        ptrs[c][i] = (float) (d + w * (y - d));
                    }
                }
            }
            for (auto& f : tapeLp) f.flush();
            for (auto& f : dcHp) f.flush();
        }

        juce::AudioParameterChoice* type;
        juce::AudioParameterFloat *drive, *mix;
        std::unique_ptr<juce::dsp::Oversampling<float>> os;
        int latency = 0, dryPos = 0;
        std::vector<float> dry;
        std::array<std::array<float, chunk>, 2> dryOut {};
        Ramp k, tapeW, wet;
        std::array<Biquad, 2> tapeLp, dcHp;
    };

    //==============================================================================
    // Telephone / radio / megaphone: band limits (Butterworth, 24 dB/oct), a presence bump, a soft
    // clip and a make-up gain per mode. All three run all the time; a mode change cross-fades
    // (40 ms), so it never clicks.
    class LoFi final : public BuiltInEffect
    {
    public:
        struct ModeSpec { double hp, lp, peakF, peakDb, peakQ, drive, makeupDb; };
        static constexpr int modes = 3;
        static const ModeSpec& spec (int m)
        {
            static const ModeSpec s[modes] = {
                { 300.0, 3400.0, 1500.0, 3.0, 1.0, 2.0, 2.3 },    // Telefon
                { 180.0, 5000.0, 2500.0, 4.0, 0.8, 1.5, 1.2 },    // Radyo
                { 500.0, 4500.0, 2000.0, 9.0, 1.2, 4.0, 4.4 },    // Megafon
            };
            return s[std::clamp (m, 0, modes - 1)];
        }

        explicit LoFi (const Info& i) : BuiltInEffect (i)
        {
            mode = addChoice ("mode", "Mod", { u8 ("Telefon"), u8 ("Radyo"), u8 ("Megafon") }, 0);
            mix  = addFloat ("mix", "Karışım", 0.0f, 100.0f, 100.0f, U::percent, 0.0f, 1.0f);
        }

    private:
        void prepareEffect() override
        {
            const double r = getRate();
            for (int m = 0; m < modes; ++m)
            {
                const auto& s = spec (m);
                const BiquadCoeffs k[5] = { BiquadCoeffs::highPass (r, s.hp, butterQ4a), BiquadCoeffs::highPass (r, s.hp, butterQ4b),
                                            BiquadCoeffs::lowPass (r, s.lp, butterQ4a),  BiquadCoeffs::lowPass (r, s.lp, butterQ4b),
                                            BiquadCoeffs::peak (r, s.peakF, s.peakQ, s.peakDb) };
                for (int c = 0; c < 2; ++c)
                    for (int j = 0; j < 5; ++j) f[(size_t) c][(size_t) m][(size_t) j].k = k[j];
                weight[(size_t) m].prepare (r, 0.04);
                weight[(size_t) m].snap (mode->getIndex() == m ? 1.0 : 0.0);
            }
            wet.prepare (r, 0.05);
            wet.snap (valueOf (mix) / 100.0);
        }
        void resetEffect() override { for (auto& a : f) for (auto& b : a) for (auto& q : b) q.reset(); }
        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            for (int m = 0; m < modes; ++m) weight[(size_t) m].setTarget (mode->getIndex() == m ? 1.0 : 0.0);
            wet.setTarget (valueOf (mix) / 100.0);
            for (int i = 0; i < n; ++i)
            {
                double w[modes];
                for (int m = 0; m < modes; ++m) w[m] = weight[(size_t) m].next();
                const double mixNow = wet.next();
                for (int c = 0; c < numCh; ++c)
                {
                    const double x = ch[c][i];
                    double y = 0.0;
                    for (int m = 0; m < modes; ++m)
                    {
                        auto& st = f[(size_t) c][(size_t) m];
                        double v = x;
                        for (auto& q : st) v = q.process (v);
                        if (w[m] == 0.0) continue;   // (the filters still run: no restart transient later)
                        const auto& s = spec (m);
                        y += w[m] * std::tanh (s.drive * v) / s.drive * dbToGainFast (s.makeupDb);
                    }
                    ch[c][i] = (float) (x + mixNow * (y - x));
                }
            }
            for (auto& a : f) for (auto& b : a) for (auto& q : b) q.flush();
        }
        static double dbToGainFast (double db) noexcept { return std::exp (db * 0.11512925464970228); }

        juce::AudioParameterChoice* mode;
        juce::AudioParameterFloat* mix;
        std::array<std::array<std::array<Biquad, 5>, modes>, 2> f {};
        std::array<Ramp, modes> weight;
        Ramp wet;
    };

    //==============================================================================
    // Room reverb: JUCE's Freeverb-style juce::Reverb (it glides its own gains and feedback), plus a
    // 150 Hz high-pass in front of it so the room does not boom. Always stereo.
    class RoomReverb final : public BuiltInEffect
    {
    public:
        explicit RoomReverb (const Info& i) : BuiltInEffect (i)
        {
            size    = addFloat ("size", "Oda boyutu", 0.0f, 100.0f, 30.0f, U::percent, 0.0f, 1.0f);
            damping = addFloat ("damping", "Sönümleme", 0.0f, 100.0f, 50.0f, U::percent, 0.0f, 1.0f);
            width   = addFloat ("width", "Genişlik", 0.0f, 100.0f, 100.0f, U::percent, 0.0f, 1.0f);
            mix     = addFloat ("mix", "Karışım", 0.0f, 100.0f, 12.0f, U::percent, 0.0f, 1.0f);
        }
        double getTailLengthSeconds() const override { return 4.0; }

    private:
        juce::Reverb::Parameters current() const noexcept
        {
            juce::Reverb::Parameters p;
            const float m = valueOf (mix) / 100.0f;
            p.roomSize = 0.2f + 0.75f * valueOf (size) / 100.0f;
            p.damping = valueOf (damping) / 100.0f;
            p.width = valueOf (width) / 100.0f;
            p.wetLevel = m;
            p.dryLevel = 0.0f;   // the dry signal is added here (dryLevelFor), not by juce::Reverb
            p.freezeMode = 0.0f;
            return p;
        }
        float dryLevelFor() const noexcept { return 1.0f - 0.5f * valueOf (mix) / 100.0f; }
        void prepareEffect() override
        {
            reverb.setSampleRate (getRate());
            reverb.setParameters (current());
            {
                // juce::Reverb glides its gains from their initial values: let them arrive, then clear
                float zl[256] = {}, zr[256] = {};
                for (int k = 0; k < (int) (0.1 * getRate()) / 256 + 1; ++k) reverb.processStereo (zl, zr, 256);
                reverb.reset();
            }
            for (auto& h : hp) h.k = BiquadCoeffs::highPass (getRate(), 150.0, butterQ2);
            wetBuf.assign ((size_t) (2 * getMaxBlock()), 0.0f);
            dryGain.reset (getRate(), 0.05);
            dryGain.setCurrentAndTargetValue (dryLevelFor());
        }
        // After a reset (added to the chain, switched on again) the reverb's input fades in (10 ms):
        // the engine fades the output in after a chain edit, but the room would otherwise start
        // from an abrupt input and carry that edge on.
        void resetEffect() override { reverb.reset(); for (auto& h : hp) h.reset(); inFade.reset (getRate()); }
        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            if (numCh < 2) return;
            // juce::Reverb mixes dry and wet itself: feed it the high-passed signal with dry 0, and add
            // the untouched dry signal here (so the low end of the voice is not thinned).
            reverb.setParameters (current());
            dryGain.setTargetValue (dryLevelFor());
            for (int start = 0; start < n; start += getMaxBlock())
            {
                const int len = std::min (getMaxBlock(), n - start);
                float* wl = wetBuf.data();
                float* wr = wetBuf.data() + getMaxBlock();
                for (int i = 0; i < len; ++i)
                {
                    const double f = inFade.next ((double) ch[0][start + i] + ch[1][start + i]);
                    wl[i] = (float) (f * hp[0].process (ch[0][start + i]));
                    wr[i] = (float) (f * hp[1].process (ch[1][start + i]));
                }
                reverb.processStereo (wl, wr, len);
                for (int i = 0; i < len; ++i)
                {
                    const float d = dryGain.getNextValue();
                    ch[0][start + i] = ch[0][start + i] * d + wl[i];
                    ch[1][start + i] = ch[1][start + i] * d + wr[i];
                }
            }
            for (auto& h : hp) h.flush();
        }
        juce::AudioParameterFloat *size, *damping, *width, *mix;
        juce::Reverb reverb;
        std::array<Biquad, 2> hp;
        std::vector<float> wetBuf;
        juce::SmoothedValue<float> dryGain { 1.0f };
        OnsetFade inFade;
    };

    //==============================================================================
    // Echo: one delay line per channel (up to 2 s). A new time cross-fades (60 ms, raised cosine)
    // from the old to the new read position - no click, no pitch sweep. The feedback path is
    // filtered (tone low-pass, 100 Hz high-pass) and limited to 90 %. Ping-pong glides the routing
    // (input into the left line, the repeats cross over).
    class Echo final : public BuiltInEffect
    {
    public:
        static constexpr double maxSeconds = 2.0;

        explicit Echo (const Info& i) : BuiltInEffect (i)
        {
            time     = addFloat ("time", "Süre", 20.0f, 1500.0f, 300.0f, U::milliseconds, 300.0f, 1.0f);
            feedback = addFloat ("feedback", "Geri besleme", 0.0f, 90.0f, 30.0f, U::percent, 0.0f, 1.0f);
            mix      = addFloat ("mix", "Karışım", 0.0f, 100.0f, 20.0f, U::percent, 0.0f, 1.0f);
            tone     = addFloat ("tone", "Ton (tekrarların parlaklığı)", 1000.0f, 20000.0f, 6000.0f, U::hertz, 5000.0f, 10.0f);
            pingPong = addBool ("pingpong", "Ping-pong (sağ-sol)", false);
        }
        double getTailLengthSeconds() const override { return 10.0; }

    private:
        int samplesFor (double ms) const noexcept { return std::clamp ((int) std::lround (ms * 0.001 * getRate()), 1, size - 2); }
        void prepareEffect() override
        {
            size = (int) std::ceil (maxSeconds * getRate()) + 4;
            buf.assign ((size_t) (2 * size), 0.0f);
            w = 0;
            cur = next = samplesFor (valueOf (time));
            fadePos = -1;
            fadeLen = std::max (1, (int) std::lround (0.06 * getRate()));
            fb.prepare (getRate(), 0.05);   fb.snap (valueOf (feedback) / 100.0);
            wet.prepare (getRate(), 0.05);  wet.snap (valueOf (mix) / 100.0);
            pp.prepare (getRate(), 0.05);   pp.snap (pingPong->get() ? 1.0 : 0.0);
            gTone.prepare (getRate(), 0.05, true); gTone.snap (valueOf (tone));
            design();
            for (auto& h : hpf) h.k = BiquadCoeffs::highPass1 (getRate(), 100.0);
        }
        void design() noexcept { for (auto& l : lpf) l.k = SvfCoeffs::make (SvfType::lowPass, getRate(), gTone.value(), 0.5); }
        // after a reset the input into the delay lines fades in (10 ms): no repeat starts with an edge
        void resetEffect() override
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (auto& l : lpf) l.reset();
            for (auto& h : hpf) h.reset();
            inFade.reset (getRate());
        }
        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            if (numCh < 2) return;
            const int target = samplesFor (valueOf (time));
            fb.setTarget (std::min (0.9, valueOf (feedback) / 100.0));
            wet.setTarget (valueOf (mix) / 100.0);
            pp.setTarget (pingPong->get() ? 1.0 : 0.0);
            gTone.setTarget (valueOf (tone));
            if (fadePos < 0 && target != cur) { next = target; fadePos = 0; }
            float* bl = buf.data();
            float* br = buf.data() + size;
            for (int i = 0; i < n; ++i)
            {
                if (gTone.advance()) design();
                const double f = fb.next(), m = wet.next(), p = pp.next();
                double yl = bl[(w - cur + size) % size], yr = br[(w - cur + size) % size];
                if (fadePos >= 0)
                {
                    const double a = 0.5 - 0.5 * std::cos (pi * fadePos / fadeLen);
                    yl += a * (bl[(w - next + size) % size] - yl);
                    yr += a * (br[(w - next + size) % size] - yr);
                    if (++fadePos >= fadeLen)
                    {
                        cur = next;
                        fadePos = -1;
                        if (target != cur) { next = target; fadePos = 0; }
                    }
                }
                const double xl = ch[0][i], xr = ch[1][i], fi = inFade.next (xl + xr);
                // routing: normal = each channel into its own line; ping-pong = mono input into the
                // left line, the repeats cross over
                const double inL = fi * ((1.0 - p) * xl + p * 0.5 * (xl + xr));
                const double inR = fi * (1.0 - p) * xr;
                const double fbl = hpf[0].process (lpf[0].process ((1.0 - p) * yl + p * yr));
                const double fbr = hpf[1].process (lpf[1].process ((1.0 - p) * yr + p * yl));
                bl[w] = (float) (inL + f * fbl);
                br[w] = (float) (inR + f * fbr);
                w = w + 1 < size ? w + 1 : 0;
                ch[0][i] = (float) (xl + m * yl);
                ch[1][i] = (float) (xr + m * yr);
            }
            for (auto& l : lpf) l.flush();
            for (auto& h : hpf) h.flush();
        }

        juce::AudioParameterFloat *time, *feedback, *mix, *tone;
        juce::AudioParameterBool* pingPong;
        std::vector<float> buf;
        int size = 1, w = 0, cur = 1, next = 1, fadePos = -1, fadeLen = 2880;
        Ramp fb, wet, pp;
        Glide gTone;
        std::array<Svf, 2> lpf;
        std::array<Biquad, 2> hpf;
        OnsetFade inFade;
    };

    //==============================================================================
    // Chorus / stereo widener (always stereo), both running all the time, cross-faded on a switch.
    //  Chorus: per channel a delay of 12 ms +/- (Miktar x 5 ms), modulated by a sine (the right
    //          channel 90 degrees later); "Karışım" adds it to the dry signal.
    //  Genişletici: mid/side - the side gets (Miktar) x a 14 ms delayed, 250 Hz high-passed copy
    //          of the mid. L = M + S', R = M - S': summed to mono the result is exactly the input
    //          mid - the voice never thins out on a mono speaker; the low end stays in the centre.
    class ChorusWidener final : public BuiltInEffect
    {
    public:
        static constexpr double baseMs = 12.0, depthMs = 5.0, widenMs = 14.0;

        explicit ChorusWidener (const Info& i) : BuiltInEffect (i)
        {
            mode   = addChoice ("mode", "Mod", { u8 ("Chorus"), u8 ("Stereo genişletici") }, 0);
            amount = addFloat ("amount", "Miktar (derinlik / genişlik)", 0.0f, 100.0f, 50.0f, U::percent, 0.0f, 1.0f);
            speed  = addFloat ("rate", "Hız (yalnız chorus)", 0.1f, 3.0f, 0.8f, U::hertz, 0.8f, 0.01f);
            mix    = addFloat ("mix", "Karışım (yalnız chorus)", 0.0f, 100.0f, 40.0f, U::percent, 0.0f, 1.0f);
        }

    private:
        void prepareEffect() override
        {
            size = (int) std::ceil ((baseMs + depthMs + widenMs + 5.0) * 0.001 * getRate()) + 4;
            buf.assign ((size_t) (3 * size), 0.0f);   // left, right, mid
            w = 0;
            phase = 0.0;
            widen.prepare (getRate(), 0.04); widen.snap (mode->getIndex() == 1 ? 1.0 : 0.0);
            amt.prepare (getRate(), 0.05);   amt.snap (valueOf (amount) / 100.0);
            wet.prepare (getRate(), 0.05);   wet.snap (valueOf (mix) / 100.0);
            for (auto& h : sideHp) h.k = BiquadCoeffs::highPass (getRate(), 250.0, butterQ2);
        }
        // after a reset the delay lines fill with a fading-in input (10 ms): the delayed copies start smoothly
        void resetEffect() override
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (auto& h : sideHp) h.reset();
            inFade.reset (getRate());
        }
        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            if (numCh < 2) return;
            widen.setTarget (mode->getIndex() == 1 ? 1.0 : 0.0);
            amt.setTarget (valueOf (amount) / 100.0);
            wet.setTarget (valueOf (mix) / 100.0);
            const double inc = twoPi * valueOf (speed) / getRate();
            float* bl = buf.data();
            float* br = buf.data() + size;
            float* bm = buf.data() + 2 * size;
            const double sr = getRate() * 0.001;
            for (int i = 0; i < n; ++i)
            {
                const double xl = ch[0][i], xr = ch[1][i];
                const double wv = widen.next(), a = amt.next(), m = wet.next();
                const double fi = inFade.next (xl + xr);
                bl[w] = (float) (fi * xl);
                br[w] = (float) (fi * xr);
                const double mid = 0.5 * (xl + xr), side = 0.5 * (xl - xr);
                bm[w] = (float) (fi * mid);

                // chorus
                const double dL = (baseMs + a * depthMs * std::sin (phase)) * sr;
                const double dR = (baseMs + a * depthMs * std::sin (phase + 0.5 * pi)) * sr;
                const double chorusL = (1.0 - 0.5 * m) * xl + 0.5 * m * readFrac (bl, size, w, dL);
                const double chorusR = (1.0 - 0.5 * m) * xr + 0.5 * m * readFrac (br, size, w, dR);

                // widener
                const double dec = sideHp[0].process (readFrac (bm, size, w, widenMs * sr));
                const double s2 = side + a * 0.7 * dec;
                const double wideL = mid + s2, wideR = mid - s2;

                ch[0][i] = (float) (chorusL + wv * (wideL - chorusL));
                ch[1][i] = (float) (chorusR + wv * (wideR - chorusR));
                w = w + 1 < size ? w + 1 : 0;
                phase += inc;
                if (phase > twoPi) phase -= twoPi;
            }
            for (auto& h : sideHp) h.flush();
        }

        juce::AudioParameterChoice* mode;
        juce::AudioParameterFloat *amount, *speed, *mix;
        std::vector<float> buf;
        int size = 1, w = 0;
        double phase = 0.0;
        Ramp widen, amt, wet;
        OnsetFade inFade;
        std::array<Biquad, 1> sideHp;
    };
}

std::unique_ptr<BuiltInEffect> createSaturation (const BuiltInEffect::Info& i) { return std::make_unique<Saturation> (i); }
std::unique_ptr<BuiltInEffect> createLoFi       (const BuiltInEffect::Info& i) { return std::make_unique<LoFi> (i); }
std::unique_ptr<BuiltInEffect> createReverb     (const BuiltInEffect::Info& i) { return std::make_unique<RoomReverb> (i); }
std::unique_ptr<BuiltInEffect> createEcho       (const BuiltInEffect::Info& i) { return std::make_unique<Echo> (i); }
std::unique_ptr<BuiltInEffect> createChorus     (const BuiltInEffect::Info& i) { return std::make_unique<ChorusWidener> (i); }
}
