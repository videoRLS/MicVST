#include "audio/AudioBridge.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace
{
    constexpr int64_t ringMask = AudioBridge::ringCapacity - 1;

    // PI controller on the time-continuous fill level. Error unit: seconds of audio.
    // Kp = 0.2/s, Ki = 0.005/s^2: over-damped (zeta ~ 1.4), settles in ~30 s; a clock drift of
    // 300 ppm is learned without the level ever sagging by more than ~1.5 ms.
    constexpr double kp = 0.2;
    constexpr double ki = 0.005;
    constexpr double maxCorrection  = 0.005;   // +-0.5 %: far beyond real clock drift (~+-100 ppm)
    constexpr double levelTau       = 0.3;     // seconds, smoothing of the measured level
    constexpr double fadeSeconds    = 0.002;   // fade-out / fade-in around gaps
    constexpr double resyncSeconds  = 0.040;   // excess latency that triggers a crossfaded trim
    constexpr int    resyncPulls    = 50;      // ... if it persists for this many output blocks
    constexpr double producerTimeout = 0.5;    // no push for this long = producer not running
    constexpr double integrateBelow  = 0.003;  // anti-windup: only small errors teach the drift
    constexpr double settleSeconds   = 1.5;    // no drift learning right after a (re-)prime or trim:
                                               // the catch-up after a stall is not clock drift

    double besselI0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        const double q = x * x / 4.0;
        for (int k = 1; k < 60; ++k)
        {
            term *= q / ((double) k * (double) k);
            sum += term;
            if (term < sum * 1.0e-12) break;
        }
        return sum;
    }

    double sinc (double x) noexcept
    {
        if (std::abs (x) < 1.0e-12) return 1.0;
        const double px = juce::MathConstants<double>::pi * x;
        return std::sin (px) / px;
    }

    float dot (const float* a, const float* b, int n) noexcept
    {
        float acc0 = 0.0f, acc1 = 0.0f, acc2 = 0.0f, acc3 = 0.0f;
        int i = 0;
        for (; i + 4 <= n; i += 4)
        {
            acc0 += a[i]     * b[i];
            acc1 += a[i + 1] * b[i + 1];
            acc2 += a[i + 2] * b[i + 2];
            acc3 += a[i + 3] * b[i + 3];
        }
        for (; i < n; ++i)
            acc0 += a[i] * b[i];
        return (acc0 + acc1) + (acc2 + acc3);
    }
}

//==============================================================================
bool AudioBridge::RealtimeGate::tryEnter() noexcept
{
    if (writer.load (std::memory_order_seq_cst))
        return false;

    users.fetch_add (1, std::memory_order_seq_cst);

    if (writer.load (std::memory_order_seq_cst))
    {
        users.fetch_sub (1, std::memory_order_seq_cst);
        return false;
    }

    return true;
}

void AudioBridge::RealtimeGate::exit() noexcept
{
    users.fetch_sub (1, std::memory_order_seq_cst);
}

void AudioBridge::RealtimeGate::lockForConfig() noexcept
{
    writerMutex.enter();
    writer.store (true, std::memory_order_seq_cst);

    while (users.load (std::memory_order_seq_cst) != 0)
        juce::Thread::yield();
}

void AudioBridge::RealtimeGate::unlockForConfig() noexcept
{
    writer.store (false, std::memory_order_seq_cst);
    writerMutex.exit();
}

//==============================================================================
AudioBridge::AudioBridge()
{
    for (auto& r : ring)     r.assign ((size_t) ringCapacity * 2, 0.0f);
    for (auto& s : scratchA) s.assign ((size_t) maxBlockFrames, 0.0f);
    for (auto& s : scratchB) s.assign ((size_t) maxBlockFrames, 0.0f);
    table.assign ((size_t) (phases + 1) * (size_t) (2 * maxHalfTaps), 0.0f);
    designFilter (0.485, 32);
}

