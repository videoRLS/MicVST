#pragma once
#include "audio/SpliceAlign.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

// Single-producer / single-consumer queue between the microphone's device thread and MicVST's
// plug-in thread:
//
//   WASAPI capture thread --write()--> [InputQueue] --readBlock()--> plug-in thread --> AudioBridge
//
// Why (1.2.1): in 1.2.0 the plug-in chain ran on the capture thread itself. Windows keeps only
// about 20 ms of captured audio for an application; whenever plug-in blocks took longer than that
// (CPU contention, e.g. a GPU/AI video render in the background), the capture buffer overflowed
// and the audio was lost inside Windows ("microphone driver gaps") - clicks that no safety buffer
// can repair, because the audio never reached MicVST. Now the capture thread only copies each
// block in here (microseconds) and returns to WASAPI at once: a slow plug-in block delays the
// audio instead of losing it, and the bridge's safety buffer absorbs the delay.
//
// The queue is bounded on purpose. If the plug-in chain stays behind for longer than maxBacklog
// (it needs more CPU time than real time), readBlock() skips the oldest audio with a crossfade
// from the oldest into (about) the newest block, at the point where the waveforms match best
// (SpliceAlign.h): the stream stays continuous (no click, no gap) and the latency stays bounded
// instead of growing as long as the overload lasts.
//
// Threading: write() - producer only; available(), readBlock(), lastReadCaptureTime() - consumer
// only; reset() only while neither side runs; getOverflows() - any thread.
class InputQueue
{
public:
    static constexpr int channels = 2;
    static constexpr int capacity = 1 << 16;   // frames (1.36 s at 48 kHz)

    // How far the plug-in chain may fall behind before readBlock() skips: enough to refill the
    // bridge at once after a stall even with the largest safety buffer (80 ms + one block + the
    // resampler's look-ahead), small enough to keep an overload from piling up latency.
    static constexpr double maxBacklogSeconds = 0.12;
    static int64_t backlogLimit (double sampleRate, int blockSize) noexcept
    {
        return std::max<int64_t> (4 * (int64_t) blockSize, (int64_t) std::ceil (maxBacklogSeconds * sampleRate));
    }

    // How far (frames) a skip may stay before the newest block to find a matching splice point.
    static int alignRange (double sampleRate) noexcept { return (int) std::ceil (0.006 * sampleRate); }

    InputQueue()
    {
        for (auto& c : ring)
            c.assign ((size_t) capacity, 0.0f);
        alignRef.assign ((size_t) maxAlignFrames, 0.0f);
        alignCand.assign ((size_t) maxAlignFrames, 0.0f);
    }

    void reset() noexcept
    {
        writeIndex.store (0, std::memory_order_relaxed);
        readIndex.store (0, std::memory_order_relaxed);
        stampSeq.store (0, std::memory_order_relaxed);
        stampEnd.store (0, std::memory_order_relaxed);
        stampTime.store (0.0, std::memory_order_relaxed);
        lastReadEnd = 0;
    }

