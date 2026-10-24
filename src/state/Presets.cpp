#include "state/Presets.h"
#include "audio/BuiltInAutoGain.h"
#include "audio/BuiltInEffects.h"
#include "audio/BuiltInGain.h"
#include "Text.h"
#include <functional>

namespace presets
{
namespace
{
    const juce::Identifier presetTag ("MicVSTPreset"), versionAttr ("version"), nameAttr ("name");
    const char* const extension = ".micvstpreset";
    constexpr int maxNameLength = 60;

    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
    const char* const prefixMen = TRK ("Erkek Sesi - ");
    const char* const prefixWomen = TRK ("Kadın Sesi - ");

    struct Setting { const char* id; std::vector<std::pair<const char*, float>> params; };

    PluginEntryState effectEntry (const Setting& s)
    {
        PluginEntryState e;
        e.fileOrId = s.id;
        if (juce::String (s.id) == "builtin:gain")
        {
            BuiltInGainProcessor g;
            g.setGainDb (s.params.empty() ? 0.0f : s.params[0].second);
            g.getStateInformation (e.state);
            return e;
        }
        if (juce::String (s.id) == "builtin:autogain")
        {
            BuiltInAutoGainProcessor a;
            a.setAutoGainEnabled (true);
            a.getStateInformation (e.state);
            return e;
        }
        auto fx = builtin::create (s.id);
        jassert (fx != nullptr);
        for (auto& [pid, v] : s.params)
        {
            jassert (fx->findParameter (pid) != nullptr);
            fx->setParam (pid, v);
        }
        fx->getStateInformation (e.state);
        return e;
    }

    // Only what the preset uses: an EQ element whose gain is 0, a gate with no range, a saturation
    // or a room with no mix is left out (not put in at zero). Always there: Low-Cut, Otomatik
    // Kazanç, Kompresör, De-esser, Kazanç (the output level - the one to turn) and Çıkış Limiteri.
    std::vector<Setting> chainFor (const Voicing& v)
    {
        const auto used = [] (float x) { return std::abs (x) > 0.01f; };
        std::vector<Setting> c;
        c.push_back ({ "builtin:lowcut", { { "freq", v.lowCut }, { "slope", 1.0f } } });
        if (used (v.proxDb))
            c.push_back ({ "builtin:proximity", { { "gain", v.proxDb }, { "freq", v.proxHz } } });
        if (used (v.mudDb))
            c.push_back ({ "builtin:mud", { { "gain", v.mudDb }, { "freq", v.mudHz }, { "q", v.mudQ } } });
        if (used (v.boxDb) || used (v.nasalDb) || used (v.hardDb) || used (v.fizzDb))
            c.push_back ({ "builtin:parameq", { { "b1type", 0.0f }, { "b1freq", v.boxHz },   { "b1gain", v.boxDb },   { "b1q", v.boxQ },
                                                { "b2type", 0.0f }, { "b2freq", v.nasalHz }, { "b2gain", v.nasalDb }, { "b2q", v.nasalQ },
                                                { "b3type", 0.0f }, { "b3freq", v.hardHz },  { "b3gain", v.hardDb },  { "b3q", v.hardQ },
                                                { "b4type", 0.0f }, { "b4freq", v.fizzHz },  { "b4gain", v.fizzDb },  { "b4q", v.fizzQ } } });
        if (used (v.bodyDb))
            c.push_back ({ "builtin:body", { { "gain", v.bodyDb }, { "freq", v.bodyHz } } });
        if (used (v.presDb))
            c.push_back ({ "builtin:presence", { { "gain", v.presDb }, { "freq", v.presHz }, { "q", v.presQ } } });
        if (used (v.airDb))
            c.push_back ({ "builtin:air", { { "gain", v.airDb }, { "freq", v.airHz } } });
        c.push_back ({ "builtin:autogain", {} });
        if (used (v.gateRange))
            c.push_back ({ "builtin:gate", { { "threshold", v.gateThr }, { "range", v.gateRange }, { "attack", 2.0f }, { "hold", v.gateHold },
                                             { "release", v.gateRelease }, { "detecthp", 100.0f } } });
        c.push_back ({ "builtin:compressor", { { "threshold", v.compThr }, { "ratio", v.compRatio }, { "attack", v.compAtk }, { "release", v.compRel },
                                               { "knee", v.compKnee }, { "makeup", v.compMakeup }, { "mix", v.compMix } } });
        if (used (v.deessRange))
            c.push_back ({ "builtin:deesser", { { "freq", v.deessHz }, { "threshold", v.deessThr }, { "range", v.deessRange }, { "mode", 0.0f }, { "detect", 0.0f } } });
        if (used (v.satDrive) && used (v.satMix))
            c.push_back ({ "builtin:saturation", { { "type", (float) v.satType }, { "drive", v.satDrive }, { "mix", v.satMix } } });
        if (used (v.roomMix))
            c.push_back ({ "builtin:reverb", { { "size", v.roomSize }, { "damping", v.roomDamping }, { "width", 100.0f }, { "mix", v.roomMix } } });
        c.push_back ({ "builtin:gain", { { "gain", v.trimDb } } });
        c.push_back ({ "builtin:limiter", { { "ceiling", v.ceiling }, { "input", 0.0f }, { "release", 80.0f } } });
        return c;
    }

