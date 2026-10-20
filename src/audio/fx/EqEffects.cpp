// Tone / EQ and cleaning effects: Low-Cut, High-Cut, Gövde, Çamur Temizleyici, Genizlik Azaltıcı,
// Netlik, Hava, Yakınlık, Tilt EQ, Parametrik EQ, Uğultu Giderici, DC Kayması Giderici.
#include "audio/fx/EffectFactories.h"
#include "audio/fx/FilterEffect.h"

namespace fx
{
namespace
{
    using U = BuiltInEffect::Unit;
    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

    //==============================================================================
    // Low-Cut / High-Cut: Butterworth, 12 or 24 dB/octave. Both slopes run all the time and a
    // switch cross-fades between them (30 ms), so changing the slope never clicks.
    class CutFilter final : public FilterEffect
    {
    public:
        CutFilter (const Info& i, bool highPassIn) : FilterEffect (i), highPass (highPassIn)
        {
            freq = highPass ? addFloat ("freq", "Frekans", 20.0f, 200.0f, 75.0f, U::hertz, 70.0f, 1.0f)
                            : addFloat ("freq", "Frekans", 4000.0f, 20000.0f, 16000.0f, U::hertz, 10000.0f, 10.0f);
            slope = addChoice ("slope", "Eğim", { u8 ("12 dB/oktav"), u8 ("24 dB/oktav") }, highPass ? 1 : 0);
            bind (f, freq, true);
            numSections = 3;   // 0: order 2; 1-2: order 4
        }

        double responseAt (double hz) const override
        {
            const double w = mix.getTarget();
            const double m12 = coeffs[0].biquad().magnitude (getRate(), hz);
            const double m24 = coeffs[1].biquad().magnitude (getRate(), hz) * coeffs[2].biquad().magnitude (getRate(), hz);
            return (1.0 - w) * m12 + w * m24;   // exact when not cross-fading (w = 0 or 1)
        }

    private:
        void onPrepare() override
        {
            mix.prepare (getRate(), 0.03);
            mix.snap (slope->getIndex() == 1 ? 1.0 : 0.0);
        }
        void readTargets() noexcept override
        {
            FilterEffect::readTargets();
            mix.setTarget (slope->getIndex() == 1 ? 1.0 : 0.0);
        }
        void design() noexcept override
        {
            const double r = getRate(), fr = f.value();
            const auto t = highPass ? SvfType::highPass : SvfType::lowPass;
            setCoeffs (0, SvfCoeffs::make (t, r, fr, butterQ2));
            setCoeffs (1, SvfCoeffs::make (t, r, fr, butterQ4a));
            setCoeffs (2, SvfCoeffs::make (t, r, fr, butterQ4b));
        }
        void frame() noexcept override { w = mix.next(); }
        double tick (int c, double x) noexcept override
        {
            auto& st = state[(size_t) c];
            const double y12 = st[0].process (x);
            const double y24 = st[2].process (st[1].process (x));
            return y12 + w * (y24 - y12);
        }

        const bool highPass;
        juce::AudioParameterFloat* freq;
        juce::AudioParameterChoice* slope;
        Glide f;
        Ramp mix;
        double w = 0.0;
    };

    //==============================================================================
    // One bell / shelf section with gain, frequency and optional Q parameters.
    class BandEffect final : public FilterEffect
    {
    public:
        enum class Shape { peak, cut, lowShelf, highShelf };

        struct Spec
        {
            Shape shape;
            const char* gainName; float gainMin, gainMax, gainDef;
            float fMin, fMax, fDef, fCentre;
            float qMin, qMax, qDef;   // qMin == qMax: fixed Q, no parameter
        };

        BandEffect (const Info& i, const Spec& s) : FilterEffect (i), spec (s)
        {
            gain = addFloat ("gain", s.gainName, s.gainMin, s.gainMax, s.gainDef,
                             s.shape == Shape::cut ? U::dbAmount : U::decibel, 0.0f, 0.1f);
            freq = addFloat ("freq", "Frekans", s.fMin, s.fMax, s.fDef, U::hertz, s.fCentre, 1.0f);
            if (s.qMax > s.qMin)
                q = addFloat ("q", "Genişlik (Q)", s.qMin, s.qMax, s.qDef, U::plain, 0.0f, 0.01f);
            bind (gg, gain, false);
            bind (gf, freq, true);
            if (q != nullptr) bind (gq, q, true);
            numSections = 1;
        }

    private:
        void onPrepare() override { if (q == nullptr) gq.snap (spec.qDef); }

