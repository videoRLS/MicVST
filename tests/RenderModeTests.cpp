#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/RenderMode.h"

// Arayüz çizimi: the setting's text in config.xml and which engine each mode picks at start.
struct RenderModeTest : juce::UnitTest
{
    RenderModeTest() : juce::UnitTest ("Render mode") {}

    void runTest() override
    {
        using namespace micvst::render;

        beginTest ("config.xml text: automatic is the default and writes nothing");
        {
            expect (toId (Mode::automatic).isEmpty());
            expectEquals (toId (Mode::gpuOnly), juce::String ("gpu"));
            expectEquals (toId (Mode::cpuOnly), juce::String ("cpu"));
            for (auto m : { Mode::automatic, Mode::gpuOnly, Mode::cpuOnly })
                expect (fromId (toId (m)) == m);
            expect (fromId ("") == Mode::automatic);
            expect (fromId ("rubbish") == Mode::automatic);
            expect (fromId (" GPU ") == Mode::gpuOnly);
        }

        beginTest ("engine at start: 0 = processor (GDI), 1 = graphics card (Direct2D)");
        {
            for (bool gpu : { false, true })
            {
                expectEquals (chooseEngine (Mode::cpuOnly, gpu), 0);
                expectEquals (chooseEngine (Mode::gpuOnly, gpu), 1);   // as asked, even without a card
                expectEquals (chooseEngine (Mode::automatic, gpu), gpu ? 1 : 0);
            }
        }

        beginTest ("a new choice waits for the next start");
        {
            startSession (Mode::cpuOnly);
            expect (! describeNow().contains ("\n"));
            setMode (Mode::gpuOnly);
            expect (current() == Mode::gpuOnly);
            expect (describeNow().contains ("\n"), "the settings say it is used from the next start");
            setMode (Mode::cpuOnly);
            expect (! describeNow().contains ("\n"));
        }
    }
};

static RenderModeTest renderModeTest;
