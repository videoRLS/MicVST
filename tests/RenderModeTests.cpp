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

        beginTest ("GPU / CPU only apply at once, Otomatik waits for the next start");
        {
            startSession (Mode::cpuOnly);
            expect (! describeNow().contains ("\n"));
            setMode (Mode::gpuOnly);
            expect (current() == Mode::gpuOnly && sessionEngine() == 1, "at once");
            expect (! describeNow().contains ("\n"));
            setMode (Mode::cpuOnly);
            expect (sessionEngine() == 0, "back at once");
            setMode (Mode::automatic);
            expect (current() == Mode::automatic && sessionEngine() == 0, "Otomatik: decided at the next start");
            expect (describeNow().contains ("\n"), "the settings say it is used from the next start");
            setMode (Mode::cpuOnly);
            expect (! describeNow().contains ("\n"));
        }

        beginTest ("FPS: 60 / 30 / 20 / 10 with the processor, 60 / 30 with the graphics card, default 30");
        {
            startSession (Mode::cpuOnly);
            expect (fpsChoices() == juce::Array<int> { 60, 30, 20, 10 });
            expectEquals (fps(), 30);
            for (int f : { 60, 20, 10 }) { setFps (f); expectEquals (fps(), f); expectEquals (savedFps(), f); }
            setFps (30);
            expectEquals (savedFps(), 0, "the default writes nothing");
            setFps (10);
            startSession (Mode::gpuOnly);
            expect (fpsChoices() == juce::Array<int> { 60, 30 });
            expectEquals (fps(), 30, "10 is not offered with the graphics card: 30");
            setFps (60);
            expectEquals (fps(), 60);
            setFps (0);
            expectEquals (fps(), 30);
            startSession (Mode::automatic);
            expectEquals (fps(), 30, "automatic starts at 30 whatever it picked");
        }

        beginTest ("level meters: shown by default, hidden on request");
        {
            expect (metersVisible());
            setMetersVisible (false);
            expect (! metersVisible());
            setMetersVisible (true);
        }
    }
};

static RenderModeTest renderModeTest;
