#include "Text.h"
#include <atomic>
#include <memory>
#include <unordered_map>

namespace micvst
{
namespace
{
    struct StringHash { size_t operator() (const juce::String& s) const noexcept { return (size_t) s.hash(); } };
    using Table = std::unordered_map<juce::String, juce::String, StringHash>;

    // The current table is replaced as a whole (message thread); readers on other threads (device
    // error messages, log lines) see either the old or the new one. Old tables are kept alive.
    std::atomic<const Table*> currentTable { nullptr };
    std::atomic<int> currentIndex { 0 };

    std::vector<std::unique_ptr<Table>>& allTables()
    {
        static std::vector<std::unique_ptr<Table>> t;
        return t;
    }

    const Language& lang() { return languages()[(size_t) currentIndex.load (std::memory_order_acquire)]; }

    void installJuceTexts()
    {
        // JUCE's own texts that can appear in MicVST (message-box buttons, the right-click menu of
        // the search field, device errors). Format: juce::LocalisedStrings; the right-hand sides
        // are the Turkish texts, translated into the current language.
        static const char* const pairs[][2] = {
            { "OK", TRK ("Tamam") }, { "Cancel", TRK ("İptal") }, { "Yes", TRK ("Evet") }, { "No", TRK ("Hayır") },
            { "Cut", TRK ("Kes") }, { "Copy", TRK ("Kopyala") }, { "Paste", TRK ("Yapıştır") }, { "Delete", TRK ("Sil") },
            { "Select All", TRK ("Tümünü seç") }, { "Undo", TRK ("Geri al") }, { "Redo", TRK ("Yinele") },
            { "(no choices)", TRK ("(seçenek yok)") },
            { "Couldn't open the input device!", TRK ("Giriş cihazı açılamadı!") },
            { "Couldn't open the output device!", TRK ("Çıkış cihazı açılamadı!") },
            { "Couldn't open the output device (buffer size mismatch)", TRK ("Çıkış cihazı açılamadı (tampon boyutu uyuşmuyor)") },
            { "Couldn't start the input device!", TRK ("Giriş cihazı başlatılamadı!") },
            { "Couldn't start the output device!", TRK ("Çıkış cihazı başlatılamadı!") },
            { "The input and output devices don't share a common sample rate!", TRK ("Giriş ve çıkış cihazlarının ortak örnekleme hızı yok!") },
        };
        juce::String map;
        map << "language: " << lang().id << "\ncountries: " << lang().id << "\n\n";
        for (auto& p : pairs)
            map << "\"" << p[0] << "\" = \"" << tr (p[1]).replace ("\"", "\\\"") << "\"\n";
        juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (map, false));
    }
}

const std::vector<Language>& languages()
{
    static const std::vector<Language> list {
        { "tr", "Türkçe",                                 true,  true  },
        { "en", "English",                                false, false },
        { "zh", "中文 (简体)",                              false, false },
        { "hi", "हिन्दी",                                   false, false },
        { "es", "Español",                                true,  false },
        { "fr", "Français",                               true,  false },
        { "ar", "العربية",                                  false, false },
        { "pt", "Português",                              true,  false },
        { "ru", "Русский",                                true,  false },
        { "de", "Deutsch",                                true,  false },
        { "ja", "日本語",                                  false, false },
    };
    return list;
}

void setLanguage (const juce::String& id)
{
    int index = 0;
    for (int i = 0; i < (int) languages().size(); ++i)
        if (id == languages()[(size_t) i].id) index = i;

    const Table* table = nullptr;
    if (index != 0)
        if (auto* pairs = translationTable (languages()[(size_t) index].id))
        {
            auto t = std::make_unique<Table>();
            for (auto* p = pairs; p->key != nullptr; ++p)
                (*t)[juce::String::fromUTF8 (p->key)] = juce::String::fromUTF8 (p->text);
            table = t.get();
            allTables().push_back (std::move (t));
        }
    currentIndex.store (index, std::memory_order_release);
    currentTable.store (table, std::memory_order_release);
    installJuceTexts();
}

juce::String currentLanguage() { return lang().id; }

juce::String tr (const juce::String& turkish)
{
    if (auto* t = currentTable.load (std::memory_order_acquire))
    {
        const auto it = t->find (turkish);
        if (it != t->end()) return it->second;
    }
    return turkish;
}

juce::String tr (const char* utf8Text) { return tr (juce::String::fromUTF8 (utf8Text)); }

juce::String trNumber (double value, int decimals)
{
    if (decimals <= 0)
        return juce::String (juce::roundToInt (value));
    auto s = juce::String (value, decimals);
    return lang().decimalComma ? s.replaceCharacter ('.', ',') : s;
}

juce::String trPercent (double percent)
{
    const auto n = juce::String (juce::roundToInt (percent));
    return lang().percentFirst ? "%" + n : n + "%";
}

juce::String scanSkipReasonText (const juce::String& reason)
{
    // Gespeicherte Gründe (plugin_cache.xml, auch aus älteren Versionen) sind englische
    // Schlüsselwörter; angezeigt wird die gewählte Sprache. Der Hex-Code eines Absturzes bleibt erhalten.
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
    installJuceTexts();
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
