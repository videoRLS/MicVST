#pragma once
#include <cmath>
#include <cstdint>

// Splices in a running audio stream (MicVST 1.2.1).
//
// Two places skip audio on purpose: the bridge trims excess latency after the output device
// stalled, and the input queue drops the oldest audio when the plug-in chain is slower than real
// time. Both crossfade from the old position into a later one. Crossfading two arbitrary points of
// a tone or a voiced sound mixes two waveforms with unrelated phase: the level dips or swells
// during the fade (up to a short silence when they are in antiphase) - a faint "blip".
//
// bestOffset() therefore moves the splice point by a few milliseconds to where the new audio
// matches the old one best (normalised cross-correlation of the next 'len' frames, the
// WSOLA/PSOLA technique of time-stretching). For a periodic signal the two waveforms are then in
// phase and an equal-gain crossfade joins them without any level change; for noise-like signals
// (low correlation) an equal-power crossfade keeps the loudness constant.
namespace splice
{
    // Equal-gain (correlated) or equal-power (uncorrelated) crossfade gains for x in (0, 1).
    inline void gains (double x, bool equalGain, float& gOld, float& gNew) noexcept
    {
        constexpr double pi = 3.14159265358979323846;
        if (equalGain)
        {
            gNew = (float) (0.5 - 0.5 * std::cos (pi * x));   // raised cosine, gOld + gNew = 1
            gOld = 1.0f - gNew;
        }
        else
        {
            gNew = (float) std::sin (0.5 * pi * x);           // gOld^2 + gNew^2 = 1
            gOld = (float) std::cos (0.5 * pi * x);
        }
    }

    // Correlation from which an equal-gain crossfade is used.
    constexpr double equalGainFrom = 0.5;

    // ref[0 .. len) is the audio that would play next, cand[k .. k + len) the candidate to splice to
    // (k = 0 .. maxK). Returns the best k; rho receives its normalised correlation (-1 .. 1).
    // Silence or a poor best match (rho < 0.3) returns preferredK, with rho measured there.
    inline int bestOffset (const float* ref, const float* cand, int maxK, int len, int preferredK, double& rho) noexcept
    {
        rho = 0.0;
        if (maxK < 0 || len <= 0)
            return 0;
        const int preferred = preferredK < 0 ? 0 : (preferredK > maxK ? maxK : preferredK);

        auto correlation = [&] (int k, int stride)
        {
            double c = 0.0, eRef = 0.0, eCand = 0.0;
            for (int i = 0; i < len; i += stride)
            {
                const double a = ref[i], b = cand[k + i];
                c += a * b; eRef += a * a; eCand += b * b;
            }
            return eRef > 1.0e-12 * len && eCand > 1.0e-12 * len ? c / std::sqrt (eRef * eCand) : 0.0;
        };

        double energy = 0.0;
        for (int i = 0; i < len; ++i)
            energy += (double) ref[i] * ref[i];
        if (energy <= 1.0e-10 * len)
            return preferred;   // silence: nothing to align

        // every lag, correlation over every second frame; refined at full resolution around the best.
        // A tiny penalty for the distance to preferredK decides between equally good periods.
        int best = 0;
        double bestScore = -2.0;
        for (int k = 0; k <= maxK; ++k)
        {
            const double s = correlation (k, 2) - 5.0e-4 * std::abs (k - preferred) / (double) (maxK + 1);
            if (s > bestScore) { bestScore = s; best = k; }
        }
        const int coarse = best;
        bestScore = -2.0;
        for (int k = coarse - 1; k <= coarse + 1; ++k)
        {
            if (k < 0 || k > maxK) continue;
            const double s = correlation (k, 1);
            if (s > bestScore) { bestScore = s; best = k; }
        }

        if (bestScore < 0.3)
        {
            rho = correlation (preferred, 1);
            return preferred;
        }

        rho = bestScore;
        return best;
    }
}
