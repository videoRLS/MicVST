#include "ui/DarkTitleBar.h"

#if JUCE_WINDOWS
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace micvst
{
    void useDarkTitleBar (juce::Component& window)
    {
       #if JUCE_WINDOWS
        auto* peer = window.getPeer();
        if (peer == nullptr)
            return;
        // dwmapi.dll is always there on Windows 10/11; resolved at run time so nothing new is linked.
        // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 (Windows 10 2004 and later), 19 on earlier builds.
        using SetAttributeFn = HRESULT (WINAPI*) (HWND, DWORD, LPCVOID, DWORD);
        static const auto setAttribute = []
        {
            const auto dll = LoadLibraryExW (L"dwmapi.dll", nullptr, 0x00000800 /*LOAD_LIBRARY_SEARCH_SYSTEM32*/);
            return dll != nullptr ? reinterpret_cast<SetAttributeFn> (reinterpret_cast<void*> (GetProcAddress (dll, "DwmSetWindowAttribute"))) : nullptr;
        }();
        if (setAttribute == nullptr)
            return;
        const auto hwnd = static_cast<HWND> (peer->getNativeHandle());
        const BOOL on = TRUE;
        if (FAILED (setAttribute (hwnd, 20, &on, sizeof (on))))
            setAttribute (hwnd, 19, &on, sizeof (on));
       #else
        juce::ignoreUnused (window);
       #endif
    }
}
