#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// "Arayüz çizimi": how MicVST's own windows (main window, settings) are drawn. Interface only -
// nothing here touches the audio engine, its threads, the sound or its timing.
//
// Otomatik is decided once per start and fixed until MicVST closes:
//   Otomatik (default): the graphics card (Direct2D) if a Direct3D 11 hardware device with Direct2D
//     support (not Windows' Basic Render Driver) can be created - checked at every start, in an SEH
//     guard - otherwise the processor (GDI).
//   Yalnızca ekran kartı: Direct2D; without a usable card the user is told, no fallback (as asked).
//   Yalnızca işlemci: GDI.
// Choosing Yalnızca ekran kartı / Yalnızca işlemci in the settings applies at once, in the same
// windows (only the engine is swapped and everything redrawn - no rebuild, no blink). Choosing
// Otomatik is saved and decided at the next start. At start the engine is set before a window's
// first paint (ComponentPeer::setCurrentRenderingEngine).
namespace micvst::render
{
    enum class Mode { automatic, gpuOnly, cpuOnly };

    juce::String toId (Mode);                    // "" (automatic, the default: not written), "gpu", "cpu"
    Mode fromId (const juce::String&);          // anything else: automatic

    // the drawing engine: 0 = processor (GDI), 1 = graphics card (Direct2D)
    int chooseEngine (Mode, bool gpuUsable);

    void startSession (Mode);                     // once at start: decides this session's engine
    Mode current();                               // the saved choice (may wait for the next start)
    void setMode (Mode);                          // saved; GPU / CPU only at once, Otomatik at the next start
    bool gpuUsable();                             // checked once per start, cached
    juce::String problem();                       // "Yalnızca ekran kartı" without a usable card
    void attach (juce::Component& window);        // a window was shown: this session's engine
    juce::String describeNow();                   // for the settings: what draws now (and what waits)
    int sessionEngine();                          // 0 / 1 for this start

    // "Arayüz akıcılığı": how often the spectrum and level meters are redrawn (on the screen's
    // refresh; their motion runs in real time, the same at every value) - 60 / 30 / 20 / 10 FPS with the
    // processor, 60 / 30 with the graphics card; default 30 in both (also whatever Otomatik picks).
    // The list follows the engine at once; the value applies at once; only the drawing - the audio
    // path never reads it. A saved value the engine does not offer gives 30 (and comes back if the
    // engine offering it is chosen again).
    juce::Array<int> fpsChoices();                // for this start's engine
    int fps();                                    // the one in use (an unavailable saved value: 30)
    int savedFps();                               // 0 = default
    void setFps (int);                            // 0 or a value from fpsChoices()

    // "Spektrumu ve ses seviyesi göstergelerini göster": the spectrum and the input / output meters
    // shown or hidden (default shown). Hidden, nothing is analysed or drawn; the audio engine
    // measures and processes exactly the same.
    bool metersVisible();
    void setMetersVisible (bool);
}