void AudioBridge::designFilter (double cutoff, int newHalfTaps)
{
    // Kaiser-windowed sinc, beta = 10 (about -100 dB stopband). Row p holds the coefficients for
    // an output position p/phases samples after the base input sample; the taps cover input
    // samples base-(H-1) .. base+H. Every row is normalised to unity DC gain so the gain does not
    // depend on the fractional position (no modulation noise).
    const int h = juce::jlimit (4, maxHalfTaps, newHalfTaps);
    const int taps = 2 * h;
    constexpr double beta = 10.0;
    const double i0Beta = besselI0 (beta);
    std::array<double, 2 * maxHalfTaps> tmp {};

    for (int p = 0; p <= phases; ++p)
    {
        const double x = (double) p / (double) phases;
        float* row = table.data() + (size_t) p * (size_t) taps;
        double sum = 0.0;

        for (int j = 0; j < taps; ++j)
        {
            const double t = (double) (j - (h - 1)) - x;   // distance to the output position
            const double u = t / (double) h;
            const double w = std::abs (u) < 1.0 ? besselI0 (beta * std::sqrt (1.0 - u * u)) / i0Beta : 0.0;
            tmp[(size_t) j] = 2.0 * cutoff * sinc (2.0 * cutoff * t) * w;
            sum += tmp[(size_t) j];
        }

        for (int j = 0; j < taps; ++j)
            row[j] = (float) (tmp[(size_t) j] / sum);
    }

    halfTaps.store (h, std::memory_order_relaxed);
}

void AudioBridge::updateResamplerConfig()
{
    const double inHz  = inputRate.load (std::memory_order_relaxed);
    const double outHz = outputRate.load (std::memory_order_relaxed);
    const double ratio = outHz > 0.0 ? inHz / outHz : 1.0;   // > 1 = downsampling

    // Passband up to ~97 % of the lower Nyquist frequency; longer filter when downsampling.
    designFilter (0.485 / juce::jmax (1.0, ratio), (int) std::ceil (32.0 * juce::jmax (1.0, ratio)));
}

void AudioBridge::setInputRate (double hz)
{
    if (hz <= 0.0) return;
    gate.lockForConfig();
    inputRate.store (hz, std::memory_order_relaxed);
    updateResamplerConfig();
    restartRequested.store (true, std::memory_order_release);
    gate.unlockForConfig();
}

void AudioBridge::setOutputRate (double hz)
{
    if (hz <= 0.0) return;
    gate.lockForConfig();
    outputRate.store (hz, std::memory_order_relaxed);
    updateResamplerConfig();
    restartRequested.store (true, std::memory_order_release);
    gate.unlockForConfig();
}

void AudioBridge::setConsumerActive (bool isActive) noexcept
{
    if (isActive) restartRequested.store (true, std::memory_order_release);
    consumerActive.store (isActive, std::memory_order_release);
}

void AudioBridge::setSafety (double ms, bool autoMode) noexcept
{
    ms = juce::jlimit (minSafetyMs, autoMaxMs * 2.0, ms);
    safetyMsSetting.store (ms, std::memory_order_relaxed);
    autoSafetyMs.store (ms, std::memory_order_relaxed);
    autoSafety.store (autoMode, std::memory_order_relaxed);
}

double AudioBridge::currentSafetyMs() const noexcept
{
    return autoSafety.load (std::memory_order_relaxed) ? autoSafetyMs.load (std::memory_order_relaxed)
                                                       : safetyMsSetting.load (std::memory_order_relaxed);
}

void AudioBridge::recordUnderrun() noexcept
{
    underruns.fetch_add (1, std::memory_order_relaxed);

    if (autoSafety.load (std::memory_order_relaxed))
        autoSafetyMs.store (juce::jmin (autoMaxMs, autoSafetyMs.load (std::memory_order_relaxed) + autoStepMs),
                            std::memory_order_relaxed);
}

