// Realtime-safety probe for the engine test (Linux): interposes the allocator and the blocking
// pthread primitives and counts every call made while an audio thread is inside a section marked
// with MICVST_REALTIME_SCOPE (capture callback, one plug-in block, output callback).
// The first events of each kind keep their return addresses for a backtrace at the end.
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <execinfo.h>
#include <malloc.h>
#include <new>
#include <pthread.h>
#include <time.h>
#include "rtprobe.h"

extern "C" void* __libc_malloc (size_t);
extern "C" void  __libc_free (void*);
extern "C" void* __libc_calloc (size_t, size_t);
extern "C" void* __libc_realloc (void*, size_t);
extern "C" void* __libc_memalign (size_t, size_t);

namespace rt { void probeEnter() noexcept; void probeExit() noexcept; }

namespace
{
    thread_local int depth = 0;
    thread_local bool inside = false;   // reentrancy guard while recording
    std::atomic<bool> armed { false };
    std::atomic<uint64_t> contended { 0 };
    std::atomic<double> maxWaitUs { 0.0 };

    std::atomic<uint64_t> counts[(int) rtprobe::Kind::count] {};
    struct Sample { int kind; void* frames[12]; int n; };
    Sample samples[256];
    std::atomic<int> sampleCount { 0 };
    std::atomic<int> perKindSamples[(int) rtprobe::Kind::count] {};

    inline void record (rtprobe::Kind k)
    {
        if (depth <= 0 || inside || ! armed.load (std::memory_order_relaxed))
            return;
        inside = true;
        counts[(int) k].fetch_add (1, std::memory_order_relaxed);
        if (perKindSamples[(int) k].fetch_add (1) < 6)
        {
            const int i = sampleCount.fetch_add (1);
            if (i < 256)
            {
                samples[i].kind = (int) k;
                samples[i].n = backtrace (samples[i].frames, 12);   // libgcc preloaded at arm()
            }
        }
        inside = false;
    }

    template <typename F> F next (const char* name) { return reinterpret_cast<F> (dlsym (RTLD_NEXT, name)); }
}

namespace rt
{
    void probeEnter() noexcept { ++depth; }
    void probeExit() noexcept  { --depth; }
}

namespace rtprobe
{
    const char* name (Kind k)
    {
        static const char* names[] = { "malloc/new", "free/delete", "realloc", "mutex lock (blocking)", "mutex trylock",
                                       "condition wait", "sleep", "sched_yield" };
        return names[(int) k];
    }
    void arm()
    {
        contended = 0; maxWaitUs = 0.0;
        void* f[2]; backtrace (f, 2);   // loads libgcc_s now, not inside an audio section
        for (auto& c : counts) c = 0;
        for (auto& c : perKindSamples) c = 0;
        sampleCount = 0;
        armed = true;
    }
    void disarm() { armed = false; }
    uint64_t count (Kind k) { return counts[(int) k].load(); }
    void printSamples (int maxPerKind)
    {
        const int n = std::min (256, sampleCount.load());
        int shown[(int) Kind::count] = {};
        for (int i = 0; i < n; ++i)
        {
            if (shown[samples[i].kind]++ >= maxPerKind) continue;
            std::printf ("    %s at:\n", name ((Kind) samples[i].kind));
            char** sym = backtrace_symbols (samples[i].frames, samples[i].n);
            for (int j = 2; j < samples[i].n && j < 10; ++j)
                std::printf ("      %s\n", sym != nullptr ? sym[j] : "?");
            std::free (sym);
        }
    }
}

//==============================================================================
// Allocator
extern "C" void* malloc (size_t n)            { record (rtprobe::Kind::alloc); return __libc_malloc (n); }
extern "C" void  free (void* p)               { if (p != nullptr) record (rtprobe::Kind::release); __libc_free (p); }
extern "C" void* calloc (size_t a, size_t b)  { record (rtprobe::Kind::alloc); return __libc_calloc (a, b); }
extern "C" void* realloc (void* p, size_t n)  { record (rtprobe::Kind::realloc); return __libc_realloc (p, n); }
extern "C" void* memalign (size_t a, size_t n) { record (rtprobe::Kind::alloc); return __libc_memalign (a, n); }
extern "C" void* aligned_alloc (size_t a, size_t n) { record (rtprobe::Kind::alloc); return __libc_memalign (a, n); }
extern "C" int posix_memalign (void** p, size_t a, size_t n)
{
    record (rtprobe::Kind::alloc);
    *p = __libc_memalign (a, n);
    return *p != nullptr ? 0 : 12;
}

