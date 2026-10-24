#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cstdint>
#include <memory>

struct LevelReading { float rms = 0.0f; float peak = 0.0f; };

// Reine Funktion: RMS/Peak über alle Kanäle eines Buffers. Testbar ohne Hardware.
LevelReading computeLevel (const juce::AudioBuffer<float>& buffer);

// The latest samples of a meter point, for the interface's spectrum and level meters only.
// The audio thread copies each block it meters (left and right; a mono block into both) into a
// ring the interface reads. Read-only towards the sound: the block is never written, nothing is
// allocated or locked on the audio thread (the ring is allocated once, in the constructor), the
// copy costs about a microsecond per block. Every element is an atomic (relaxed: plain moves on
// x64 / ARM64), so a reader racing the writer sees old or new values, never torn ones; read()
// tells whether the range it copied was overwritten meanwhile.
class SampleTap
{
public:
    static constexpr int capacity = 1 << 15;          // frames per channel (0.68 s at 48 kHz, 0.17 s at 192 kHz)
    static constexpr int maxBlock = 1 << 13;          // the largest block the writer may be in the middle of
    static constexpr int maxRead  = capacity - 2 * maxBlock;   // frames one read may ask for

    SampleTap()
        : left (std::make_unique<std::atomic<float>[]> ((size_t) capacity)),
          right (std::make_unique<std::atomic<float>[]> ((size_t) capacity))
    {
        for (int i = 0; i < capacity; ++i)
        {
            left[(size_t) i].store (0.0f, std::memory_order_relaxed);
            right[(size_t) i].store (0.0f, std::memory_order_relaxed);
        }
    }

    // audio thread: one writer per tap
    void write (const juce::AudioBuffer<float>& b) noexcept
    {
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        if (n <= 0 || ch <= 0) return;
        const float* l = b.getReadPointer (0);
        const float* r = b.getReadPointer (ch > 1 ? 1 : 0);
        auto pos = written.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i, ++pos)
        {
            const auto k = (size_t) (pos & (uint64_t) (capacity - 1));
            left[k].store (l[i], std::memory_order_relaxed);
            right[k].store (r[i], std::memory_order_relaxed);
        }
        written.store (pos, std::memory_order_release);
    }

    // interface: frames written so far (64 bit: never wraps)
    uint64_t total() const noexcept { return written.load (std::memory_order_acquire); }

    // interface: copies the n frames ending at 'end' (a value from total()); false if they are not
    // all there (too old, overwritten while copying) - then the copy must not be used
    bool read (uint64_t end, float* l, float* r, int n) const noexcept
    {
        if (n <= 0 || n > maxRead || end < (uint64_t) n) return false;
        const uint64_t start = end - (uint64_t) n;
        if (total() - start > (uint64_t) (capacity - maxBlock)) return false;   // already (partly) overwritten
        for (int i = 0; i < n; ++i)
        {
            const auto k = (size_t) ((start + (uint64_t) i) & (uint64_t) (capacity - 1));
            l[i] = left[k].load (std::memory_order_relaxed);
            r[i] = right[k].load (std::memory_order_relaxed);
        }
        // the writer may be up to one block past total() with its stores
        return total() - start <= (uint64_t) (capacity - maxBlock);
    }

private:
    std::unique_ptr<std::atomic<float>[]> left, right;
    std::atomic<uint64_t> written { 0 };
};

// Thread-sicherer Meter: process() im Audio-Thread, read() im GUI-Thread.
class LevelMeter
{
public:
    void process (const juce::AudioBuffer<float>& buffer)
    {
        const auto r = computeLevel (buffer);
        rms_.store (r.rms,  std::memory_order_relaxed);
        peak_.store (r.peak, std::memory_order_relaxed);
        tap_.write (buffer);   // read-only copy for the interface (spectrum, meters)
    }
    LevelReading read() const
    {
        return { rms_.load (std::memory_order_relaxed),
                 peak_.load (std::memory_order_relaxed) };
    }
    const SampleTap& tap() const noexcept { return tap_; }
private:
    std::atomic<float> rms_  { 0.0f };
    std::atomic<float> peak_ { 0.0f };
    SampleTap tap_;
};