    //==============================================================================
    // Producer. Channels beyond numChannels (or null pointers) are stored as silence. Returns
    // false and drops the block if it does not fit (the consumer has been stuck for > 1 s).
    bool write (const float* const* data, int numChannels, int numFrames, double timeSeconds) noexcept
    {
        if (numFrames <= 0)
            return true;

        const int64_t w = writeIndex.load (std::memory_order_relaxed);
        const int64_t r = readIndex.load (std::memory_order_acquire);

        if (numFrames > capacity || w + numFrames - r > capacity)
        {
            overflows.fetch_add (1, std::memory_order_relaxed);
            return false;
        }

        const int pos   = (int) (w & mask);
        const int first = std::min (numFrames, capacity - pos);
        const int rest  = numFrames - first;

        for (int c = 0; c < channels; ++c)
        {
            float* dst = ring[(size_t) c].data();
            const float* src = (data != nullptr && c < numChannels) ? data[c] : nullptr;

            if (src != nullptr)
            {
                std::memcpy (dst + pos, src, (size_t) first * sizeof (float));
                std::memcpy (dst, src + first, (size_t) rest * sizeof (float));
            }
            else
            {
                std::fill (dst + pos, dst + pos + first, 0.0f);
                std::fill (dst, dst + rest, 0.0f);
            }
        }

        writeIndex.store (w + numFrames, std::memory_order_release);

        // seqlock publish of (end index, capture time) of this block
        const auto s = stampSeq.load (std::memory_order_relaxed);
        stampSeq.store (s + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        stampEnd.store (w + numFrames, std::memory_order_relaxed);
        stampTime.store (timeSeconds, std::memory_order_relaxed);
        stampSeq.store (s + 2, std::memory_order_release);
        return true;
    }

    //==============================================================================
    // Consumer
    int64_t available() const noexcept
    {
        return writeIndex.load (std::memory_order_acquire) - readIndex.load (std::memory_order_relaxed);
    }

    // Reads the next numFrames of both channels into dst[0], dst[1]. If more than maxBacklog
    // frames are waiting, the block crossfades from the oldest queued audio into the newest block
    // (or up to alignFrames before it, where the waveform matches best) and everything in between
    // is dropped. Returns the number of frames skipped that way (0 = ordinary read), or -1 if
    // fewer than numFrames are queued.
    int64_t readBlock (float* const* dst, int numFrames, int64_t maxBacklog, int alignFrames = 0) noexcept
    {
        const int64_t w = writeIndex.load (std::memory_order_acquire);
        const int64_t r = readIndex.load (std::memory_order_relaxed);
        const int64_t avail = w - r;

        if (numFrames <= 0 || numFrames > capacity / 2 || avail < numFrames)
            return -1;

        if (avail <= std::max<int64_t> (maxBacklog, 2 * (int64_t) numFrames))
        {
            for (int c = 0; c < channels; ++c)
                copyOut (c, r, dst[c], numFrames);

            readIndex.store (r + numFrames, std::memory_order_release);
            lastReadEnd = r + numFrames;
            return 0;
        }

        // The chain fell behind by more than maxBacklog: splice to the newest block - or a little
        // before it, where the new audio continues the old waveform best.
        const int64_t newest = w - numFrames;
        int64_t from = newest;
        bool equalGain = false;
        const auto range = std::min<int64_t> (alignFrames, newest - (r + numFrames));   // stays after the old block

        if (range > 0 && (int64_t) numFrames + range <= maxAlignFrames)
        {
            float* ref  = alignRef.data();
            float* cand = alignCand.data();
            const int64_t lo = newest - range;
            for (int i = 0; i < numFrames; ++i)                        ref[i]  = at (0, r + i) + at (1, r + i);
            for (int64_t i = 0; i < range + numFrames; ++i)            cand[i] = at (0, lo + i) + at (1, lo + i);
            double rho = 0.0;
            from = lo + splice::bestOffset (ref, cand, (int) range, numFrames, (int) range, rho);
            equalGain = rho >= splice::equalGainFrom;
        }

        for (int i = 0; i < numFrames; ++i)
        {
            float gOld = 1.0f, gNew = 0.0f;
            splice::gains (((double) i + 0.5) / (double) numFrames, equalGain, gOld, gNew);
            for (int c = 0; c < channels; ++c)
                dst[c][i] = gOld * at (c, r + i) + gNew * at (c, from + i);
        }

        readIndex.store (from + numFrames, std::memory_order_release);
        lastReadEnd = from + numFrames;
        return from - r;
    }

    // Capture time (producer clock) of the newest frame returned by the last readBlock(): the
    // time of the latest write, corrected by the audio queued after that frame.
    bool lastReadCaptureTime (double sampleRate, double& time) const noexcept
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const auto s1 = stampSeq.load (std::memory_order_acquire);
            if ((s1 & 1u) != 0 || s1 == 0)
                continue;

            const int64_t end = stampEnd.load (std::memory_order_relaxed);
            const double  t   = stampTime.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);

            if (stampSeq.load (std::memory_order_relaxed) == s1)
            {
                time = t - (double) (end - lastReadEnd) / std::max (1.0, sampleRate);
                return true;
            }
        }
        return false;
    }

    uint64_t getOverflows() const noexcept { return overflows.load (std::memory_order_relaxed); }

private:
    static constexpr int64_t mask = capacity - 1;
    static constexpr int64_t maxAlignFrames = 16384;

    float at (int c, int64_t index) const noexcept   { return ring[(size_t) c][(size_t) (index & mask)]; }

    void copyOut (int c, int64_t from, float* dst, int n) const noexcept
    {
        const int pos   = (int) (from & mask);
        const int first = std::min (n, capacity - pos);
        const float* src = ring[(size_t) c].data();
        std::memcpy (dst, src + pos, (size_t) first * sizeof (float));
        std::memcpy (dst + first, src, (size_t) (n - first) * sizeof (float));
    }

    std::array<std::vector<float>, channels> ring;
    std::atomic<int64_t> writeIndex { 0 };   // producer-owned
    std::atomic<int64_t> readIndex { 0 };    // consumer-owned

    std::atomic<uint32_t> stampSeq { 0 };
    std::atomic<int64_t>  stampEnd { 0 };
    std::atomic<double>   stampTime { 0.0 };

    std::atomic<uint64_t> overflows { 0 };
    int64_t lastReadEnd = 0;                 // consumer only
    std::vector<float> alignRef, alignCand;  // consumer only, preallocated
};
