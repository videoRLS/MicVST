#include "audio/AudioBridge.h"
#include "audio/SpliceAlign.h"
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
    // The proportional part only moves the level back to the target (the integral carries the
    // real clock drift). Bounded to +-0.05 % (0.9 cent, well below the ~0.2 % pitch difference
    // limen): after a disturbance the latency returns slowly instead of the pitch bending audibly
    // (up to 1.2.0: 200 ppm per ms of excess, i.e. 0.5 % for seconds after an output stall).
    constexpr double maxProportional = 0.0005;
    constexpr double levelTau       = 0.3;     // seconds, smoothing of the measured level ...
    // ... and a second stage for the speed correction itself. The measured level jitters by about
    // a millisecond from pull to pull (thread wake-ups); through the proportional term every pull
    // then changed the playback speed by a few ppm - phase noise on every tone, rising with its
    // frequency (about -84 dB at 1 kHz, -60 dB at 10 kHz in 43 ms windows). Two stages
    // (0.3 s + 2 s) leave changes far below 0.1 ppm per pull; the loop (time constant ~5 s) keeps
    // a phase margin of > 60 degrees.
    constexpr double errorTau       = 2.0;
    constexpr double fadeSeconds    = 0.002;   // fade-out / fade-in around gaps
    // Larger excess (an output stall: the device played silence while the input kept coming) is
    // trimmed at once: if the level stayed > 20 ms above the target for 150 ms, the read position
    // jumps by the smallest excess seen in that time, crossfaded over 10 ms at the splice point
    // that matches the waveform best (searched +-6 ms around the ideal one).
    constexpr double trimExcessSeconds  = 0.020;
    constexpr double trimHoldSeconds    = 0.150;
    constexpr double spliceSeconds      = 0.010;
    constexpr double spliceShiftSeconds = 0.006;
    constexpr int    maxAlignFrames     = 16384;   // scratch size for the alignment search
    constexpr double producerTimeout = 0.5;    // no push for this long = producer not running
    constexpr double integrateBelow  = 0.003;  // anti-windup: only small errors teach the drift ...
    constexpr double settleSeconds   = 1.5;    // no drift learning right after a (re-)prime or trim:
                                               // the catch-up after a stall is not clock drift
    // ... and none after a disturbance (start, trim, a sudden level step of > 3 ms within a second)
    // until the level has been back within 0.5 ms of the target or has stopped moving (< 0.1 ms in
    // a second): playing off an excess is not clock drift either. A larger error is learned only once it has not shrunk for 3 s in a row
    // (a clock that differs by more than the proportional range, > ~500 ppm): then up to 20 ms
    // of it count.
    constexpr double recoveredBelow  = 0.0005;
    constexpr double lateLimit       = 0.005;   // a block arriving > 5 ms later than the normal pipeline delay ...
    constexpr double steadySeconds   = 2.0;     // ... pauses drift learning until 2 s without one
    constexpr double jumpSeconds     = 0.003;
    constexpr double integrateLimit  = 0.020;

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
    alignRef.assign ((size_t) maxAlignFrames, 0.0f);
    alignCand.assign ((size_t) maxAlignFrames, 0.0f);
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
    if (isActive)
    {
        holdRequested.store (false, std::memory_order_release);
        restartRequested.store (true, std::memory_order_release);
    }
    consumerActive.store (isActive, std::memory_order_release);
}

void AudioBridge::fadeOutAndHold() noexcept
{
    holdRequested.store (true, std::memory_order_release);
}

void AudioBridge::resume() noexcept
{
    if (holdRequested.exchange (false, std::memory_order_acq_rel))
        restartRequested.store (true, std::memory_order_release);
}

