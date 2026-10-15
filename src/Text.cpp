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
)MAP"), false));
    }
}
