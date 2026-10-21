#include <juce_core/juce_core.h>
#include "Text.h"
#include <map>

// The Turkish user-facing texts: UTF-8 handling, number formatting, stored scan reasons and the
// translations of JUCE's own strings.
struct TextTest : juce::UnitTest
{
    TextTest() : juce::UnitTest ("Turkish texts") {}

    void runTest() override
    {
        using micvst::tr;

        beginTest ("UTF-8 literals keep the Turkish letters (no Latin-1 double encoding)");
        {
            const auto s = tr ("Çıkış ğüşöı İĞÜŞÖ");
            expectEquals (s.length(), 17);
            expect (s[0] == (juce::juce_wchar) 0x00C7);   // Ç
            expect (s[1] == (juce::juce_wchar) 0x0131);   // ı (dotless i)
            expect (s.contains (juce::String (juce::CharPointer_UTF8 ("\xc4\xb0"))));   // İ
            expect (s.toUTF8().sizeInBytes() > (size_t) s.length() + 1, "multi-byte characters stay single characters");
        }

        beginTest ("decimal comma and Turkish percent");
        {
            expectEquals (micvst::trNumber (49.66, 1), juce::String ("49,7"));
            expectEquals (micvst::trNumber (10.0, 1), juce::String ("10,0"));
            expectEquals (micvst::trNumber (12.6, 0), juce::String ("13"));
            expectEquals (micvst::trPercent (45.4), juce::String ("%45"));
            expectEquals (micvst::trPercent (112.6), juce::String ("%113"));
        }

        beginTest ("stored scan-skip reasons are shown in Turkish (also those of older versions)");
        {
            expectEquals (micvst::scanSkipReasonText ("unresponsive"), tr ("yanıt vermedi"));
            expectEquals (micvst::scanSkipReasonText ("skipped"), tr ("atlandı"));
            expectEquals (micvst::scanSkipReasonText ("failed"), tr ("taranamadı"));
            expectEquals (micvst::scanSkipReasonText ("failed (exit 0xC0000005)"), tr ("taranamadı (hata 0xC0000005)"));
            expectEquals (micvst::scanSkipReasonText ("crashed (2 plugin(s) rescued, 0xC0000005)"),
                          tr ("çöktü (2 eklenti kurtarıldı, 0xC0000005)"));
            expectEquals (micvst::scanSkipReasonText ("crashed (0 plugin(s) rescued)"), tr ("çöktü (0 eklenti kurtarıldı)"));
        }

        beginTest ("JUCE's own texts are translated (message-box buttons, text-field menu)");
        {
            micvst::installTurkishTranslations();
            expectEquals (TRANS ("OK"), tr ("Tamam"));
            expectEquals (TRANS ("Cancel"), tr ("İptal"));
            expectEquals (TRANS ("Select All"), tr ("Tümünü seç"));
            expectEquals (TRANS ("Paste"), tr ("Yapıştır"));
            expectEquals (TRANS ("(no choices)"), tr ("(seçenek yok)"));
            juce::LocalisedStrings::setCurrentMappings (nullptr);
        }

        beginTest ("device errors in the log are Turkish (also JUCE's untranslated ones)");
        {
            micvst::installTurkishTranslations();
            expectEquals (micvst::deviceErrorText ("No such device: CABLE Input (VB-Audio Virtual Cable)"),
                          tr ("Cihaz bulunamadı: CABLE Input (VB-Audio Virtual Cable)"));
            expect (micvst::deviceErrorText ("Can't open the audio device!\n\nThis may be because another application is currently using the same device")
                        .startsWith (tr ("Ses cihazı açılamadı")));
            expectEquals (micvst::deviceErrorText (TRANS ("Couldn't open the input device!")), tr ("Giriş cihazı açılamadı!"));
            expectEquals (micvst::deviceErrorText ("Couldn't start the output device!"), tr ("Çıkış cihazı başlatılamadı!"));
            juce::LocalisedStrings::setCurrentMappings (nullptr);
        }

        beginTest ("every language has a translation for every Turkish text, with the same edges");
        {
            int keys = 0;
            for (auto* k = micvst::translationKeys(); *k != nullptr; ++k) ++keys;
            expectGreaterThan (keys, 300);
            for (auto& l : micvst::languages())
            {
                if (juce::String (l.id) == "tr")
                {
                    expect (micvst::translationTable ("tr") == nullptr, "Türkçe needs no table");
                    continue;
                }
                const auto* table = micvst::translationTable (l.id);
                expect (table != nullptr, juce::String ("no table for ") + l.id);
                if (table == nullptr) continue;
                std::map<juce::String, juce::String> m;
                for (auto* p = table; p->key != nullptr; ++p)
                    m[juce::String::fromUTF8 (p->key)] = juce::String::fromUTF8 (p->text);
                expectEquals ((int) m.size(), keys, juce::String ("entries of ") + l.id);
                int missing = 0, bad = 0;
                for (auto* k = micvst::translationKeys(); *k != nullptr; ++k)
                {
                    const auto key = juce::String::fromUTF8 (*k);
                    const auto it = m.find (key);
                    if (it == m.end()) { ++missing; continue; }
                    const auto& t = it->second;
                    const bool sameEdges = key.startsWithChar (' ') == t.startsWithChar (' ')
                                        && key.endsWithChar (' ') == t.endsWithChar (' ')
                                        && juce::StringArray::fromLines (key).size() == juce::StringArray::fromLines (t).size();
                    if (t.trim().isEmpty() || ! sameEdges) ++bad;
                }
                expectEquals (missing, 0, juce::String ("missing in ") + l.id);
                expectEquals (bad, 0, juce::String ("empty or different edges in ") + l.id);
            }
        }

        beginTest ("switching the language: texts, numbers, JUCE's buttons; unknown id = Turkish");
        {
            micvst::setLanguage ("en");
            expectEquals (micvst::currentLanguage(), juce::String ("en"));
            expectEquals (tr ("Kaydet"), juce::String ("Save"));
            expectEquals (tr (juce::String::fromUTF8 (TRK ("Erkek Sesi Ayarları"))), juce::String ("Male Voice Presets"));
            expectEquals (tr ("bu metin tabloda yok"), juce::String ("bu metin tabloda yok"));
            expectEquals (micvst::trNumber (49.66, 1), juce::String ("49.7"));
            expectEquals (micvst::trPercent (45.4), juce::String ("45%"));
            expectEquals (TRANS ("Cancel"), juce::String ("Cancel"));

            micvst::setLanguage ("de");
            expectEquals (tr ("Kaydet"), juce::String ("Speichern"));
            expectEquals (micvst::trNumber (49.66, 1), juce::String ("49,7"));
            expectEquals (TRANS ("Cancel"), juce::String ("Abbrechen"));

            micvst::setLanguage ("ja");
            expectEquals (tr ("Kaydet"), juce::String (juce::CharPointer_UTF8 ("\xe4\xbf\x9d\xe5\xad\x98")));   // 保存

            micvst::setLanguage ("xx");
            expectEquals (micvst::currentLanguage(), juce::String ("tr"));
            expectEquals (tr ("Kaydet"), juce::String (juce::CharPointer_UTF8 ("Kaydet")));
            expectEquals (micvst::trPercent (45.4), juce::String ("%45"));
            expectEquals (TRANS ("Cancel"), tr ("İptal"));

            micvst::setLanguage ("tr");
            juce::LocalisedStrings::setCurrentMappings (nullptr);
        }
    }
};

static TextTest textTest;