        void design() noexcept override
        {
            const double r = getRate(), g = gg.value(), fr = gf.value(), qq = q != nullptr ? gq.value() : spec.qDef;
            switch (spec.shape)
            {
                case Shape::peak:      setCoeffs (0, SvfCoeffs::make (SvfType::bell, r, fr, qq, g)); break;
                case Shape::cut:       setCoeffs (0, SvfCoeffs::make (SvfType::bell, r, fr, qq, -g)); break;
                case Shape::lowShelf:  setCoeffs (0, SvfCoeffs::make (SvfType::lowShelf, r, fr, qq, g)); break;
                case Shape::highShelf: setCoeffs (0, SvfCoeffs::make (SvfType::highShelf, r, fr, qq, g)); break;
            }
        }

        const Spec spec;
        juce::AudioParameterFloat* gain = nullptr;
        juce::AudioParameterFloat* freq = nullptr;
        juce::AudioParameterFloat* q = nullptr;
        Glide gg, gf, gq;
    };

    //==============================================================================
    // Tilt: a low shelf and a high shelf at the same centre, -t/2 and +t/2 (gentle Q 0.5).
    class TiltEq final : public FilterEffect
    {
    public:
        explicit TiltEq (const Info& i) : FilterEffect (i)
        {
            tilt = addFloat ("tilt", "Ton (- sıcak / + parlak)", -6.0f, 6.0f, 0.0f, U::decibel, 0.0f, 0.1f);
            centre = addFloat ("centre", "Merkez", 300.0f, 3000.0f, 800.0f, U::hertz, 950.0f, 1.0f);
            bind (gt, tilt, false);
            bind (gc, centre, true);
            numSections = 2;
        }
    private:
        void design() noexcept override
        {
            const double r = getRate(), t = gt.value(), fc = gc.value();
            setCoeffs (0, SvfCoeffs::make (SvfType::lowShelf, r, fc, 0.5, -0.5 * t));
            setCoeffs (1, SvfCoeffs::make (SvfType::highShelf, r, fc, 0.5, 0.5 * t));
        }
        juce::AudioParameterFloat *tilt, *centre;
        Glide gt, gc;
    };

    //==============================================================================
    // Four bands. A type change first glides the band's gain to 0 dB (where every type is flat),
    // switches, then glides back - no click.
    class ParamEq final : public FilterEffect
    {
    public:
        static constexpr int bands = 4;

        explicit ParamEq (const Info& i) : FilterEffect (i)
        {
            static const float defF[bands] = { 100.0f, 400.0f, 2500.0f, 8000.0f };
            static const int   defT[bands] = { 1, 0, 0, 2 };
            const juce::StringArray types { u8 ("Tepe (Bell)"), u8 ("Alçak raf"), u8 ("Yüksek raf") };
            for (int b = 0; b < bands; ++b)
            {
                auto& B = band[(size_t) b];
                const auto n = juce::String (b + 1);
                B.type = addChoice (("b" + n + "type").toRawUTF8(), (u8 ("Bant ") + n + u8 (" türü")).toRawUTF8(), types, defT[b]);
                B.freq = addFloat (("b" + n + "freq").toRawUTF8(), (u8 ("Bant ") + n + u8 (" frekans")).toRawUTF8(),
                                   20.0f, 20000.0f, defF[b], U::hertz, 1000.0f, 1.0f);
                B.gain = addFloat (("b" + n + "gain").toRawUTF8(), (u8 ("Bant ") + n + u8 (" kazanç")).toRawUTF8(),
                                   -18.0f, 18.0f, 0.0f, U::decibel, 0.0f, 0.1f);
                B.q    = addFloat (("b" + n + "q").toRawUTF8(), (u8 ("Bant ") + n + " Q").toRawUTF8(),
                                   0.1f, 10.0f, 1.0f, U::plain, 1.0f, 0.01f);
                bind (B.gf, B.freq, true);
                bind (B.gq, B.q, true);
            }
            numSections = bands;
        }

    private:
        struct Band
        {
            juce::AudioParameterChoice* type = nullptr;
            juce::AudioParameterFloat *freq = nullptr, *gain = nullptr, *q = nullptr;
            Glide gf, gq, gg;
            int activeType = 0;
        };

