// MicVSTSnapshot - renders MicVST's real UI components to PNG files (CI artefact) so the Turkish
// texts can be checked with the Windows fonts: main window (empty chain / chain / scanning /
// large), How-To window, plug-in picker, every tooltip, the menus. Runs the real AudioEngine on
// two test devices (tests/snapshot/audio/MicVSTDeviceManager.h).
// Usage: MicVSTSnapshot <output folder>
#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/AudioEngine.h"
#include "audio/BuiltInEffects.h"
#include "SpeechSynth.h"
#include "ui/MainComponent.h"
#include "ui/PluginPicker.h"
#include "ui/Theme.h"
#include "Text.h"

using micvst::tr;

namespace
{
    void save (const juce::Image& img, const juce::File& out)
    {
        out.deleteFile();
        juce::FileOutputStream os (out);
        juce::PNGImageFormat().writeImageToStream (img, os);
    }

    void snap (juce::Component& c, int w, int h, const juce::File& out, juce::Colour bg = micvst::theme::background)   // the window colour
    {
        c.setSize (w, h);
        juce::Image img (juce::Image::ARGB, w, h, true);
        {
            juce::Graphics g (img);
            g.fillAll (bg);
            c.paintEntireComponent (g, true);
        }
        save (img, out);
    }

    void collectTooltips (juce::Component& c, juce::StringArray& out)
    {
        if (auto* t = dynamic_cast<juce::TooltipClient*> (&c))
            if (const auto tip = t->getTooltip(); tip.isNotEmpty() && ! out.contains (tip))
                out.add (tip);
        for (auto* child : c.getChildren())
            collectTooltips (*child, out);
    }

    void renderTooltips (const juce::StringArray& tips, const juce::File& out)
    {
        auto& lf = juce::LookAndFeel::getDefaultLookAndFeel();
        std::vector<juce::Rectangle<int>> boxes;
        int h = 8, w = 0;
        for (auto& t : tips)
        {
            boxes.push_back (lf.getTooltipBounds (t, {}, { 0, 0, 2000, 2000 }));
            h += boxes.back().getHeight() + 8;
            w = juce::jmax (w, boxes.back().getWidth());
        }
        juce::Image img (juce::Image::ARGB, w + 16, h, true);
        {
            // Windows (Direct2D): the drawing reaches the image only when the context ends
            juce::Graphics g (img);
            g.fillAll (juce::Colours::darkgrey);
            int y = 8;
            for (int i = 0; i < tips.size(); ++i)
            {
                juce::Graphics::ScopedSaveState ss (g);
                g.setOrigin (8, y);
                lf.drawTooltip (g, tips[i], boxes[(size_t) i].getWidth(), boxes[(size_t) i].getHeight());
                y += boxes[(size_t) i].getHeight() + 8;
            }
        }
        save (img, out);
    }

    // Menu items drawn with the real LookAndFeel (a popup menu cannot be opened off-screen).
    void renderMenu (const juce::StringArray& items, const juce::File& out)
    {
        auto& lf = juce::LookAndFeel::getDefaultLookAndFeel();
        const int w = 320, itemH = 24;
        juce::Image img (juce::Image::ARGB, w, itemH * items.size() + 8, true);
        {
            juce::Graphics g (img);   // see renderTooltips
            lf.drawPopupMenuBackground (g, w, img.getHeight());
            for (int i = 0; i < items.size(); ++i)
            {
                juce::Graphics::ScopedSaveState ss (g);
                const juce::Rectangle<int> area (0, 4 + i * itemH, w, itemH);
                if (items[i] == "-")
                    lf.drawPopupMenuItem (g, area, true, false, false, false, false, {}, {}, nullptr, nullptr);
                else
                    lf.drawPopupMenuItem (g, area, false, true, false, false, false, items[i], {}, nullptr, nullptr);
            }
        }
        save (img, out);
    }

    // MainComponent shows the version of the running JUCEApplication.
    struct SnapApp : juce::JUCEApplication
    {
        const juce::String getApplicationName() override    { return "MicVST"; }
        const juce::String getApplicationVersion() override { return MICVST_VERSION_STRING; }
        void initialise (const juce::String&) override {}
        void shutdown() override {}
    };
}

