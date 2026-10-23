// The engine test's message pump, timed with the performance counter. JUCE's
// MessageManager::runDispatchLoopUntil() measures its time-out with the wall clock
// (Time::currentTimeMillis): when Windows' clock is set back during a call (test R5 sets it back by
// a day), the call waits until the wall clock catches up - the test hung, not the engine. MicVST
// itself runs JUCE's endless dispatch loop and its timers on the monotonic counter.
#include <juce_events/juce_events.h>
#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

void pumpMonotonic (double seconds)
{
   #if JUCE_WINDOWS
    // JUCE's messages are window messages to its hidden message window: dispatching the thread's queue runs them
    const double end = juce::Time::getMillisecondCounterHiRes() + seconds * 1000.0;
    for (;;)
    {
        MSG m;
        while (PeekMessageW (&m, nullptr, 0, 0, PM_REMOVE))
        {
            if (m.message == WM_QUIT) { PostQuitMessage ((int) m.wParam); return; }
            TranslateMessage (&m);
            DispatchMessageW (&m);
        }
        const double left = end - juce::Time::getMillisecondCounterHiRes();
        if (left <= 0.0)
            return;
        MsgWaitForMultipleObjectsEx (0, nullptr, (DWORD) juce::jlimit (1.0, 10.0, left), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
   #else
    juce::MessageManager::getInstance()->runDispatchLoopUntil ((int) (seconds * 1000.0));
   #endif
}
