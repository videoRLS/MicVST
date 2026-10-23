#pragma once
#include <cstdint>

// Small OS-level helpers for realtime audio. Deliberately free of JUCE and <windows.h> in the
// header (windows.h pollutes JUCE headers with macros), implemented in RealtimeSupport.cpp.
namespace rt
{
    struct ProcessSetup
    {
        bool executionThrottlingOff = false;    // EcoQoS / power throttling disabled
        bool timerThrottlingOff = false;        // timer resolution honoured while hidden (Win 11)
    };

    // Main MicVST process: keep Windows from treating the (often hidden, tray-only) audio
    // process as background work. Windows 11 may otherwise classify it as EcoQoS (slower
    // cores / lower clocks) by its own heuristics. The priority class is not touched here
    // (setProcessPriority: the user's choice, Normal by default).
    ProcessSetup configureRealtimeProcess() noexcept;

    // The process priority class the user picks in MicVST (saved in config.xml as this number).
    // It does not affect the audio threads: the capture, plug-in and output threads run in the
    // MMCSS "Pro Audio" task (realtime priority band) whatever the class is. It moves MicVST's
    // other threads (window, meters, device changes, saving, plug-in windows) relative to other
    // programs' threads.
    enum class ProcessPriority : int { normal = 0, aboveNormal = 1, high = 2 };
    constexpr int numProcessPriorities = 3;

    struct PriorityResult
    {
        bool ok = false;           // the process now has exactly the requested class
        int  actual = -1;          // the class it has now (as ProcessPriority, -1 = another class)
        unsigned long error = 0;   // GetLastError() when Windows refused
    };

    // Windows only (elsewhere: not supported, nothing happens). Message thread.
    bool           processPrioritySupported() noexcept;
    PriorityResult setProcessPriority (ProcessPriority) noexcept;
    // The class the process has now: 0..2 as ProcessPriority, -1 for any other class (idle,
    // below normal, realtime - set from outside, e.g. in Task Manager) or where not supported.
    int            currentProcessPriority() noexcept;

    // Plug-in scan child processes: below normal priority, so a scan never competes with the game,
    // OBS or MicVST's own threads (no EcoQoS, see RealtimeSupport.cpp).
    void configureBackgroundProcess() noexcept;

    // Called at the start of every audio callback; does its work once per thread. Opts the
    // audio thread out of power throttling (complements the MMCSS registration done by JUCE).
    void configureAudioThread() noexcept;

    // MicVST's own plug-in thread, for its whole lifetime (create it first thing in the thread):
    // MMCSS "Pro Audio" one step below the WASAPI device threads (those run at HIGH, so the light
    // capture/output work always gets in first), plus the power-throttling opt-out. If MMCSS
    // refuses, the thread keeps the time-critical priority it was started with.
    class ProcessingThreadSetup
    {
    public:
        ProcessingThreadSetup() noexcept;
        ~ProcessingThreadSetup() noexcept;
        bool mmcssActive() const noexcept { return handle != nullptr; }

    private:
        void* handle = nullptr;
        ProcessingThreadSetup (const ProcessingThreadSetup&) = delete;
        ProcessingThreadSetup& operator= (const ProcessingThreadSetup&) = delete;
    };

    // Auto-reset event that wakes the plug-in thread when the capture thread has queued audio.
    // signal() is realtime-safe on Windows (SetEvent); the POSIX fallback (used by the tests on
    // Linux) takes a mutex for a few instructions.
    class WakeEvent
    {
    public:
        WakeEvent();
        ~WakeEvent();
        void signal() noexcept;
        bool wait (int timeoutMs) noexcept;   // true = signalled, false = timed out

    private:
        void* impl = nullptr;
        WakeEvent (const WakeEvent&) = delete;
        WakeEvent& operator= (const WakeEvent&) = delete;
    };

    // Wall-clock time and CPU time of the calling thread in one common unit. Over a busy period,
    // dCpu / dWall = the share of that period the thread actually ran on a CPU; the rest it was
    // runnable but waiting (pre-empted by drivers (DPC/ISR) or by higher-priority threads).
    // Windows x64: time-stamp counter and QueryThreadCycleTime (both TSC ticks); Linux:
    // nanoseconds. Both zero where this is not available.
    struct ThreadTimes { uint64_t wall = 0, cpu = 0; };
    ThreadTimes threadTimes() noexcept;

   #if MICVST_RT_PROBE
    // Test builds only: marks the code that runs on an audio thread, so a test can catch memory
    // allocations and lock waits there (tests interpose malloc/new/mutexes). In MicVST itself
    // MICVST_REALTIME_SCOPE compiles to nothing.
    void probeEnter() noexcept;
    void probeExit() noexcept;
    struct ProbeScope { ProbeScope() noexcept { probeEnter(); } ~ProbeScope() noexcept { probeExit(); } };
   #endif
}

#if MICVST_RT_PROBE
 #define MICVST_REALTIME_SCOPE const rt::ProbeScope micvstRealtimeScope
#else
 #define MICVST_REALTIME_SCOPE
#endif