void AudioBridge::setSafety (double ms, bool autoMode) noexcept
{
    ms = juce::jlimit (minSafetyMs, autoMaxMs * 2.0, ms);
    const double before = currentSafetyMs();
    safetyMsSetting.store (ms, std::memory_order_relaxed);
    autoSafetyMs.store (ms, std::memory_order_relaxed);
    autoSafety.store (autoMode, std::memory_order_relaxed);

    // A clearly larger margin: refill now (fade-out, short silence, fade-in) - the slow level
    // correction would need about 2 s per ms.
    if (currentSafetyMs() > before + 2.0)
        reprimeRequested.store (true, std::memory_order_release);
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
void AudioBridge::push (const float* const* data, int numChannels, int numFrames, double timeSeconds,
                        double arrivalSeconds) noexcept
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
        // The queued audio now jumps at w (this block is lost): the consumer must not play
        // across that point without a fade.
        discontinuityIndex.store (w, std::memory_order_release);
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

    // Normal delay between capture and arrival (plug-in thread wake-up + processing): the
    // smallest of the last 16 blocks, so that a block arriving that much after its capture time
    // is not taken for a late one (see the extrapolation in pullBlock). The smallest, because a
    // backlog (plug-ins slower than real time) is real lateness, not the pipeline's normal delay.
    const double thisLag = arrivalSeconds >= timeSeconds ? juce::jmin (0.5, arrivalSeconds - timeSeconds) : 0.0;
    lagHistory[(size_t) lagCursor] = thisLag;
    lagCursor = (lagCursor + 1) % (int) lagHistory.size();
    lagCount = juce::jmin (lagCount + 1, (int) lagHistory.size());
    const double lag = *std::min_element (lagHistory.begin(), lagHistory.begin() + lagCount);

    // seqlock publish of (end index, time, lag)
    const auto seq = pushSeq.load (std::memory_order_relaxed);
    pushSeq.store (seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence (std::memory_order_release);
    pushEndIndex.store (w + numFrames, std::memory_order_relaxed);
    pushTime.store (timeSeconds, std::memory_order_relaxed);
    pushArrival.store (arrivalSeconds >= timeSeconds ? arrivalSeconds : timeSeconds, std::memory_order_relaxed);
    pushLag.store (lag, std::memory_order_relaxed);
    pushLate.store (thisLag - lag, std::memory_order_relaxed);
    lastPushFrames.store (numFrames, std::memory_order_relaxed);
    pushSeq.store (seq + 2, std::memory_order_release);
}

bool AudioBridge::readLastPush (LastPush& p) const noexcept
{
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const auto s1 = pushSeq.load (std::memory_order_acquire);
        if ((s1 & 1u) != 0 || s1 == 0) continue;
        p.endIndex = pushEndIndex.load (std::memory_order_relaxed);
        p.time     = pushTime.load (std::memory_order_relaxed);
        p.arrival  = pushArrival.load (std::memory_order_relaxed);
        p.lag      = pushLag.load (std::memory_order_relaxed);
        p.late     = pushLate.load (std::memory_order_relaxed);
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

    // Renders n samples at the read position; while a trim is in progress, crossfaded from the
    // audio before the splice point (spliceOffset frames earlier, same fractional position).
    auto renderOut = [&] (int n, double st)
    {
        render (out, numCh, n, readIndex, frac, st);
        if (spliceRemaining <= 0 || n <= 0)
            return;

        const int m = juce::jmin (n, spliceRemaining);
        std::array<float*, maxChannels> old {};
        for (int c = 0; c < numCh; ++c)
            old[(size_t) c] = scratchA[(size_t) c].data();
        render (old.data(), numCh, m, readIndex - spliceOffset, frac, st);

        const int done = spliceLength - spliceRemaining;
        for (int i = 0; i < m; ++i)
        {
            float gOld = 0.0f, gNew = 1.0f;
            splice::gains ((double) (done + i + 1) / (double) (spliceLength + 1), spliceEqualGain, gOld, gNew);
            for (int c = 0; c < numCh; ++c)
                if (out[c] != nullptr)
                    out[c][i] = gOld * old[(size_t) c][i] + gNew * out[c][i];
        }
        spliceRemaining -= m;
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
        excessHeldSeconds = 0.0;
        spliceRemaining = 0;
        resetTrend();
        reprimeRequested.store (false, std::memory_order_relaxed);
    }

    auto publishRelease = [&]
    {
        const int64_t oldest = spliceRemaining > 0 ? readIndex - spliceOffset : readIndex;
        releaseIndex.store (oldest - (int64_t) h - 1, std::memory_order_release);
    };

    const double safetyFrames = currentSafetyMs() * inHz / 1000.0;
    const double blockIn = (double) lastPushFrames.load (std::memory_order_relaxed);
    int64_t avail = w - readIndex;

    // Time-continuous view of the producer: frames written by its last push plus the frames
    // captured since then. Those are counted from the push's ARRIVAL plus the normal capture ->
    // arrival delay of the pipeline (the plug-in thread), and at most one block plus that delay
    // (beyond, the producer is late). So the estimate does not jump by a whole block whenever the
    // two device clocks change their phase, does not dip during the pipeline's normal delay
    // (counted from the capture time with a one-block limit, it did: as the clock phases slid,
    // that read as a slow level change - a speed wander of up to 0.04 %), and does not count
    // audio still waiting in a backlog in front of the plug-ins - that arrives late.
    LastPush last;
    const bool havePush = readLastPush (last);
    const int64_t lastEnd = last.endIndex;
    const bool producerAlive = havePush && (timeSeconds - last.arrival) < producerTimeout;
    const double lagFrames = havePush ? juce::jmin (blockIn, last.lag * inHz) : 0.0;
    const double extrap = havePush ? juce::jlimit (0.0, blockIn + lagFrames, (timeSeconds - last.arrival) * inHz + lagFrames) : 0.0;

    // Fade reserve: a block is only rendered normally if, after it, at least one fade length of
    // audio is left. When the queue runs dry, the last rendered block can therefore always fade
    // out smoothly (avail shrinks by at most one block per pull) - never a hard cut to zero.
    auto reserveFor = [&] (double st) { return (int64_t) std::ceil ((double) fadeLen * st) + 2; };

    const bool hold = holdRequested.load (std::memory_order_acquire);

    //--------------------------------------------------------------------------- priming
    if (state == State::priming)
    {
        reprimeRequested.store (false, std::memory_order_relaxed);   // priming uses the new margin anyway
        const double step0 = nominal * (1.0 + integral);
        const double need0 = (double) (framesNeeded (0.0, step0, numFrames) + reserveFor (step0));
        const double target = need0 + safetyFrames + blockIn - extrap;

        // While priming the output is silent and older audio is stale anyway: keep only what the
        // start needs. Otherwise a long output stall during priming lets the ring fill up, the
        // producer's pushes (and with them its timestamps) are dropped, "producer alive" turns
        // false and priming would wait forever.
        const auto keep = (int64_t) std::ceil (target + blockIn);
        if (avail > keep)
        {
            readIndex = w - keep;
            avail = keep;
        }

        // Input was dropped (ring overflow): only audio after the gap is usable.
        const int64_t disc = discontinuityIndex.load (std::memory_order_acquire);
        if (disc > readIndex)
        {
            readIndex = disc;
            avail = w - disc;
        }

        if (hold || ! producerAlive || (double) avail < target)
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
        excessHeldSeconds = 0.0;
        spliceRemaining = 0;
        sinceStartSeconds = 0.0;
        resetTrend();
        fadeInRemaining = fadeLen;
        state = State::running;
        runningAtomic.store (true, std::memory_order_relaxed);
        starts.fetch_add (1, std::memory_order_relaxed);
    }

    //--------------------------------------------------------------------------- running
    double step = nominal * (1.0 + correction);
    int64_t need = framesNeeded (frac, step, numFrames) + reserveFor (step);

    // Fades out over (at most) one fade length and restarts with priming: used when the queued
    // audio jumps (input dropped) and when a larger safety margin was chosen.
    auto fadeOutAndReprime = [&]
    {
        const int k = avail < need ? 0 : juce::jmin (numFrames, fadeLen);
        if (k > 0)
        {
            renderOut (k, step);
            applyFadeIn (k, fadeLen);
            for (int c = 0; c < numCh; ++c)
                if (out[c] != nullptr)
                    for (int i = 0; i < k; ++i)
                        out[c][i] *= 1.0f - (float) (i + 1) / (float) k;

            const double pos = frac + (double) k * step;   // played (faded out): not again after the refill
            const auto consumed = (int64_t) std::floor (pos);
            readIndex += consumed;
            frac = pos - (double) consumed;
        }
        clearFrom (k);
        spliceRemaining = 0;
        state = State::priming;
        runningAtomic.store (false, std::memory_order_relaxed);
        publishRelease();
    };

    // Input blocks were dropped while the output stalled for longer than the ring lasts: the
    // queued audio jumps at the drop point. Fade out now and re-prime from the audio after it
    // (the stall itself was an audible gap anyway; this avoids a click on top of it).
    // (The device usually continues with the block it had already taken before freezing, so
    // the stale audio is faded out rather than cut.)
    if (discontinuityIndex.load (std::memory_order_acquire) > readIndex)
    {
        fadeOutAndReprime();
        return;
    }

    if (hold || reprimeRequested.exchange (false, std::memory_order_acq_rel))
    {
        fadeOutAndReprime();   // on hold, priming then stays silent
        return;
    }

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
            renderOut (k, step);
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
        spliceRemaining = 0;
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

        const double alpha2 = 1.0 - std::exp (-dt / errorTau);
        if (levelEmaValid)
        {
            levelEma += alpha * ((level - target) - levelEma);
            errorSmooth += alpha2 * (levelEma - errorSmooth);
        }
        else
        {
            levelEma = errorSmooth = level - target;
        }
        levelEmaValid = true;
        sinceStartSeconds += dt;

        const double err  = errorSmooth / inHz;   // seconds; > 0 = more queued than needed
        const double fast = levelEma / inHz;      // the same, less smoothed: shows what the level does now

        // Disturbance / drift classification on the less smoothed error (the strongly smoothed
        // one still rises for seconds after a step - that must not look like a drifting clock).
        if (std::abs (fast) < recoveredBelow)
            recovered = true;

        trendSeconds += dt;
        if (trendSeconds >= 1.0)
        {
            if (std::abs (fast - trendFast) < 0.0001)
                recovered = true;                                             // settled (with drift: off target)
            if (std::abs (fast - trendFast) > jumpSeconds)
                recovered = false;                                            // a step: a disturbance
            const bool persists = std::abs (fast) > std::abs (trendFast) - 0.0002   // not being played off
                                    && (fast > 0.0) == (trendFast > 0.0);
            // The tail of a recovery (the proportional part playing a disturbance off: the error
            // shrinks by ~20 % per second) is not drift either; a drifting clock keeps the error
            // nearly constant (it shrinks only as fast as the drift is learned, ~2.5 % per second).
            shrinking = std::abs (trendFast) > 0.0001 && std::abs (fast) < 0.9 * std::abs (trendFast);
            persistingSeconds = persists ? persistingSeconds + trendSeconds : 0.0;
            trendFast = fast;
            trendSeconds = 0.0;
        }

        // Blocks arriving late (the plug-ins fall behind, e.g. under a render's CPU load) make the
        // level look low until the backlog arrives: that is not clock drift either. Learning waits
        // until the input has arrived on time for steadySeconds (the level is still held by the
        // proportional part meanwhile).
        steadyInputSeconds = last.late > lateLimit ? 0.0 : steadyInputSeconds + dt;

        if (recovered && sinceStartSeconds >= settleSeconds && steadyInputSeconds >= steadySeconds)
        {
            if (std::abs (err) < integrateBelow)
            {
                if (! shrinking)
                    integral += ki * err * dt;
            }
            else if (persistingSeconds >= 3.0)
                integral += ki * juce::jlimit (-integrateLimit, integrateLimit, err) * dt;
            integral = juce::jlimit (-maxCorrection, maxCorrection, integral);
        }
        const double proportional = juce::jlimit (-maxProportional, maxProportional, kp * err);
        const double newCorrection = juce::jlimit (-maxCorrection, maxCorrection, proportional + integral);
        const double newStep = nominal * (1.0 + newCorrection);
        const auto newNeed = framesNeeded (frac, newStep, numFrames) + reserveFor (newStep);

        if (newNeed <= avail)
        {
            correction = newCorrection;
            step = newStep;
            need = newNeed;
        }

        // Mean time a frame spends in the ring (Little's law): the level minus the frames still
        // on their way in (half a block plus the pipeline delay on average) minus half an output
        // block (the ring drops by one at every pull).
        bufferedFrames.store (level - blockIn * 0.5 - lagFrames - 0.5 * (double) numFrames * step, std::memory_order_relaxed);

        // Excess that does not go away (the output stalled while the input kept coming): trim it
        // once it has stayed above the threshold for trimHoldSeconds - by the smallest excess
        // seen meanwhile (the instantaneous level, not the lagging average; a burst cannot
        // make the trim too large).
        const double excess = level - target;
        if (spliceRemaining == 0 && excess > trimExcessSeconds * inHz)
        {
            excessMinFrames = excessHeldSeconds > 0.0 ? juce::jmin (excessMinFrames, excess) : excess;
            excessHeldSeconds += dt;
        }
        else
        {
            excessHeldSeconds = 0.0;
        }

        if (excessHeldSeconds >= trimHoldSeconds)
            skip = (int64_t) std::floor (excessMinFrames);
    }

    if (skip > 0)
    {
        // Splice point: within +-6 ms of the ideal skip (never more than half the safety margin
        // too far, and the new position must have all frames of this block).
        const auto shift = (int64_t) (spliceShiftSeconds * inHz);
        const int outLen = juce::jmax (1, (int) std::lround (spliceSeconds * outHz));
        const int len = juce::jmax (8, (int) std::ceil ((double) outLen * step));
        const int64_t lo = juce::jmax ((int64_t) 1, skip - shift);
        const int64_t hi = juce::jmin (juce::jmin (avail - need, avail - (int64_t) len),   // compared audio is all there
                                       skip + juce::jmin (shift, (int64_t) (0.5 * safetyFrames)));

        if (hi >= lo)
        {
            double rho = 0.0;
            const auto s = alignedSkip (juce::jlimit (lo, hi, skip), lo, hi, len, rho);

            readIndex += s;
            spliceOffset = s;
            spliceLength = spliceRemaining = outLen;
            spliceEqualGain = rho >= splice::equalGainFrom;
            resyncs.fetch_add (1, std::memory_order_relaxed);
        }

        levelEmaValid = false;   // restart the average from the trimmed level
        excessHeldSeconds = 0.0;
        sinceStartSeconds = 0.0;
        resetTrend();
    }

    renderOut (numFrames, step);
    applyFadeIn (numFrames, fadeLen);

    const double pos = frac + (double) numFrames * step;
    const auto consumed = (int64_t) std::floor (pos);
    readIndex += consumed;
    frac = pos - (double) consumed;
    publishRelease();

    correctionAtomic.store (correction, std::memory_order_relaxed);
    driftAtomic.store (integral, std::memory_order_relaxed);
}

