#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/BuiltInEffects.h"

// Every built-in effect's window opened, closed and opened again (user report: the second time
// it was empty - the processor still had its old "active editor").
struct EditorTwiceTest : juce::UnitTest
{
    EditorTwiceTest() : juce::UnitTest ("Built-in effect editors") {}
    void runTest() override
    {
        beginTest ("each effect: open, close, open again - a full editor every time");
        for (auto& info : builtin::effects())
        {
            auto e = builtin::create (info.id);
            if (e == nullptr || ! e->hasEditor()) continue;
            auto* ed1 = e->createEditorAndMakeActive();
            expect (ed1 != nullptr, juce::String (info.id) + " first");
            const int w1 = ed1 ? ed1->getWidth() : 0, h1 = ed1 ? ed1->getHeight() : 0;
            delete ed1;
            expect (e->getActiveEditor() == nullptr, juce::String (info.id) + " cleared");
            auto* ed2 = e->createEditorAndMakeActive();
            expect (ed2 != nullptr, juce::String (info.id) + " second");
            if (ed2) expect (ed2->getWidth() == w1 && ed2->getHeight() == h1, juce::String (info.id) + " same size " + juce::String (ed2->getWidth()) + "x" + juce::String (ed2->getHeight()) + " vs " + juce::String (w1) + "x" + juce::String (h1));
            delete ed2;
        }
    }
};
static EditorTwiceTest editorTwiceTest;
