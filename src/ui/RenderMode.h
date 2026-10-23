#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// "Arayüz çizimi": how MicVST's own windows (main window, settings) are drawn. Interface only -
// nothing here touches the audio engine, its threads, the sound or its timing.
//
//   Otomatik (default): the graphics card (Direct2D) when this computer has a usable one (checked
//     once: a Direct3D 11 hardware device with Direct2D support can be created); otherwise the
//     processor (GDI). While a full-screen program runs (Windows reports it) or the screens change,
//     the windows are drawn by the processor, and 3 s after that a fresh graphics-card drawing is
//     set up: after a full-screen game (GTA 5) the old Direct2D picture sat shifted by the title bar
//     (white strip) - a picture made after the game is right. More than 6 switches in 10 minutes:
//     the processor for the rest of the session (unstable graphics card).
//   Yalnızca ekran kartı: always Direct2D; if no usable graphics card was found the user is told
//     (Settings, log) and it is still Direct2D, as asked.
//   Yalnızca işlemci: always GDI.
//
// Switches happen on the message thread only, outside paint, through JUCE's
// ComponentPeer::setCurrentRenderingEngine (which frees the old drawing resources first).
namespace micvst::render
{
    enum class Mode { automatic, gpuOnly, cpuOnly };

    juce::String toId (Mode);                    // "" (automatic, the default: not written), "gpu", "cpu"
    Mode fromId (const juce::String&);          // anything else: automatic

    // the drawing engine for a window: 0 = processor (GDI), 1 = graphics card (Direct2D)
    int chooseEngine (Mode, bool gpuUsable, bool risky, bool braked);

    // flap brake: true when more than maxSwitches switches fall into the last windowMs
    struct SwitchBrake
    {
        static constexpr int maxSwitches = 6;
        static constexpr juce::uint32 windowMs = 10 * 60 * 1000;
        bool note (juce::uint32 nowMs);          // a switch happened; returns true once the brake is on
        bool on = false;
        juce::Array<juce::uint32> times;
    };

    Mode current();
    void setMode (Mode);                          // applies to every window at once
    bool gpuUsable();                             // checked once, cached
    juce::String problem();                       // set when "Yalnızca ekran kartı" has no usable graphics card

    // MicVST's own windows: register when shown (repeatable); forgotten when deleted
    void attach (juce::Component& window);
    void screensChanged();                        // a top-level window's parentSizeChanged()
    juce::String describeNow();                   // for the settings: what draws now and why
}
