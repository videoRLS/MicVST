#pragma once

// Small OS-level helpers for realtime audio. Deliberately free of JUCE and <windows.h> in the
// header (windows.h pollutes JUCE headers with macros), implemented in RealtimeSupport.cpp.
namespace rt
{
    struct ProcessSetup
    {
        bool priorityRaised = false;            // ABOVE_NORMAL_PRIORITY_CLASS
        bool executionThrottlingOff = false;    // EcoQoS / power throttling disabled
        bool timerThrottlingOff = false;        // timer resolution honoured while hidden (Win 11)
    };

    // Main MicVST process: keep Windows from treating the (often hidden, tray-only) audio
    // process as background work. Windows 11 may otherwise classify it as EcoQoS (slower
    // cores / lower clocks) by its own heuristics.
    ProcessSetup configureRealtimeProcess() noexcept;

    // Plug-in scan child processes: below normal priority, so a scan never competes with the game,
    // OBS or MicVST's own threads (no EcoQoS, see RealtimeSupport.cpp).
    void configureBackgroundProcess() noexcept;

    // Called at the start of every audio callback; does its work once per thread. Opts the
    // audio thread out of power throttling (complements the MMCSS registration done by JUCE).
    void configureAudioThread() noexcept;
}
