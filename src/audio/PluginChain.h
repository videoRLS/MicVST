#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/GraphConnections.h"
#include <atomic>
#include <vector>

// Verwaltet die geordnete VST3-Kette innerhalb eines AudioProcessorGraph.
// Hält pro Plugin einen Node + Bypass-Flag. Baut bei jeder Änderung die
// Verbindungen input->...->output neu auf (via computeChainConnections).
class PluginChain
{
public:
    // Marker im fileOrId-Feld für die internen Glieder (kein VST).
    static constexpr const char* monoToStereoId = "builtin:mono2stereo";
    static constexpr const char* stereoToMonoId = "builtin:stereo2mono";
    static constexpr const char* gainId         = "builtin:gain";   // "Kazanç" (BuiltInGain.h)

    // pluginId/-Name/-Uid identifizieren das Plugin INNERHALB seiner Datei: eine VST3-Datei
    // kann viele Plugins enthalten (Waves-WaveShell, LSP-Bündel). Nur über den Dateipfad
    // wiederhergestellt, wurde aus jedem Plugin einer solchen Datei dasselbe (bis 1.2.0).
    struct Entry { NodeID node; juce::String fileOrId; bool bypassed = false;
                   juce::String pluginId, pluginName; int pluginUid = 0;
                   bool isBuiltIn() const { return fileOrId.startsWith ("builtin:"); } };

    PluginChain (juce::AudioProcessorGraph& graph,
                 NodeID inputNode, NodeID outputNode);

    // Lädt ein VST3 (synchron) und hängt es ans Ende der Kette. Gibt false bei Fehler.
    bool addPlugin (juce::AudioPluginFormatManager& fm,
                    const juce::PluginDescription& desc,
                    double sampleRate, int blockSize,
                    juce::String& errorOut);

    // Hängt ein internes Glied ans Ende der Kette: Mono->Stereo (1 in -> 2 out, dupliziert)
    // bzw. Stereo->Mono (2 in -> 1 out, gemittelt).
    void addMonoToStereo();
    void addStereoToMono();

    // Hängt den eingebauten Kazanç (Gain, -30..+12 dB) ans Ende der Kette.
    void addGain (float gainDb = 0.0f);
    bool isGain (int index) const;
    // Live-Änderung (Message-Thread): lock-frei, ohne Graph-Umbau; der Plugin-Thread gleitet in
    // ~40 ms zum neuen Wert (kein Knacken). Ungültiger Index / kein Gain-Glied: nichts passiert.
    void  setGainDb (int index, float gainDb);
    float getGainDb (int index) const;

    void removePlugin (int index);
    void movePlugin (int from, int to);
    void setBypass (int index, bool shouldBypass);

    const std::vector<Entry>& entries() const { return chain; }

    // Baut alle Graph-Verbindungen neu auf. Pro Übergang werden min(quelle.outs, ziel.ins)
    // Kanäle verbunden -> wechselnde Kanalbreite (z. B. ein Mono->Stereo-Glied) wird
    // automatisch korrekt verdrahtet. Alle Änderungen gehen als EIN Umbau an den Graphen
    // (1.1.1: ein synchroner Umbau pro Verbindung -> das Audio-Thread konnte Zwischenstände
    // ohne Verbindungen = Stille/Knackser erwischen).
    void rebuildConnections();

    // true, wenn nur EIN Kanal am Ausgang ankommt (Kette endet mono). Die Engine legt die
    // Stimme dann auf beide Kanäle des Kabels statt nur links. Lock-frei lesbar (Audio-Thread).
    bool isOutputMono() const noexcept { return outputMono.load (std::memory_order_relaxed); }

private:
    juce::AudioProcessorGraph& graph;
    NodeID inputNode, outputNode;
    std::vector<Entry> chain;
    std::atomic<bool> outputMono { false };
};
