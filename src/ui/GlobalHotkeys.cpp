#include "ui/GlobalHotkeys.h"
#include "Text.h"
#include <atomic>

#if JUCE_WINDOWS
 #include <windows.h>

namespace
{
    constexpr UINT msgAction = WM_APP + 41, msgCaptured = WM_APP + 42;
}

struct GlobalHotkeys::Impl
{
    explicit Impl (GlobalHotkeys& o) : owner (o)
    {
        WNDCLASSW wc {};
        wc.lpfnWndProc = &Impl::wndProc;
        wc.hInstance = GetModuleHandleW (nullptr);
        wc.lpszClassName = L"MicVSTHotkeys";
        RegisterClassW (&wc);   // fails harmlessly if already registered
        hwnd = CreateWindowExW (0, L"MicVSTHotkeys", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
        if (hwnd != nullptr)
            SetWindowLongPtrW (hwnd, GWLP_USERDATA, (LONG_PTR) this);
    }
    ~Impl()
    {
        stopHook();
        for (int a = 0; a < numActions; ++a) unregisterKey (a);
        if (hwnd != nullptr) DestroyWindow (hwnd);
    }

    // --- keys ---------------------------------------------------------------------------------
    static UINT toMods (int m)
    {
        UINT r = MOD_NOREPEAT;
        if (m & juce::ModifierKeys::shiftModifier)   r |= MOD_SHIFT;
        if (m & juce::ModifierKeys::ctrlModifier)    r |= MOD_CONTROL;
        if (m & juce::ModifierKeys::altModifier)     r |= MOD_ALT;
        return r;
    }
    bool registerKey (int action, const Binding& b)
    {
        unregisterKey (action);
        if (hwnd == nullptr || ! b.enabled || b.isMouse() || b.keyCode == 0) return false;
        const UINT vk = (UINT) (b.keyCode & 0xffff);
        registered[action] = RegisterHotKey (hwnd, 0x4d00 + action, toMods (b.modifiers), vk) != FALSE;
        return registered[action];
    }
    void unregisterKey (int action)
    {
        if (registered[action] && hwnd != nullptr) UnregisterHotKey (hwnd, 0x4d00 + action);
        registered[action] = false;
    }

    // --- mouse: low-level hook on its own thread ------------------------------------------------
    struct HookThread final : juce::Thread
    {
        explicit HookThread (Impl& i) : juce::Thread ("MicVST mouse shortcuts"), impl (i) {}
        void run() override
        {
            threadId.store (GetCurrentThreadId());
            MSG m;
            PeekMessageW (&m, nullptr, WM_USER, WM_USER, PM_NOREMOVE);   // the thread's message queue exists now
            HHOOK hook = SetWindowsHookExW (WH_MOUSE_LL, &Impl::hookProc, GetModuleHandleW (nullptr), 0);
            installed.store (hook != nullptr);
            ready.signal();
            if (hook == nullptr) return;
            while (GetMessageW (&m, nullptr, 0, 0) > 0)
            {
                TranslateMessage (&m);
                DispatchMessageW (&m);
            }
            UnhookWindowsHookEx (hook);
        }
        Impl& impl;
        std::atomic<DWORD> threadId { 0 };
        std::atomic<bool> installed { false };
        juce::WaitableEvent ready;
    };

    static inline std::atomic<Impl*> hookOwner { nullptr };

    // read by the hook (any time), written by the message thread
    std::atomic<int> mouseOf[numActions] {}, modsOf[numActions] {};
    std::atomic<bool> capturing { false };
    std::atomic<int> swallowedDowns { 0 };   // buttons whose press was taken: their release is taken too

    std::unique_ptr<HookThread> hookThread;

    bool startHook()
    {
        if (hookThread != nullptr) return hookThread->installed.load();
        hookOwner.store (this);
        hookThread = std::make_unique<HookThread> (*this);
        hookThread->startThread (juce::Thread::Priority::highest);
        hookThread->ready.wait (2000);
        if (! hookThread->installed.load())
        {
            stopHook();
            return false;
        }
        return true;
    }
    void stopHook()
    {
        if (hookThread == nullptr) return;
        if (const auto id = hookThread->threadId.load()) PostThreadMessageW (id, WM_QUIT, 0, 0);
        hookThread->stopThread (2000);
        hookThread = nullptr;
        swallowedDowns.store (0);
        Impl* expected = this;
        hookOwner.compare_exchange_strong (expected, nullptr);
    }

    static int currentModifiers()
    {
        int m = 0;
        if (GetAsyncKeyState (VK_SHIFT) & 0x8000)   m |= juce::ModifierKeys::shiftModifier;
        if (GetAsyncKeyState (VK_CONTROL) & 0x8000) m |= juce::ModifierKeys::ctrlModifier;
        if (GetAsyncKeyState (VK_MENU) & 0x8000)    m |= juce::ModifierKeys::altModifier;
        return m;
    }

    // The hook: only reads and posts - nothing here may wait.
    bool handleMouse (WPARAM msg, const MSLLHOOKSTRUCT& m)
    {
        int button = noMouse;
        bool up = false;
        switch (msg)
        {
            case WM_MBUTTONDOWN: button = middleButton; break;
            case WM_MBUTTONUP:   button = middleButton; up = true; break;
            case WM_XBUTTONDOWN: button = HIWORD (m.mouseData) == XBUTTON1 ? backButton : forwardButton; break;
            case WM_XBUTTONUP:   button = HIWORD (m.mouseData) == XBUTTON1 ? backButton : forwardButton; up = true; break;
            case WM_MOUSEWHEEL:  button = (short) HIWORD (m.mouseData) > 0 ? wheelUp : wheelDown; break;
            default: return false;
        }
        const int bit = 1 << button;
        if (up)
        {
            if ((swallowedDowns.load() & bit) == 0) return false;
            swallowedDowns.fetch_and (~bit);
            return true;
        }
        const int mods = currentModifiers();
        const bool wheel = button == wheelUp || button == wheelDown;
        if (capturing.load())
        {
            if (wheel && mods == 0) return false;   // the plain wheel keeps scrolling
            if (! wheel) swallowedDowns.fetch_or (bit);
            PostMessageW (hwnd, msgCaptured, (WPARAM) button, (LPARAM) mods);
            return true;
        }
        for (int a = 0; a < numActions; ++a)
            if (mouseOf[a].load() == button && modsOf[a].load() == mods)
            {
                if (! wheel) swallowedDowns.fetch_or (bit);
                PostMessageW (hwnd, msgAction, (WPARAM) a, 0);
                return true;
            }
        return false;
    }

    static LRESULT CALLBACK hookProc (int code, WPARAM wp, LPARAM lp)
    {
        if (code == HC_ACTION)
            if (auto* self = hookOwner.load())
                if (self->handleMouse (wp, *reinterpret_cast<const MSLLHOOKSTRUCT*> (lp)))
                    return 1;   // taken: the button does nothing else
        return CallNextHookEx (nullptr, code, wp, lp);
    }

    static LRESULT CALLBACK wndProc (HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (auto* self = (Impl*) GetWindowLongPtrW (h, GWLP_USERDATA))
        {
            if (msg == WM_HOTKEY || msg == msgAction)
            {
                const int action = msg == WM_HOTKEY ? (int) wp - 0x4d00 : (int) wp;
                if (juce::isPositiveAndBelow (action, (int) numActions) && self->owner.onPressed)
                    self->owner.onPressed ((Action) action);
                return 0;
            }
            if (msg == msgCaptured)
            {
                if (self->onCaptured) self->onCaptured ((int) wp, (int) lp);
                return 0;
            }
        }
        return DefWindowProcW (h, msg, wp, lp);
    }

    // which mouse shortcuts the hook acts on; the hook runs only while one is on or one is chosen
    bool updateMouse (const Binding* b, bool suspended)
    {
        bool any = false;
        for (int a = 0; a < numActions; ++a)
        {
            const bool on = ! suspended && b[a].enabled && b[a].isMouse();
            mouseOf[a].store (on ? b[a].mouse : noMouse);
            modsOf[a].store (shortcutModifiers (b[a].modifiers));
            any = any || on;
        }
        if (any || capturing.load()) return startHook();
        stopHook();
        return true;
    }

    GlobalHotkeys& owner;
    HWND hwnd = nullptr;
    bool registered[numActions] {};
    std::function<void (int, int)> onCaptured;
};

bool GlobalHotkeys::supported() { return true; }

void GlobalHotkeys::apply (int a)
{
    const auto& b = bindings[a];
    if (b.isMouse())
    {
        impl->unregisterKey (a);
        active[a] = impl->updateMouse (bindings, suspended) && b.enabled && ! suspended;
    }
    else
    {
        active[a] = ! suspended && impl->registerKey (a, b);
        impl->updateMouse (bindings, suspended);
    }
}

void GlobalHotkeys::suspend (bool s)
{
    suspended = s;
    for (int a = 0; a < numActions; ++a)
    {
        if (s) { impl->unregisterKey (a); active[a] = false; }
        else   apply (a);
    }
    impl->updateMouse (bindings, suspended);
}

void GlobalHotkeys::startMouseCapture (std::function<void (int, int)> cb)
{
    impl->onCaptured = std::move (cb);
    impl->capturing.store (true);
    impl->updateMouse (bindings, suspended);
}

void GlobalHotkeys::stopMouseCapture()
{
    impl->capturing.store (false);
    impl->onCaptured = nullptr;
    impl->updateMouse (bindings, suspended);
}

#else
struct GlobalHotkeys::Impl
{
    explicit Impl (GlobalHotkeys&) {}
};
bool GlobalHotkeys::supported() { return false; }
void GlobalHotkeys::apply (int a)            { active[a] = false; }
void GlobalHotkeys::suspend (bool s)         { suspended = s; }
void GlobalHotkeys::startMouseCapture (std::function<void (int, int)>) {}
void GlobalHotkeys::stopMouseCapture()       {}
#endif

GlobalHotkeys::GlobalHotkeys() : impl (std::make_unique<Impl> (*this)) {}
GlobalHotkeys::~GlobalHotkeys() = default;

bool GlobalHotkeys::set (Action a, const Binding& b)
{
    bindings[a] = b;
    bindings[a].modifiers = shortcutModifiers (b.modifiers);
    if (b.isMouse()) bindings[a].keyCode = 0;
    apply ((int) a);
    return active[a] || ! b.enabled || suspended;
}

int GlobalHotkeys::shortcutModifiers (int m)
{
    return m & (juce::ModifierKeys::shiftModifier | juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier);
}

bool GlobalHotkeys::sameShortcut (const Binding& x, const Binding& y)
{
    if (! x.isSet() || ! y.isSet()) return false;
    if (x.isMouse() != y.isMouse()) return false;
    const bool sameButton = x.isMouse() ? x.mouse == y.mouse : x.keyCode == y.keyCode;
    return sameButton && shortcutModifiers (x.modifiers) == shortcutModifiers (y.modifiers);
}

GlobalHotkeys::Binding GlobalHotkeys::defaultFor (Action a)
{
    Binding b;
    b.enabled = (a == mute);   // Sustur: F9 on; Ham ses: F10, off until the user turns it on
    b.keyCode = a == mute ? juce::KeyPress::F9Key : juce::KeyPress::F10Key;
    return b;
}

juce::String GlobalHotkeys::encode (const Binding& b)
{
    return juce::String (b.enabled ? 1 : 0) + ":" + juce::String (b.keyCode) + ":" + juce::String (b.modifiers)
         + ":" + juce::String (b.mouse);
}

GlobalHotkeys::Binding GlobalHotkeys::decode (const juce::String& text, Action a)
{
    const auto parts = juce::StringArray::fromTokens (text, ":", "");
    if (parts.size() != 3 && parts.size() != 4) return defaultFor (a);
    for (auto& p : parts)
        if (p.isEmpty() || ! p.trimCharactersAtStart ("-").containsOnly ("0123456789")) return defaultFor (a);
    Binding b;
    b.enabled = parts[0].getIntValue() != 0;
    b.keyCode = parts[1].getIntValue();
    b.modifiers = shortcutModifiers (parts[2].getIntValue());
    b.mouse = parts.size() == 4 ? parts[3].getIntValue() : (int) noMouse;
    if (b.mouse < noMouse || b.mouse > wheelDown) return defaultFor (a);
    const bool wheel = b.mouse == wheelUp || b.mouse == wheelDown;
    if (wheel && b.modifiers == 0) return defaultFor (a);   // the plain wheel is never a shortcut
    if (b.isMouse()) b.keyCode = 0;
    if (! b.isSet()) return defaultFor (a);
    return b;
}

juce::String GlobalHotkeys::describe (const Binding& b)
{
    if (! b.isSet()) return micvst::tr ("(yok)");
    juce::String mods;
    if (b.modifiers & juce::ModifierKeys::ctrlModifier)  mods << "Ctrl + ";
    if (b.modifiers & juce::ModifierKeys::altModifier)   mods << "Alt + ";
    if (b.modifiers & juce::ModifierKeys::shiftModifier) mods << "Shift + ";
    if (! b.isMouse())
        return mods + juce::KeyPress (b.keyCode, juce::ModifierKeys(), 0).getTextDescription().toUpperCase();
    switch (b.mouse)
    {
        case middleButton:  return mods + micvst::tr ("Orta fare tuşu");
        case backButton:    return mods + micvst::tr ("Fare 4 (geri)");
        case forwardButton: return mods + micvst::tr ("Fare 5 (ileri)");
        case wheelUp:       return mods + micvst::tr ("Tekerlek yukarı");
        case wheelDown:     return mods + micvst::tr ("Tekerlek aşağı");
        default: break;
    }
    return micvst::tr ("(yok)");
}