    // ---------------------------------------------------------------------------------------------
    // The factory presets (1.2.3): fifteen purposes, each for men's and for women's voices.
    //
    // Aim of every one: natural, full ("tok"), clear, balanced, smooth - and not tiring after hours.
    // So no move is narrow or large: they suit the great majority of adult voices and of USB /
    // condenser microphones at 5-20 cm, not one particular voice. What differs from voice to voice
    // is left to the adaptive elements: Otomatik Kazanç brings every voice to the same level first,
    // so the gate, the compressor and the de-esser (relative: it reacts to the highs against the
    // voice itself) work alike for quiet and loud microphones.
    //
    //  Men (fundamental ~85-155 Hz): low cut 65-100 Hz (deep voices keep their fundamental), close-
    //  microphone build-up ~150 Hz, body ~115-130 Hz, mud ~300 Hz, box ~500 Hz, nose ~1 kHz, hard
    //  edge ~3 kHz, presence 3.5-4.5 kHz, sibilants ~5.5-6.5 kHz, fizz ~7.5 kHz, air 10-11 kHz.
    //  Women (fundamental ~165-255 Hz): a third to half an octave higher - low cut 85-120 Hz, build-
    //  up ~200 Hz, body ~200-220 Hz, mud ~350 Hz, box ~600 Hz, nose ~1.2 kHz, edge ~3.5 kHz,
    //  presence 4.5-5 kHz, sibilants ~6.5-7.5 kHz, fizz ~9 kHz, air 12 kHz.
    //  Turkish has many sibilants (s, ş, ç, z): every preset de-esses.
    //
    // Measured with MicVSTPresetLab (tests/PresetLab.cpp, tools/preset_lab.py) on real and
    // synthetic men's and women's speech: all thirty equally loud (-20 LUFS on their voices), true
    // peaks at -2.4 dBTP or lower, word onsets intact, sibilants down (the de-esser's relative
    // threshold -12: sibilants only, the vowels' air stays as the EQ sets it).
    // The gate: after Otomatik Kazanç the peaks of room noise sit at about -44 to -53 dBFS, so the
    // thresholds are -44 to -47 dBFS (Gürültülü Ortam -40); the gate closes 4 dB lower. On a quiet
    // microphone the noise between sentences ends lower than it came in although the compressor
    // lifts it; a noisier room is what Gürültülü Ortam is for.
    std::vector<Preset> build()
    {
        static const char* const men = TRK ("Erkek Sesi Ayarları");
        static const char* const women = TRK ("Kadın Sesi Ayarları");

        // the common starting point: "Doğal ve Dengeli"
        auto base = [] (bool woman)
        {
            Voicing v;
            v.lowCut  = woman ? 100.0f : 75.0f;
            v.proxDb  = -1.0f;  v.proxHz  = woman ? 200.0f : 150.0f;
            v.mudDb   = 2.0f;   v.mudHz   = woman ? 350.0f : 300.0f;  v.mudQ = 1.0f;
            v.boxDb   = -1.5f;  v.boxHz   = woman ? 600.0f : 500.0f;
            v.nasalDb = -0.5f;  v.nasalHz = woman ? 1200.0f : 1000.0f;
            v.hardDb  = -1.0f;  v.hardHz  = woman ? 3500.0f : 3000.0f;
            v.fizzDb  = 0.0f;   v.fizzHz  = woman ? 9000.0f : 7500.0f;
            v.bodyDb  = 1.0f;   v.bodyHz  = woman ? 210.0f : 125.0f;
            v.presDb  = 1.5f;   v.presHz  = woman ? 4800.0f : 4000.0f; v.presQ = 1.0f;
            v.airDb   = 1.5f;   v.airHz   = woman ? 12000.0f : 11000.0f;
            v.gateThr = -46.0f; v.gateRange = 10.0f; v.gateHold = 200.0f; v.gateRelease = 300.0f;
            v.compThr = -26.0f; v.compRatio = 2.0f; v.compAtk = 15.0f; v.compRel = 180.0f; v.compKnee = 8.0f;
            v.compMix = 100.0f; v.compMakeup = 4.0f;
            v.deessHz = woman ? 6800.0f : 5800.0f; v.deessThr = -12.0f; v.deessRange = 8.0f;
            v.satType = 0; v.satDrive = 0.0f; v.satMix = 0.0f;     // no colour unless the purpose wants it
            v.roomMix = 0.0f;                                        // no room: only for singing
            v.ceiling = -1.0f;
            return v;
        };

        struct Def
        {
            const char* name;
            std::function<void (Voicing&, bool woman)> tune;
            float trimMen, trimWomen;           // Kazanç: the loudness match (-20 LUFS on the voices measured)
            const char* womenName = nullptr;    // when the women's preset is called differently
        };
        // Each comment ends with the preset's elements after Low-Cut (LC), Otomatik Kazanç (OK),
        // Kompresör (K), De-esser (DS), Kazanç and Çıkış Limiteri, which every preset has. No speech
        // preset has a room: a live voice into a call, a stream or a recording should be dry -
        // added reverb only makes words less clear and tiring, and the room the voice is in is
        // already there (a room is added in post-production when wanted). Saturation only where a
        // colour is the purpose (radio, podcast, narration, warm, small microphones, singing).
        const std::vector<Def> defs {
            // everyday, safe on any microphone: a little cleaner, a little fuller, a little clearer;
            // transparent (no colour)
            // + Yakınlık, Çamur, Parametrik EQ, Gövde, Netlik, Hava, Kapı
            { TRK ("Doğal ve Dengeli"), [] (Voicing&, bool) {}, 3.4f, 2.9f },

            // close and warm, like two people talking: more body, softer top, gentle density, a
            // light tape colour
            // + Çamur, Parametrik EQ, Gövde, Netlik, Hava, Kapı, Saturasyon (bant)
            { TRK ("Podcast / Sohbet"), [] (Voicing& v, bool w)
              { v.proxDb = 0.0f; v.bodyDb = w ? 1.5f : 2.0f; v.mudDb = 2.5f; v.boxDb = -2.0f; v.hardDb = -1.5f; v.fizzDb = -1.0f;
                v.presDb = 1.0f; v.airDb = 1.0f; v.compThr = -28.0f; v.compRatio = 2.5f; v.compAtk = 12.0f; v.compRel = 150.0f;
                v.gateRange = 12.0f; v.satType = 1; v.satDrive = 3.0f; v.satMix = 40.0f; }, 4.7f, 4.4f },

            // games, Discord, streams: present over game sound, a firmer gate against keyboard / fan,
            // quicker compression (as a streaming chain: gate -> compressor ~3:1 -> limiter); clean
            // + Yakınlık, Çamur, Parametrik EQ, Netlik, Hava, Kapı
            { TRK ("Oyun / Canlı Yayın"), [] (Voicing& v, bool w)
              { v.lowCut = w ? 110.0f : 90.0f; v.proxDb = -2.0f; v.mudDb = 2.5f; v.nasalDb = -1.0f; v.bodyDb = 0.0f;
                v.presDb = 2.5f; v.presHz = w ? 4800.0f : 4200.0f; v.airDb = 2.0f;
                v.gateThr = -44.0f; v.gateRange = 14.0f; v.gateHold = 150.0f; v.gateRelease = 220.0f;
                v.compThr = -30.0f; v.compRatio = 3.0f; v.compAtk = 6.0f; v.compRel = 110.0f; v.compKnee = 6.0f; }, 6.7f, 6.1f },

            // meetings, lessons: words first - lean low end, the speech band forward, light dynamics.
            // Zoom / Teams add their own noise suppression and level control and send no air: no
            // body, no air, no colour - only what helps the words
            // + Yakınlık, Çamur, Parametrik EQ, Netlik, Kapı
            { TRK ("Toplantı / Ders"), [] (Voicing& v, bool w)
              { v.lowCut = w ? 120.0f : 100.0f; v.proxDb = -3.0f; v.mudDb = 3.0f; v.boxDb = -2.0f; v.hardDb = -0.5f;
                v.bodyDb = 0.0f; v.presDb = 1.5f; v.presHz = w ? 4000.0f : 3500.0f; v.presQ = 0.8f; v.airDb = 0.0f;
                v.gateThr = -44.0f; v.gateRange = 14.0f; v.gateHold = 150.0f; v.gateRelease = 220.0f;
                v.compThr = -28.0f; v.compRatio = 2.5f; v.compAtk = 8.0f; v.compRel = 140.0f; }, 5.3f, 4.6f },

            // radio presenter: big, dense, polished - more low end and air, firmer compression, tape
            // + (men: Yakınlık), Çamur, Parametrik EQ, Gövde, Netlik, Hava, Kapı, Saturasyon (bant)
            { TRK ("Radyo Spikeri"), [] (Voicing& v, bool w)
              { v.lowCut = w ? 95.0f : 70.0f; v.proxDb = w ? 0.0f : 1.0f; v.proxHz = w ? 180.0f : 120.0f;
                v.bodyDb = w ? 1.5f : 2.5f; v.bodyHz = w ? 200.0f : 115.0f; v.mudDb = 3.0f; v.boxDb = -2.0f; v.nasalDb = -1.5f;
                v.fizzDb = -1.0f; v.presDb = 2.5f; v.airDb = 2.5f;
                v.gateThr = -45.0f; v.gateRange = 14.0f;
                v.compThr = -32.0f; v.compRatio = 3.5f; v.compAtk = 5.0f; v.compRel = 90.0f; v.compKnee = 6.0f;
                v.satType = 1; v.satDrive = 5.0f; v.satMix = 60.0f; }, 8.2f, 7.8f },

            // news, corporate: neutral, firm, very clear - nothing coloured
            // + Yakınlık, Çamur, Parametrik EQ, Gövde, Netlik, Hava, Kapı
            { TRK ("Haber Sunucusu"), [] (Voicing& v, bool w)
              { v.lowCut = w ? 105.0f : 85.0f; v.bodyDb = 1.5f; v.mudDb = 2.5f; v.boxDb = -2.0f; v.nasalDb = -1.0f;
                v.presDb = 2.0f; v.presHz = w ? 4900.0f : 4300.0f;
                v.gateThr = -45.0f; v.gateRange = 12.0f;
                v.compThr = -29.0f; v.compRatio = 3.0f; v.compAtk = 8.0f; v.compRel = 120.0f; v.compKnee = 6.0f; }, 5.8f, 5.4f },

            // narration, documentary: deep, calm, controlled, the edge taken off, a light tape density;
            // dry (a narration's room, if any, is added in the edit)
            // + Çamur, Parametrik EQ, Gövde, Netlik, Kapı, Saturasyon (bant)
            { TRK ("Seslendirme / Belgesel"), [] (Voicing& v, bool w)
              { v.lowCut = w ? 85.0f : 65.0f; v.proxDb = 0.0f;
                v.bodyDb = w ? 1.5f : 2.5f; v.bodyHz = w ? 200.0f : 115.0f; v.mudDb = 3.0f; v.boxDb = -2.0f; v.hardDb = -2.0f;
                v.fizzDb = -1.5f; v.presDb = 1.0f; v.airDb = 0.0f;
                v.gateRange = 12.0f;
                v.compThr = -30.0f; v.compRatio = 3.0f; v.compAtk = 15.0f; v.compRel = 180.0f;
                v.deessThr = -13.0f; v.satType = 1; v.satDrive = 4.0f; v.satMix = 50.0f; }, 5.9f, 5.7f,
              TRK ("Seslendirme / Anlatıcı") },

            // audiobooks, hours of listening: natural and consistent - the gentlest top end, slow
            // soft dynamics, an unhurried gate that only turns the room down (room tone stays), no
            // colour; peaks at most -3 dB (as audiobook platforms ask, e.g. ACX: peaks <= -3 dB,
            // -23 to -18 dB RMS)
            // + Çamur, Parametrik EQ, Gövde, Kapı; limiter at -3 dBTP
            { TRK ("Sesli Kitap"), [] (Voicing& v, bool w)
              { v.lowCut = w ? 90.0f : 70.0f; v.proxDb = 0.0f; v.bodyDb = 1.5f; v.mudDb = 2.5f; v.hardDb = -2.0f;
                v.fizzDb = -2.0f; v.presDb = 0.0f; v.airDb = 0.0f;
                v.gateThr = -47.0f; v.gateHold = 250.0f; v.gateRelease = 400.0f;
                v.compThr = -28.0f; v.compRatio = 2.0f; v.compAtk = 20.0f; v.compRel = 250.0f; v.compKnee = 10.0f;
                v.deessThr = -13.0f; v.ceiling = -3.0f; }, 3.9f, 3.6f },

            // thin, light or bright voices: body and warmth added, the top softened, tape colour
            // + (men: Yakınlık), Çamur, Parametrik EQ, Gövde, Kapı, Saturasyon (bant)
            { TRK ("Sıcak ve Dolgun"), [] (Voicing& v, bool w)
              { v.proxDb = w ? 0.0f : 1.0f; v.bodyDb = w ? 3.0f : 3.5f; v.bodyHz = w ? 200.0f : 120.0f; v.mudDb = 1.5f; v.boxDb = -1.0f;
                v.hardDb = -2.0f; v.fizzDb = -1.5f; v.presDb = 0.0f; v.airDb = 0.0f;
                v.compThr = -28.0f; v.compRatio = 2.5f; v.compAtk = 12.0f; v.compRel = 160.0f;
                v.satType = 1; v.satDrive = 5.0f; v.satMix = 60.0f; }, 4.5f, 4.3f },

            // dull, muffled voices or dark microphones: less low-mid, more presence and air; clean
            // (harmonics would only add edge to an already lifted top)
            // + Yakınlık, Çamur, Parametrik EQ, Netlik, Hava, Kapı
            { TRK ("Net ve Parlak"), [] (Voicing& v, bool w)
              { v.lowCut = w ? 105.0f : 85.0f; v.proxDb = -2.5f; v.bodyDb = 0.0f; v.mudDb = 3.5f; v.boxDb = -2.0f;
                v.hardDb = 0.0f; v.presDb = 3.5f; v.presHz = w ? 5000.0f : 4500.0f; v.airDb = 3.5f; v.airHz = w ? 11000.0f : 10000.0f;
                v.gateThr = -44.0f; v.gateRange = 12.0f;
                v.compThr = -28.0f; v.compRatio = 2.5f; v.compAtk = 10.0f; v.compRel = 140.0f;
                v.deessThr = -11.0f; }, 5.0f, 4.4f },

            // sharp, edgy, hissing voices: the hard edge and the fizz taken down, the air a little
            // lower, de-esser earlier, slow soft compression; no saturation (its harmonics add edge)
            // + Yakınlık, Çamur, Parametrik EQ, Gövde, Hava (-), Kapı
            { TRK ("Yumuşak ve Pürüzsüz"), [] (Voicing& v, bool)
              { v.bodyDb = 1.5f; v.boxDb = -1.0f; v.nasalDb = -1.0f; v.hardDb = -3.0f; v.fizzDb = -2.5f;
                v.presDb = 0.0f; v.airDb = -1.0f;
                v.gateThr = -47.0f; v.gateRelease = 350.0f;
                v.compThr = -28.0f; v.compRatio = 2.0f; v.compAtk = 20.0f; v.compRel = 250.0f; v.compKnee = 10.0f;
                v.deessThr = -15.0f; v.deessRange = 9.0f; }, 4.0f, 3.6f },

            // talking right into the microphone: the boom and the box of the proximity effect removed
            // + Yakınlık, Çamur, Parametrik EQ, Netlik, Hava, Kapı
            { TRK ("Yakın Mikrofon"), [] (Voicing& v, bool w)
              { v.lowCut = w ? 105.0f : 85.0f; v.proxDb = -4.5f; v.proxHz = w ? 230.0f : 170.0f;
                v.mudDb = 4.0f; v.mudHz = w ? 380.0f : 300.0f; v.mudQ = 1.2f; v.boxDb = -2.5f;
                v.bodyDb = 0.0f; v.presDb = 1.0f; v.gateThr = -45.0f;
                v.compThr = -28.0f; v.compRatio = 2.5f; v.compAtk = 12.0f; v.compRel = 150.0f; }, 5.1f, 4.7f },

            // fan, keyboard, street: a firm but natural gate (20 dB down between sentences), the hiss
            // band down, no air (it would lift the noise), light compression (compression lifts the
            // noise between words)
            // + Yakınlık, Çamur, Parametrik EQ, Netlik, Kapı
            { TRK ("Gürültülü Ortam"), [] (Voicing& v, bool w)
              { v.lowCut = w ? 120.0f : 100.0f; v.proxDb = -2.0f; v.mudDb = 2.5f; v.fizzDb = -2.0f; v.bodyDb = 0.0f;
                v.presDb = 2.0f; v.airDb = 0.0f;
                v.gateThr = -40.0f; v.gateRange = 20.0f; v.gateHold = 150.0f; v.gateRelease = 200.0f;
                v.compThr = -26.0f; v.compRatio = 2.0f; v.compAtk = 10.0f; v.compRel = 150.0f; }, 3.8f, 3.1f },

            // headsets, laptop microphones: thin, honky and harsh - body back, nose and edge down, no
            // air (these capsules have none to lift, only hiss); light tube harmonics make the small
            // capsule sound fuller
            // + Yakınlık, Çamur, Parametrik EQ, Gövde, Netlik, Kapı, Saturasyon (tüp)
            { TRK ("Kulaklık / Dizüstü Mikrofonu"), [] (Voicing& v, bool w)
              { v.lowCut = w ? 120.0f : 100.0f; v.proxDb = w ? 1.0f : 1.5f; v.proxHz = w ? 200.0f : 150.0f;
                v.bodyDb = w ? 2.0f : 3.0f; v.mudDb = 1.0f; v.boxDb = -1.0f; v.nasalDb = -2.0f; v.nasalHz = w ? 1400.0f : 1200.0f;
                v.hardDb = -3.0f; v.fizzDb = -2.0f; v.presDb = 1.0f; v.airDb = 0.0f;
                v.gateThr = -44.0f; v.gateRange = 14.0f;
                v.compThr = -28.0f; v.compRatio = 3.0f; v.compAtk = 10.0f; v.compRel = 140.0f;
                v.satDrive = 3.0f; v.satMix = 40.0f; }, 5.1f, 4.7f },

            // singing: the only preset with a room - a small, darker room (14 % mix) that carries the
            // voice without washing it; a very gentle gate that only acts in long pauses (sustained
            // notes and breaths stay), more air, tube colour
            // + Yakınlık, Çamur, Parametrik EQ, Gövde, Netlik, Hava, Kapı, Saturasyon (tüp), Oda Yankısı
            { TRK ("Şarkı / Vokal"), [] (Voicing& v, bool w)
              { v.lowCut = w ? 110.0f : 90.0f; v.proxDb = -1.5f; v.mudDb = 2.5f; v.hardDb = -1.5f; v.bodyDb = 1.0f;
                v.presDb = 2.0f; v.airDb = 3.0f;
                v.gateThr = -47.0f; v.gateRange = 8.0f; v.gateHold = 300.0f; v.gateRelease = 500.0f;
                v.compThr = -26.0f; v.compRatio = 3.0f; v.compAtk = 10.0f; v.compRel = 150.0f;
                v.deessThr = -11.0f; v.satDrive = 3.0f; v.satMix = 40.0f;
                v.roomSize = 45.0f; v.roomDamping = 55.0f; v.roomMix = 14.0f; }, 4.7f, 4.0f },
        };

        std::vector<Preset> out;
        for (const bool woman : { false, true })
            for (auto& d : defs)
            {
                Voicing v = base (woman);
                d.tune (v, woman);
                v.trimDb = woman ? d.trimWomen : d.trimMen;
                Preset p;
                p.group = u8 (woman ? women : men);
                p.shortName = u8 (woman && d.womenName != nullptr ? d.womenName : d.name);
                p.name = u8 (woman ? prefixWomen : prefixMen) + p.shortName;   // stored (config.xml): Turkish
                p.factory = true;
                p.elements = buildChain (v);
                out.push_back (std::move (p));
            }
        return out;
    }

