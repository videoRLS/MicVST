#pragma once
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

// Sample-rate conversion for recordings (writer thread only, never the audio thread): exact
// rational ratio (48000 -> 44100 = 147/160), Kaiser-windowed sinc evaluated per output sample
// from a polyphase table, double precision.
//  * pass band flat to 0.4535 x the lower rate (20.0 kHz at 44.1 kHz, 21.8 kHz at 48 kHz),
//    ripple < 0.001 dB
//  * stop band from 0.5 x the lower rate: >= 140 dB down (no audible aliasing / imaging)
//  * no time shift: output sample n is the input at exactly n * inRate / outRate, so the
//    recording stays in sync; length = ceil (inFrames * outRate / inRate)
// Same rate: passes samples through untouched (bit-exact).
class Resampler
{
public:
    void prepare (int inRate, int outRate, int numChannels)
    {
        const int g = std::gcd (inRate, outRate);
        L = outRate / g;            // output step in "phases"
        M = inRate / g;
        channels = numChannels;
        passThrough = inRate == outRate;
        consumedOut = 0; totalIn = 0;
        hist.assign ((size_t) channels, {});
        if (passThrough) return;

        const double lo = std::min (inRate, outRate);
        const double pass = 0.4535 * lo, stop = 0.5 * lo;
        const double atten = 140.0;
        const double beta = 0.1102 * (atten - 8.7);
        const double dw = 2.0 * pi * (stop - pass) / inRate;          // transition, input rad/sample
        K = (int) std::ceil ((atten - 7.95) / (2.285 * dw));
        K += K & 1;                                                   // even: K/2 taps each side
        const double fc = 0.5 * (pass + stop) / inRate;               // cutoff, cycles per input sample
        const double half = K / 2.0;
        table.assign ((size_t) L * (size_t) K, 0.0);
        for (int p = 0; p < L; ++p)
        {
            double sum = 0.0;
            double* row = table.data() + (size_t) p * (size_t) K;
            for (int i = 0; i < K; ++i)
            {
                const double tau = (double) p / L + half - 1.0 - i;   // distance from tap to output time
                const double x = 2.0 * fc * tau;
                const double sinc = x == 0.0 ? 1.0 : std::sin (pi * x) / (pi * x);
                const double r = tau / half;
                const double w = std::abs (r) >= 1.0 ? 0.0 : bessel0 (beta * std::sqrt (1.0 - r * r)) / bessel0 (beta);
                row[i] = 2.0 * fc * sinc * w;
                sum += row[i];
            }
            for (int i = 0; i < K; ++i) row[i] /= sum;               // every phase: DC gain exactly 1
        }
        // K/2 zeros before the first sample (inputs at negative times)
        for (auto& h : hist) h.assign ((size_t) (K / 2), 0.0f);
        histStart = -(int64_t) (K / 2);
    }

    bool isPassThrough() const noexcept { return passThrough; }
    int taps() const noexcept { return K; }

    // Converts numFrames input frames; appends the outputs that are complete to 'out' (one vector
    // per channel). finish() then appends the rest (the input followed by silence).
    void process (const float* const* in, int numFrames, std::vector<std::vector<float>>& out)
    {
        out.resize ((size_t) channels);
        if (passThrough)
        {
            for (int c = 0; c < channels; ++c) out[(size_t) c].insert (out[(size_t) c].end(), in[c], in[c] + numFrames);
            totalIn += numFrames;
            consumedOut += numFrames;
            return;
        }
        for (int c = 0; c < channels; ++c) hist[(size_t) c].insert (hist[(size_t) c].end(), in[c], in[c] + numFrames);
        totalIn += numFrames;
        run (totalIn, out);
    }

    void finish (std::vector<std::vector<float>>& out)
    {
        out.resize ((size_t) channels);
        if (passThrough) return;
        for (auto& h : hist) h.insert (h.end(), (size_t) K, 0.0f);   // the tail: silence after the end
        run (totalIn + K, out, outputsFor (totalIn));
    }

    // outputs a whole recording of inFrames gives
    int64_t outputsFor (int64_t inFrames) const noexcept
    {
        return passThrough ? inFrames : (inFrames * L + M - 1) / M;
    }

private:
    void run (int64_t available, std::vector<std::vector<float>>& out, int64_t limit = INT64_MAX)
    {
        // output n needs inputs up to floor (n*M/L) + K/2
        for (;; ++consumedOut)
        {
            if (consumedOut >= limit) break;
            const int64_t num = consumedOut * M;
            const int64_t base = num / L;
            if (base + K / 2 >= available) break;
            const int phase = (int) (num % L);
            const double* row = table.data() + (size_t) phase * (size_t) K;
            const size_t first = (size_t) (base - K / 2 + 1 - histStart);
            for (int c = 0; c < channels; ++c)
            {
                const float* x = hist[(size_t) c].data() + first;
                double acc = 0.0;
                for (int i = 0; i < K; ++i) acc += row[i] * x[i];
                out[(size_t) c].push_back ((float) acc);
            }
        }
        // drop inputs no future output needs
        const int64_t needFrom = (consumedOut * M) / L - K / 2 + 1;
        const int64_t drop = needFrom - histStart;
        if (drop > 65536)
        {
            for (auto& h : hist) h.erase (h.begin(), h.begin() + (std::ptrdiff_t) drop);
            histStart += drop;
        }
    }

    static double bessel0 (double x)
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 200; ++k)
        {
            term *= (x / (2.0 * k)) * (x / (2.0 * k));
            sum += term;
            if (term < sum * 1e-17) break;
        }
        return sum;
    }

    static constexpr double pi = 3.14159265358979323846;
    int L = 1, M = 1, K = 0, channels = 2;
    bool passThrough = true;
    std::vector<double> table;
    std::vector<std::vector<float>> hist;
    int64_t histStart = 0, totalIn = 0, consumedOut = 0;
};
