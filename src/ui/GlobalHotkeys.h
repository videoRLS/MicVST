#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// System-wide shortcut keys (they work while MicVST is in the background or only in the tray):
// Windows' RegisterHotKey on a hidden message-only window. A registered key combination goes to
// MicVST only - other programs no longer receive it while it is set - so every shortcut can be
// switched off and given another key. Other systems: nothing is registered (no-op).
class GlobalHotkeys
{
public:
    enum Action { mute = 0, rawCompare = 1, numActions = 2 };

    struct Binding
    {
        bool enabled = false;
        int keyCode = 0;        // juce::KeyPress key code (on Windows: the virtual-key code + JUCE's extended flag)
        int modifiers = 0;      // juce::ModifierKeys flags (shift / ctrl / alt)
        bool operator== (const Binding& o) const { return enabled == o.enabled && keyCode == o.keyCode && modifiers == o.modifiers; }
    };

    GlobalHotkeys();
    ~GlobalHotkeys();

    // Message thread. Returns false if Windows refused the key (another program has it); the
    // binding is kept, but inactive.
    bool set (Action, const Binding&);
    const Binding& get (Action a) const { return bindings[a]; }
    bool isActive (Action a) const { return active[a]; }

    // While a new key is being chosen: every key released (so the old one can be pressed too).
    void suspend (bool);

    std::function<void (Action)> onPressed;

    static juce::String describe (const Binding&);   // "Ctrl+F9"
    static Binding defaultFor (Action);
    static bool supported();

    struct Impl;
private:
    std::unique_ptr<Impl> impl;
    Binding bindings[numActions];
    bool active[numActions] {};
};
