// Unit tests for the presets (src/state/Presets.h): factory presets (valid, same elements, really
// different, loudness-matched design), user preset files (round trip of every byte incl. large
// plug-in state blobs, Turkish names, overwrite, rename, delete, damaged files, name rules).
// (Test names are ASCII: the runner prints them as Latin-1.)
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/BuiltInEffects.h"
#include "audio/fx/FilterEffect.h"
#include "state/Presets.h"
#include <random>

namespace
{
    struct TempFolder
    {
        TempFolder() : dir (juce::File::getSpecialLocation (juce::File::tempDirectory)
                                .getChildFile ("micvst-preset-test-" + juce::String (juce::Random::getSystemRandom().nextInt64())))
        { dir.createDirectory(); }
        ~TempFolder() { dir.deleteRecursively(); }
        juce::File dir;
    };

    juce::Array<PluginEntryState> sampleChain (unsigned seed, int blobBytes)
    {
        std::mt19937 rng (seed);
        juce::Array<PluginEntryState> c;
        PluginEntryState vst;   // a VST3 plug-in with a big binary state (as FabFilter / Clear keep them)
        vst.fileOrId = "C:\\Program Files\\Common Files\\VST3\\FabFilter Pro-Q 4.vst3";
        vst.pluginId = "VST3-FabFilter Pro-Q 4-1a2b3c4d-5e6f7a8b";
        vst.pluginName = "FabFilter Pro-Q 4";
        vst.pluginUid = 0x1a2b3c4d;
        for (int i = 0; i < blobBytes; ++i) { const char b = (char) (rng() & 0xff); vst.state.append (&b, 1); }
        c.add (vst);
        auto fx = builtin::create ("builtin:deesser");
        fx->setParam ("threshold", -17.3f);
        PluginEntryState d;
        d.fileOrId = "builtin:deesser";
        d.bypassed = true;
        fx->getStateInformation (d.state);
        c.add (d);
        PluginEntryState g;
        g.fileOrId = "builtin:autogain";
        c.add (g);
        return c;
    }

    bool sameChain (const juce::Array<PluginEntryState>& a, const juce::Array<PluginEntryState>& b)
    {
        if (a.size() != b.size()) return false;
        for (int i = 0; i < a.size(); ++i)
            if (a[i].fileOrId != b[i].fileOrId || a[i].bypassed != b[i].bypassed || a[i].state != b[i].state
                || a[i].pluginId != b[i].pluginId || a[i].pluginName != b[i].pluginName || a[i].pluginUid != b[i].pluginUid)
                return false;
        return true;
    }
}

struct PresetTest : juce::UnitTest
{
    PresetTest() : juce::UnitTest ("Presets") {}

