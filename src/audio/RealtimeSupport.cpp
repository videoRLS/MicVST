#include "audio/RealtimeSupport.h"

#if defined (_WIN32)
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>

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

        SetInformationFn kernelFunction (const char* name) noexcept
        {
            if (auto* kernel = GetModuleHandleW (L"kernel32.dll"))
                if (auto* proc = GetProcAddress (kernel, name))
                    return reinterpret_cast<SetInformationFn> (reinterpret_cast<void*> (proc));
            return nullptr;
        }

        // Resolved once at program start, so the audio threads never touch the loader.
        const SetInformationFn setProcessInformationFn = kernelFunction ("SetProcessInformation");
        const SetInformationFn setThreadInformationFn  = kernelFunction ("SetThreadInformation");

        bool setProcessThrottling (ULONG controlMask, ULONG stateMask) noexcept
        {
            if (setProcessInformationFn == nullptr)
                return false;

            PowerThrottlingState state { throttlingVersion, controlMask, stateMask };
            return setProcessInformationFn (GetCurrentProcess(), processPowerThrottlingClass,
                                            &state, (DWORD) sizeof (state)) != FALSE;
        }
    }

    ProcessSetup configureRealtimeProcess() noexcept
    {
        ProcessSetup r;
        // Above normal (not HIGH): the audio threads themselves run in the MMCSS realtime band
        // anyway; this only keeps MicVST's other threads ahead of ordinary background work
        // without starving games or OBS.
        r.priorityRaised = SetPriorityClass (GetCurrentProcess(), ABOVE_NORMAL_PRIORITY_CLASS) != FALSE;
        // "HighQoS": ControlMask selects the mechanism, StateMask = 0 turns it off. Two separate
        // calls, so an older Windows that does not know the timer flag still applies the first.
        r.executionThrottlingOff = setProcessThrottling (executionSpeed, 0);
        r.timerThrottlingOff     = setProcessThrottling (ignoreTimerResolution, 0);
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

        if (setThreadInformationFn == nullptr)
            return;

        PowerThrottlingState state { throttlingVersion, executionSpeed, 0 };   // HighQoS
        setThreadInformationFn (GetCurrentThread(), threadPowerThrottlingClass, &state, (DWORD) sizeof (state));
    }
}

#else

namespace rt
{
    ProcessSetup configureRealtimeProcess() noexcept { return {}; }
    void configureBackgroundProcess() noexcept {}
    void configureAudioThread() noexcept {}
}

#endif
