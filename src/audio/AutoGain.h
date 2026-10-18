#pragma once
#include <algorithm>
#include <atomic>
#include <cmath>
#include "audio/GainStage.h"

// The DSP core of MicVST's built-in "Otomatik Kazanç" (Auto Gain): a slow level rider for the
// voice. It measures the real input block by block and sets ONE gain for all channels so that
// speech peaks land near -6 dBFS. No compressor, limiter, gate or look-ahead: the only thing that
// changes the audio is that gain, applied through GainStage (smooth glide, no clicks).
//
//  * Range: at most +12 dB boost (only as much as needed), down to -12 dB for loud input.
//  * Level estimate: the loudest block peak, held for 1 s of speech, then released at 2 dB/s, so
//    the gain follows phrases, not syllables - the voice keeps its natural dynamics.
//  * Gain moves: up at most 4 dB/s (only while someone speaks); down 3 dB/s, or quickly
//    (time constant 30 ms) when the output would be more than 4 dB above the target (shouting).
//  * Silence: a block counts as speech when its RMS is above -50 dBFS and 10 dB above the noise
//    floor (tracked continuously). Otherwise - after a 300 ms hangover - the gain is held, so
//    room noise between phrases never makes the gain climb.
//  * Off: the auto-gain contribution returns to 0 dB at 12 dB/s; at 0 dB the block is not touched
//    (bit-exact passthrough).
//  * Realtime: no allocation, no locks; on/off and the meters cross threads through atomics.
class AutoGainStage
{
public:
    static constexpr double targetPeakDb       = -6.0;
    static constexpr double maxBoostDb         = 12.0;
    static constexpr double maxCutDb           = -12.0;
    static constexpr double activityFloorDb    = -50.0;   // block RMS below this is never speech
    static constexpr double activityMarginDb   = 10.0;    // speech must be this far above the noise floor
    static constexpr double hangoverSeconds    = 0.3;     // pauses between words still count as speech
    static constexpr double peakHoldSeconds    = 1.0;     // the level estimate holds the loudest peak this long ...
    static constexpr double levelDecayDbPerSec = 2.0;     // ... then releases this fast (both: speech time only)
    static constexpr double riseDbPerSec       = 4.0;
    static constexpr double fallDbPerSec       = 3.0;
    static constexpr double fastZoneDb         = 4.0;     // more than this above the target: fast reduction
    static constexpr double fastFallSeconds    = 0.03;
    static constexpr double offReturnDbPerSec  = 12.0;
    static constexpr double floorRiseDbPerSec  = 3.0;     // noise floor follows a louder room this fast
    static constexpr double floorFallSeconds   = 0.25;    // ... and a quieter one with this time constant

    //==============================================================================
    void setEnabled (bool shouldBeOn) noexcept { enabled.store (shouldBeOn, std::memory_order_relaxed); }
    bool isEnabled() const noexcept            { return enabled.load (std::memory_order_relaxed); }

