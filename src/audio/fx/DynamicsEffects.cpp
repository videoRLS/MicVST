// Dynamics: Kompresör and Gürültü Kapısı (noise gate).
#include "audio/fx/EffectFactories.h"
#include "audio/fx/FxDsp.h"
#include "audio/fx/Readout.h"
#include "Text.h"

namespace fx
{
namespace
{
    using U = BuiltInEffect::Unit;
    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
    float valueOf (const juce::RangedAudioParameter* p) noexcept { return p->convertFrom0to1 (p->getValue()); }

    //==============================================================================
    // Compressor (feed-forward, stereo-linked): the louder channel's level in dB goes through the
    // static curve (threshold, ratio, soft knee); the gain reduction is smoothed in the dB domain
    // with separate attack and release (a level detector that does not follow single waves, so low
    // voices are not distorted at normal settings). Make-up gain and a dry/wet mix (parallel
    // compression) after it. Threshold, ratio, knee, make-up and mix glide (50 ms), so turning a
    // knob never clicks. No latency.
    class Compressor final : public BuiltInEffect
    {
    public:
        explicit Compressor (const Info& i) : BuiltInEffect (i)
        {
            thr     = addFloat ("threshold", "Eşik", -60.0f, 0.0f, -24.0f, U::decibel, -20.0f, 0.1f);
            ratio   = addFloat ("ratio", "Oran", 1.0f, 20.0f, 3.0f, U::ratio, 4.0f, 0.1f);
            attack  = addFloat ("attack", "Atak süresi", 0.5f, 100.0f, 5.0f, U::milliseconds, 10.0f, 0.1f);
            release = addFloat ("release", "Bırakma süresi", 10.0f, 1000.0f, 120.0f, U::milliseconds, 150.0f, 1.0f);
            knee    = addFloat ("knee", "Yumuşak diz", 0.0f, 12.0f, 6.0f, U::dbAmount, 0.0f, 0.1f);
            makeup  = addFloat ("makeup", "Telafi kazancı", 0.0f, 24.0f, 0.0f, U::decibel, 0.0f, 0.1f);
            mix     = addFloat ("mix", "Karışım", 0.0f, 100.0f, 100.0f, U::percent, 0.0f, 1.0f);
        }

        std::unique_ptr<juce::Component> createDisplay() override
        {
            return std::make_unique<Readout> ([this]
            {
                const float gr = grHold.exchange (0.0f, std::memory_order_relaxed);
                return micvst::tr ("Anlık azaltma: ") + builtin::formatValue (-gr, U::decibel);
            });
        }

        // Static gain reduction (dB, >= 0) for a level (tests).
        static double reductionDb (double levelDb, double thresholdDb, double ratioIn, double kneeDb) noexcept
        {
            const double slope = 1.0 - 1.0 / std::max (1.0, ratioIn), over = levelDb - thresholdDb;
            if (kneeDb > 0.0 && std::abs (over) <= kneeDb * 0.5)
            {
                const double t = over + kneeDb * 0.5;
                return slope * t * t / (2.0 * kneeDb);
            }
            return over > 0.0 ? slope * over : 0.0;
        }

