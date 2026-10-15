#pragma once
#include <juce_core/juce_core.h>

// Everything MicVST shows to the user - window texts, menus, dialogs, tooltips and the log file -
// is Turkish (MicVST 1.2.1). Two rules keep that intact:
//
//  * Never build user-facing text with juce::String (const char*): JUCE reads such literals as
//    Latin-1, so every UTF-8 letter (ç, ğ, ı, ö, ş, ü) turns into two garbage characters (the
//    "Audio: lÃ¤uft" lines in 1.1.1's log came from exactly that). Use tr ("...") instead; the
//    sources are UTF-8 (/utf-8 for MSVC).
//  * Decimal numbers use the Turkish decimal comma ("49,7 ms"), percentages "%45".
namespace micvst
{
    inline juce::String tr (const char* utf8Text) { return juce::String::fromUTF8 (utf8Text); }

    // Number with a fixed count of decimals and a decimal comma.
    inline juce::String trNumber (double value, int decimals)
    {
        if (decimals <= 0)
            return juce::String (juce::roundToInt (value));
        return juce::String (value, decimals).replaceCharacter ('.', ',');
    }

    // "%45" (Turkish places the percent sign before the number).
    inline juce::String trPercent (double percent) { return "%" + juce::String (juce::roundToInt (percent)); }

    // Turkish text for a stored scan-skip reason. plugin_cache.xml keeps the English key words
    // ("unresponsive", "failed (exit 0x...)", ...) - also those written by older versions.
    juce::String scanSkipReasonText (const juce::String& reason);

    // Turkish texts for the strings JUCE itself shows (TRANS): the buttons of native message
    // boxes ("OK"/"Cancel"/...) and the right-click menu of text fields. Installed once at start.
    void installTurkishTranslations();
}
