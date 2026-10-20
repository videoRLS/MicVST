// Level, protection, time and channel effects: Çıkış Limiteri, Sessize Alma, De-esser,
// Ses Gecikmesi (senkron), Kanal Araçları.
#include "audio/fx/EffectFactories.h"
#include "audio/fx/FxDsp.h"
#include "audio/fx/Readout.h"
#include <vector>

namespace fx
{
namespace
{
    using U = BuiltInEffect::Unit;
    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
    float valueOf (const juce::RangedAudioParameter* p) noexcept { return p->convertFrom0to1 (p->getValue()); }

    //==============================================================================
    // Output limiter: Otomatik Kazanç's limiter (AutoGain.h) with an adjustable ceiling and release.
    // The audio passes a 3 ms delay while the limiter sees it undelayed (true peak, 8x), so the
    // reduction is in place when a peak leaves the delay. Peaks above (ceiling - 2.3 dB) are bent
    // smoothly towards (ceiling - 0.3 dB) (tanh knee); an instant guard makes sure no sample ever
    // exceeds the ceiling. Hold 20 ms, then release.
    class OutputLimiter final : public BuiltInEffect
    {
    public:
        static constexpr double lookAheadSeconds = 0.003, holdSeconds = 0.02, marginDb = 0.3, kneeDb = 2.0;
        static constexpr int maxLookAhead = 1024;

        explicit OutputLimiter (const Info& i) : BuiltInEffect (i)
        {
            ceiling = addFloat ("ceiling", "Tavan (dBTP)", -12.0f, 0.0f, -1.0f, U::decibel, 0.0f, 0.1f);
            drive   = addFloat ("input", "Giriş kazancı", 0.0f, 12.0f, 0.0f, U::decibel, 0.0f, 0.1f);
            release = addFloat ("release", "Bırakma süresi", 20.0f, 500.0f, 80.0f, U::milliseconds, 100.0f, 1.0f);
        }

        std::unique_ptr<juce::Component> createDisplay() override
        {
            return std::make_unique<Readout> ([this]
            {
                const float gr = grHold.exchange (0.0f, std::memory_order_relaxed);
                return u8 ("Anlık azaltma: ") + builtin::formatValue (-gr, U::decibel);
            });
        }

        int computeLatency() const override { return lookAheadFor (getRate()); }
        static int lookAheadFor (double rate) { return std::clamp ((int) std::lround (lookAheadSeconds * rate), 1, maxLookAhead); }