        void onPrepare() override
        {
            for (auto& B : band)
            {
                B.gg.prepare (getRate(), glideSeconds, false);
                B.activeType = B.type->getIndex();
                B.gg.snap (value (B.gain));
            }
        }
        void readTargets() noexcept override
        {
            FilterEffect::readTargets();
            for (auto& B : band)
                B.gg.setTarget (B.type->getIndex() != B.activeType ? 0.0 : (double) value (B.gain));
        }
        bool step() noexcept override
        {
            bool changed = false;
            for (auto& B : band)
            {
                changed = B.gg.advance() || changed;
                if (B.type->getIndex() != B.activeType && ! B.gg.moving() && B.gg.value() == 0.0)
                {
                    B.activeType = B.type->getIndex();
                    B.gg.setTarget (value (B.gain));
                    changed = true;
                }
            }
            return changed;
        }
        void design() noexcept override
        {
            const double r = getRate();
            for (int b = 0; b < bands; ++b)
            {
                auto& B = band[(size_t) b];
                const double fr = B.gf.value(), g = B.gg.value(), qq = B.gq.value();
                switch (B.activeType)
                {
                    case 1:  setCoeffs (b, SvfCoeffs::make (SvfType::lowShelf, r, fr, std::min (qq, 2.0), g)); break;
                    case 2:  setCoeffs (b, SvfCoeffs::make (SvfType::highShelf, r, fr, std::min (qq, 2.0), g)); break;
                    default: setCoeffs (b, SvfCoeffs::make (SvfType::bell, r, fr, qq, g)); break;
                }
            }
        }

        std::array<Band, bands> band;
    };

    //==============================================================================
    // Hum: y = x - depth_k * bandpass_k(x) for the mains frequency and its harmonics (a notch is
    // exactly x minus the 0 dB band-pass with the same centre and Q). The depths glide (50 ms), so
    // switching harmonics on/off never clicks; changing 50 <-> 60 Hz first fades all notches out.
    class HumRemover final : public FilterEffect
    {
    public:
        static constexpr int maxHarmonics = 8;

        explicit HumRemover (const Info& i) : FilterEffect (i)
        {
            mains = addChoice ("mains", "Şebeke", { "50 Hz", "60 Hz" }, 0);
            count = addFloat ("harmonics", "Harmonik sayısı", 1.0f, (float) maxHarmonics, 4.0f, U::count, 0.0f, 1.0f);
            qp    = addFloat ("q", "Darlık (Q)", 5.0f, 80.0f, 30.0f, U::plain, 25.0f, 0.1f);
            bind (gq, qp, true);
            numSections = maxHarmonics;
        }

        double responseAt (double hz) const override
        {
            // the cascade of (1 - d_k * BP_k), complex
            const double w = twoPi * hz / getRate();
            double re = 1.0, im = 0.0;
            const double cr1 = std::cos (w), ci1 = -std::sin (w), cr2 = std::cos (2 * w), ci2 = -std::sin (2 * w);
            for (int k = 0; k < maxHarmonics; ++k)
            {
                const auto c = coeffs[(size_t) k].biquad();
                const double nr = c.b0 + c.b1 * cr1 + c.b2 * cr2, ni = c.b1 * ci1 + c.b2 * ci2;
                const double dr = 1.0 + c.a1 * cr1 + c.a2 * cr2, di = c.a1 * ci1 + c.a2 * ci2;
                const double den = dr * dr + di * di;
                const double hr = (nr * dr + ni * di) / den, hi = (ni * dr - nr * di) / den;
                const double d = depth[(size_t) k].getTarget();
                const double fr = 1.0 - d * hr, fi = -d * hi;
                const double nre = re * fr - im * fi, nim = re * fi + im * fr;
                re = nre; im = nim;
            }
            return std::sqrt (re * re + im * im);
        }

    private:
        void onPrepare() override
        {
            activeMains = mains->getIndex();
            for (int k = 0; k < maxHarmonics; ++k)
            {
                depth[(size_t) k].prepare (getRate(), glideSeconds);
                depth[(size_t) k].snap (wanted (k));
            }
        }
        double wanted (int k) const noexcept
        {
            return (mains->getIndex() == activeMains && k < juce::roundToInt (value (count))) ? 1.0 : 0.0;
        }
        void readTargets() noexcept override
        {
            FilterEffect::readTargets();
            for (int k = 0; k < maxHarmonics; ++k) depth[(size_t) k].setTarget (wanted (k));
        }
        bool step() noexcept override
        {
            if (mains->getIndex() == activeMains) return false;
            for (auto& d : depth) if (d.moving() || d.value() != 0.0) return false;
            activeMains = mains->getIndex();   // all notches faded out: switch, then fade in again
            for (auto& ch : state) for (auto& s : ch) s.reset();
            for (int k = 0; k < maxHarmonics; ++k) depth[(size_t) k].setTarget (wanted (k));
            return true;
        }
        void design() noexcept override
        {
            const double base = activeMains == 1 ? 60.0 : 50.0;
            for (int k = 0; k < maxHarmonics; ++k)
                setCoeffs (k, SvfCoeffs::make (SvfType::bandPass, getRate(), base * (k + 1), gq.value()));
        }
        void frame() noexcept override { for (int k = 0; k < maxHarmonics; ++k) dNow[(size_t) k] = depth[(size_t) k].next(); }
        double tick (int c, double x) noexcept override
        {
            auto& st = state[(size_t) c];
            double y = x;
            for (int k = 0; k < maxHarmonics; ++k)
                y -= dNow[(size_t) k] * st[(size_t) k].process (y);
            return y;
        }