//==============================================================================
void AudioBridge::push (const float* const* data, int numChannels, int numFrames, double timeSeconds) noexcept
{
    if (numFrames <= 0 || numChannels <= 0 || ! consumerActive.load (std::memory_order_acquire))
        return;

    const RealtimeScope scope (gate);
    if (! scope.entered)
        return;   // reconfiguration in progress: the consumer re-primes anyway

    const int64_t w   = writeIndex.load (std::memory_order_relaxed);
    const int64_t rel = releaseIndex.load (std::memory_order_acquire);

    if (numFrames > ringCapacity || w + numFrames - rel > ringCapacity)
    {
        droppedBlocks.fetch_add (1, std::memory_order_relaxed);   // consumer stalled / gone
        return;
    }

    const int64_t pos   = w & ringMask;
    const int     first = (int) juce::jmin ((int64_t) numFrames, (int64_t) ringCapacity - pos);
    const int     rest  = numFrames - first;

    for (int c = 0; c < maxChannels; ++c)
    {
        const float* src = data[juce::jmin (c, numChannels - 1)];   // mono source -> both channels
        float* r = ring[(size_t) c].data();

        std::memcpy (r + pos,                src, (size_t) first * sizeof (float));
        std::memcpy (r + pos + ringCapacity, src, (size_t) first * sizeof (float));

        if (rest > 0)
        {
            std::memcpy (r,                src + first, (size_t) rest * sizeof (float));
            std::memcpy (r + ringCapacity, src + first, (size_t) rest * sizeof (float));
        }
    }

    writeIndex.store (w + numFrames, std::memory_order_release);

    // seqlock publish of (end index, time)
    const auto seq = pushSeq.load (std::memory_order_relaxed);
    pushSeq.store (seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    pushEndIndex.store (w + numFrames, std::memory_order_relaxed);
    pushTime.store (timeSeconds, std::memory_order_relaxed);
    lastPushFrames.store (numFrames, std::memory_order_relaxed);
    pushSeq.store (seq + 2, std::memory_order_release);
}

bool AudioBridge::readLastPush (int64_t& endIndex, double& time) const noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto s1 = pushSeq.load (std::memory_order_acquire);
        if ((s1 & 1u) != 0 || s1 == 0) continue;
        endIndex = pushEndIndex.load (std::memory_order_relaxed);
        time     = pushTime.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (pushSeq.load (std::memory_order_relaxed) == s1)
            return true;
    }
    return false;
}

//==============================================================================
int64_t AudioBridge::framesNeeded (double startFrac, double step, int numOut) const noexcept
{
    // Frames from the base index up to (and including) the newest input sample touched by the
    // last output sample of this block.
    const double lastPos = startFrac + (double) (numOut - 1) * step;
    return (int64_t) std::floor (lastPos) + halfTaps.load (std::memory_order_relaxed) + 1;
}

void AudioBridge::render (float* const* dst, int numCh, int numOut, int64_t baseIndex,
                          double startFrac, double step) const noexcept
{
    const int h = halfTaps.load (std::memory_order_relaxed);
    const int taps = 2 * h;
    std::array<float, 2 * maxHalfTaps> coef;

    double pos = startFrac;

    for (int i = 0; i < numOut; ++i)
    {
        const double ip  = std::floor (pos);
        const double ph  = (pos - ip) * (double) phases;
        const int    p0  = juce::jlimit (0, phases - 1, (int) ph);
        const float  f   = (float) (ph - (double) p0);
        const float* r0  = table.data() + (size_t) p0 * (size_t) taps;
        const float* r1  = r0 + taps;

        for (int j = 0; j < taps; ++j)
            coef[(size_t) j] = r0[j] + f * (r1[j] - r0[j]);

        const int64_t first = (baseIndex + (int64_t) ip - (h - 1)) & ringMask;

        for (int c = 0; c < numCh; ++c)
            if (dst[c] != nullptr)
                dst[c][i] = dot (ring[(size_t) juce::jmin (c, maxChannels - 1)].data() + first, coef.data(), taps);

        pos += step;
    }
}