    juce::String sanitize (const juce::String& name)
    {
        // a file name for any letters: keep letters/digits/space/-/_ (Turkish letters included)
        juce::String f;
        for (auto c : name.trim())
            f << (juce::CharacterFunctions::isLetterOrDigit (c) || c == ' ' || c == '-' || c == '_' ? juce::String::charToString (c) : "_");
        f = f.trim();
        return f.isEmpty() ? juce::String ("preset") : f.substring (0, maxNameLength);
    }
}

juce::Array<PluginEntryState> buildChain (const Voicing& v)
{
    juce::Array<PluginEntryState> out;
    for (auto& s : chainFor (v)) out.add (effectEntry (s));
    return out;
}

const std::vector<Preset>& factoryPresets()
{
    static const std::vector<Preset> list = build();
    return list;
}

juce::String displayName (const juce::String& name)
{
    for (auto& p : factoryPresets())
        if (p.name == name)
            return micvst::tr (p.group == juce::String::fromUTF8 (TRK ("Erkek Sesi Ayarları")) ? prefixMen : prefixWomen) + micvst::tr (p.shortName);
    return name;
}

const Preset* findFactory (const juce::String& name)
{
    for (auto& p : factoryPresets())
        if (p.name == name) return &p;
    return nullptr;
}

juce::File userFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("MicVST").getChildFile ("presets");
}

