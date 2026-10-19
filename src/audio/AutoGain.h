#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

// The DSP core of MicVST's built-in "Otomatik Kazanç" (Auto Gain): a real-time voice level rider.
// It analyses the real input block by block and sets ONE gain for all channels.
//
//  * Levels (approximate aims, not fixed output levels): normal speech peaks about -15 dBFS,
//    shouting peaks about -12 dBFS. "Shouting" is judged against the speaker's own natural level
//    (learned from the voice): from 3 dB above it the aim slides smoothly towards -12 dBFS,
//    reached 9 dB above it.
//  * Level detection: a "sustained peak" - in every 10 ms window, the highest level that is reached
//    or exceeded for at least 1 ms in total (histogram in 0.25 dB steps). A vowel's crests last far
//    longer, so they count fully (a sine reads 0.1 dB under its peak); clicks, lip smacks and bursts
//    shorter than 1 ms are ignored: the gain does not drop because of them (the limiter below still
//    catches them).
//  * Analysis: phrase level = the loudest sustained peak (instantly), held 1 s, then following
//    quieter speech at 2 dB/s down to the recent syllable peaks (never below them, so no ripple
//    between syllables) - or at 12 dB/s when the voice is clearly (6 dB) quieter, e.g. after
//    shouting. The gain follows phrases, not syllables, so the voice keeps its dynamics.
//  * Gain: no fixed value - it is the aim minus the analysed level, limited to -4 ... +8 dB (only
//    as much as needed inside that range).
//    Up: exponential approach (time constant 0.35 s, 2..18 dB/s of voiced time); down (back
//    towards 0 dB): time constant 0.15 s, or 30 ms when the voice is more than 3 dB too loud.
//    A two-stage glide (6 ms each) makes every change click-free.
//  * Silence: NO noise gate, nothing is ever muted or faded. The gain moves only on blocks with
//    voice (RMS above -50 dBFS and 10 dB above the tracked noise floor); between words and in
//    silence the last gain is kept exactly (no ramp, no reset) and speaking again continues from
//    it. The level analysis keeps its clock running through short pauses between words (300 ms
//    hangover), so the hold/decay times are real speaking time.
//  * Output ceiling, separate from the gain control: a soft limiter with 3 ms look-ahead - the
//    audio passes a 3 ms delay while the limiter sees it undelayed, so the reduction for a
//    coming peak is already in place when the peak leaves the delay. Peaks above -8.3 dBFS are
//    bent smoothly towards -6.3 dBFS (tanh curve), an instant guard makes sure no sample ever
//    exceeds -6 dBFS; the reduction is held 20 ms and released in 80 ms. Normal speech
//    (-15 dBFS) stays below its knee: not compressed.
//  * Latency: exactly the look-ahead (144 samples at 48 kHz), reported to the host; it stays the same
//    when the stage is switched off, so on/off never shifts the audio.
//  * Off: the gain returns to 0 dB at 12 dB/s; then the audio only passes the delay (bit-exact,
//    3 ms later).
//  * Realtime: no allocation at all (fixed buffers), no locks; on/off and the meters cross threads
//    through atomics.
class AutoGainStage
{
public:
    // levels
    static constexpr double normalPeakDb        = -15.0;
    static constexpr double shoutPeakDb         = -12.0;
    static constexpr double shoutStartDb        = 3.0;     // above the natural level: aim starts to slide ...
    static constexpr double shoutFullDb         = 9.0;     // ... and is fully the shouting aim here
    // speech detection (decides only whether the gain may move; the audio is never gated)
    static constexpr double activityFloorDb     = -50.0;
    static constexpr double activityMarginDb    = 10.0;
    static constexpr double hangoverSeconds     = 0.3;
    // level detection: sustained peak
    static constexpr double detectWindowSeconds = 0.01;
    static constexpr double detectMinSeconds    = 0.001;   // a level must be present this long (in total) per window
    static constexpr int    detectBins          = 448;     // 0.25 dB steps from -100 to +12 dBFS
    // level analysis (times count speech only)
    static constexpr double peakHoldSeconds     = 1.0;
    static constexpr double levelDecayDbPerSec  = 2.0;
    static constexpr double recentHoldSeconds   = 0.3;     // short-term level (syllables) ...
    static constexpr double recentDecayDbPerSec = 15.0;
    static constexpr double dropDb              = 6.0;     // ... this far under the phrase level:
    static constexpr double dropFollowDbPerSec  = 12.0;    //     the phrase level follows it this fast
    static constexpr double naturalLearnSeconds = 3.0;     // natural level: learned quickly at first ...
    static constexpr double naturalLearnTau     = 0.3;
    static constexpr double naturalUpTau        = 20.0;    // ... then follows a louder voice slowly
    static constexpr double naturalDownTau      = 6.0;     // ... and a quieter one a bit faster
    // gain range and movement
    static constexpr double maxBoostDb          = 8.0;
    static constexpr double maxCutDb            = -4.0;
    static constexpr double riseTau             = 0.35;
    static constexpr double riseMinDbPerSec     = 2.0;
    static constexpr double riseMaxDbPerSec     = 18.0;
    static constexpr double fallTau             = 0.15;
    static constexpr double fastZoneDb          = 3.0;
    static constexpr double fastFallTau         = 0.03;
    static constexpr double offReturnDbPerSec   = 12.0;
    static constexpr double smoothingSeconds    = 0.006;   // each of the two glide stages
    // output ceiling (soft limiter with look-ahead)
    static constexpr double ceilingDb           = -6.0;
    static constexpr double limiterMarginDb     = 0.3;     // the curve aims this far under the ceiling ...
    static constexpr double limiterKneeDb       = 2.0;     // ... and starts this far under that
    static constexpr double lookAheadSeconds    = 0.003;
    static constexpr double limiterHoldSeconds  = 0.02;
    static constexpr double limiterReleaseSeconds = 0.08;
    // noise floor tracking (for the speech detection)
    static constexpr double floorRiseDbPerSec   = 3.0;
    static constexpr double floorFallSeconds    = 0.25;
    // fixed buffer sizes (no allocation anywhere)
    static constexpr int maxChannels = 8;
    static constexpr int maxLookAheadSamples = 1024;       // 3 ms up to 341 kHz

