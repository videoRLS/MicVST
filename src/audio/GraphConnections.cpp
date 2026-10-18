#include "audio/GraphConnections.h"

std::vector<ChannelConnection> computeChainConnections (const std::vector<NodeChannels>& chainIn)
{
    // Kanalneutrale Glieder übernehmen die Breite ihres (bereits aufgelösten) Vorgängers.
    std::vector<NodeChannels> seq (chainIn);
    for (size_t i = 1; i < seq.size(); ++i)
        if (seq[i].passThrough)
            seq[i].numIns = seq[i].numOuts = juce::jmin (seq[i - 1].numOuts, seq[i].numIns, seq[i].numOuts);

    std::vector<ChannelConnection> result;

    for (size_t hop = 0; hop + 1 < seq.size(); ++hop)
    {
        const int n = juce::jmin (seq[hop].numOuts, seq[hop + 1].numIns);
        for (int ch = 0; ch < n; ++ch)
            result.push_back ({ seq[hop].id, ch, seq[hop + 1].id, ch });
    }

    return result;
}