int main (int argc, char** argv)
{
    const juce::File dir = juce::File::getCurrentWorkingDirectory().getChildFile (argc > 1 ? argv[1] : "ui");
    dir.createDirectory();
    juce::ScopedJuceInitialiser_GUI gui;
    SnapApp app;
    micvst::installTurkishTranslations();
    micvst::theme::NightLookAndFeel nightTheme;   // as MicVST.exe (Main.cpp)
    juce::LookAndFeel::setDefaultLookAndFeel (&nightTheme);

    auto pump = [] (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); };

    AudioEngine engine;
    MicVSTState st;
    st.inputDevice = juce::String::fromUTF8 ("Mikrofon (HyperX QuadCast 2 S)");
    st.outputDevice = "CABLE Input (VB-Audio Virtual Cable)";
    st.bufferSize = 480;
    engine.applyState (st);
    pump (2500);

    {
        MainComponent mc (engine);
        pump (700);
        snap (mc, 584, 521, dir.getChildFile ("1_main_empty.png"));

        engine.getChain().addMonoToStereo();
        engine.getChain().addStereoToMono();
        engine.getChain().addMonoToStereo();
        engine.getChain().addGain (6.0f);   // Kazanç-Zeile mit Regler
        engine.getChain().setBypass (1, true);
        engine.getChain().addAutoGain();    // Otomatik-Kazanç-Zeile (am Ende)
        engine.rebuildGraph();
        if (engine.onScanFinished) engine.onScanFinished();   // list view rebuilds its rows
        pump (800);
        snap (mc, 584, 521, dir.getChildFile ("2_main_chain.png"));
        snap (mc, 760, 640, dir.getChildFile ("3_main_large.png"));

        if (engine.onScanProgress) engine.onScanProgress (12, 34, "FabFilter Pro-Q 4");
        for (auto* c : mc.getChildren())
        {
            bool isList = false;
            for (auto* cc : c->getChildren())
                if (dynamic_cast<juce::ProgressBar*> (cc) != nullptr) isList = true;
            if (! isList) continue;
            for (auto* cc : c->getChildren())
            {
                if (dynamic_cast<juce::ProgressBar*> (cc) != nullptr) cc->setVisible (true);
                if (auto* l = dynamic_cast<juce::Label*> (cc))
                    if (l->getText().isNotEmpty() && ! l->isVisible()) l->setVisible (true);
                if (auto* b = dynamic_cast<juce::TextButton*> (cc))
                    if (b->getButtonText() == tr ("Atla")) b->setVisible (true);
            }
            c->resized();
        }
        pump (300);
        snap (mc, 584, 521, dir.getChildFile ("4_main_scanning.png"));

        juce::StringArray tips;
        collectTooltips (mc, tips);
        renderTooltips (tips, dir.getChildFile ("6_tooltips.png"));
        juce::String all;
        for (auto& t : tips) all << "---\n" << t << "\n";
        dir.getChildFile ("6_tooltips.txt").replaceWithText (all);
    }

    // Built-in effects: a chain of them in the main window (incl. the mute row's switch) ...
    {
        MainComponent mc (engine);
        auto& chain = engine.getChain();
        while (! chain.entries().empty()) chain.removePlugin (0);
        for (auto* id : { "builtin:lowcut", "builtin:mud", "builtin:presence", "builtin:deesser", "builtin:saturation",
                          "builtin:limiter", "builtin:mute", "builtin:lufs", "builtin:syncdelay" })
            chain.addBuiltInEffect (id);
        chain.addAutoGain();
        chain.movePlugin ((int) chain.entries().size() - 1, 3);
        chain.getBuiltInEffect (9)->setParam ("delay", 120.0f);
        chain.setBypass (5, true);
        engine.rebuildGraph();
        if (engine.onScanFinished) engine.onScanFinished();
        pump (800);
        snap (mc, 584, 640, dir.getChildFile ("2b_main_effects.png"));
        juce::StringArray tips;
        collectTooltips (mc, tips);
        juce::String all;
        for (auto& t : tips) all << "---\n" << t << "\n";
        dir.getChildFile ("2b_tooltips.txt").replaceWithText (all);
    }

    // ... and every effect's settings window (the meters after 12 s of speech)
    {
        speech::Params sp;
        sp.peakDb = -18.0;
        sp.noiseDb = -65.0;
        auto voice = speech::generate (12.0, sp).x;
        float peak = 0.0f;
        for (float v : voice) peak = std::max (peak, std::abs (v));
        for (float& v : voice) v *= 0.7f / peak;   // the synthetic voice's plosives peak at -3 dBFS
        int n = 0;
        for (auto& info : builtin::effects())
        {
            auto fx = builtin::create (info.id);
            fx->setPlayConfigDetails (2, 2, 48000.0, 480);
            fx->prepareToPlay (48000.0, 480);
            juce::AudioBuffer<float> b (2, 480);
            juce::MidiBuffer midi;
            for (size_t k = 0; k + 480 <= voice.size(); k += 480)
            {
                b.copyFrom (0, 0, voice.data() + k, 480);
                b.copyFrom (1, 0, voice.data() + k, 480);
                fx->processBlock (b, midi);
            }
            std::unique_ptr<juce::AudioProcessorEditor> ed (fx->createEditorAndMakeActive());
            pump (400);   // meter windows: a few timer ticks
            const auto name = juce::String (info.id).fromFirstOccurrenceOf (":", false, false);
            snap (*ed, ed->getWidth(), ed->getHeight(), dir.getChildFile ("10_fx_" + juce::String (++n).paddedLeft ('0', 2) + "_" + name + ".png"));
            juce::StringArray tips;
            collectTooltips (*ed, tips);
            ed.reset();
        }
    }

    dir.getChildFile ("5_howto_title.txt").replaceWithText (MainComponent::howToTitle());
    {
        auto howTo = MainComponent::createHowToContent();
        snap (*howTo, 556, 600, dir.getChildFile ("5_howto.png"), micvst::theme::surface);
    }

    {
        juce::Array<juce::PluginDescription> entries;
        auto add = [&] (const char* n, const juce::String& m) { juce::PluginDescription d; d.name = n; d.manufacturerName = m; d.fileOrIdentifier = n; entries.add (d); };
        add ("Mono -> Stereo", tr ("Dahili")); add ("Stereo -> Mono", tr ("Dahili"));
        { juce::PluginDescription d; d.name = tr ("Kazanç (Gain)"); d.manufacturerName = tr ("Dahili"); d.fileOrIdentifier = "builtin:gain"; entries.add (d); }
        { juce::PluginDescription d; d.name = tr ("Otomatik Kazanç (Auto Gain)"); d.manufacturerName = tr ("Dahili"); d.fileOrIdentifier = "builtin:autogain"; entries.add (d); }
        add ("Pro-Q 4", "FabFilter"); add ("Pro-C 3", "FabFilter"); add ("Pro-DS", "FabFilter");
        add ("soothe2", "oeksound"); add ("CLA-2A Mono", "Waves"); add ("TDR VOS SlickEQ GE", "Tokyo Dawn Labs");
        add ("Plugin", {});
        PluginPickerComponent picker (entries, [] (const juce::PluginDescription&) {});
        snap (picker, 320, 420, dir.getChildFile ("7_picker.png"));

        for (auto& info : builtin::effects())   // as PluginListView::showPluginPicker
        {
            juce::PluginDescription d;
            d.name = juce::String::fromUTF8 (info.name);
            d.manufacturerName = juce::String::fromUTF8 (info.group);
            d.fileOrIdentifier = info.id;
            entries.add (d);
        }
        PluginPickerComponent all (entries, [] (const juce::PluginDescription&) {});
        snap (all, 320, 1040, dir.getChildFile ("7b_picker_effects.png"));
    }

    renderMenu ({ tr ("Klasör ekle..."), "-", tr ("(ek klasör yok)"), "-", tr ("Tüm eklentileri yeniden tara"),
                  tr ("Atlanan eklentileri yeniden dene"), "-", tr ("Programı sıfırla (tüm veriler silinir)...") },
                dir.getChildFile ("8_menu_folders.png"));
    renderMenu ({ tr ("Windows açılışında başlat"), "-", tr ("MicVST'yi kapat"),
                  "-", TRANS ("Cut"), TRANS ("Copy"), TRANS ("Paste"), TRANS ("Delete"), TRANS ("Select All") },
                dir.getChildFile ("9_menu_tray_and_textfield.png"));

    // The chosen microphone is unplugged (long device name): "... (bağlı değil)" in the list and
    // the stopped status line must fit.
    {
        MicVSTState st2 = st;
        st2.inputDevice = juce::String::fromUTF8 ("Mikrofon (Logitech G PRO X 2 LIGHTSPEED Wireless Gaming Headset)");
        engine.applyState (st2);
        pump (1500);
        MainComponent mc (engine);
        pump (700);
        snap (mc, 584, 521, dir.getChildFile ("1b_main_device_missing.png"));
    }
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    return 0;
}
