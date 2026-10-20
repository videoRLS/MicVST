#include "audio/RealtimeSupport.h"

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
 #if defined (_M_X64) || defined (_M_IX86) || defined (__x86_64__) || defined (__i386__)
  #define MICVST_HAS_TSC 1
  #if defined (_MSC_VER)
   #include <intrin.h>
  #else
   #include <x86intrin.h>
  #endif
 #endif

namespace rt
{
    namespace
    {
        // The power-throttling API is resolved at run time and described with its ABI values
        // instead of the SDK declarations: those depend on the SDK version and on _WIN32_WINNT,
        // and the features only exist on newer Windows builds (thread throttling: Windows 10
        // 1709, timer-resolution flag: Windows 11). A missing piece simply reports "not applied".
        struct PowerThrottlingState   // = PROCESS_POWER_THROTTLING_STATE / THREAD_POWER_THROTTLING_STATE
        {
            ULONG Version;
            ULONG ControlMask;
            ULONG StateMask;
        };

        constexpr int   processPowerThrottlingClass = 4;    // PROCESS_INFORMATION_CLASS::ProcessPowerThrottling
        constexpr int   threadPowerThrottlingClass  = 3;    // THREAD_INFORMATION_CLASS::ThreadPowerThrottling
        constexpr ULONG throttlingVersion           = 1;    // *_POWER_THROTTLING_CURRENT_VERSION
        constexpr ULONG executionSpeed              = 0x1;  // *_POWER_THROTTLING_EXECUTION_SPEED
        constexpr ULONG ignoreTimerResolution       = 0x4;  // PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION

        using SetInformationFn = BOOL (WINAPI*) (HANDLE, int, LPVOID, DWORD);
        using QueryCycleTimeFn = BOOL (WINAPI*) (HANDLE, PULONG64);

        template <typename Fn>
        Fn kernelFunction (const char* name) noexcept
        {
            if (auto* kernel = GetModuleHandleW (L"kernel32.dll"))
                if (auto* proc = GetProcAddress (kernel, name))
                    return reinterpret_cast<Fn> (reinterpret_cast<void*> (proc));
            return nullptr;
        }

        // Resolved once at program start, so the audio threads never touch the loader.
        const SetInformationFn setProcessInformationFn = kernelFunction<SetInformationFn> ("SetProcessInformation");
        const SetInformationFn setThreadInformationFn  = kernelFunction<SetInformationFn> ("SetThreadInformation");
       #ifdef MICVST_HAS_TSC
        const QueryCycleTimeFn queryThreadCycleTimeFn  = kernelFunction<QueryCycleTimeFn> ("QueryThreadCycleTime");
       #endif

        bool setProcessThrottling (ULONG controlMask, ULONG stateMask) noexcept
        {
            if (setProcessInformationFn == nullptr)
                return false;

            PowerThrottlingState state { throttlingVersion, controlMask, stateMask };
            return setProcessInformationFn (GetCurrentProcess(), processPowerThrottlingClass,
                                            &state, (DWORD) sizeof (state)) != FALSE;
        }

        void optOutOfThreadThrottling() noexcept
        {
            if (setThreadInformationFn == nullptr)
                return;

            PowerThrottlingState state { throttlingVersion, executionSpeed, 0 };   // HighQoS
            setThreadInformationFn (GetCurrentThread(), threadPowerThrottlingClass, &state, (DWORD) sizeof (state));
        }

        // avrt.dll (MMCSS), loaded from System32 only, on first use (when the plug-in thread
        // starts - never inside the audio loop).
        struct Avrt
        {
            using SetCharacteristicsFn = HANDLE (WINAPI*) (LPCWSTR, LPDWORD);
            using SetPriorityFn        = BOOL (WINAPI*) (HANDLE, int);
            using RevertFn             = BOOL (WINAPI*) (HANDLE);

            Avrt()
            {
                if (auto* dll = LoadLibraryExW (L"avrt.dll", nullptr, 0x00000800 /*LOAD_LIBRARY_SEARCH_SYSTEM32*/))
                {
                    setCharacteristics = reinterpret_cast<SetCharacteristicsFn> (reinterpret_cast<void*> (GetProcAddress (dll, "AvSetMmThreadCharacteristicsW")));
                    setPriority        = reinterpret_cast<SetPriorityFn>        (reinterpret_cast<void*> (GetProcAddress (dll, "AvSetMmThreadPriority")));
                    revert             = reinterpret_cast<RevertFn>             (reinterpret_cast<void*> (GetProcAddress (dll, "AvRevertMmThreadCharacteristics")));
                }
            }

