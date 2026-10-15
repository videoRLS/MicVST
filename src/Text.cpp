#include "Text.h"

namespace micvst
{
    juce::String scanSkipReasonText (const juce::String& reason)
    {
        // Gespeicherte Gründe (plugin_cache.xml, auch aus älteren Versionen) sind englische
        // Schlüsselwörter; angezeigt wird Türkisch. Der Hex-Code eines Absturzes bleibt erhalten.
        const auto code = reason.fromFirstOccurrenceOf ("0x", true, false).upToFirstOccurrenceOf (")", false, false);
        if (reason == "unresponsive") return tr ("yanıt vermedi");
        if (reason == "skipped")      return tr ("atlandı");
        if (reason.startsWith ("crashed"))
        {
            const auto rescued = reason.fromFirstOccurrenceOf ("(", false, false).getIntValue();
            return tr ("çöktü (") + juce::String (rescued) + tr (" eklenti kurtarıldı")
                   + (code.isNotEmpty() ? ", " + code : juce::String()) + ")";
        }
        if (reason.startsWith ("failed"))
            return tr ("taranamadı") + (code.isNotEmpty() ? " (" + tr ("hata ") + code + ")" : juce::String());
        return reason;
    }

    void installTurkishTranslations()
    {
        // JUCE's own texts that can appear in MicVST (message-box buttons, the right-click menu
        // of the search field, an empty device list). Format: juce::LocalisedStrings.
        juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (tr (R"MAP(
language: Turkish
countries: tr

"OK" = "Tamam"
"Cancel" = "İptal"
"Yes" = "Evet"
"No" = "Hayır"
"Cut" = "Kes"
"Copy" = "Kopyala"
"Paste" = "Yapıştır"
"Delete" = "Sil"
"Select All" = "Tümünü seç"
"Undo" = "Geri al"
"Redo" = "Yinele"
"(no choices)" = "(seçenek yok)"
"Couldn't open the input device!" = "Giriş cihazı açılamadı!"
"Couldn't open the output device!" = "Çıkış cihazı açılamadı!"
"Couldn't open the output device (buffer size mismatch)" = "Çıkış cihazı açılamadı (tampon boyutu uyuşmuyor)"
"Couldn't start the input device!" = "Giriş cihazı başlatılamadı!"
"Couldn't start the output device!" = "Çıkış cihazı başlatılamadı!"
"The input and output devices don't share a common sample rate!" = "Giriş ve çıkış cihazlarının ortak örnekleme hızı yok!"
)MAP"), false));
    }

    juce::String deviceErrorText (const juce::String& error)
    {
        if (error.startsWith ("No such device: "))
            return tr ("Cihaz bulunamadı: ") + error.fromFirstOccurrenceOf ("No such device: ", false, false);
        if (error.startsWith ("Can't open the audio device!"))
            return tr ("Ses cihazı açılamadı - başka bir program cihazı tek başına kullanıyor olabilir");
        return juce::translate (error);
    }
}
