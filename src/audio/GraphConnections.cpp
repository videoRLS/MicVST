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

        // A mono part (mono microphone, a plug-in with a mono bus) followed by a stereo plug-in:
        // the one channel goes into both of its inputs. Before, only its left input was fed and
        // the right one got silence - the voice then came out of the left cable channel only.
        // Not into the output node: a chain that ends mono stays one channel there (the engine
        // puts it on both cable channels).
        const bool intoOutput = hop + 2 == seq.size();
        if (n == 1 && seq[hop].numOuts == 1 && seq[hop + 1].numIns >= 2 && ! intoOutput)
            result.push_back ({ seq[hop].id, 0, seq[hop + 1].id, 1 });
    }

    return result;
}