            SetCharacteristicsFn setCharacteristics = nullptr;
            SetPriorityFn        setPriority = nullptr;
            RevertFn             revert = nullptr;
        };

        const Avrt& avrt()
        {
            static const Avrt instance;
            return instance;
        }

        constexpr int avrtPriorityNormal = 0;   // AVRT_PRIORITY_NORMAL (JUCE's device threads: HIGH = 1)
    }

    ProcessSetup configureRealtimeProcess() noexcept
    {
        ProcessSetup r;
        // "HighQoS": ControlMask selects the mechanism, StateMask = 0 turns it off. Two separate
        // calls, so an older Windows that does not know the timer flag still applies the first.
        r.executionThrottlingOff = setProcessThrottling (executionSpeed, 0);
        r.timerThrottlingOff     = setProcessThrottling (ignoreTimerResolution, 0);
        return r;
    }

    bool processPrioritySupported() noexcept { return true; }

    int currentProcessPriority() noexcept
    {
        switch (GetPriorityClass (GetCurrentProcess()))
        {
            case NORMAL_PRIORITY_CLASS:       return (int) ProcessPriority::normal;
            case ABOVE_NORMAL_PRIORITY_CLASS: return (int) ProcessPriority::aboveNormal;
            case HIGH_PRIORITY_CLASS:         return (int) ProcessPriority::high;
            default:                          return -1;
        }
    }

    PriorityResult setProcessPriority (ProcessPriority p) noexcept
    {
        // Normal, above normal or high - never REALTIME: that would put MicVST's window and
        // device threads above Windows' own input and audio handling. None of the three needs a
        // privilege (only REALTIME requires SeIncreaseBasePriorityPrivilege), but a job object
        // or a policy can still refuse or override it, so the class is read back. Not inherited
        // by the scan child processes (Windows passes only IDLE/BELOW_NORMAL on), which set
        // their own below-normal class anyway (configureBackgroundProcess).
        const DWORD wanted = p == ProcessPriority::high        ? HIGH_PRIORITY_CLASS
                           : p == ProcessPriority::aboveNormal ? ABOVE_NORMAL_PRIORITY_CLASS
                                                               : NORMAL_PRIORITY_CLASS;
        PriorityResult r;
        SetLastError (0);
        const bool called = SetPriorityClass (GetCurrentProcess(), wanted) != FALSE;
        r.error  = called ? 0 : GetLastError();
        r.actual = currentProcessPriority();
        r.ok     = called && r.actual == (int) p;
        return r;
    }

    void configureBackgroundProcess() noexcept
    {
        // Below normal only - deliberately NOT EcoQoS: on hybrid CPUs EcoQoS pins the process to
        // the efficiency cores even when the machine is idle, and a huge shell plug-in (WaveShell)
        // could then run into the scan timeout. The audio threads are MMCSS anyway.
        SetPriorityClass (GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);
    }

    void configureAudioThread() noexcept
    {
        thread_local bool configured = false;
        if (configured)
            return;
        configured = true;
        optOutOfThreadThrottling();
    }

    //==============================================================================
    ProcessingThreadSetup::ProcessingThreadSetup() noexcept
    {
        optOutOfThreadThrottling();

        const auto& a = avrt();
        if (a.setCharacteristics == nullptr)
            return;

        DWORD taskIndex = 0;
        if (auto h = a.setCharacteristics (L"Pro Audio", &taskIndex))
        {
            if (a.setPriority != nullptr)
                a.setPriority (h, avrtPriorityNormal);
            handle = h;
        }
    }

    ProcessingThreadSetup::~ProcessingThreadSetup() noexcept
    {
        if (handle != nullptr && avrt().revert != nullptr)
            avrt().revert ((HANDLE) handle);
    }

    //==============================================================================
    WakeEvent::WakeEvent()
        : impl (CreateEventW (nullptr, FALSE /*auto-reset*/, FALSE, nullptr))
    {
    }

    WakeEvent::~WakeEvent()
    {
        if (impl != nullptr)
            CloseHandle ((HANDLE) impl);
    }

    void WakeEvent::signal() noexcept
    {
        if (impl != nullptr)
            SetEvent ((HANDLE) impl);
    }

    bool WakeEvent::wait (int timeoutMs) noexcept
    {
        if (impl == nullptr)
        {
            Sleep ((DWORD) (timeoutMs > 0 ? timeoutMs : 0));
            return false;
        }
        return WaitForSingleObject ((HANDLE) impl, (DWORD) (timeoutMs >= 0 ? timeoutMs : INFINITE)) == WAIT_OBJECT_0;
    }

