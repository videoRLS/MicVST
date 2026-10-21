#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

// System-wide shortcuts (they work while MicVST is in the background or only in the tray), one
// per action, each either a key (with Ctrl / Alt / Shift) or a mouse button: the middle button,
// the side buttons "back" (mouse 4) and "forward" (mouse 5), or the wheel up / down together with
// Ctrl / Alt / Shift (the plain wheel stays scrolling). Left and right click are never shortcuts.
//
// Windows: keys with RegisterHotKey on a hidden message-only window; mouse buttons with a low-level
// mouse hook (WH_MOUSE_LL) on its own thread with its own message loop - the hook only reads the
// button and the modifier keys and posts a message, so it always answers at once (Windows removes a
// slow hook) and never waits for MicVST's window. The hook is installed only while a mouse shortcut
// is on (or a new one is being chosen). A shortcut's key or button goes to MicVST only - other
// programs do not receive it while it is set - so every shortcut can be switched off and changed.
// Other systems: nothing is registered (no-op).
class GlobalHotkeys
{
public:
    enum Action { mute = 0, rawCompare = 1, numActions = 2 };
    enum Mouse { noMouse = 0, middleButton = 1, backButton = 2, forwardButton = 3, wheelUp = 4, wheelDown = 5 };

    struct Binding
    {
        bool enabled = false;
        int keyCode = 0;        // keyboard: juce::KeyPress key code (Windows: virtual-key code + JUCE's extended flag)
        int modifiers = 0;      // juce::ModifierKeys flags (shift / ctrl / alt), for keys and mouse buttons
        int mouse = noMouse;    // mouse: Mouse (then keyCode is 0)
        bool isMouse() const { return mouse != noMouse; }
        bool isSet() const   { return mouse != noMouse || keyCode != 0; }
        bool operator== (const Binding& o) const
            { return enabled == o.enabled && keyCode == o.keyCode && modifiers == o.modifiers && mouse == o.mouse; }
    };

    GlobalHotkeys();
    ~GlobalHotkeys();

    // Message thread. Returns false if the shortcut could not be set (Windows refused the key -
    // another program has it - or the mouse hook could not be installed); the binding is kept, but
    // inactive.
    bool set (Action, const Binding&);
    const Binding& get (Action a) const { return bindings[a]; }
    bool isActive (Action a) const { return active[a]; }

    // While a new shortcut is being chosen: every shortcut released (so the old one can be chosen
    // again), and mouse buttons are reported to onMouseCaptured instead of acting.
    void suspend (bool);
    void startMouseCapture (std::function<void (int mouse, int modifiers)> onMouseCaptured);
    void stopMouseCapture();

    std::function<void (Action)> onPressed;

    static juce::String describe (const Binding&);   // "Ctrl + F9", "Fare 5 (ileri)"
    static Binding defaultFor (Action);
    static bool supported();

    // config.xml: "enabled:keyCode:modifiers:mouse" (older: without ":mouse"); anything else = the default
    static juce::String encode (const Binding&);
    static Binding decode (const juce::String&, Action);
    // the same key or button with the same modifier keys (enabled or not)
    static bool sameShortcut (const Binding&, const Binding&);
    // modifier keys as they count for shortcuts (shift / ctrl / alt only)
    static int shortcutModifiers (int juceModifierFlags);

    struct Impl;
private:
    std::unique_ptr<Impl> impl;
    Binding bindings[numActions];
    bool active[numActions] {};
    bool suspended = false;
    void apply (int action);
};