    private:
        void prepareEffect() override
        {
            for (auto* g : { &gThr, &gRatio, &gKnee, &gMakeup, &gMix })
                g->prepare (getRate(), 0.05, false);
            gThr.snap (valueOf (thr));
            gRatio.snap (valueOf (ratio));
            gKnee.snap (valueOf (knee));
            gMakeup.snap (valueOf (makeup));
            gMix.snap (0.01 * valueOf (mix));
        }
        void resetEffect() override { grDb = 0.0; }

        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            gThr.setTarget (valueOf (thr));
            gRatio.setTarget (valueOf (ratio));
            gKnee.setTarget (valueOf (knee));
            gMakeup.setTarget (valueOf (makeup));
            gMix.setTarget (0.01 * valueOf (mix));
            const double a = std::exp (-1.0 / (0.001 * valueOf (attack) * getRate()));
            const double r = std::exp (-1.0 / (0.001 * valueOf (release) * getRate()));
            double maxGr = 0.0;
            for (int i = 0; i < n; ++i)
            {
                gThr.advance(); gRatio.advance(); gKnee.advance(); gMakeup.advance(); gMix.advance();
                double peak = 0.0;
                for (int c = 0; c < numCh; ++c)
                    peak = std::max (peak, std::abs ((double) ch[c][i]));
                const double want = peak > 1.0e-6 ? reductionDb (gainToDb (peak), gThr.value(), gRatio.value(), gKnee.value()) : 0.0;
                const double k = want > grDb ? a : r;
                grDb = k * grDb + (1.0 - k) * want;
                maxGr = std::max (maxGr, grDb);
                const double wet = dbToGain (gMakeup.value() - grDb), m = gMix.value();
                const double g = m * wet + (1.0 - m);
                for (int c = 0; c < numCh; ++c)
                    ch[c][i] = (float) (ch[c][i] * g);
            }
            if (grDb < 1.0e-9) grDb = 0.0;
            storeMax (grHold, (float) maxGr);
        }

        juce::AudioParameterFloat *thr, *ratio, *attack, *release, *knee, *makeup, *mix;
        Glide gThr, gRatio, gKnee, gMakeup, gMix;
        double grDb = 0.0;
        std::atomic<float> grHold { 0.0f };
    };

    //==============================================================================
    // Noise gate: turns the audio down by "Kısma" while the voice is below the threshold - room
    // noise, fan, keyboard between sentences. The detector hears the voice 3 ms earlier than the
    // output (look-ahead, reported as latency), so the gate is already open when a word starts and
    // no first consonant is cut; it opens within "Açılma süresi" (a raised-cosine curve), stays open for "Bekleme" after
    // the voice falls below the threshold, and closes over "Kapanma süresi". It closes only 4 dB
    // below the threshold (hysteresis: no flutter at the threshold). A high-pass in the detector
    // (only there) keeps rumble from opening it. The gain moves in dB, sample by sample: no clicks.
    class NoiseGate final : public BuiltInEffect
    {
    public:
        static constexpr double lookAheadSeconds = 0.003, hysteresisDb = 4.0;
        static constexpr int maxLookAhead = 1024;

        explicit NoiseGate (const Info& i) : BuiltInEffect (i)
        {
            thr     = addFloat ("threshold", "Eşik", -80.0f, -10.0f, -50.0f, U::decibel, -45.0f, 0.1f);
            range   = addFloat ("range", "Kısma", 0.0f, 90.0f, 40.0f, U::dbAmount, 30.0f, 0.5f);
            attack  = addFloat ("attack", "Açılma süresi", 0.5f, 50.0f, 2.0f, U::milliseconds, 5.0f, 0.1f);
            hold    = addFloat ("hold", "Bekleme", 0.0f, 1000.0f, 120.0f, U::milliseconds, 150.0f, 1.0f);
            release = addFloat ("release", "Kapanma süresi", 10.0f, 2000.0f, 200.0f, U::milliseconds, 200.0f, 1.0f);
            hpf     = addFloat ("detecthp", "Algılama alt kesimi", 20.0f, 500.0f, 80.0f, U::hertz, 100.0f, 1.0f);
        }

        std::unique_ptr<juce::Component> createDisplay() override
        {
            return std::make_unique<Readout> ([this]
            {
                const float atten = attenHold.exchange (0.0f, std::memory_order_relaxed);
                return atten < 0.5f ? micvst::tr ("Kapı: açık")
                                    : micvst::tr ("Kapı: kısıyor ") + builtin::formatValue (-atten, U::decibel);
            });
        }

        int computeLatency() const override { return lookAheadFor (getRate()); }
        static int lookAheadFor (double rate) { return std::clamp ((int) std::lround (lookAheadSeconds * rate), 1, maxLookAhead); }