    static int latencyFor (double rate) noexcept
    {
        return std::clamp ((int) std::lround (lookAheadSeconds * (rate > 0.0 ? rate : 48000.0)), 1, maxLookAheadSamples);
    }

    AutoGainStage() noexcept { reset(); }

    //==============================================================================
    void setEnabled (bool shouldBeOn) noexcept { enabled.store (shouldBeOn, std::memory_order_relaxed); }
    bool isEnabled() const noexcept            { return enabled.load (std::memory_order_relaxed); }

    // While the plug-in thread does not process this stage: new rate; the gain starts at 0 dB.
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate > 0.0 ? newSampleRate : 48000.0;
        lookAhead = latencyFor (sampleRate);
        glideCoeff = 1.0 - std::exp (-1.0 / (smoothingSeconds * sampleRate));
        limiterReleaseCoeff = 1.0 - std::exp (-1.0 / (limiterReleaseSeconds * sampleRate));
        limiterAttackCoeff = 1.0 - std::exp (-5.0 / (double) lookAhead);   // > 99 % of the way within the look-ahead
        limiterHoldSamples = (int) std::lround (limiterHoldSeconds * sampleRate);
        detectWindow = std::max (1, (int) std::lround (detectWindowSeconds * sampleRate));
        detectMin = std::max (1, (int) std::lround (detectMinSeconds * sampleRate));
        reset();
    }

    void reset() noexcept
    {
        gainDb = glide1 = glide2 = 0.0;
        currentGain = 1.0;
        glideSettled = true;
        limiterGain = 1.0;
        limiterHold = 0;
        delay.fill (0.0f);
        delayPos = 0;
        wants.fill (1.0);
        wantPos = wantsBelowOne = 0;
        histogram.fill (0);
        histCount = 0;
        sustainedDb = -120.0;
        sustainedValid = false;
        guardCount = 0;
        forgetVoice();
        floorValid = false;
        noiseFloorDb = -120.0;
        hangover = 0.0;
        appliedGainDb.store (0.0f, std::memory_order_relaxed);
    }

    int getLatencySamples() const noexcept { return lookAhead; }

    //==============================================================================
    // Plug-in thread: analyses the block, updates the gain and applies it in place (delayed by the
    // look-ahead). Channels beyond maxChannels are left untouched.
    void process (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (channels == nullptr || numChannels <= 0 || numSamples <= 0)
            return;
        numChannels = std::min (numChannels, maxChannels);

        int counted = 0;
        for (int c = 0; c < numChannels; ++c)
            if (channels[c] != nullptr) ++counted;
        if (counted == 0)
            return;

        // --- analysis of the incoming (undelayed) block -------------------------------------------
        float peakIn = 0.0f;
        double sumSquares = 0.0, blockSustainedDb = -200.0;
        for (int i = 0; i < numSamples; ++i)
        {
            double a = 0.0;
            for (int c = 0; c < numChannels; ++c)
                if (const float* x = channels[c])
                {
                    const double v = (double) x[i];
                    a = std::max (a, std::abs (v));
                    sumSquares += v * v;
                }
            peakIn = std::max (peakIn, (float) a);

            // sustained peak: histogram of the 10 ms window, read when it is full
            ++histogram[(size_t) (a > 1.0e-5 ? std::clamp ((int) ((20.0 * std::log10 (a) + 100.0) * 4.0), 0, detectBins - 1) : 0)];
            if (++histCount >= detectWindow)
            {
                int acc = 0, b = detectBins - 1;
                for (; b > 0; --b)
                    if ((acc += histogram[(size_t) b]) >= detectMin) break;
                sustainedDb = b > 0 ? (double) b * 0.25 - 100.0 + 0.125 : -120.0;
                sustainedValid = true;
                blockSustainedDb = std::max (blockSustainedDb, sustainedDb);
                histogram.fill (0);
                histCount = 0;
            }
        }

        const double dt = (double) numSamples / sampleRate;
        const double rmsDb = toDb (std::sqrt (sumSquares / (double) (counted * numSamples)));
        // blocks shorter than the window reuse the last window; before the first one, the block peak
        const double peakDb = blockSustainedDb > -200.0 ? blockSustainedDb : (sustainedValid ? sustainedDb : toDb ((double) peakIn));

        // --- voice detection (not a gate: it only decides whether the gain may move) ----------
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
        const bool on = enabled.load (std::memory_order_relaxed);
        if (on)
        {
            if (speech)
                analyse (peakDb, active, dt);
            if (active)
                moveGainTowards (std::clamp (currentAimDb() - levelDb, maxCutDb, maxBoostDb), dt);
            // no voice in this block (pause, silence): the last gain is kept exactly
        }
        else
        {
            forgetVoice();   // re-learn the voice when switched on again
            gainDb = gainDb > 0.0 ? std::max (0.0, gainDb - offReturnDbPerSec * dt)
                                  : std::min (0.0, gainDb + offReturnDbPerSec * dt);
        }

        // --- apply: delay (look-ahead) + glide + output ceiling, sample by sample ---------------
        const int L = lookAhead, W = lookAhead + 1;
        const bool idle = ! on && glideSettled && gainDb == 0.0 && glide2 == 0.0 && limiterGain == 1.0;
        if (idle && wantsBelowOne != 0)
        {
            std::fill (wants.begin(), wants.begin() + W, 1.0);
            wantsBelowOne = 0;
        }

        const double target = gainDb;
        float peakOut = 0.0f;
        std::array<float, maxChannels> out {};
        for (int i = 0; i < numSamples; ++i)
        {
            // the delay line: the newest sample goes in, the one from L samples ago comes out
            double aNew = 0.0, aOut = 0.0;
            for (int c = 0; c < numChannels; ++c)
                if (const float* x = channels[c])
                {
                    float& slot = delay[(size_t) (c * maxLookAheadSamples + delayPos)];
                    out[(size_t) c] = slot;
                    slot = x[i];
                    aNew = std::max (aNew, (double) std::abs (x[i]));
                    aOut = std::max (aOut, (double) std::abs (out[(size_t) c]));
                }
            delayPos = delayPos + 1 < L ? delayPos + 1 : 0;

            if (idle)   // off and back at 0 dB: only the delay (bit-exact)
            {
                for (int c = 0; c < numChannels; ++c)
                    if (float* x = channels[c]) { x[i] = out[(size_t) c]; peakOut = std::max (peakOut, std::abs (x[i])); }
                continue;
            }

            if (! glideSettled || glide2 != target)
            {
                glide1 += glideCoeff * (target - glide1);
                glide2 += glideCoeff * (glide1 - glide2);
                if (std::abs (target - glide1) < 1.0e-5 && std::abs (target - glide2) < 1.0e-5)
                {
                    glide1 = glide2 = target;
                    glideSettled = true;
                }
                else
                {
                    glideSettled = false;
                }
                currentGain = std::exp (glide2 * dbToLog);
            }

            // look-ahead soft limiter: what the newest sample will need, the smallest need of all
            // samples still in the delay line, approached so it is in place when they come out
            double want = 1.0;
            if (const double a = aNew * currentGain; on && a > kneeLinear)
            {
                const double xDb = 20.0 * std::log10 (a), k0 = ceilingDb - limiterMarginDb - limiterKneeDb;
                want = std::exp ((k0 + limiterKneeDb * std::tanh ((xDb - k0) / limiterKneeDb) - xDb) * dbToLog);
            }
            if (wants[(size_t) wantPos] < 1.0) --wantsBelowOne;
            wants[(size_t) wantPos] = want;
            if (want < 1.0) ++wantsBelowOne;
            wantPos = wantPos + 1 < W ? wantPos + 1 : 0;

            double need = 1.0;
            if (wantsBelowOne > 0)
                for (int k = 0; k < W; ++k) need = std::min (need, wants[(size_t) k]);

            if (need < limiterGain)       { limiterGain += (need - limiterGain) * limiterAttackCoeff; limiterHold = limiterHoldSamples; }
            else if (limiterHold > 0)     --limiterHold;
            else if (limiterGain < 1.0)
            {
                limiterGain = std::min (need, limiterGain + (1.0 - limiterGain) * limiterReleaseCoeff);
                if (limiterGain > 1.0 - 1.0e-7 && need >= 1.0) limiterGain = 1.0;
            }
            if (const double a = aOut * currentGain; on && a * limiterGain > ceilingLinear)
            {
                limiterGain = ceilingLinear / a;   // guard: no sample ever above the ceiling
                ++guardCount;
            }

            const float g = (float) (currentGain * limiterGain);
            for (int c = 0; c < numChannels; ++c)
                if (float* x = channels[c])
                {
                    x[i] = out[(size_t) c] * g;
                    peakOut = std::max (peakOut, std::abs (x[i]));
                }
        }

        // --- meters (UI) ------------------------------------------------------------------------
        storeMax (inPeakHold, peakIn);
        storeMax (outPeakHold, peakOut);
        appliedGainDb.store ((float) getAppliedGainDb(), std::memory_order_relaxed);
        speaking.store (speech && on, std::memory_order_relaxed);
    }

    //==============================================================================
    // UI thread: peaks since the previous call (dBFS, -120 = nothing), the gain applied at the end
    // of the last block, whether the stage currently hears speech (false = gain kept).
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

    // Plug-in thread / tests.
    double getTargetGainDb() const noexcept   { return gainDb; }                 // what the analysis asks for
    double getAppliedGainDb() const noexcept  { return glide2 + getLimiterGainDb(); }
    double getLimiterGainDb() const noexcept  { return 20.0 * std::log10 (std::max (1.0e-9, limiterGain)); }
    double getNoiseFloorDb() const noexcept   { return noiseFloorDb; }
    double getLevelDb() const noexcept        { return levelDb; }                // phrase peak level (input)
    double getNaturalLevelDb() const noexcept { return naturalDb; }              // the speaker's natural level
    double getAimDb() const noexcept          { return currentAimDb(); }         // current output aim
    int    getGuardCount() const noexcept     { return guardCount; }             // samples the guard had to catch