std::unique_ptr<juce::XmlElement> toXml (const juce::String& name, const juce::Array<PluginEntryState>& elements)
{
    auto xml = std::make_unique<juce::XmlElement> (presetTag.toString());
    xml->setAttribute (versionAttr, 1);
    xml->setAttribute (nameAttr, name);
    if (auto list = pluginsToValueTree (elements).createXml())
        xml->addChildElement (list.release());
    return xml;
}

bool fromXml (const juce::XmlElement& xml, Preset& out)
{
    if (! xml.hasTagName (presetTag.toString())) return false;
    out.name = xml.getStringAttribute (nameAttr).trim();
    if (out.name.isEmpty()) return false;
    out.factory = false;
    out.elements.clear();
    if (auto* list = xml.getChildByName ("plugins"))
        out.elements = pluginsFromValueTree (juce::ValueTree::fromXml (*list));
    return true;
}

std::vector<Preset> loadUserPresets (const juce::File& folder)
{
    std::vector<Preset> out;
    for (auto& f : folder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + extension))
    {
        Preset p;
        auto xml = juce::XmlDocument::parse (f);
        if (xml != nullptr && fromXml (*xml, p))
        {
            p.file = f;
            out.push_back (std::move (p));
        }
        else
        {
            juce::Logger::writeToLog (micvst::tr ("Hazır ayar dosyası okunamadı, atlandı: ") + f.getFullPathName());
        }
    }
    std::sort (out.begin(), out.end(), [] (const Preset& a, const Preset& b) { return a.name.compareNatural (b.name) < 0; });
    return out;
}