    private:
        void prepareEffect() override
        {
            L = lookAheadFor (getRate());
            gHp.prepare (getRate(), 0.05, true);
            gHp.snap (valueOf (hpf));
            gRange.prepare (getRate(), 0.05, false);
            gRange.snap (valueOf (range));
            envRel = std::exp (-1.0 / (0.010 * getRate()));
            design();
        }
        void design() noexcept
        {
            const auto k = BiquadCoeffs::highPass (getRate(), gHp.value(), 0.7071);
            for (auto& h : hp) h.k = k;
        }
        void resetEffect() override
        {
            delay.fill (0.0f);
            pos = 0;
            for (auto& h : hp) h.reset();
            env = 0.0;
            open = rampOpening = true;   // starts open (and closes smoothly if there is only noise)
            holdLeft = 0;
            g = 1.0;
            t = 1.0;
        }

        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            gHp.setTarget (valueOf (hpf));
            gRange.setTarget (valueOf (range));
            const double thrDb = valueOf (thr);
            const double openLin = dbToGain (thrDb), closeLin = dbToGain (thrDb - hysteresisDb);
            const int holdSamples = (int) std::lround (0.001 * valueOf (hold) * getRate());
            const double attackSamples = std::max (1.0, 0.001 * valueOf (attack) * getRate());
            const double releaseSamples = std::max (1.0, 0.001 * valueOf (release) * getRate());
            double maxAtten = 0.0;
            for (int i = 0; i < n; ++i)
            {
                if (gHp.advance()) design();
                gRange.advance();
                double d = 0.0;
                float out[2] = {};
                for (int c = 0; c < numCh; ++c)
                {
                    const float x = ch[c][i];
                    d = std::max (d, std::abs (hp[(size_t) c].process (x)));
                    float* line = delay.data() + c * maxLookAhead;
                    out[c] = line[pos];
                    line[pos] = x;
                }
                pos = pos + 1 < L ? pos + 1 : 0;

                env = d > env ? d : env * envRel + d * (1.0 - envRel);
                if (env >= openLin)       { open = true; holdLeft = holdSamples; }
                else if (env < closeLin)  { if (holdLeft > 0) --holdLeft; else open = false; }
                else if (open)            holdLeft = holdSamples;   // between the two: stays as it is

                // opening: a raised-cosine curve in amplitude over the attack time; closing: a
                // raised-cosine curve in dB over the release time - both start and end smoothly
                if (open != rampOpening) { rampOpening = open; rampStart = g; rampStartDb = gainToDb (g); t = 0.0; }
                if (t < 1.0) t = std::min (1.0, t + 1.0 / (open ? attackSamples : releaseSamples));
                const double sh = 0.5 - 0.5 * std::cos (pi * t);
                if (open) g = rampStart + (1.0 - rampStart) * sh;
                else      g = dbToGain (rampStartDb + (-gRange.value() - rampStartDb) * sh);
                for (int c = 0; c < numCh; ++c)
                    ch[c][i] = (float) (out[c] * g);
                maxAtten = std::max (maxAtten, -gainToDb (g));
            }
            for (auto& h : hp) h.flush();
            if (env < 1.0e-20) env = 0.0;
            storeMax (attenHold, (float) maxAtten);
        }

        juce::AudioParameterFloat *thr, *range, *attack, *hold, *release, *hpf;
        Glide gHp, gRange;
        std::array<Biquad, 2> hp;
        std::array<float, 2 * maxLookAhead> delay {};
        int L = 144, pos = 0, holdLeft = 0;
        double env = 0.0, envRel = 0.99, g = 1.0, t = 1.0, rampStart = 1.0, rampStartDb = 0.0;
        bool open = true, rampOpening = true;
        std::atomic<float> attenHold { 0.0f };
    };
}

std::unique_ptr<BuiltInEffect> createCompressor (const BuiltInEffect::Info& i) { return std::make_unique<Compressor> (i); }
std::unique_ptr<BuiltInEffect> createGate       (const BuiltInEffect::Info& i) { return std::make_unique<NoiseGate> (i); }

double compressorReductionDb (double levelDb, double thresholdDb, double ratio, double kneeDb)
{
    return Compressor::reductionDb (levelDb, thresholdDb, ratio, kneeDb);
}
}