void* operator new (size_t n)                                   { record (rtprobe::Kind::alloc); if (void* p = __libc_malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new[] (size_t n)                                 { record (rtprobe::Kind::alloc); if (void* p = __libc_malloc (n ? n : 1)) return p; throw std::bad_alloc(); }
void* operator new (size_t n, const std::nothrow_t&) noexcept   { record (rtprobe::Kind::alloc); return __libc_malloc (n ? n : 1); }
void* operator new[] (size_t n, const std::nothrow_t&) noexcept { record (rtprobe::Kind::alloc); return __libc_malloc (n ? n : 1); }
void* operator new (size_t n, std::align_val_t a)               { record (rtprobe::Kind::alloc); return __libc_memalign ((size_t) a, n ? n : 1); }
void* operator new[] (size_t n, std::align_val_t a)             { record (rtprobe::Kind::alloc); return __libc_memalign ((size_t) a, n ? n : 1); }
void operator delete (void* p) noexcept                         { if (p) record (rtprobe::Kind::release); __libc_free (p); }
void operator delete[] (void* p) noexcept                       { if (p) record (rtprobe::Kind::release); __libc_free (p); }
void operator delete (void* p, size_t) noexcept                 { if (p) record (rtprobe::Kind::release); __libc_free (p); }
void operator delete[] (void* p, size_t) noexcept               { if (p) record (rtprobe::Kind::release); __libc_free (p); }
void operator delete (void* p, std::align_val_t) noexcept       { if (p) record (rtprobe::Kind::release); __libc_free (p); }
void operator delete[] (void* p, std::align_val_t) noexcept     { if (p) record (rtprobe::Kind::release); __libc_free (p); }
void operator delete (void* p, size_t, std::align_val_t) noexcept   { if (p) record (rtprobe::Kind::release); __libc_free (p); }
void operator delete[] (void* p, size_t, std::align_val_t) noexcept { if (p) record (rtprobe::Kind::release); __libc_free (p); }

//==============================================================================
// Blocking primitives
namespace rtprobe
{
    uint64_t contendedLocks() { return contended.load(); }
    double maxLockWaitUs() { return maxWaitUs.load(); }
}
extern "C" int pthread_mutex_lock (pthread_mutex_t* m)
{
    static auto real = next<int (*) (pthread_mutex_t*)> ("pthread_mutex_lock");
    static auto tryReal = next<int (*) (pthread_mutex_t*)> ("pthread_mutex_trylock");
    record (rtprobe::Kind::lock);
    if (depth > 0 && ! inside && armed.load (std::memory_order_relaxed))
    {
        if (tryReal (m) == 0)
            return 0;                       // free: no wait
        timespec a {}, b {};
        clock_gettime (CLOCK_MONOTONIC, &a);
        const int r = real (m);
        clock_gettime (CLOCK_MONOTONIC, &b);
        const double us = 1e6 * (double) (b.tv_sec - a.tv_sec) + 1e-3 * (double) (b.tv_nsec - a.tv_nsec);
        contended.fetch_add (1);
        double prev = maxWaitUs.load();
        while (us > prev && ! maxWaitUs.compare_exchange_weak (prev, us)) {}
        return r;
    }
    return real (m);
}
extern "C" int pthread_mutex_trylock (pthread_mutex_t* m)
{
    static auto real = next<int (*) (pthread_mutex_t*)> ("pthread_mutex_trylock");
    record (rtprobe::Kind::trylock);
    return real (m);
}
extern "C" int pthread_cond_wait (pthread_cond_t* c, pthread_mutex_t* m)
{
    static auto real = next<int (*) (pthread_cond_t*, pthread_mutex_t*)> ("pthread_cond_wait");
    record (rtprobe::Kind::wait);
    return real (c, m);
}
extern "C" int pthread_cond_timedwait (pthread_cond_t* c, pthread_mutex_t* m, const struct timespec* t)
{
    static auto real = next<int (*) (pthread_cond_t*, pthread_mutex_t*, const struct timespec*)> ("pthread_cond_timedwait");
    record (rtprobe::Kind::wait);
    return real (c, m, t);
}
extern "C" int nanosleep (const struct timespec* a, struct timespec* b)
{
    static auto real = next<int (*) (const struct timespec*, struct timespec*)> ("nanosleep");
    record (rtprobe::Kind::sleep);
    return real (a, b);
}
extern "C" int usleep (useconds_t u)
{
    static auto real = next<int (*) (useconds_t)> ("usleep");
    record (rtprobe::Kind::sleep);
    return real (u);
}
extern "C" int sched_yield()
{
    static auto real = next<int (*)()> ("sched_yield");
    record (rtprobe::Kind::yield);
    return real();
}
