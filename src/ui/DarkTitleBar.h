#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace micvst
{
    // Windows 10/11: the window's native title bar in dark mode (matches the night theme). Call
    // when the window is on the desktop (it has a peer); elsewhere nothing happens.
    void useDarkTitleBar (juce::Component& window);
}
