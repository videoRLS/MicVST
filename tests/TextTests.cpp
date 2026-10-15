#include <juce_core/juce_core.h>
#include "Text.h"

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
    }
};

static TextTest textTest;