private:
    static constexpr double dbToLog = 0.11512925464970228;          // ln(10) / 20
    static constexpr double ceilingLinear = 0.50118723362727224;    // -6 dBFS
    static constexpr double kneeLinear    = 0.38459178204535355;    // -8.3 dBFS (ceiling - margin - knee)

    static double toDb (double linear) noexcept { return linear > 1.0e-6 ? 20.0 * std::log10 (linear) : -120.0; }

    static void storeMax (std::atomic<float>& a, float v) noexcept
    {
        float current = a.load (std::memory_order_relaxed);
        while (v > current && ! a.compare_exchange_weak (current, v, std::memory_order_relaxed)) {}
    }

    void forgetVoice() noexcept
    {
        levelValid = naturalValid = false;
        levelDb = recentDb = naturalDb = -120.0;
        holdLeft = recentHold = 0.0;
        learnLeft = naturalLearnSeconds;
    }

    // Phrase level, short-term level and (from blocks with voice) the speaker's natural level.
    void analyse (double peakDb, bool active, double dt) noexcept
    {
        if (! levelValid)
        {
            levelDb = recentDb = peakDb;
            holdLeft = peakHoldSeconds;
            recentHold = recentHoldSeconds;
            levelValid = true;
        }
        else
        {
            if (peakDb >= recentDb)    { recentDb = peakDb; recentHold = recentHoldSeconds; }
            else if (recentHold > 0.0) recentHold -= dt;
            else                       recentDb = std::max (peakDb, recentDb - recentDecayDbPerSec * dt);

            if (peakDb >= levelDb)                { levelDb = peakDb; holdLeft = peakHoldSeconds; }
            else if (levelDb - recentDb > dropDb) levelDb = std::max (recentDb, levelDb - dropFollowDbPerSec * dt);
            else if (holdLeft > 0.0)              holdLeft -= dt;
            else                                  levelDb = std::max (recentDb, levelDb - levelDecayDbPerSec * dt);
        }

        if (active)
        {
            if (! naturalValid) { naturalDb = levelDb; naturalValid = true; }
            const double tau = learnLeft > 0.0 ? naturalLearnTau : (levelDb > naturalDb ? naturalUpTau : naturalDownTau);
            naturalDb += (levelDb - naturalDb) * (1.0 - std::exp (-dt / tau));
            learnLeft = std::max (0.0, learnLeft - dt);
        }
    }

    // Normal speech -> -15 dBFS; the more the voice rises above its natural level, the closer to
    // -12 dBFS (smooth, no steps).
    double currentAimDb() const noexcept
    {
        if (! naturalValid) return normalPeakDb;
        const double s = std::clamp ((levelDb - naturalDb - shoutStartDb) / (shoutFullDb - shoutStartDb), 0.0, 1.0);
        return normalPeakDb + (shoutPeakDb - normalPeakDb) * s * s * (3.0 - 2.0 * s);
    }

    void moveGainTowards (double desired, double dt) noexcept
    {
        if (! std::isfinite (desired)) return;
        const double diff = desired - gainDb;
        if (diff > 0.0)
            gainDb = std::min (desired, gainDb + std::clamp (diff / riseTau, riseMinDbPerSec, riseMaxDbPerSec) * dt);
        else if (diff < 0.0)
            gainDb += diff * (1.0 - std::exp (-dt / (-diff > fastZoneDb ? fastFallTau : fallTau)));
    }

    std::atomic<bool>  enabled { true };
    std::atomic<float> inPeakHold { 0.0f }, outPeakHold { 0.0f }, appliedGainDb { 0.0f };
    std::atomic<bool>  speaking { false };

    // plug-in thread only (and prepare/reset while it does not process this stage)
    double sampleRate = 48000.0;
    int    lookAhead = 144;                         // 3 ms at 48 kHz until prepare() is called
    double glideCoeff = 1.0 - 0.99653379;           // 6 ms at 48 kHz
    double limiterReleaseCoeff = 1.0 - 0.99973961;  // 80 ms at 48 kHz
    double limiterAttackCoeff = 1.0 - 0.96587368;   // 5 time constants within 144 samples
    int    limiterHoldSamples = 960;                // 20 ms at 48 kHz
    int    detectWindow = 480, detectMin = 48;      // 10 ms / 1 ms at 48 kHz
    double gainDb = 0.0;                            // the gain the analysis asks for (dB)
    double glide1 = 0.0, glide2 = 0.0;              // glide states (dB); glide2 is applied
    double currentGain = 1.0;                       // exp of glide2
    bool   glideSettled = true;
    double limiterGain = 1.0;
    int    limiterHold = 0;
    std::array<float, (size_t) (maxChannels * maxLookAheadSamples)> delay {};
    int    delayPos = 0;
    std::array<double, (size_t) (maxLookAheadSamples + 1)> wants {};   // limiter needs of the samples in the delay
    int    wantPos = 0, wantsBelowOne = 0;
    std::array<int, (size_t) detectBins> histogram {};   // sustained-peak detection (current window)
    int    histCount = 0;
    double sustainedDb = -120.0;                    // the last full window's sustained peak
    bool   sustainedValid = false;
    int    guardCount = 0;
    double levelDb = -120.0, recentDb = -120.0, naturalDb = -120.0;
    double holdLeft = 0.0, recentHold = 0.0, learnLeft = naturalLearnSeconds;
    double noiseFloorDb = -120.0;
    double hangover = 0.0;
    bool   levelValid = false, naturalValid = false, floorValid = false;
};
