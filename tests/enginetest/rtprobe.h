#pragma once
#include <cstdint>

namespace rtprobe
{
    enum class Kind { alloc, release, realloc, lock, trylock, wait, sleep, yield, count };
    const char* name (Kind);
    void arm();
    void disarm();
    uint64_t count (Kind);
    void printSamples (int maxPerKind);
    uint64_t contendedLocks();   // blocking locks that actually had to wait
    double maxLockWaitUs();
}
