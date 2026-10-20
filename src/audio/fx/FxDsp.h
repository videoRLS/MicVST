#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

// Small DSP building blocks for MicVST's built-in effects (BuiltInEffects.h). Free of JUCE, no
// allocation, no locks: everything here may run on the plug-in thread.
namespace fx
{
    constexpr double pi = 3.141592653589793;
    constexpr double twoPi = 6.283185307179586;

    inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }
    inline double gainToDb (double g) noexcept  { return 20.0 * std::log10 (std::max (1.0e-12, g)); }

    // Highest usable filter frequency at a rate (filters above it are clamped, never unstable).
    inline double maxFreq (double sampleRate) noexcept { return 0.45 * sampleRate; }

    //==============================================================================
    // Second-order section (transposed direct form II, double precision). The coefficients can be
    // changed between samples: the effects glide them in small steps, so no step ever clicks.
    struct BiquadCoeffs
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;

        static BiquadCoeffs normalised (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
        {
            const double k = 1.0 / a0;
            return { b0 * k, b1 * k, b2 * k, a1 * k, a2 * k };
        }

        // Robert Bristow-Johnson's "Audio EQ Cookbook" designs.
        static BiquadCoeffs lowPass (double rate, double f, double q) noexcept
        {
            const double w = twoPi * clampF (rate, f) / rate, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            return normalised ((1.0 - c) * 0.5, 1.0 - c, (1.0 - c) * 0.5, 1.0 + a, -2.0 * c, 1.0 - a);
        }
        static BiquadCoeffs highPass (double rate, double f, double q) noexcept
        {
            const double w = twoPi * clampF (rate, f) / rate, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            return normalised ((1.0 + c) * 0.5, -(1.0 + c), (1.0 + c) * 0.5, 1.0 + a, -2.0 * c, 1.0 - a);
        }
        static BiquadCoeffs bandPass (double rate, double f, double q) noexcept   // 0 dB peak gain
        {
            const double w = twoPi * clampF (rate, f) / rate, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            return normalised (a, 0.0, -a, 1.0 + a, -2.0 * c, 1.0 - a);
        }
        static BiquadCoeffs notch (double rate, double f, double q) noexcept
        {
            const double w = twoPi * clampF (rate, f) / rate, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            return normalised (1.0, -2.0 * c, 1.0, 1.0 + a, -2.0 * c, 1.0 - a);
        }
        static BiquadCoeffs peak (double rate, double f, double q, double gainDb) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = twoPi * clampF (rate, f) / rate, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            return normalised (1.0 + a * A, -2.0 * c, 1.0 - a * A, 1.0 + a / A, -2.0 * c, 1.0 - a / A);
        }
        static BiquadCoeffs lowShelf (double rate, double f, double q, double gainDb) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
            const double w = twoPi * clampF (rate, f) / rate, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            return normalised (A * ((A + 1.0) - (A - 1.0) * c + 2.0 * sA * a),
                               2.0 * A * ((A - 1.0) - (A + 1.0) * c),
                               A * ((A + 1.0) - (A - 1.0) * c - 2.0 * sA * a),
                               (A + 1.0) + (A - 1.0) * c + 2.0 * sA * a,
                               -2.0 * ((A - 1.0) + (A + 1.0) * c),
                               (A + 1.0) + (A - 1.0) * c - 2.0 * sA * a);
        }
        static BiquadCoeffs highShelf (double rate, double f, double q, double gainDb) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0), sA = std::sqrt (A);
            const double w = twoPi * clampF (rate, f) / rate, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            return normalised (A * ((A + 1.0) + (A - 1.0) * c + 2.0 * sA * a),
                               -2.0 * A * ((A - 1.0) + (A + 1.0) * c),
                               A * ((A + 1.0) + (A - 1.0) * c - 2.0 * sA * a),
                               (A + 1.0) - (A - 1.0) * c + 2.0 * sA * a,
                               2.0 * ((A - 1.0) - (A + 1.0) * c),
                               (A + 1.0) - (A - 1.0) * c - 2.0 * sA * a);
        }
        // First-order sections (b2 = a2 = 0), bilinear transform with pre-warping.
        static BiquadCoeffs highPass1 (double rate, double f) noexcept
        {
            const double k = std::tan (pi * clampF (rate, f) / rate);
            return normalised (1.0, -1.0, 0.0, 1.0 + k, k - 1.0, 0.0);
        }
        static BiquadCoeffs lowPass1 (double rate, double f) noexcept
        {
            const double k = std::tan (pi * clampF (rate, f) / rate);
            return normalised (k, k, 0.0, 1.0 + k, k - 1.0, 0.0);
        }

        // Magnitude response at frequency f (tests, displays).
        double magnitude (double rate, double f) const noexcept
        {
            const double w = twoPi * f / rate;
            const double cr1 = std::cos (w), ci1 = -std::sin (w), cr2 = std::cos (2.0 * w), ci2 = -std::sin (2.0 * w);
            const double nr = b0 + b1 * cr1 + b2 * cr2, ni = b1 * ci1 + b2 * ci2;
            const double dr = 1.0 + a1 * cr1 + a2 * cr2, di = a1 * ci1 + a2 * ci2;
            return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
        }

        static double clampF (double rate, double f) noexcept { return std::clamp (f, 1.0, maxFreq (rate)); }
    };

    struct Biquad
    {
        BiquadCoeffs k;
        double s1 = 0.0, s2 = 0.0;

        void reset() noexcept { s1 = s2 = 0.0; }
        double process (double x) noexcept
        {
            const double y = k.b0 * x + s1;
            s1 = k.b1 * x - k.a1 * y + s2;
            s2 = k.b2 * x - k.a2 * y;
            return y;
        }
        // Denormals: the states are flushed to zero once they are far below audibility.
        void flush() noexcept
        {
            if (std::abs (s1) < 1.0e-25) s1 = 0.0;
            if (std::abs (s2) < 1.0e-25) s2 = 0.0;
        }
    };

    //==============================================================================
    // Andrew Simper's linear trapezoidal state-variable filter (Cytomic, "SvfLinearTrapOptimised2"):
    // the same responses as the cookbook biquads (bilinear transform of the same analog prototypes,
    // pre-warped at f0), but its states stay well-behaved when the coefficients change every sample -
    // the glides of the EQ effects are computed per sample on it, without zipper noise.
    enum class SvfType { lowPass, highPass, bandPass, notch, bell, lowShelf, highShelf };

    struct SvfCoeffs
    {
        double a1 = 1.0, a2 = 0.0, a3 = 0.0, m0 = 1.0, m1 = 0.0, m2 = 0.0;
        SvfType type = SvfType::bell;
        double f = 1000.0, q = 0.707, gainDb = 0.0, rate = 48000.0;

        static SvfCoeffs make (SvfType t, double rate, double f, double q, double gainDb = 0.0) noexcept
        {
            SvfCoeffs c;
            c.type = t; c.f = f; c.q = q; c.gainDb = gainDb; c.rate = rate;
            const double w = std::tan (pi * BiquadCoeffs::clampF (rate, f) / rate);
            double g = w, k = 1.0 / q;
            const double A = std::pow (10.0, gainDb / 40.0);
            switch (t)
            {
                case SvfType::lowPass:   c.m0 = 0.0; c.m1 = 0.0; c.m2 = 1.0; break;
                case SvfType::highPass:  c.m0 = 1.0; c.m1 = -k; c.m2 = -1.0; break;
                case SvfType::bandPass:  c.m0 = 0.0; c.m1 = k; c.m2 = 0.0; break;   // 0 dB at f0
                case SvfType::notch:     c.m0 = 1.0; c.m1 = -k; c.m2 = 0.0; break;
                case SvfType::bell:      k = 1.0 / (q * A); c.m0 = 1.0; c.m1 = k * (A * A - 1.0); c.m2 = 0.0; break;
                case SvfType::lowShelf:  g = w / std::sqrt (A); c.m0 = 1.0; c.m1 = k * (A - 1.0); c.m2 = A * A - 1.0; break;
                case SvfType::highShelf: g = w * std::sqrt (A); c.m0 = A * A; c.m1 = k * (1.0 - A) * A; c.m2 = 1.0 - A * A; break;
            }
            c.a1 = 1.0 / (1.0 + g * (g + k));
            c.a2 = g * c.a1;
            c.a3 = g * c.a2;
            return c;
        }

        // The equivalent cookbook biquad (analytic responses for tests and displays).
        BiquadCoeffs biquad() const noexcept
        {
            switch (type)
            {
                case SvfType::lowPass:   return BiquadCoeffs::lowPass (rate, f, q);
                case SvfType::highPass:  return BiquadCoeffs::highPass (rate, f, q);
                case SvfType::bandPass:  return BiquadCoeffs::bandPass (rate, f, q);
                case SvfType::notch:     return BiquadCoeffs::notch (rate, f, q);
                case SvfType::bell:      return BiquadCoeffs::peak (rate, f, q, gainDb);
                case SvfType::lowShelf:  return BiquadCoeffs::lowShelf (rate, f, q, gainDb);
                case SvfType::highShelf: return BiquadCoeffs::highShelf (rate, f, q, gainDb);
            }
            return {};
        }
    };

    struct Svf
    {
        SvfCoeffs k;
        double ic1 = 0.0, ic2 = 0.0;

        void reset() noexcept { ic1 = ic2 = 0.0; }
        double process (double v0) noexcept
        {
            const double v3 = v0 - ic2;
            const double v1 = k.a1 * ic1 + k.a2 * v3;
            const double v2 = ic2 + k.a2 * ic1 + k.a3 * v3;
            ic1 = 2.0 * v1 - ic1;
            ic2 = 2.0 * v2 - ic2;
            return k.m0 * v0 + k.m1 * v1 + k.m2 * v2;
        }
        void flush() noexcept
        {
            if (std::abs (ic1) < 1.0e-25) ic1 = 0.0;
            if (std::abs (ic2) < 1.0e-25) ic2 = 0.0;
        }
    };

    // Butterworth Q values of the second-order sections of an order-2 / order-4 filter.
    constexpr double butterQ2 = 0.7071067811865476;
    constexpr double butterQ4a = 0.5411961001461970, butterQ4b = 1.3065629648763766;

    //==============================================================================
    // A parameter value on its way to its target: linear (dB, %) or exponential (Hz) steps, one per
    // sample, over a fixed time. While it moves, the effects recompute their coefficients every sample.
    class Glide
    {
    public:
        void prepare (double rate, double seconds, bool exponential) noexcept
        {
            steps = std::max (1, (int) std::lround (seconds * rate));
            expo = exponential;
            left = 0;
        }
        void snap (double v) noexcept { cur = target = v; left = 0; }
        void setTarget (double v) noexcept
        {
            if (v == target) return;
            target = v;
            left = steps;
            if (expo && cur > 0.0 && v > 0.0) step = std::pow (v / cur, 1.0 / steps);
            else if (expo) { cur = v; left = 0; }
            else step = (v - cur) / steps;
        }
        // Advances by one sample; returns true while the value changed.
        bool advance() noexcept
        {
            if (left <= 0) return false;
            if (--left == 0) cur = target;
            else cur = expo ? cur * step : cur + step;
            return true;
        }
        bool moving() const noexcept { return left > 0; }
        double value() const noexcept { return cur; }
        double getTarget() const noexcept { return target; }

        static constexpr int subBlock = 16;   // (for effects that check their parameters per 16 samples)

    private:
        double cur = 0.0, target = 0.0, step = 0.0;
        int steps = 1, left = 0;
        bool expo = false;
    };

    // Per-sample gain ramp for mixes, mutes and cross-fades between two signals: a raised-cosine
    // move from the value it has to the new target (no corner at either end, so even a full
    // sign flip through zero leaves no click). A new target in the middle of a move starts a new
    // move from the current value.
    class Ramp
    {
    public:
        void prepare (double rate, double seconds) noexcept { len = std::max (1, (int) std::lround (seconds * rate)); pos = len; }
        void snap (double v) noexcept { cur = start = target = v; pos = len; }
        void setTarget (double v) noexcept
        {
            if (v == target) return;
            start = cur; target = v; pos = 0;
        }
        double next() noexcept
        {
            if (pos < len)
            {
                ++pos;
                cur = pos >= len ? target : start + (target - start) * (0.5 - 0.5 * std::cos (pi * pos / len));
            }
            return cur;
        }
        bool moving() const noexcept { return pos < len; }
        double value() const noexcept { return cur; }
        double getTarget() const noexcept { return target; }
    private:
        double cur = 0.0, start = 0.0, target = 0.0;
        int len = 1, pos = 1;
    };

    // Fade-in of an effect's input into its delay lines after a reset: it starts with the first
    // non-silent sample (a new element runs on silence until the chain is wired to it - a fade that
    // started then would be over before the voice arrives, and the first repeat would begin with an
    // edge), then rises over 10 ms.
    class OnsetFade
    {
    public:
        void reset (double rate) noexcept { ramp.prepare (rate, 0.01); ramp.snap (0.0); armed = true; }
        double next (double input) noexcept
        {
            if (armed)
            {
                if (input == 0.0) return 0.0;
                armed = false;
                ramp.setTarget (1.0);
            }
            return ramp.next();
        }
    private:
        Ramp ramp;
        bool armed = false;
    };

    // Raised-cosine shape of a 0..1 fade position (no corner at either end).
    inline double smoothStep (double t) noexcept { t = std::clamp (t, 0.0, 1.0); return 0.5 - 0.5 * std::cos (pi * t); }

    //==============================================================================
    // Inter-sample peak detection: windowed-sinc interpolation at the points 1/8 ... 7/8 between the
    // 6th and 7th of the last 12 samples (8x oversampling) - the same detector as Otomatik Kazanç's
    // limiter (AutoGain.h). Its reading lags the input by 6 samples.
    class TruePeakDetector
    {
    public:
        static constexpr int taps = 12, phases = 7, delay = 6;

        TruePeakDetector() noexcept
        {
            for (int ph = 0; ph < phases; ++ph)
            {
                const double at = 5.0 + (ph + 1) / (double) (phases + 1);
                double sum = 0.0;
                for (int k = 0; k < taps; ++k)
                {
                    const double t = (double) k - at;
                    const double sinc = std::abs (t) < 1.0e-12 ? 1.0 : std::sin (pi * t) / (pi * t);
                    const double w = 0.5 + 0.5 * std::cos (pi * t / 6.5);
                    coeffs[(size_t) (ph * taps + k)] = sinc * w;
                    sum += sinc * w;
                }
                for (int k = 0; k < taps; ++k) coeffs[(size_t) (ph * taps + k)] /= sum;
            }
        }

        void reset() noexcept { history.fill (0.0f); pos = 0; }

        // Pushes one sample; returns the largest magnitude between (and at) the samples 6 and 7 back.
        double push (float x) noexcept
        {
            history[(size_t) pos] = std::isfinite (x) ? x : 0.0f;
            pos = pos + 1 < taps ? pos + 1 : 0;   // now the oldest
            double m = std::abs ((double) history[(size_t) ((pos + 6) % taps)]);
            for (int ph = 0; ph < phases; ++ph)
            {
                const double* k = coeffs.data() + ph * taps;
                double y = 0.0;
                for (int j = 0, idx = pos; j < taps; ++j, idx = idx + 1 < taps ? idx + 1 : 0)
                    y += k[j] * (double) history[(size_t) idx];
                m = std::max (m, std::abs (y));
            }
            return m;
        }

    private:
        std::array<double, (size_t) (phases * taps)> coeffs {};
        std::array<float, (size_t) taps> history {};
        int pos = 0;
    };

    //==============================================================================
    // Lock-free "largest value since the last read" for meters (any thread reads, one writes).
    inline void storeMax (std::atomic<float>& a, float v) noexcept
    {
        float cur = a.load (std::memory_order_relaxed);
        while (v > cur && ! a.compare_exchange_weak (cur, v, std::memory_order_relaxed)) {}
    }
}
