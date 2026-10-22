#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

// The 32-bit millisecond counter (juce::Time::getMillisecondCounter; Windows timeGetTime) starts
// again from 0 every 49.7 days - with Windows' Fast Startup a shutdown does not reset it, so it
// happens to a PC that is used every day. Every use in MicVST compares unsigned differences
// (now - then >= interval), which stay right across the wrap (test "Counters R1").
//
// msCounter(): that counter plus an offset that only tests set (0 in the program: the same value),
// so a test can start just before the wrap and watch the timers go on across it.
namespace micvst
{
    inline std::atomic<juce::uint32>& msCounterOffset() noexcept
    {
        static std::atomic<juce::uint32> offset { 0 };
        return offset;
    }

    inline juce::uint32 msCounter() noexcept
    {
        return juce::Time::getMillisecondCounter() + msCounterOffset().load (std::memory_order_relaxed);
    }

    // A chosen device that is listed but cannot be opened is tried again after a pause of at most
    // maxWaitMs (AudioEngine::reconnectIfNeeded). The time left is an unsigned difference: once the
    // time has passed it is huge, so "due". (Before 1.2.2 a signed difference: a pause scheduled
    // more than 24.8 days earlier - the PC asleep meanwhile - looked like one still to come, for up
    // to 24.8 more days.)
    struct ReopenBackoff
    {
        static constexpr juce::uint32 maxWaitMs = 10000;
        juce::uint32 nextMs = 0;
        int failures = 0;

        bool due (juce::uint32 nowMs) const noexcept
        {
            const juce::uint32 left = nextMs - nowMs;
            return failures == 0 || left == 0 || left > maxWaitMs;
        }
    };
}
