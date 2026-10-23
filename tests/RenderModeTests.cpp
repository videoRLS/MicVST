#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/RenderMode.h"

// Arayüz çizimi: the setting's text in config.xml, which engine each mode picks, and the brake
// that keeps an unstable graphics card from flipping the windows back and forth.
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

        beginTest ("engine: 0 = processor (GDI), 1 = graphics card (Direct2D)");
        {
            for (bool gpu : { false, true })
                for (bool risky : { false, true })
                    for (bool braked : { false, true })
                    {
                        expectEquals (chooseEngine (Mode::cpuOnly, gpu, risky, braked), 0);
                        expectEquals (chooseEngine (Mode::gpuOnly, gpu, risky, braked), 1);   // as asked, even without a card
                        expectEquals (chooseEngine (Mode::automatic, gpu, risky, braked), gpu && ! risky && ! braked ? 1 : 0);
                    }
        }

        beginTest ("brake: more than 6 switches in 10 minutes -> on; spread out -> off");
        {
            SwitchBrake spread;
            juce::uint32 t = 5000;
            for (int i = 0; i < 50; ++i) { expect (! spread.note (t)); t += 2 * 60 * 1000; }   // one every 2 min
            SwitchBrake burst;
            bool on = false;
            for (int i = 0; i < 7; ++i) on = burst.note (1000u + (juce::uint32) i * 1000u);
            expect (on, "7 switches in 7 s");
            SwitchBrake wrap;   // across the 32-bit millisecond counter's wrap
            juce::uint32 w = 0xFFFFFFFFu - 3000u;
            for (int i = 0; i < 6; ++i) { expect (! wrap.note (w)); w += 1000u; }
            expect (wrap.note (w), "the 7th, after the counter wrapped");
        }
    }
};

static RenderModeTest renderModeTest;