juce::String nameProblem (const juce::String& raw)
{
    const auto name = raw.trim();
    if (name.isEmpty()) return micvst::tr ("Bir ad yazın.");
    if (name.length() > maxNameLength) return micvst::tr ("Ad en fazla ") + juce::String (maxNameLength) + micvst::tr (" karakter olabilir.");
    for (auto& f : factoryPresets())
        if (f.name.equalsIgnoreCase (name)) return micvst::tr ("Bu ad hazır bir ayarın adı; başka bir ad seçin.");
    return {};
}

juce::File fileFor (const juce::String& name, const juce::File& folder)
{
    // the same name always gives the same file; different names that sanitize alike get a number
    for (auto& p : loadUserPresets (folder))
        if (p.name == name.trim()) return p.file;
    auto base = sanitize (name);
    auto f = folder.getChildFile (base + extension);
    for (int k = 2; f.exists(); ++k)
        f = folder.getChildFile (base + " (" + juce::String (k) + ")" + extension);
    return f;
}

bool saveUserPreset (const juce::String& raw, const juce::Array<PluginEntryState>& elements, juce::String& error,
                     const juce::File& folder)
{
    const auto name = raw.trim();
    if (const auto problem = nameProblem (name); problem.isNotEmpty()) { error = problem; return false; }
    if (! folder.createDirectory()) { error = micvst::tr ("Klasör oluşturulamadı: ") + folder.getFullPathName(); return false; }
    const auto file = fileFor (name, folder);
    auto xml = toXml (name, elements);
    // writeTo goes through a temporary file that replaces the old one at once
    if (! xml->writeTo (file)) { error = micvst::tr ("Dosya yazılamadı: ") + file.getFullPathName(); return false; }
    // read back: what is on disk must be what was meant
    Preset check;
    auto back = juce::XmlDocument::parse (file);
    if (back == nullptr || ! fromXml (*back, check) || check.name != name || check.elements.size() != elements.size())
    {
        error = micvst::tr ("Kaydedilen dosya doğrulanamadı: ") + file.getFullPathName();
        return false;
    }
    for (int i = 0; i < elements.size(); ++i)
        if (check.elements[i].state != elements[i].state || check.elements[i].fileOrId != elements[i].fileOrId)
        {
            error = micvst::tr ("Kaydedilen dosya doğrulanamadı: ") + file.getFullPathName();
            return false;
        }
    return true;
}