int64_t AudioBridge::alignedSkip (int64_t desired, int64_t lo, int64_t hi, int len, double& rho) noexcept
{
    // Mono (sum of both channels) of the audio that would play next and of the candidates.
    rho = 0.0;
    const int64_t span = hi - lo + len;
    if (len > maxAlignFrames || span > maxAlignFrames || hi < lo)
        return desired;

    const float* l = ring[0].data();
    const float* r = ring[1].data();
    const int64_t a = readIndex & ringMask, b = (readIndex + lo) & ringMask;   // mirrored ring: contiguous
    float* ref  = alignRef.data();
    float* cand = alignCand.data();
    for (int i = 0; i < len; ++i)            ref[i]  = l[a + i] + r[a + i];
    for (int64_t i = 0; i < span; ++i)       cand[i] = l[b + i] + r[b + i];

    return lo + splice::bestOffset (ref, cand, (int) (hi - lo), len, (int) (desired - lo), rho);
}

//==============================================================================
AudioBridge::Stats AudioBridge::getStats() const noexcept
{
    Stats s;
    s.underruns     = underruns.load (std::memory_order_relaxed);
    s.droppedBlocks = droppedBlocks.load (std::memory_order_relaxed);
    s.resyncs       = resyncs.load (std::memory_order_relaxed);
    s.starts        = starts.load (std::memory_order_relaxed);
    s.safetyMs      = currentSafetyMs();
    s.bufferedMs    = bufferedFrames.load (std::memory_order_relaxed) * 1000.0
                        / juce::jmax (1.0, inputRate.load (std::memory_order_relaxed));
    s.inputDelayMs  = pushLag.load (std::memory_order_relaxed) * 1000.0;
    s.correctionPpm = correctionAtomic.load (std::memory_order_relaxed) * 1.0e6;
    s.driftPpm      = driftAtomic.load (std::memory_order_relaxed) * 1.0e6;
    s.running       = runningAtomic.load (std::memory_order_relaxed);
    return s;
}