double AudioBridge::testResampleMono (const float* src, int srcLen, double startPos, double step,
                                      float* dst, int numFrames) const noexcept
{
    const int h = halfTaps.load (std::memory_order_relaxed);
    const int taps = 2 * h;
    std::array<float, 2 * maxHalfTaps> coef;
    double pos = startPos;

    for (int i = 0; i < numFrames; ++i)
    {
        const double ip = std::floor (pos);
        const double ph = (pos - ip) * (double) phases;
        const int    p0 = juce::jlimit (0, phases - 1, (int) ph);
        const float  f  = (float) (ph - (double) p0);
        const float* r0 = table.data() + (size_t) p0 * (size_t) taps;
        const float* r1 = r0 + taps;

        for (int j = 0; j < taps; ++j)
            coef[(size_t) j] = r0[j] + f * (r1[j] - r0[j]);

        const int64_t first = (int64_t) ip - (h - 1);
        dst[i] = (first >= 0 && first + taps <= srcLen) ? dot (src + first, coef.data(), taps) : 0.0f;
        pos += step;
    }

    return pos;
}

//==============================================================================
void AudioBridge::pull (float* const* out, int numChannels, int numFrames, double timeSeconds) noexcept
{
    if (numFrames <= 0 || numChannels <= 0)
        return;

    std::array<float*, maxChannels> ptrs {};
    const int numCh = juce::jmin (numChannels, maxChannels);
    const double outHz = juce::jmax (1.0, outputRate.load (std::memory_order_relaxed));

    for (int done = 0; done < numFrames; done += maxBlockFrames)
    {
        const int n = juce::jmin (maxBlockFrames, numFrames - done);
        for (int c = 0; c < numCh; ++c)
            ptrs[(size_t) c] = out[c] != nullptr ? out[c] + done : nullptr;
        pullBlock (ptrs.data(), numCh, n, timeSeconds + (double) done / outHz);
    }

    // more than two device channels: repeat the first one (all channels carry the voice)
    for (int c = numCh; c < numChannels; ++c)
        if (out[c] != nullptr && out[0] != nullptr)
            juce::FloatVectorOperations::copy (out[c], out[0], numFrames);
}