bool deleteUserPreset (const juce::String& name, const juce::File& folder)
{
    for (auto& p : loadUserPresets (folder))
        if (p.name == name) return p.file.deleteFile();
    return false;
}

bool renameUserPreset (const juce::String& from, const juce::String& to, juce::String& error, const juce::File& folder)
{
    const auto newName = to.trim();
    if (const auto problem = nameProblem (newName); problem.isNotEmpty()) { error = problem; return false; }
    for (auto& p : loadUserPresets (folder))
        if (p.name == newName && newName != from) { error = micvst::tr ("Bu adda bir ayar zaten var."); return false; }
    for (auto& p : loadUserPresets (folder))
        if (p.name == from)
        {
            const auto oldFile = p.file;
            if (! saveUserPreset (newName, p.elements, error, folder)) return false;
            if (fileFor (newName, folder) != oldFile) oldFile.deleteFile();
            return true;
        }
    error = micvst::tr ("Ayar bulunamadı.");
    return false;
}
juce::String fileNameFor (const juce::String& name)
{
    return sanitize (name) + extension;
}

bool exportPreset (const juce::String& name, const juce::Array<PluginEntryState>& elements, const juce::File& file,
                   juce::String& error)
{
    auto xml = toXml (name.trim().isEmpty() ? juce::String ("MicVST") : name.trim().substring (0, maxNameLength), elements);
    if (! xml->writeTo (file)) { error = micvst::tr ("Dosya yazılamadı: ") + file.getFullPathName(); return false; }
    Preset check;
    auto back = juce::XmlDocument::parse (file);
    if (back == nullptr || ! fromXml (*back, check) || check.elements.size() != elements.size())
    {
        error = micvst::tr ("Kaydedilen dosya doğrulanamadı: ") + file.getFullPathName();
        return false;
    }
    return true;
}

bool importPreset (const juce::File& file, juce::String& nameOut, juce::String& error, const juce::File& folder)
{
    Preset p;
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! fromXml (*xml, p))
    {
        error = micvst::tr ("Bu dosya bir MicVST hazır ayarı değil: ") + file.getFullPathName();
        return false;
    }
    const auto existing = loadUserPresets (folder);
    auto taken = [&] (const juce::String& n)
    {
        if (nameProblem (n).isNotEmpty()) return true;
        for (auto& e : existing) if (e.name.equalsIgnoreCase (n)) return true;
        return false;
    };
    const auto base = p.name.trim().substring (0, maxNameLength - 6);
    juce::String name = p.name.trim().substring (0, maxNameLength);
    for (int k = 2; taken (name) && k < 1000; ++k)
        name = base + " (" + juce::String (k) + ")";
    if (! saveUserPreset (name, p.elements, error, folder)) return false;
    nameOut = name;
    return true;
}
}
