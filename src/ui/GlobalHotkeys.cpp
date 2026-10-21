#include "ui/GlobalHotkeys.h"
#include "Text.h"

#if JUCE_WINDOWS
 #include <windows.h>

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
        for (int a = 0; a < numActions; ++a) unregisterKey (a);
        if (hwnd != nullptr) DestroyWindow (hwnd);
    }

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
        if (hwnd == nullptr || ! b.enabled || b.keyCode == 0) return false;
        const UINT vk = (UINT) (b.keyCode & 0xffff);
        registered[action] = RegisterHotKey (hwnd, 0x4d00 + action, toMods (b.modifiers), vk) != FALSE;
        return registered[action];
    }
    void unregisterKey (int action)
    {
        if (registered[action] && hwnd != nullptr) UnregisterHotKey (hwnd, 0x4d00 + action);
        registered[action] = false;
    }

    static LRESULT CALLBACK wndProc (HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == WM_HOTKEY)
            if (auto* self = (Impl*) GetWindowLongPtrW (h, GWLP_USERDATA))
            {
                const int action = (int) wp - 0x4d00;
                if (juce::isPositiveAndBelow (action, (int) numActions) && self->owner.onPressed)
                    self->owner.onPressed ((Action) action);
                return 0;
            }
        return DefWindowProcW (h, msg, wp, lp);
    }

    GlobalHotkeys& owner;
    HWND hwnd = nullptr;
    bool registered[numActions] {};
};

bool GlobalHotkeys::supported() { return true; }
#else
struct GlobalHotkeys::Impl
{
    explicit Impl (GlobalHotkeys&) {}
    bool registerKey (int, const Binding&) { return false; }
};
bool GlobalHotkeys::supported() { return false; }
#endif

GlobalHotkeys::GlobalHotkeys() : impl (std::make_unique<Impl> (*this)) {}
GlobalHotkeys::~GlobalHotkeys() = default;

void GlobalHotkeys::suspend (bool s)
{
    for (int a = 0; a < numActions; ++a)
    {
        if (s) { impl->registerKey (a, Binding{}); active[a] = false; }
        else   active[a] = impl->registerKey (a, bindings[a]);
    }
}

bool GlobalHotkeys::set (Action a, const Binding& b)
{
    bindings[a] = b;
    active[a] = impl->registerKey ((int) a, b);
    return active[a] || ! b.enabled;
}

GlobalHotkeys::Binding GlobalHotkeys::defaultFor (Action a)
{
    Binding b;
    b.enabled = (a == mute);   // Sustur: F9 on; Önce/Sonra: F10, off until the user turns it on
    b.keyCode = a == mute ? juce::KeyPress::F9Key : juce::KeyPress::F10Key;
    b.modifiers = 0;
    return b;
}

juce::String GlobalHotkeys::describe (const Binding& b)
{
    if (b.keyCode == 0) return micvst::tr ("(yok)");
    return juce::KeyPress (b.keyCode, juce::ModifierKeys (b.modifiers), 0).getTextDescription();
}