    private:
        void prepareEffect() override
        {
            L = lookAheadFor (getRate());
            attackCoeff = 1.0 - std::exp (-5.0 / (double) L);
            holdSamples = (int) std::lround (holdSeconds * getRate());
            inGain.prepare (getRate(), 0.05);
            inGain.snap (dbToGain (valueOf (drive)));
            ceil.prepare (getRate(), 0.05, false);
            ceil.snap (valueOf (ceiling));
        }
        void resetEffect() override
        {
            delay.fill (0.0f);
            wants.fill (1.0);
            pos = wantPos = belowOne = 0;
            gain = 1.0;
            hold = 0;
            for (auto& d : tp) d.reset();
            for (auto& d : tpOut) d.reset();
            for (auto& d : tpNext) d.reset();
        }
        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            inGain.setTarget (dbToGain (valueOf (drive)));
            ceil.setTarget (valueOf (ceiling));
            const double relCoeff = 1.0 - std::exp (-1.0 / (0.001 * valueOf (release) * getRate()));
            const int W = L + 1;
            double minGain = 1.0;
            for (int i = 0; i < n; ++i)
            {
                if (ceil.advance() || i == 0)
                {
                    cDb = ceil.value();
                    cLin = dbToGain (cDb);
                    k0 = cDb - marginDb - kneeDb;
                    kneeLin = dbToGain (k0);
                }
                const double g = inGain.next();

                double aNew = 0.0, aOut = 0.0;
                float out[2] = {};
                for (int c = 0; c < numCh; ++c)
                {
                    float* d = delay.data() + c * maxLookAhead;
                    out[c] = d[pos];
                    const float xin = (float) (ch[c][i] * g);
                    d[pos] = xin;
                    aNew = std::max (aNew, std::max (std::abs ((double) xin), tp[(size_t) c].push (xin)));
                    // the guard looks at the outgoing sample and the wave on both sides of it (true
                    // peak: detectors fed 6 and 7 samples ahead of the output read exactly the
                    // intervals before and after it)
                    double ahead = 0.0;
                    if (L > TruePeakDetector::delay + 2)
                        ahead = std::max (tpOut[(size_t) c].push (d[(pos + TruePeakDetector::delay) % L]),
                                          tpNext[(size_t) c].push (d[(pos + TruePeakDetector::delay + 1) % L]));
                    aOut = std::max ({ aOut, std::abs ((double) out[c]), ahead });
                }
                pos = pos + 1 < L ? pos + 1 : 0;

                double want = 1.0;
                if (aNew > kneeLin)
                {
                    const double xDb = gainToDb (aNew);
                    want = dbToGain (k0 + kneeDb * std::tanh ((xDb - k0) / kneeDb) - xDb);
                }
                if (wants[(size_t) wantPos] < 1.0) --belowOne;
                wants[(size_t) wantPos] = want;
                if (want < 1.0) ++belowOne;
                wantPos = wantPos + 1 < W ? wantPos + 1 : 0;

                double need = 1.0;
                if (belowOne > 0)
                    for (int k = 0; k < W; ++k) need = std::min (need, wants[(size_t) k]);

                if (need < gain)       { gain += (need - gain) * attackCoeff; hold = holdSamples; }
                else if (hold > 0)     --hold;
                else if (gain < 1.0)
                {
                    gain = std::min (need, gain + (1.0 - gain) * relCoeff);
                    if (gain > 1.0 - 1.0e-7 && need >= 1.0) gain = 1.0;
                }
                if (aOut * gain > guardLin * cLin)
                    gain = guardLin * cLin / aOut;   // guard: no sample (and no wave between) above the ceiling

                for (int c = 0; c < numCh; ++c)
                    ch[c][i] = (float) (out[c] * gain);
                minGain = std::min (minGain, gain);
            }
            storeMax (grHold, (float) -gainToDb (minGain));
        }

        juce::AudioParameterFloat *ceiling, *drive, *release;
        int L = 144, holdSamples = 960, pos = 0, wantPos = 0, belowOne = 0, hold = 0;
        double attackCoeff = 0.03, gain = 1.0, cDb = -1.0, cLin = 0.89, k0 = -3.3, kneeLin = 0.68;
        Ramp inGain;
        Glide ceil;
        std::array<float, 2 * maxLookAhead> delay {};
        std::array<double, maxLookAhead + 1> wants {};
        std::array<TruePeakDetector, 2> tp, tpOut, tpNext;
        static constexpr double guardLin = 0.98855;   // -0.1 dB: room for the 12-tap reading's own error
        std::atomic<float> grHold { 0.0f };
    };

    //==============================================================================
    // Mute: raised-cosine fade (15 ms) to exact silence and back.
    class Mute final : public BuiltInEffect
    {
    public:
        explicit Mute (const Info& i) : BuiltInEffect (i)
        {
            muted = addBool ("muted", "Sessiz", false);
        }
        bool isMuted() const { return muted->get(); }
        void setMuted (bool m) { muted->setValueNotifyingHost (m ? 1.0f : 0.0f); }

