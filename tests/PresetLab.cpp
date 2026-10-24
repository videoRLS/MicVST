// MicVSTPresetLab - offline renders of hazır ayar chains with MicVST's real built-in effects
// (BuiltInEffect, Otomatik Kazanç, Kazanç: the same code as MicVST.exe), for measuring and tuning the
// presets (tools/preset_lab.py measures the files). Not part of MicVST.
//
//   MicVSTPresetLab synth <folder>                      synthetic men's / women's speech (48 kHz) -> WAV
//   MicVSTPresetLab factory <in.wav> <folder>           every factory preset on one file -> <folder>/<nn>.wav
//   MicVSTPresetLab personal <spec.txt> <in.wav> <folder>
//                                                       presets made with the factory recipe from a text
//                                                       file (see readSpec) -> <folder>/<name>.micvstpreset
//                                                       + <name>.wav (e.g. one user's own presets, which
//                                                       are not part of MicVST)
//   MicVSTPresetLab file <preset> <in.wav> <out.wav>    one .micvstpreset file on one input
//
// Every input is played twice through the chain and only the second pass is written: Otomatik
// Kazanç has learnt the voice by then, as it has in MicVST after a few seconds of talking.
#include <juce_audio_formats/juce_audio_formats.h>
#include "audio/BuiltInAutoGain.h"
#include "audio/BuiltInEffects.h"
#include "audio/BuiltInGain.h"
#include "state/Presets.h"
#include "SpeechSynth.h"
#include <cstdio>
#include <map>

namespace
{
    constexpr double fs = 48000.0;
    constexpr int block = 480;

