#include "ui/PluginSearch.h"
#include "audio/BuiltInEffects.h"
#include "Text.h"
#include <algorithm>

juce::Array<juce::PluginDescription> filterPlugins (const juce::Array<juce::PluginDescription>& all,
                                                    const juce::String& query)
{
    const auto q = query.trim();
    juce::Array<juce::PluginDescription> out;
    for (auto& d : all)
        if (q.isEmpty() || d.name.containsIgnoreCase (q) || d.manufacturerName.containsIgnoreCase (q))
            out.add (d);

    // The built-in effect groups ("Dahili - Ton ve EQ", ...) come right after "Dahili", in the order
    // of the effect list (tone first, meters last), each effect at its place in that list; all other
    // manufacturers follow A-Z as before.
    auto groupRank = [] (const juce::PluginDescription& d)
    {
        if (d.fileOrIdentifier.startsWith ("builtin:"))
        {
            int k = 0;
            for (auto& i : builtin::effects())
            {
                ++k;
                if (d.fileOrIdentifier == i.id) return k;
            }
        }
        return 0;
    };
    std::stable_sort (out.begin(), out.end(),
                      [&] (const juce::PluginDescription& a, const juce::PluginDescription& b)
    {
        // the effects sort as manufacturer "Dahili", behind its own four entries, in list order
        const auto dahili = micvst::tr ("Dahili");   // as the four older built-ins are listed
        const int ra = groupRank (a), rb = groupRank (b);
        const auto& ma = ra != 0 ? dahili : a.manufacturerName;
        const auto& mb = rb != 0 ? dahili : b.manufacturerName;
        const int m = ma.compareIgnoreCase (mb);
        if (m != 0) return m < 0;
        if (ra != rb) return ra < rb;
        return a.name.compareIgnoreCase (b.name) < 0;
    });
    return out;
}