    // While the plug-in thread does not process this stage: new rate; the gain starts at 0 dB.
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        stage.prepare (sampleRate);
        reset();
    }

    void reset() noexcept
    {
        gainDb = 0.0;
        levelValid = floorValid = false;
        levelDb = noiseFloorDb = -120.0;
        hangover = holdLeft = 0.0;
        stage.setGainDb (0.0f, true);
        stage.reset();
        appliedGainDb.store (0.0f, std::memory_order_relaxed);
    }

    //==============================================================================
    // Plug-in thread: analyses the block, updates the gain and applies it in place.
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
            return;

        float peakIn = 0.0f;
        double sumSquares = 0.0;
        int counted = 0;
        for (int c = 0; c < numChannels; ++c)
            if (const float* x = channels[c])
            {
                ++counted;
                for (int i = 0; i < numSamples; ++i)
                {
                    const float a = std::abs (x[i]);
                    peakIn = std::max (peakIn, a);
                    sumSquares += (double) x[i] * (double) x[i];
                }
            }
        if (counted == 0)
            return;

        const double dt = (double) numSamples / sampleRate;
        const double rmsDb = toDb (std::sqrt (sumSquares / (double) (counted * numSamples)));
        const double peakDb = toDb ((double) peakIn);

        // --- silence detection (not a gate: it only decides whether the gain may move) --------
        if (rmsDb > -100.0)   // digital silence (muted device, dropout) says nothing about the room
        {
            if (! floorValid)             { noiseFloorDb = rmsDb; floorValid = true; }
            else if (rmsDb < noiseFloorDb) noiseFloorDb += (rmsDb - noiseFloorDb) * (1.0 - std::exp (-dt / floorFallSeconds));
            else                           noiseFloorDb += std::min (rmsDb - noiseFloorDb, floorRiseDbPerSec * dt);
        }
        const bool active = rmsDb > std::max (activityFloorDb, noiseFloorDb + activityMarginDb);
        hangover = active ? hangoverSeconds : std::max (0.0, hangover - dt);
        const bool speech = active || hangover > 0.0;

        // --- gain decision ----------------------------------------------------------------------
        if (enabled.load (std::memory_order_relaxed))
        {
            if (speech)
            {
                if (! levelValid || peakDb >= levelDb)   { levelDb = peakDb; holdLeft = peakHoldSeconds; }
                else if (holdLeft > 0.0)                 holdLeft -= dt;
                else                                     levelDb = std::max (peakDb, levelDb - levelDecayDbPerSec * dt);
                levelValid = true;
                const double desired = std::clamp (targetPeakDb - levelDb, maxCutDb, maxBoostDb);
                if (desired < gainDb - fastZoneDb)
                    gainDb += (desired - gainDb) * (1.0 - std::exp (-dt / fastFallSeconds));
                else if (desired < gainDb)
                    gainDb = std::max (desired, gainDb - fallDbPerSec * dt);
                else
                    gainDb = std::min (desired, gainDb + riseDbPerSec * dt);
            }
            // silence: the gain is held
        }
        else
        {
            levelValid = false;   // re-learn the level when switched on again
            gainDb = gainDb > 0.0 ? std::max (0.0, gainDb - offReturnDbPerSec * dt)
                                  : std::min (0.0, gainDb + offReturnDbPerSec * dt);
        }

        stage.setGainDb ((float) gainDb);
        stage.process (channels, numChannels, numSamples);

        // --- meters (UI) ------------------------------------------------------------------------
        float peakOut = 0.0f;
        for (int c = 0; c < numChannels; ++c)
            if (const float* x = channels[c])
                for (int i = 0; i < numSamples; ++i)
                    peakOut = std::max (peakOut, std::abs (x[i]));
        storeMax (inPeakHold, peakIn);
        storeMax (outPeakHold, peakOut);
        appliedGainDb.store ((float) (20.0 * std::log10 (std::max (1.0e-9, stage.getCurrentGain()))), std::memory_order_relaxed);
        speaking.store (speech, std::memory_order_relaxed);
    }

    //==============================================================================
    // UI thread: peaks since the previous call (dBFS, -120 = nothing), the gain applied at the end
    // of the last block, whether the stage currently hears speech (false = holding).
    struct Meters { float inPeakDb = -120.0f, outPeakDb = -120.0f, gainDb = 0.0f; bool speech = false; };
    Meters takeMeters() noexcept
    {
        Meters m;
        m.inPeakDb  = (float) toDb ((double) inPeakHold.exchange (0.0f, std::memory_order_relaxed));
        m.outPeakDb = (float) toDb ((double) outPeakHold.exchange (0.0f, std::memory_order_relaxed));
        m.gainDb    = appliedGainDb.load (std::memory_order_relaxed);
        m.speech    = speaking.load (std::memory_order_relaxed);
        return m;
    }

    // Plug-in thread / tests: the gain the analysis currently asks for, and the noise floor.
    double getTargetGainDb() const noexcept { return gainDb; }
    double getNoiseFloorDb() const noexcept { return noiseFloorDb; }
    double getAppliedGainDb() const noexcept { return 20.0 * std::log10 (std::max (1.0e-9, stage.getCurrentGain())); }

private:
    static double toDb (double linear) noexcept { return linear > 1.0e-6 ? 20.0 * std::log10 (linear) : -120.0; }

    static void storeMax (std::atomic<float>& a, float v) noexcept
    {
        float current = a.load (std::memory_order_relaxed);
        while (v > current && ! a.compare_exchange_weak (current, v, std::memory_order_relaxed)) {}
    }

    std::atomic<bool>  enabled { true };
    std::atomic<float> inPeakHold { 0.0f }, outPeakHold { 0.0f }, appliedGainDb { 0.0f };
    std::atomic<bool>  speaking { false };

    // plug-in thread only (and prepare/reset while it does not process this stage)
    GainStage stage;
    double sampleRate = 48000.0;
    double gainDb = 0.0;                 // the gain the analysis asks for (dB); GainStage glides to it
    double levelDb = -120.0;             // speech peak level estimate (dBFS)
    double noiseFloorDb = -120.0;        // room noise estimate (block RMS, dBFS)
    double hangover = 0.0;               // seconds of "still speech" after the last active block
    double holdLeft = 0.0;               // seconds the level estimate still holds its peak
    bool   levelValid = false, floorValid = false;
};
