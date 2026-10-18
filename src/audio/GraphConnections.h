#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

using NodeID = juce::AudioProcessorGraph::NodeID;

struct ChannelConnection
{
    NodeID sourceNode; int sourceChannel;
    NodeID destNode;   int destChannel;
    bool operator== (const ChannelConnection& o) const
    {
        return sourceNode == o.sourceNode && sourceChannel == o.sourceChannel
            && destNode == o.destNode && destChannel == o.destChannel;
    }
};

// Ein Glied der Kette mit seiner tatsächlichen Kanalzahl (Eingang am Knoten = numIns,
// Ausgang = numOuts). Reihenfolge: input-Node, dann Plugins, dann output-Node.
// passThrough: ein kanalneutrales Glied (der eingebaute "Kazanç"/Gain) übernimmt die Breite
// seines Vorgängers (höchstens seine eigene Kanalzahl) - hinter einem Mono-Teil bleibt die Kette
// mono, hinter einem Stereo-Teil stereo.
struct NodeChannels
{
    NodeID id;
    int    numIns;
    int    numOuts;
    bool   passThrough = false;
};

// Reine Funktion: verbindet je benachbartes Knotenpaar Kanal i -> i für
// i in [0, min(quelle.numOuts, ziel.numIns)). Dadurch ergibt sich automatisch das
// richtige Verhalten bei wechselnder Kanalbreite (z. B. ein Mono->Stereo-Glied:
// davor 1 Kanal, danach 2). passThrough-Glieder werden vorher auf die Breite ihres
// Vorgängers gesetzt.
std::vector<ChannelConnection> computeChainConnections (const std::vector<NodeChannels>& seq);