void AudioBridge::pullBlock (float* const* out, int numCh, int numFrames, double timeSeconds) noexcept
{
    auto clearFrom = [&] (int start)
    {
        if (start < numFrames)
            for (int c = 0; c < numCh; ++c)
                if (out[c] != nullptr)
                    juce::FloatVectorOperations::clear (out[c] + start, numFrames - start);
    };

    // Continues a fade-in that started after the last (re-)prime over the first n samples.
    auto applyFadeIn = [&] (int n, int fadeLength)
    {
        if (fadeInRemaining <= 0 || n <= 0)
            return;
        const int done = fadeLength - fadeInRemaining;
        const int fl = juce::jmin (fadeInRemaining, n);
        for (int c = 0; c < numCh; ++c)
            if (out[c] != nullptr)
                for (int i = 0; i < fl; ++i)
                    out[c][i] *= (float) (done + i + 1) / (float) fadeLength;
        fadeInRemaining -= fl;
    };

    const RealtimeScope scope (gate);
    if (! scope.entered)
    {
        clearFrom (0);
        return;
    }

    const double inHz    = inputRate.load (std::memory_order_relaxed);
    const double outHz   = outputRate.load (std::memory_order_relaxed);
    const double nominal = inHz / outHz;
    const int    h       = halfTaps.load (std::memory_order_relaxed);
    const int    fadeLen = juce::jmax (1, (int) (fadeSeconds * outHz));
    const int64_t w      = writeIndex.load (std::memory_order_acquire);

    if (restartRequested.exchange (false, std::memory_order_acq_rel))
    {
        state = State::priming;
        readIndex = w;
        frac = 0.0;
        correction = integral;
        levelEmaValid = false;
        excessCount = 0;
    }

    auto publishRelease = [&]
    {
        releaseIndex.store (readIndex - (int64_t) h - 1, std::memory_order_release);
    };

    const double safetyFrames = currentSafetyMs() * inHz / 1000.0;
    const double blockIn = (double) lastPushFrames.load (std::memory_order_relaxed);
    int64_t avail = w - readIndex;

    // Time-continuous view of the producer: frames written by its last push plus the frames that
    // have arrived at the input device since then (at most one block). Unlike the raw fill level,
    // this does not jump by a whole block whenever the two device clocks change their phase.
    int64_t lastEnd = 0;
    double  lastTime = 0.0;
    const bool havePush = readLastPush (lastEnd, lastTime);
    const bool producerAlive = havePush && (timeSeconds - lastTime) < producerTimeout;
    const double extrap = havePush ? juce::jlimit (0.0, blockIn, (timeSeconds - lastTime) * inHz) : 0.0;

    // Fade reserve: a block is only rendered normally if, after it, at least one fade length of
    // audio is left. When the queue runs dry, the last rendered block can therefore always fade
    // out smoothly (avail shrinks by at most one block per pull) - never a hard cut to zero.
    auto reserveFor = [&] (double st) { return (int64_t) std::ceil ((double) fadeLen * st) + 2; };

    //--------------------------------------------------------------------------- priming
    if (state == State::priming)
    {
        const double step0 = nominal * (1.0 + integral);
        const double need0 = (double) (framesNeeded (0.0, step0, numFrames) + reserveFor (step0));
        const double target = need0 + safetyFrames + blockIn - extrap;

        if (! producerAlive || (double) avail < target)
        {
            clearFrom (0);
            runningAtomic.store (false, std::memory_order_relaxed);
            bufferedFrames.store ((double) avail, std::memory_order_relaxed);
            publishRelease();
            return;
        }

        // Start exactly at the target: anything older is stale audio = needless latency.
        const auto excess = avail - (int64_t) std::ceil (target);
        if (excess > 0) { readIndex += excess; avail -= excess; }

        frac = 0.0;
        correction = integral;
        levelEmaValid = false;
        excessCount = 0;
        sinceStartSeconds = 0.0;
        fadeInRemaining = fadeLen;
        state = State::running;
        runningAtomic.store (true, std::memory_order_relaxed);
    }

    //--------------------------------------------------------------------------- running
    double step = nominal * (1.0 + correction);
    int64_t need = framesNeeded (frac, step, numFrames) + reserveFor (step);

    if (avail < need)
    {
        // Underrun: render what is still there (at least one fade length thanks to the
        // reserve), fade it out, then silence + re-prime.
        int k = 0;
        const double room = (double) avail - (double) h - 1.0 - frac;
        if (room >= 0.0)
            k = juce::jmin (numFrames, (int) std::floor (room / step) + 1);

        if (k > 0)
        {
            render (out, numCh, k, readIndex, frac, step);
            applyFadeIn (k, fadeLen);   // starved again right after a re-prime: keep the ramp
            const int fl = juce::jmin (k, fadeLen);
            for (int c = 0; c < numCh; ++c)
                if (out[c] != nullptr)
                    for (int i = 0; i < fl; ++i)
                        out[c][k - fl + i] *= 1.0f - (float) (i + 1) / (float) fl;

            const double pos = frac + (double) k * step;
            const auto consumed = (int64_t) std::floor (pos);
            readIndex += consumed;
            frac = pos - (double) consumed;
        }

        clearFrom (k);
        state = State::priming;
        runningAtomic.store (false, std::memory_order_relaxed);
        recordUnderrun();
        publishRelease();
        return;
    }

    // PI controller: keep the time-continuous level at need + safety + one input block. The
    // discrete fill seen at any pull is then never below need + safety, whatever the phase
    // between the two device clocks; the safety margin is left for scheduling jitter.
    int64_t skip = 0;

    if (havePush)
    {
        const double level  = (double) (lastEnd - readIndex) - frac + extrap;
        const double target = (double) need + safetyFrames + blockIn;
        const double dt     = (double) numFrames / outHz;
        const double alpha  = 1.0 - std::exp (-dt / levelTau);

        levelEma = levelEmaValid ? levelEma + alpha * ((level - target) - levelEma) : (level - target);
        levelEmaValid = true;
        sinceStartSeconds += dt;

        const double err = levelEma / inHz;   // seconds; > 0 = more queued than needed
        if (sinceStartSeconds >= settleSeconds && std::abs (err) < integrateBelow)
            integral = juce::jlimit (-maxCorrection, maxCorrection, integral + ki * err * dt);
        const double newCorrection = juce::jlimit (-maxCorrection, maxCorrection, kp * err + integral);
        const double newStep = nominal * (1.0 + newCorrection);
        const auto newNeed = framesNeeded (frac, newStep, numFrames) + reserveFor (newStep);

        if (newNeed <= avail)
        {
            correction = newCorrection;
            step = newStep;
            need = newNeed;
        }

        bufferedFrames.store (level - blockIn * 0.5, std::memory_order_relaxed);

        // Persistent excess (e.g. the output stalled while the input kept coming): trim the
        // latency back to the target right away with a crossfade instead of draining it slowly.
        const double resyncThreshold = juce::jmax (resyncSeconds * inHz, 2.0 * blockIn);
        excessCount = levelEma > resyncThreshold ? excessCount + 1 : 0;

        if (excessCount >= resyncPulls)
            skip = juce::jlimit ((int64_t) 0, avail - need, (int64_t) (level - target));   // instantaneous, not the lagging average
    }

    if (skip > 0)
    {
        std::array<float*, maxChannels> a {}, b {};
        for (int c = 0; c < numCh; ++c) { a[(size_t) c] = scratchA[(size_t) c].data(); b[(size_t) c] = scratchB[(size_t) c].data(); }

        render (a.data(), numCh, numFrames, readIndex,        frac, step);
        render (b.data(), numCh, numFrames, readIndex + skip, frac, step);

        for (int i = 0; i < numFrames; ++i)
        {
            const double th = juce::MathConstants<double>::halfPi * (double) (i + 1) / (double) numFrames;
            const float ga = (float) std::cos (th), gb = (float) std::sin (th);
            for (int c = 0; c < numCh; ++c)
                if (out[c] != nullptr)
                    out[c][i] = ga * a[(size_t) c][i] + gb * b[(size_t) c][i];
        }

        readIndex += skip;
        levelEmaValid = false;   // restart the average from the trimmed level
        excessCount = 0;
        sinceStartSeconds = 0.0;
        resyncs.fetch_add (1, std::memory_order_relaxed);
    }
    else
    {
        render (out, numCh, numFrames, readIndex, frac, step);
    }

    applyFadeIn (numFrames, fadeLen);

    const double pos = frac + (double) numFrames * step;
    const auto consumed = (int64_t) std::floor (pos);
    readIndex += consumed;
    frac = pos - (double) consumed;
    publishRelease();

    correctionAtomic.store (correction, std::memory_order_relaxed);
    driftAtomic.store (integral, std::memory_order_relaxed);
}

//==============================================================================
AudioBridge::Stats AudioBridge::getStats() const noexcept
{
    Stats s;
    s.underruns     = underruns.load (std::memory_order_relaxed);
    s.droppedBlocks = droppedBlocks.load (std::memory_order_relaxed);
    s.resyncs       = resyncs.load (std::memory_order_relaxed);
    s.safetyMs      = currentSafetyMs();
    s.bufferedMs    = bufferedFrames.load (std::memory_order_relaxed) * 1000.0
                        / juce::jmax (1.0, inputRate.load (std::memory_order_relaxed));
    s.correctionPpm = correctionAtomic.load (std::memory_order_relaxed) * 1.0e6;
    s.driftPpm      = driftAtomic.load (std::memory_order_relaxed) * 1.0e6;
    s.running       = runningAtomic.load (std::memory_order_relaxed);
    return s;
}
