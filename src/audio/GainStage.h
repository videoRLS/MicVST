#pragma once
#include <atomic>
#include <cmath>

// The DSP core of MicVST's built-in "Kazanç" (gain) element - the counterpart of OBS Studio's Gain
// filter: one gain in dB, applied equally to every channel. Free of JUCE so the unit tests can
// drive it directly; BuiltInGain.h wraps it into an AudioProcessor for the plug-in chain.
//
//  * Range -30 ... +12 dB. +12 dB is the upper limit by design; the UI moves in 0.1 dB steps.
//  * Thread safety: setGainDb() only stores the new target in an atomic (any thread, lock-free,
//    no allocation). The plug-in thread picks it up at the start of its next block. Nothing here
//    ever blocks or allocates.
//  * No clicks, no zipper noise: the gain glides to a new target through two cascaded one-pole
//    smoothers in dB (critically damped, time constant 6 ms each: 99 % of a change after ~40 ms).
//    Neither the gain curve nor its slope ever jumps - also not when the target changes again in
//    the middle of a glide, as it does many times per second while the slider is dragged. (A
//    straight ramp would put a corner into the gain curve at every new slider value.)
//  * Transparent: at 0 dB, once settled, a block is not touched at all (bit-exact passthrough).
//    Otherwise one float multiplication per sample; the glide runs in double precision and lands
//    exactly on the target (the last step is below 0.00001 dB).
//  * Headroom: MicVST's whole path is 32-bit float, so +12 dB on a full-scale signal (peak 4.0 =
//    +12 dBFS) does not clip inside MicVST. It clips where the stream becomes integer PCM (the
//    cable's 24-bit format, converted by the Windows audio engine) - hence the advice to put the
//    gain in front of a limiter/compressor, and the red peak mark of the output meter at 0 dBFS.
class GainStage
{
public:
    static constexpr float  minDb = -30.0f;
    static constexpr float  maxDb = 12.0f;
    static constexpr float  defaultDb = 0.0f;
    static constexpr double smoothingSeconds = 0.006;   // time constant of each smoother stage
    static constexpr double settleDb = 1.0e-5;          // closer than this to the target: done

    // NaN/infinite -> 0 dB; otherwise limited to [minDb, maxDb].
    static float clampDb (float db) noexcept
    {
        if (! std::isfinite (db)) return defaultDb;
        return db < minDb ? minDb : (db > maxDb ? maxDb : db);
    }

    static double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }

    //==============================================================================
    // Any thread. immediate = jump to the new value at the next block instead of gliding - only for
    // restoring a saved chain (the chain is muted then anyway), never for live changes.
    void setGainDb (float db, bool immediate = false) noexcept
    {
        targetDb.store (clampDb (db), std::memory_order_relaxed);
        if (immediate)
            snapRequested.store (true, std::memory_order_release);
    }

    float getGainDb() const noexcept { return targetDb.load (std::memory_order_relaxed); }

    //==============================================================================
    // While the plug-in thread does not process this stage (prepareToPlay, or a reset of a suspended
    // node): new sample rate, and the gain goes straight to the target.
    void prepare (double sampleRate) noexcept
    {
        const double rate = sampleRate > 0.0 ? sampleRate : 48000.0;
        coeff = 1.0 - std::exp (-1.0 / (smoothingSeconds * rate));
        reset();
    }

    void reset() noexcept
    {
        settledDb = stage1 = stage2 = (double) targetDb.load (std::memory_order_relaxed);
        gain = dbToGain (settledDb);
        settled = true;
    }

    //==============================================================================
    // Plug-in thread: applies the gain in place to numChannels non-interleaved channels.
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (snapRequested.exchange (false, std::memory_order_acquire))
            reset();

        const double wanted = (double) targetDb.load (std::memory_order_relaxed);
        if (settled && wanted != settledDb)
            settled = false;

        if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
            return;

        int i = 0;
        // gliding: the gain changes every sample (same value for all channels of a sample frame)
        for (; i < numSamples && ! settled; ++i)
        {
            stage1 += coeff * (wanted - stage1);
            stage2 += coeff * (stage1 - stage2);
            if (std::abs (wanted - stage1) < settleDb && std::abs (wanted - stage2) < settleDb)
            {
                settledDb = stage1 = stage2 = wanted;
                gain = dbToGain (wanted);
                settled = true;
            }
            else
            {
                gain = std::exp (stage2 * dbToLog);
            }

            const float g = (float) gain;
            for (int c = 0; c < numChannels; ++c)
                if (channels[c] != nullptr)
                    channels[c][i] *= g;
        }

        if (i >= numSamples || gain == 1.0)   // 0 dB and settled: untouched (bit-exact)
            return;

        const float g = (float) gain;
        for (int c = 0; c < numChannels; ++c)
            if (float* x = channels[c])
                for (int k = i; k < numSamples; ++k)
                    x[k] *= g;
    }

    bool   isGliding() const noexcept      { return ! settled; }
    double getCurrentGain() const noexcept { return gain; }   // plug-in thread / tests

private:
    static constexpr double dbToLog = 0.11512925464970228;   // ln(10) / 20

    std::atomic<float> targetDb { defaultDb };
    std::atomic<bool>  snapRequested { false };

    // plug-in thread only (and prepare/reset while it does not process this stage)
    double coeff = 1.0 - 0.99653379;           // 6 ms at 48 kHz until prepare() is called
    double stage1 = 0.0, stage2 = 0.0;         // smoother states in dB
    double settledDb = 0.0;                    // the value the gain rests at (when settled)
    double gain = 1.0;                         // current linear gain
    bool   settled = true;
};