    void runTest() override
    {
        beginTest ("factory presets: five, built-in only, same elements in the same order, every state valid");
        {
            const auto& f = presets::factoryPresets();
            expectEquals ((int) f.size(), 5);
            for (const char* n : { "Sinema Sesi", "Kristal Netlik", "Yumuşak ve Pürüzsüz", "Derin ve Sıcak", "Dengeli Profesyonel" })
                expect (presets::findFactory (juce::String::fromUTF8 (n)) != nullptr, juce::String::fromUTF8 (n));
            for (auto& p : f)
            {
                expect (p.factory);
                expectEquals (p.elements.size(), f[0].elements.size());
                for (int i = 0; i < p.elements.size(); ++i)
                {
                    const auto& e = p.elements.getReference (i);
                    expect (e.fileOrId.startsWith ("builtin:"), p.name + ": not built-in: " + e.fileOrId);
                    expect (e.fileOrId == f[0].elements[i].fileOrId, p.name + ": element " + juce::String (i) + " differs");
                    expect (! e.bypassed);
                    expect (e.state.getSize() > 0);
                    if (auto fx = builtin::create (e.fileOrId))
                    {
                        auto xml = juce::AudioProcessor::getXmlFromBinary (e.state.getData(), (int) e.state.getSize());
                        expect (xml != nullptr && fx->applyStateXml (*xml), p.name + ": state of " + e.fileOrId);
                    }
                }
                // never a gate or a compressor; Otomatik Kazanç once
                int autos = 0;
                for (auto& e : p.elements) if (e.fileOrId == "builtin:autogain") ++autos;
                expectEquals (autos, 1);
            }
        }

        beginTest ("factory presets: really different tone curves (EQ part: at least 2 dB apart somewhere)");
        {
            const double fs = 48000.0;
            auto curve = [&] (const presets::Preset& p)
            {
                std::vector<double> c;
                for (double f = 50.0; f < 16000.0; f *= 1.12)
                {
                    double m = 1.0;
                    for (auto& e : p.elements)
                        if (auto fx = builtin::create (e.fileOrId))
                            if (auto* fe = dynamic_cast<fx::FilterEffect*> (fx.get()))
                            {
                                fx->setStateInformation (e.state.getData(), (int) e.state.getSize());
                                fx->setPlayConfigDetails (2, 2, fs, 480);
                                fx->prepareToPlay (fs, 480);
                                m *= fe->responseAt (f);
                            }
                    c.push_back (20.0 * std::log10 (m));
                }
                return c;
            };
            const auto& f = presets::factoryPresets();
            std::vector<std::vector<double>> curves;
            for (auto& p : f) curves.push_back (curve (p));
            for (size_t a = 0; a < curves.size(); ++a)
                for (size_t b = a + 1; b < curves.size(); ++b)
                {
                    double most = 0.0;
                    for (size_t k = 0; k < curves[a].size(); ++k) most = std::max (most, std::abs (curves[a][k] - curves[b][k]));
                    logMessage (f[a].name + " vs " + f[b].name + ": up to " + juce::String (most, 1) + " dB apart");
                    expectGreaterThan (most, 2.0);
                }
            // nothing extreme above the low cut (100 Hz on): no point of any curve beyond +/- 9 dB
            for (auto& c : curves)
            {
                double f = 50.0;
                for (double v : c) { if (f >= 100.0) expectLessThan (std::abs (v), 9.0); f *= 1.12; }
            }
        }

        beginTest ("user preset: save -> file -> load gives back every byte (100 kB plug-in state, Turkish name)");
        {
            TempFolder t;
            const auto chain = sampleChain (1, 100000);
            const auto name = juce::String::fromUTF8 ("Benim sesim - akşam yayını (ğüşıöç)");
            juce::String err;
            expect (presets::saveUserPreset (name, chain, err, t.dir), err);
            auto list = presets::loadUserPresets (t.dir);
            expectEquals ((int) list.size(), 1);
            expect (list[0].name == name);
            expect (sameChain (list[0].elements, chain));
            expect (list[0].file.getFileName().endsWith (".micvstpreset"));
        }

        beginTest ("overwrite, rename, delete; names that make the same file name stay apart");
        {
            TempFolder t;
            juce::String err;
            expect (presets::saveUserPreset ("Yayin", sampleChain (1, 10), err, t.dir));
            expect (presets::saveUserPreset ("Yayin", sampleChain (2, 20), err, t.dir));   // overwrite
            auto list = presets::loadUserPresets (t.dir);
            expectEquals ((int) list.size(), 1);
            expect (sameChain (list[0].elements, sampleChain (2, 20)));
            expect (presets::saveUserPreset ("a/b", sampleChain (3, 5), err, t.dir));
            expect (presets::saveUserPreset ("a:b", sampleChain (4, 5), err, t.dir));      // both sanitize to "a_b"
            list = presets::loadUserPresets (t.dir);
            expectEquals ((int) list.size(), 3);
            expect (presets::renameUserPreset ("Yayin", juce::String::fromUTF8 ("Yayın gece"), err, t.dir), err);
            list = presets::loadUserPresets (t.dir);
            bool found = false;
            for (auto& p : list) if (p.name == juce::String::fromUTF8 ("Yayın gece")) { found = true; expect (sameChain (p.elements, sampleChain (2, 20))); }
            expect (found);
            expectEquals ((int) list.size(), 3);
            expect (! presets::renameUserPreset ("a/b", "a:b", err, t.dir));   // taken
            expect (presets::deleteUserPreset ("a/b", t.dir));
            expectEquals ((int) presets::loadUserPresets (t.dir).size(), 2);
            expect (! presets::deleteUserPreset ("yok", t.dir));
        }

        beginTest ("name rules: empty, too long, a factory name");
        {
            expect (presets::nameProblem ("   ").isNotEmpty());
            expect (presets::nameProblem (juce::String::repeatedString ("x", 61)).isNotEmpty());
            expect (presets::nameProblem ("dengeli profesyonel").isNotEmpty());
            expect (presets::nameProblem ("Benim ayarım").isEmpty());
            TempFolder t;
            juce::String err;
            expect (! presets::saveUserPreset ("Sinema Sesi", sampleChain (1, 4), err, t.dir));
            expect (err.isNotEmpty());
        }

        beginTest ("damaged and foreign files in the folder are skipped, the good ones load");
        {
            TempFolder t;
            juce::String err;
            expect (presets::saveUserPreset ("good", sampleChain (5, 50), err, t.dir));
            t.dir.getChildFile ("broken.micvstpreset").replaceWithText ("<MicVSTPreset version=\"1\" name=\"x\"><plugins><plu");
            t.dir.getChildFile ("other.micvstpreset").replaceWithText ("<SomethingElse/>");
            t.dir.getChildFile ("noname.micvstpreset").replaceWithText ("<MicVSTPreset version=\"1\"/>");
            t.dir.getChildFile ("notes.txt").replaceWithText ("hello");
            const auto list = presets::loadUserPresets (t.dir);
            expectEquals ((int) list.size(), 1);
            expect (list[0].name == "good");
        }

        beginTest ("XML: a preset of a newer version with unknown attributes still loads its chain");
        {
            auto xml = presets::toXml ("x", sampleChain (6, 30));
            xml->setAttribute ("version", 7);
            xml->setAttribute ("somethingNew", "1");
            presets::Preset p;
            expect (presets::fromXml (*xml, p));
            expect (sameChain (p.elements, sampleChain (6, 30)));
        }
    }
};
static PresetTest presetTest;