    private:
        void prepareEffect() override { fade.prepare (getRate(), 0.015); fade.snap (muted->get() ? 0.0 : 1.0); }
        void resetEffect() override {}
        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            fade.setTarget (muted->get() ? 0.0 : 1.0);
            if (! fade.moving())
            {
                if (fade.value() >= 1.0) return;   // open: untouched (bit-exact)
                for (int c = 0; c < numCh; ++c) juce::FloatVectorOperations::clear (ch[c], n);
                return;
            }
            for (int i = 0; i < n; ++i)
            {
                const float g = (float) fade.next();   // raised-cosine (Ramp)
                for (int c = 0; c < numCh; ++c) ch[c][i] *= g;
            }
        }
        juce::AudioParameterBool* muted;
        Ramp fade;
    };

    //==============================================================================
    // De-esser: the part above the chosen frequency (Butterworth high-pass, 12 dB/oct) is the
    // detector; its envelope (attack 0.5 ms, release 60 ms) above the threshold is reduced 4:1
    // with a 6 dB soft knee, at most by "Azaltma".
    //  Split mode ("Yalnız tizler"): a dynamic high shelf (state-variable filter, half its reduction
    //  at the chosen frequency, the full reduction about half an octave above) whose gain follows the
    //  reduction every sample - only the hiss is turned down; at 0 dB of reduction the shelf is flat.
    //  Wideband mode: the whole signal follows the reduction.
    class DeEsser final : public BuiltInEffect
    {
    public:
        explicit DeEsser (const Info& i) : BuiltInEffect (i)
        {
            freq  = addFloat ("freq", "Frekans", 3000.0f, 10000.0f, 6000.0f, U::hertz, 5500.0f, 10.0f);
            thr   = addFloat ("threshold", "Eşik", -60.0f, 0.0f, -30.0f, U::decibel, 0.0f, 0.1f);
            range = addFloat ("range", "En fazla azaltma", 0.0f, 12.0f, 6.0f, U::dbAmount, 0.0f, 0.1f);
            mode  = addChoice ("mode", "Mod", { u8 ("Yalnız tizler (bölünmüş)"), u8 ("Tüm ses (geniş bant)") }, 0);
        }

        std::unique_ptr<juce::Component> createDisplay() override
        {
            return std::make_unique<Readout> ([this]
            {
                const float gr = grHold.exchange (0.0f, std::memory_order_relaxed);
                return u8 ("Anlık azaltma: ") + builtin::formatValue (-gr, U::decibel);
            });
        }

        static constexpr double ratio = 4.0, softKneeDb = 6.0, shelfQ = 0.6;

        // Static gain reduction (dB, >= 0) for a detector level (tests).
        static double reductionDb (double levelDb, double thresholdDb, double rangeDb) noexcept
        {
            const double over = levelDb - thresholdDb, slope = 1.0 - 1.0 / ratio;
            double gr;
            if (over <= -softKneeDb * 0.5)     gr = 0.0;
            else if (over >= softKneeDb * 0.5) gr = over * slope;
            else { const double t = over + softKneeDb * 0.5; gr = slope * t * t / (2.0 * softKneeDb); }
            return std::clamp (gr, 0.0, rangeDb);
        }

    private:
        void prepareEffect() override
        {
            gf.prepare (getRate(), 0.05, true);
            gf.snap (valueOf (freq));
            atk = 1.0 - std::exp (-1.0 / (0.0005 * getRate()));
            rel = 1.0 - std::exp (-1.0 / (0.060 * getRate()));
            grAtk = 1.0 - std::exp (-1.0 / (0.001 * getRate()));
            split.prepare (getRate(), 0.03);
            split.snap (mode->getIndex() == 0 ? 1.0 : 0.0);
            designDetector();
        }
        void designDetector() noexcept
        {
            const auto k = BiquadCoeffs::highPass (getRate(), gf.value(), butterQ2);
            for (auto& h : hp) h.k = k;
            designShelf (shelfDb);
        }
        void designShelf (double gainDb) noexcept
        {
            shelfDb = gainDb;
            const auto k = SvfCoeffs::make (SvfType::highShelf, getRate(), gf.value(), shelfQ, gainDb);
            for (auto& sh : shelf) sh.k = k;
        }
        void resetEffect() override
        {
            for (auto& h : hp) h.reset();
            for (auto& sh : shelf) sh.reset();
            env = grNow = 0.0;
            designShelf (0.0);
        }
        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            gf.setTarget (valueOf (freq));
            split.setTarget (mode->getIndex() == 0 ? 1.0 : 0.0);
            const double t = valueOf (thr), r = valueOf (range);
            double maxGr = 0.0;
            for (int i = 0; i < n; ++i)
            {
                if (gf.advance()) designDetector();
                double a = 0.0;
                for (int c = 0; c < numCh; ++c)
                    a = std::max (a, std::abs (hp[(size_t) c].process (ch[c][i])));
                env += (a - env) * (a > env ? atk : rel);
                // the reduction itself moves smoothly (1 ms down, 60 ms back up): also a sudden
                // threshold change glides
                const double gr = reductionDb (gainToDb (env), t, r);
                grNow += (gr - grNow) * (gr > grNow ? grAtk : rel);
                maxGr = std::max (maxGr, grNow);
                if (std::abs (-grNow - shelfDb) > 0.01) designShelf (-grNow);
                const double g = dbToGain (-grNow), s = split.next();
                for (int c = 0; c < numCh; ++c)
                {
                    const double x = ch[c][i];
                    const double ySplit = shelf[(size_t) c].process (x);
                    ch[c][i] = (float) (s * ySplit + (1.0 - s) * g * x);
                }
            }
            for (auto& h : hp) h.flush();
            for (auto& sh : shelf) sh.flush();
            if (env < 1.0e-20) env = 0.0;
            if (grNow < 1.0e-9) grNow = 0.0;
            storeMax (grHold, (float) maxGr);
        }

        juce::AudioParameterFloat *freq, *thr, *range;
        juce::AudioParameterChoice* mode;
        Glide gf;
        Ramp split;
        std::array<Biquad, 2> hp;
        std::array<Svf, 2> shelf;
        double env = 0.0, atk = 0.1, rel = 0.001, grAtk = 0.02, grNow = 0.0, shelfDb = 0.0;
        std::atomic<float> grHold { 0.0f };
    };

    //==============================================================================
    // Sync delay: the audio comes out "Gecikme" later (aligning the voice with a camera). A change
    // cross-fades (20 ms, raised cosine) from the old to the new read position - no click, no pitch
    // glide. The delay is reported as latency.
    class SyncDelay final : public BuiltInEffect
    {
    public:
        static constexpr float maxMs = 1000.0f;

        explicit SyncDelay (const Info& i) : BuiltInEffect (i)
        {
            ms = addFloat ("delay", "Gecikme", 0.0f, maxMs, 0.0f, U::milliseconds, 150.0f, 0.5f);
        }
        int computeLatency() const override { return samplesFor (valueOf (ms)); }

    private:
        int samplesFor (double v) const noexcept
        {
            return std::clamp ((int) std::lround (v * 0.001 * getRate()), 0, maxDelay);
        }
        void prepareEffect() override
        {
            maxDelay = (int) std::ceil (maxMs * 0.001 * getRate());
            size = maxDelay + 1;
            buf.assign ((size_t) (2 * size), 0.0f);
            fadeLen = std::max (1, (int) std::lround (0.02 * getRate()));
            cur = samplesFor (valueOf (ms));
            next = cur;
            fadePos = -1;
        }
        void resetEffect() override { std::fill (buf.begin(), buf.end(), 0.0f); w = 0; }
        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            const int target = samplesFor (valueOf (ms));
            if (fadePos < 0 && target != cur) { next = target; fadePos = 0; }
            // (always written: a later change must find the recent audio in the buffer; at 0 ms the
            // output is the input sample itself, bit-exact)
            for (int i = 0; i < n; ++i)
            {
                double a = 1.0;
                if (fadePos >= 0) a = 1.0 - smoothStep ((double) fadePos / fadeLen);
                for (int c = 0; c < numCh; ++c)
                {
                    float* b = buf.data() + c * size;
                    b[w] = ch[c][i];
                    const float yOld = b[(w - cur + size) % size];
                    float y = yOld;
                    if (fadePos >= 0)
                        y = (float) (a * yOld + (1.0 - a) * b[(w - next + size) % size]);
                    ch[c][i] = y;
                }
                w = w + 1 < size ? w + 1 : 0;
                if (fadePos >= 0 && ++fadePos >= fadeLen)
                {
                    cur = next;
                    fadePos = -1;
                    if (target != cur) { next = target; fadePos = 0; }
                }
            }
        }

        juce::AudioParameterFloat* ms;
        std::vector<float> buf;   // sized in prepareEffect (message thread)
        int size = 1, w = 0, maxDelay = 48000, cur = 0, next = 0, fadePos = -1, fadeLen = 960;
    };

    //==============================================================================
    // Channel tool (stereo): phase flips (each a 10 ms glide of the sign, through zero), a channel
    // matrix (stereo / left to both / right to both / swapped / mono sum) whose elements glide over
    // 20 ms, and a balance (the other side is turned down, the centre stays at unity).
    class ChannelTool final : public BuiltInEffect
    {
    public:
        explicit ChannelTool (const Info& i) : BuiltInEffect (i)
        {
            invL = addBool ("phaseL", "Sol kanal faz çevir", false);
            invR = addBool ("phaseR", "Sağ kanal faz çevir", false);
            mode = addChoice ("mode", "Kanal", { u8 ("Stereo (değişmez)"), u8 ("Sol kanal iki kanala"), u8 ("Sağ kanal iki kanala"),
                                                 u8 ("Kanalları değiştir"), u8 ("Mono (sol + sağ)") }, 0);
            bal  = addFloat ("balance", "Denge (- sol / + sağ)", -100.0f, 100.0f, 0.0f, U::percent, 0.0f, 1.0f);
        }

    private:
        static void matrixFor (int m, double* k) noexcept   // k = { LL, RL, LR, RR }: outL = LL*l + RL*r
        {
            switch (m)
            {
                case 1:  k[0] = 1; k[1] = 0; k[2] = 1; k[3] = 0; break;
                case 2:  k[0] = 0; k[1] = 1; k[2] = 0; k[3] = 1; break;
                case 3:  k[0] = 0; k[1] = 1; k[2] = 1; k[3] = 0; break;
                case 4:  k[0] = 0.5; k[1] = 0.5; k[2] = 0.5; k[3] = 0.5; break;
                default: k[0] = 1; k[1] = 0; k[2] = 0; k[3] = 1; break;
            }
        }
        void targets (double* t) const noexcept
        {
            t[0] = invL->get() ? -1.0 : 1.0;
            t[1] = invR->get() ? -1.0 : 1.0;
            matrixFor (mode->getIndex(), t + 2);
            const double b = valueOf (bal) / 100.0;
            t[6] = b > 0.0 ? 1.0 - b : 1.0;
            t[7] = b < 0.0 ? 1.0 + b : 1.0;
        }
        void prepareEffect() override
        {
            double t[8];
            targets (t);
            for (int k = 0; k < 8; ++k)
            {
                r[(size_t) k].prepare (getRate(), k < 2 ? 0.01 : 0.02);
                r[(size_t) k].snap (t[k]);
            }
        }
        void resetEffect() override {}
        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            double t[8];
            targets (t);
            bool identity = true;
            for (int k = 0; k < 8; ++k)
            {
                r[(size_t) k].setTarget (t[k]);
                const double neutral = (k == 0 || k == 1 || k == 2 || k == 5 || k == 6 || k == 7) ? 1.0 : 0.0;
                identity = identity && ! r[(size_t) k].moving() && r[(size_t) k].value() == neutral;
            }
            if (identity || numCh < 2) return;   // stereo, no flip, centred: untouched
            float* L = ch[0];
            float* R = ch[1];
            for (int i = 0; i < n; ++i)
            {
                double v[8];
                for (int k = 0; k < 8; ++k) v[k] = r[(size_t) k].next();
                const double l = L[i] * v[0], rr = R[i] * v[1];
                L[i] = (float) ((v[2] * l + v[3] * rr) * v[6]);
                R[i] = (float) ((v[4] * l + v[5] * rr) * v[7]);
            }
        }

        juce::AudioParameterBool *invL, *invR;
        juce::AudioParameterChoice* mode;
        juce::AudioParameterFloat* bal;
        std::array<Ramp, 8> r;   // flipL, flipR, LL, RL, LR, RR, gainL, gainR
    };
}

std::unique_ptr<BuiltInEffect> createLimiter     (const BuiltInEffect::Info& i) { return std::make_unique<OutputLimiter> (i); }
std::unique_ptr<BuiltInEffect> createMute        (const BuiltInEffect::Info& i) { return std::make_unique<Mute> (i); }
std::unique_ptr<BuiltInEffect> createDeEsser     (const BuiltInEffect::Info& i) { return std::make_unique<DeEsser> (i); }
std::unique_ptr<BuiltInEffect> createSyncDelay   (const BuiltInEffect::Info& i) { return std::make_unique<SyncDelay> (i); }
std::unique_ptr<BuiltInEffect> createChannelTool (const BuiltInEffect::Info& i) { return std::make_unique<ChannelTool> (i); }

// De-esser curve for the unit tests.
double deEsserReductionDb (double levelDb, double thresholdDb, double rangeDb) { return DeEsser::reductionDb (levelDb, thresholdDb, rangeDb); }
}