    juce::AudioBuffer<float> readWav (const juce::File& f)
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr) { std::printf ("cannot read %s\n", f.getFullPathName().toRawUTF8()); std::exit (2); }
        if (std::abs (r->sampleRate - fs) > 1.0) { std::printf ("%s: needs 48 kHz (convert first)\n", f.getFullPathName().toRawUTF8()); std::exit (2); }
        juce::AudioBuffer<float> b (2, (int) r->lengthInSamples);
        r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
        if (r->numChannels == 1) b.copyFrom (1, 0, b, 0, 0, b.getNumSamples());
        return b;
    }

    void writeWav (const juce::File& f, const juce::AudioBuffer<float>& b)
    {
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os = std::make_unique<juce::FileOutputStream> (f);
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (fs).withNumChannels (2)
                                              .withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint));
        w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    std::unique_ptr<juce::AudioProcessor> makeProcessor (const PluginEntryState& e)
    {
        std::unique_ptr<juce::AudioProcessor> p;
        if (e.fileOrId == "builtin:autogain") p = std::make_unique<BuiltInAutoGainProcessor>();
        else if (e.fileOrId == "builtin:gain") p = std::make_unique<BuiltInGainProcessor>();
        else p = builtin::create (e.fileOrId);
        if (p == nullptr) { std::printf ("unknown element %s\n", e.fileOrId.toRawUTF8()); std::exit (3); }
        p->setPlayConfigDetails (2, 2, fs, block);
        p->prepareToPlay (fs, block);
        p->setStateInformation (e.state.getData(), (int) e.state.getSize());
        return p;
    }

    // the chain on the input played twice; the second pass comes back
    juce::AudioBuffer<float> render (const juce::Array<PluginEntryState>& chain, const juce::AudioBuffer<float>& in)
    {
        std::vector<std::unique_ptr<juce::AudioProcessor>> procs;
        int latency = 0;
        for (auto& e : chain)
            if (! e.bypassed) { procs.push_back (makeProcessor (e)); latency += procs.back()->getLatencySamples(); }
        const int n = in.getNumSamples();
        juce::AudioBuffer<float> out (2, n);
        juce::AudioBuffer<float> blk (2, block);
        juce::MidiBuffer midi;
        // pass 1 (learning) + pass 2 (kept), plus the chain's latency so the kept part lines up
        const int total = 2 * n + latency;
        for (int pos = 0; pos < total; pos += block)
        {
            const int len = juce::jmin (block, total - pos);
            blk.setSize (2, len, false, false, true);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < len; ++i)
                {
                    const int s = pos + i;
                    blk.setSample (c, i, s < 2 * n ? in.getSample (c, s % n) : 0.0f);
                }
            for (auto& p : procs) p->processBlock (blk, midi);
            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < len; ++i)
                {
                    const int s = pos + i - n - latency;
                    if (s >= 0 && s < n) out.setSample (c, s, blk.getSample (c, i));
                }
        }
        return out;
    }

    // A text file of presets made with the factory recipe (presets::Voicing):
    //   [Kişisel 5 - Doğal]          a preset's name
    //   lowCut = 70                  any Voicing field (the others keep the Voicing defaults)
    //   # ...                        a comment
    struct SpecEntry { juce::String name; presets::Voicing voicing; };
    std::vector<SpecEntry> readSpec (const juce::File& f)
    {
        std::vector<SpecEntry> out;
        auto fields = [] (presets::Voicing& v) -> std::map<juce::String, float*>
        {
            return { { "lowCut", &v.lowCut }, { "proxDb", &v.proxDb }, { "proxHz", &v.proxHz },
                     { "mudDb", &v.mudDb }, { "mudHz", &v.mudHz }, { "mudQ", &v.mudQ },
                     { "boxDb", &v.boxDb }, { "boxHz", &v.boxHz }, { "boxQ", &v.boxQ },
                     { "nasalDb", &v.nasalDb }, { "nasalHz", &v.nasalHz }, { "nasalQ", &v.nasalQ },
                     { "hardDb", &v.hardDb }, { "hardHz", &v.hardHz }, { "hardQ", &v.hardQ },
                     { "fizzDb", &v.fizzDb }, { "fizzHz", &v.fizzHz }, { "fizzQ", &v.fizzQ },
                     { "bodyDb", &v.bodyDb }, { "bodyHz", &v.bodyHz },
                     { "presDb", &v.presDb }, { "presHz", &v.presHz }, { "presQ", &v.presQ },
                     { "airDb", &v.airDb }, { "airHz", &v.airHz },
                     { "gateThr", &v.gateThr }, { "gateRange", &v.gateRange }, { "gateHold", &v.gateHold }, { "gateRelease", &v.gateRelease },
                     { "compThr", &v.compThr }, { "compRatio", &v.compRatio }, { "compAtk", &v.compAtk }, { "compRel", &v.compRel },
                     { "compKnee", &v.compKnee }, { "compMix", &v.compMix }, { "compMakeup", &v.compMakeup },
                     { "deessHz", &v.deessHz }, { "deessThr", &v.deessThr }, { "deessRange", &v.deessRange },
                     { "satDrive", &v.satDrive }, { "satMix", &v.satMix }, { "roomSize", &v.roomSize }, { "roomMix", &v.roomMix },
                     { "trimDb", &v.trimDb }, { "ceiling", &v.ceiling } };
        };
        juce::StringArray lines;
        lines.addLines (f.loadFileAsString());
        int lineNo = 0;
        for (auto line : lines)
        {
            ++lineNo;
            line = line.upToFirstOccurrenceOf ("#", false, false).trim();
            if (line.isEmpty()) continue;
            if (line.startsWith ("["))
            {
                out.push_back ({ line.substring (1).upToLastOccurrenceOf ("]", false, false).trim(), {} });
                continue;
            }
            const auto key = line.upToFirstOccurrenceOf ("=", false, false).trim();
            const float value = line.fromFirstOccurrenceOf ("=", false, false).trim().getFloatValue();
            if (out.empty()) { std::printf ("line %d: no [name] yet\n", lineNo); std::exit (7); }
            auto& v = out.back().voicing;
            if (key == "satType") { v.satType = (int) value; continue; }
            auto map = fields (v);
            const auto it = map.find (key);
            if (it == map.end()) { std::printf ("line %d: unknown field %s\n", lineNo, key.toRawUTF8()); std::exit (7); }
            *it->second = value;
        }
        return out;
    }

    juce::String fileSafe (const juce::String& s)
    {
        juce::String f;
        for (auto c : s) f << (juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ' || c == '-' ? juce::String::charToString (c) : "_");
        return f.trim();
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String cmd = argc > 1 ? argv[1] : "";
    const auto cwd = juce::File::getCurrentWorkingDirectory();

    if (cmd == "synth" && argc > 2)
    {
        const auto dir = cwd.getChildFile (argv[2]);
        dir.createDirectory();
        struct S { const char* name; bool female; unsigned seed; double noise; double pop; };
        for (auto& s : { S { "synth_m1", false, 11, -70.0, -200.0 }, S { "synth_m2", false, 23, -62.0, -6.0 },
                         S { "synth_f1", true, 12, -70.0, -200.0 }, S { "synth_f2", true, 24, -62.0, -6.0 } })
        {
            speech::Params p;
            p.fs = fs; p.female = s.female; p.seed = s.seed; p.noiseDb = s.noise; p.popDb = s.pop; p.peakDb = -22.0;
            const auto r = speech::generate (40.0, p);
            juce::AudioBuffer<float> b (2, (int) r.x.size());
            for (int i = 0; i < b.getNumSamples(); ++i) { b.setSample (0, i, r.x[(size_t) i]); b.setSample (1, i, r.x[(size_t) i]); }
            writeWav (dir.getChildFile (juce::String (s.name) + ".wav"), b);
        }
        return 0;
    }
    if (cmd == "factory" && argc > 3)
    {
        const auto in = readWav (cwd.getChildFile (argv[2]));
        const auto dir = cwd.getChildFile (argv[3]);
        dir.createDirectory();
        juce::String index;
        const auto& f = presets::factoryPresets();
        for (size_t i = 0; i < f.size(); ++i)
        {
            const auto name = juce::String (i).paddedLeft ('0', 2);
            writeWav (dir.getChildFile (name + ".wav"), render (f[i].elements, in));
            index << name << "\t" << f[i].name << "\n";
        }
        dir.getChildFile ("index.tsv").replaceWithText (index);
        return 0;
    }
    if (cmd == "personal" && argc > 4)
    {
        const auto in = readWav (cwd.getChildFile (argv[3]));
        const auto dir = cwd.getChildFile (argv[4]);
        dir.createDirectory();
        for (auto& p : readSpec (cwd.getChildFile (argv[2])))
        {
            const auto chain = presets::buildChain (p.voicing);
            juce::String err;
            const auto file = dir.getChildFile (fileSafe (p.name) + ".micvstpreset");
            if (! presets::exportPreset (p.name, chain, file, err)) { std::printf ("%s\n", err.toRawUTF8()); return 4; }
            writeWav (dir.getChildFile (fileSafe (p.name) + ".wav"), render (chain, in));
            std::printf ("%s\t%s\n", fileSafe (p.name).toRawUTF8(), p.name.toRawUTF8());
        }
        return 0;
    }
    // tweak <in.wav> <out.wav> <factory index> [element:param=value ...] - one factory preset with
    // some settings changed (element = the id without "builtin:"), for finding out what does what
    if (cmd == "tweak" && argc > 4)
    {
        auto chain = presets::factoryPresets()[(size_t) juce::String (argv[4]).getIntValue()].elements;
        for (int a = 5; a < argc; ++a)
        {
            const juce::String spec (argv[a]);
            const auto id = "builtin:" + spec.upToFirstOccurrenceOf (":", false, false);
            const auto param = spec.fromFirstOccurrenceOf (":", false, false).upToFirstOccurrenceOf ("=", false, false);
            const float value = spec.fromFirstOccurrenceOf ("=", false, false).getFloatValue();
            bool found = false;
            for (auto& e : chain)
                if (e.fileOrId == id)
                {
                    if (param == "bypass") { e.bypassed = value > 0.5f; found = true; continue; }
                    if (id == "builtin:gain")
                    {
                        BuiltInGainProcessor g; g.setGainDb (value); e.state.reset(); g.getStateInformation (e.state); found = true; continue;
                    }
                    auto fx = builtin::create (id);
                    fx->setStateInformation (e.state.getData(), (int) e.state.getSize());
                    if (fx->findParameter (param) == nullptr) break;
                    fx->setParam (param, value);
                    e.state.reset();
                    fx->getStateInformation (e.state);
                    found = true;
                }
            if (! found) { std::printf ("no %s\n", spec.toRawUTF8()); return 6; }
        }
        writeWav (cwd.getChildFile (argv[3]), render (chain, readWav (cwd.getChildFile (argv[2]))));
        return 0;
    }
    if (cmd == "file" && argc > 4)
    {
        auto xml = juce::XmlDocument::parse (cwd.getChildFile (argv[2]));
        presets::Preset p;
        if (xml == nullptr || ! presets::fromXml (*xml, p)) { std::printf ("not a preset\n"); return 5; }
        writeWav (cwd.getChildFile (argv[4]), render (p.elements, readWav (cwd.getChildFile (argv[3]))));
        return 0;
    }
    std::printf ("usage: MicVSTPresetLab synth <dir> | factory <in.wav> <dir> | personal <spec.txt> <in.wav> <dir> |\n"
                 "       tweak <in.wav> <out.wav> <index> [element:param=value ...] | file <preset> <in.wav> <out.wav>\n");
    return 1;
}