        juce::AudioParameterChoice* mains;
        juce::AudioParameterFloat *count, *qp;
        Glide gq;
        std::array<Ramp, maxHarmonics> depth;
        std::array<double, maxHarmonics> dNow {};
        int activeMains = 0;
    };

    //==============================================================================
    // DC remover: first-order high-pass at 5 Hz (no parameters).
    class DcBlocker final : public BuiltInEffect
    {
    public:
        using BuiltInEffect::BuiltInEffect;
    private:
        void prepareEffect() override { for (auto& f : hp) f.k = BiquadCoeffs::highPass1 (getRate(), 5.0); }
        void resetEffect() override { for (auto& f : hp) f.reset(); }
        void processEffect (float* const* ch, int numCh, int n) noexcept override
        {
            for (int c = 0; c < numCh; ++c)
            {
                auto& f = hp[(size_t) c];
                for (int i = 0; i < n; ++i) ch[c][i] = (float) f.process (ch[c][i]);
                f.flush();
            }
        }
        std::array<Biquad, 2> hp;
    };
}

std::unique_ptr<BuiltInEffect> createLowCut  (const BuiltInEffect::Info& i) { return std::make_unique<CutFilter> (i, true); }
std::unique_ptr<BuiltInEffect> createHighCut (const BuiltInEffect::Info& i) { return std::make_unique<CutFilter> (i, false); }

std::unique_ptr<BuiltInEffect> createBody (const BuiltInEffect::Info& i)
{
    return std::make_unique<BandEffect> (i, BandEffect::Spec { BandEffect::Shape::peak, "Miktar", -6.0f, 8.0f, 2.0f,
                                                               100.0f, 300.0f, 180.0f, 170.0f, 0.8f, 0.8f, 0.8f });
}
std::unique_ptr<BuiltInEffect> createMud (const BuiltInEffect::Info& i)
{
    return std::make_unique<BandEffect> (i, BandEffect::Spec { BandEffect::Shape::cut, "Azaltma", 0.0f, 12.0f, 3.0f,
                                                               200.0f, 800.0f, 400.0f, 400.0f, 0.5f, 4.0f, 1.2f });
}
std::unique_ptr<BuiltInEffect> createNasal (const BuiltInEffect::Info& i)
{
    return std::make_unique<BandEffect> (i, BandEffect::Spec { BandEffect::Shape::cut, "Azaltma", 0.0f, 12.0f, 3.0f,
                                                               600.0f, 2000.0f, 1000.0f, 1100.0f, 0.5f, 4.0f, 1.4f });
}
std::unique_ptr<BuiltInEffect> createPresence (const BuiltInEffect::Info& i)
{
    return std::make_unique<BandEffect> (i, BandEffect::Spec { BandEffect::Shape::peak, "Kazanç", -6.0f, 8.0f, 2.5f,
                                                               2000.0f, 6000.0f, 4000.0f, 3500.0f, 0.5f, 3.0f, 0.9f });
}
std::unique_ptr<BuiltInEffect> createAir (const BuiltInEffect::Info& i)
{
    return std::make_unique<BandEffect> (i, BandEffect::Spec { BandEffect::Shape::highShelf, "Kazanç", -6.0f, 8.0f, 2.5f,
                                                               6000.0f, 16000.0f, 10000.0f, 10000.0f, 0.7f, 0.7f, 0.7f });
}
std::unique_ptr<BuiltInEffect> createProximity (const BuiltInEffect::Info& i)
{
    return std::make_unique<BandEffect> (i, BandEffect::Spec { BandEffect::Shape::lowShelf, "Miktar", -12.0f, 6.0f, -3.0f,
                                                               80.0f, 300.0f, 150.0f, 160.0f, 0.7f, 0.7f, 0.7f });
}
std::unique_ptr<BuiltInEffect> createTilt     (const BuiltInEffect::Info& i) { return std::make_unique<TiltEq> (i); }
std::unique_ptr<BuiltInEffect> createParamEq  (const BuiltInEffect::Info& i) { return std::make_unique<ParamEq> (i); }
std::unique_ptr<BuiltInEffect> createHum      (const BuiltInEffect::Info& i) { return std::make_unique<HumRemover> (i); }
std::unique_ptr<BuiltInEffect> createDcBlock  (const BuiltInEffect::Info& i) { return std::make_unique<DcBlocker> (i); }
}
