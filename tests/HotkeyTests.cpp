#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/GlobalHotkeys.h"
#include "Text.h"

// Shortcuts as they are saved in config.xml and compared: keys and mouse buttons, older files,
// damaged values, the same shortcut for both actions.
struct HotkeyTest : juce::UnitTest
{
    HotkeyTest() : juce::UnitTest ("Shortcuts (keys and mouse buttons)") {}

    using H = GlobalHotkeys;
    static H::Binding key (int code, int mods = 0, bool on = true) { H::Binding b; b.enabled = on; b.keyCode = code; b.modifiers = mods; return b; }
    static H::Binding mouse (int m, int mods = 0, bool on = true)  { H::Binding b; b.enabled = on; b.mouse = m; b.modifiers = mods; return b; }

    void runTest() override
    {
        const int ctrl = juce::ModifierKeys::ctrlModifier, alt = juce::ModifierKeys::altModifier,
                  shift = juce::ModifierKeys::shiftModifier;

        beginTest ("every key and mouse shortcut is read back exactly as it was saved");
        {
            const H::Binding all[] = {
                key (juce::KeyPress::F9Key), key (juce::KeyPress::F10Key, ctrl | shift, false), key ('M', alt),
                mouse (H::middleButton), mouse (H::backButton), mouse (H::forwardButton, ctrl, false),
                mouse (H::wheelUp, ctrl), mouse (H::wheelDown, alt | shift), mouse (H::backButton, ctrl | alt | shift)
            };
            for (auto& b : all)
                for (auto a : { H::mute, H::rawCompare })
                {
                    const auto text = H::encode (b);
                    expect (H::decode (text, a) == b, text);
                }
        }

        beginTest ("older settings files (without the mouse part) keep their key");
        {
            const juce::String f9 (juce::KeyPress::F9Key), f10 (juce::KeyPress::F10Key);
            expect (H::decode ("1:" + f9 + ":0", H::mute) == key (juce::KeyPress::F9Key));
            expect (H::decode ("0:" + f10 + ":" + juce::String (ctrl), H::rawCompare) == key (juce::KeyPress::F10Key, ctrl, false));
            expect (H::decode ("1:77:" + juce::String (alt), H::rawCompare) == key ('M', alt));
        }

        beginTest ("damaged or impossible values give the default, never a broken shortcut");
        {
            for (auto text : { "", "x", "1:2", "1:a:0", "1:120:0:9", "1:0:0:-1", "1:0:0", "1:0:0:0", "1:120:0:0:0",
                               "1:0:0:4", "1:0:0:5" /* plain wheel */ })
            {
                expect (H::decode (text, H::mute) == H::defaultFor (H::mute), text);
                expect (H::decode (text, H::rawCompare) == H::defaultFor (H::rawCompare), text);
            }
            // other modifier flags (command, mouse buttons) are not part of a shortcut
            expect (H::decode ("1:0:" + juce::String (ctrl | juce::ModifierKeys::leftButtonModifier) + ":2", H::mute)
                    == mouse (H::backButton, ctrl));
            // a mouse shortcut has no key code
            expectEquals (H::decode ("1:120:0:1", H::mute).keyCode, 0);
        }

        beginTest ("defaults: Sustur F9 on, Ham ses F10 off");
        {
            expect (H::defaultFor (H::mute) == key (juce::KeyPress::F9Key, 0, true));
            expect (H::defaultFor (H::rawCompare) == key (juce::KeyPress::F10Key, 0, false));
        }

        beginTest ("the same shortcut is found for both actions, a different one is not");
        {
            expect (H::sameShortcut (mouse (H::backButton), mouse (H::backButton, 0, false)));
            expect (H::sameShortcut (key ('M', ctrl), key ('M', ctrl)));
            expect (! H::sameShortcut (mouse (H::backButton), mouse (H::forwardButton)));
            expect (! H::sameShortcut (mouse (H::wheelUp, ctrl), mouse (H::wheelUp, alt)));
            expect (! H::sameShortcut (key ('M'), key ('M', shift)));
            expect (! H::sameShortcut (key (4), mouse (4)), "a key code is never a mouse button");
            expect (! H::sameShortcut (H::Binding(), H::Binding()), "no shortcut is never the same shortcut");
        }

        beginTest ("names: keys and mouse buttons with their modifier keys");
        {
            micvst::setLanguage ("tr");
            expectEquals (H::describe (key (juce::KeyPress::F9Key, ctrl)), juce::String ("Ctrl + F9"));
            expectEquals (H::describe (mouse (H::forwardButton)), micvst::tr ("Fare 5 (ileri)"));
            expectEquals (H::describe (mouse (H::backButton)), micvst::tr ("Fare 4 (geri)"));
            expectEquals (H::describe (mouse (H::middleButton, shift)), "Shift + " + micvst::tr ("Orta fare tuşu"));
            expectEquals (H::describe (mouse (H::wheelUp, ctrl | alt)), "Ctrl + Alt + " + micvst::tr ("Tekerlek yukarı"));
            expectEquals (H::describe (mouse (H::wheelDown, ctrl)), "Ctrl + " + micvst::tr ("Tekerlek aşağı"));
            expectEquals (H::describe (H::Binding()), micvst::tr ("(yok)"));
            for (auto lang : { "en", "de", "ja", "ar" })
            {
                micvst::setLanguage (lang);
                const auto t = H::describe (mouse (H::wheelUp, ctrl));
                expect (t.startsWith ("Ctrl + ") && t != "Ctrl + " + juce::String::fromUTF8 ("Tekerlek yukarı"), lang + juce::String (": ") + t);
            }
            micvst::setLanguage ("tr");
        }
    }
};

static HotkeyTest hotkeyTest;