    //==============================================================================
    ThreadTimes threadTimes() noexcept
    {
       #ifdef MICVST_HAS_TSC
        ULONG64 cycles = 0;
        if (queryThreadCycleTimeFn == nullptr || ! queryThreadCycleTimeFn (GetCurrentThread(), &cycles))
            return {};
        return { (uint64_t) __rdtsc(), (uint64_t) cycles };
       #else
        return {};
       #endif
    }
}

#else

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <time.h>
#if defined (__linux__)
 #include <cerrno>
 #include <semaphore.h>
#endif

namespace rt
{
    ProcessSetup configureRealtimeProcess() noexcept { return {}; }
    bool processPrioritySupported() noexcept { return false; }
    int currentProcessPriority() noexcept { return -1; }
    PriorityResult setProcessPriority (ProcessPriority) noexcept { return {}; }
    void configureBackgroundProcess() noexcept {}
    void configureAudioThread() noexcept {}

    ProcessingThreadSetup::ProcessingThreadSetup() noexcept {}
    ProcessingThreadSetup::~ProcessingThreadSetup() noexcept {}

   #if defined (__linux__)
    // Semaphore: signal() is one atomic operation (a futex wake when someone waits), it never
    // blocks on a lock - like SetEvent on Windows. Several signals before a wait wake it once.
    WakeEvent::WakeEvent()
    {
        auto* s = new sem_t;
        if (sem_init (s, 0, 0) != 0) { delete s; s = nullptr; }
        impl = s;
    }
    WakeEvent::~WakeEvent()
    {
        if (auto* s = static_cast<sem_t*> (impl)) { sem_destroy (s); delete s; }
    }

    void WakeEvent::signal() noexcept
    {
        auto* s = static_cast<sem_t*> (impl);
        int value = 0;
        if (s != nullptr && sem_getvalue (s, &value) == 0 && value == 0)
            sem_post (s);
    }

    bool WakeEvent::wait (int timeoutMs) noexcept
    {
        auto* s = static_cast<sem_t*> (impl);
        if (s == nullptr)
            return false;
        int r = 0;
        if (timeoutMs < 0)
        {
            while ((r = sem_wait (s)) != 0 && errno == EINTR) {}
        }
        else
        {
            timespec until {};
            clock_gettime (CLOCK_REALTIME, &until);
            until.tv_sec  += timeoutMs / 1000;
            until.tv_nsec += (long) (timeoutMs % 1000) * 1000000L;
            if (until.tv_nsec >= 1000000000L) { until.tv_nsec -= 1000000000L; ++until.tv_sec; }
            while ((r = sem_timedwait (s, &until)) != 0 && errno == EINTR) {}
        }
        while (sem_trywait (s) == 0) {}   // auto-reset: one wake-up for any number of signals
        return r == 0;
    }
   #else
    namespace
    {
        struct PosixEvent
        {
            std::mutex mutex;
            std::condition_variable cv;
            bool flag = false;
        };
    }

    WakeEvent::WakeEvent() : impl (new PosixEvent()) {}
    WakeEvent::~WakeEvent() { delete static_cast<PosixEvent*> (impl); }

    void WakeEvent::signal() noexcept
    {
        auto* e = static_cast<PosixEvent*> (impl);
        {
            const std::lock_guard<std::mutex> lock (e->mutex);
            e->flag = true;
        }
        e->cv.notify_one();
    }

    bool WakeEvent::wait (int timeoutMs) noexcept
    {
        auto* e = static_cast<PosixEvent*> (impl);
        std::unique_lock<std::mutex> lock (e->mutex);
        if (timeoutMs < 0)
            e->cv.wait (lock, [e] { return e->flag; });
        else
            e->cv.wait_for (lock, std::chrono::milliseconds (timeoutMs), [e] { return e->flag; });
        const bool signalled = e->flag;
        e->flag = false;
        return signalled;
    }
   #endif

    ThreadTimes threadTimes() noexcept
    {
        timespec wall {}, cpu {};
        if (clock_gettime (CLOCK_MONOTONIC, &wall) != 0 || clock_gettime (CLOCK_THREAD_CPUTIME_ID, &cpu) != 0)
            return {};
        return { (uint64_t) wall.tv_sec * 1000000000ull + (uint64_t) wall.tv_nsec,
                 (uint64_t) cpu.tv_sec  * 1000000000ull + (uint64_t) cpu.tv_nsec };
    }
}

#endif
