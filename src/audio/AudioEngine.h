#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/AudioBridge.h"
#include "audio/Metering.h"
#include "audio/PluginChain.h"
#include "audio/MicVSTDeviceManager.h"
#include "audio/ScanCoordinator.h"
#include "state/Persistence.h"

// Owns the two audio devices, the plug-in graph and the bridge between them.
//
//   microphone (input-only WASAPI device, own MMCSS thread)
//        -> plug-in chain (AudioProcessorGraph, processed on the microphone's thread)
//        -> AudioBridge (lock-free FIFO + drift-compensating resampler)
//   virtual cable (output-only WASAPI device, own MMCSS thread) <- AudioBridge
//
// MicVST <= 1.1.1 drove both devices from ONE JUCE device (one thread, output clock): input
// shortfalls were zero-padded, clock drift was never compensated and a processing spike went
// straight into the output. See AudioBridge.h.
class AudioEngine : private juce::ChangeListener,
                    private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    juce::String initialise (const juce::String& inputDeviceName,
                             const juce::String& outputDeviceName);

    // Runtime device switch (DevicePanel). Keeps graph + plug-in chain. sampleRate<=0 /
    // bufferSize<=0 = unchanged.
    void setDeviceConfig (const juce::String& input, const juce::String& output,
                          double sampleRate, int bufferSize);

    // User buffer preference in samples; 0 = Auto (device default period).
    void setPreferredBufferSize (int samples) { preferredBufferSize = samples; }
    int  getPreferredBufferSize() const       { return preferredBufferSize; }

    // Safety buffer between microphone and cable in ms; 0 = Auto (adaptive).
    void setSafetyBufferMs (int ms);
    int  getSafetyBufferMs() const { return safetyBufferMs; }
    static constexpr int safetyChoicesMs[] = { 5, 10, 20, 40, 80 };

    // Finds an installed virtual audio cable as output (VB-Cable, VoiceMeeter, VAC).
    juce::String detectCableOutput();

    MicVSTState captureState();
    void        applyState (const MicVSTState&);

    bool isRunning() const;   // microphone running (and the output, if one is selected)
    std::function<void()> onStatusChanged;
    std::function<void()> onDeviceChanged;

    // UI -> app: persist the WHOLE state (incl. window state) - see Main.cpp.
    std::function<void()> onStateChanged;
    void requestPersist() { if (onStateChanged) onStateChanged(); }

    std::function<void()> onFactoryResetRequested;
    void requestFactoryReset() { if (onFactoryResetRequested) onFactoryResetRequested(); }

    MicVSTDeviceManager&            getInputDeviceManager()  { return inputDevices; }
    MicVSTDeviceManager&            getOutputDeviceManager() { return outputDevices; }
    // Rate / block size the plug-in chain runs at (= the microphone's), with fallbacks while
    // no microphone has started yet.
    double getProcessingSampleRate() const
    {
        const double sr = inputRateNow.load (std::memory_order_relaxed);
        return sr > 0.0 ? sr : preferredInputRate;
    }
    int getProcessingBlockSize() const
    {
        const int bs = inputBlockNow.load (std::memory_order_relaxed);
        return bs > 0 ? bs : 480;
    }

    juce::AudioProcessorGraph&      getGraph()         { return graph; }
    PluginChain&                    getChain()         { return *pluginChain; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    juce::KnownPluginList&          getKnownPlugins()  { return knownPlugins; }

    // --- plug-in scan (out of process, asynchronous, cached) ---
    void loadPluginCache();
    void startBackgroundScan (int timeoutMs = ScanCoordinator::defaultTimeoutMs,
                              const juce::StringArray& forceRescan = {});
    void rescanAllPlugins();
    void retrySkippedPlugins();
    void skipCurrentScanFile();
    bool isScanning() const { return scanner != nullptr; }
    const juce::Array<SkippedPlugin>& getSkippedPlugins() const { return skippedPlugins; }
    std::function<void (int, int, juce::String)> onScanProgress;
    std::function<void()> onScanFinished;

    void addPluginFolder (const juce::String& folder);
    void removePluginFolder (const juce::String& folder);
    void setPluginFolders (const juce::StringArray& f) { pluginFolders = f; }
    const juce::StringArray& getPluginFolders() const  { return pluginFolders; }

    static juce::File pluginCacheFile();

    LevelReading inputLevel()  const { return inputMeter.read(); }
    LevelReading outputLevel() const { return outputMeter.read(); }

    void rebuildGraph();

    // Live diagnostics (UI status line, log).
    struct Diagnostics
    {
        bool   inputRunning = false, outputRunning = false, outputSelected = false;
        double inputRate = 0.0, outputRate = 0.0;
        int    inputBlock = 0, outputBlock = 0;
        double dspLoadPercent = 0.0;      // average plug-in chain load (share of the block time)
        double dspPeakPercent = 0.0;      // worst block during the last second
        uint64_t overloads = 0;           // blocks that took longer than real time
        int    inputDeviceXruns = 0;      // WASAPI capture discontinuities
        uint64_t watchdogRestarts = 0;
        double latencyMs = 0.0;           // estimated microphone -> cable latency
        AudioBridge::Stats bridge;
    };
    Diagnostics getDiagnostics();

private:
    // Playhead that always reports "playing" (some routing/streaming plug-ins only send audio
    // while the transport runs).
    struct PlayingHead : juce::AudioPlayHead
    {
        std::atomic<juce::int64> samples { 0 };
        std::atomic<double> sampleRate { 48000.0 };
        juce::Optional<PositionInfo> getPosition() const override
        {
            const auto s  = samples.load (std::memory_order_relaxed);
            const auto sr = sampleRate.load (std::memory_order_relaxed);
            PositionInfo info;
            info.setIsPlaying (true);
            info.setIsRecording (false);
            info.setIsLooping (false);
            info.setTimeInSamples (s);
            info.setTimeInSeconds ((double) s / sr);
            info.setBpm (120.0);
            info.setTimeSignature (juce::AudioPlayHead::TimeSignature{});
            info.setPpqPosition (((double) s / sr) * (120.0 / 60.0));
            return info;
        }
    };
    PlayingHead playHead;

    // The two device callbacks (each device has its own thread).
    struct InputSide final : juce::AudioIODeviceCallback
    {
        explicit InputSide (AudioEngine& e) : engine (e) {}
        void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const*, int,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override
            { engine.processInput (in, numIn, numSamples); }
        void audioDeviceAboutToStart (juce::AudioIODevice* d) override { engine.inputAboutToStart (d); }
        void audioDeviceStopped() override                             { engine.inputStopped(); }
        void audioDeviceError (const juce::String& e) override         { juce::Logger::writeToLog ("Input device error: " + e); }
        AudioEngine& engine;
    };
    struct OutputSide final : juce::AudioIODeviceCallback
    {
        explicit OutputSide (AudioEngine& e) : engine (e) {}
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override
            { engine.processOutput (out, numOut, numSamples); }
        void audioDeviceAboutToStart (juce::AudioIODevice* d) override { engine.outputAboutToStart (d); }
        void audioDeviceStopped() override                             { engine.outputStopped(); }
        void audioDeviceError (const juce::String& e) override         { juce::Logger::writeToLog ("Output device error: " + e); }
        AudioEngine& engine;
    };

    void processInput (const float* const* in, int numIn, int numSamples) noexcept;
    void processOutput (float* const* out, int numOut, int numSamples) noexcept;
    void inputAboutToStart (juce::AudioIODevice*);
    void inputStopped();
    void outputAboutToStart (juce::AudioIODevice*);
    void outputStopped();

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;   // watchdog + diagnostics log (1 Hz)
    void openDevices (const juce::String& input, const juce::String& output, int bufferSize);
    static double nowSeconds() noexcept;

    MicVSTDeviceManager inputDevices, outputDevices;
    InputSide  inputSide  { *this };
    OutputSide outputSide { *this };
    AudioBridge bridge;

    juce::AudioProcessorGraph graph;
    std::unique_ptr<PluginChain> pluginChain;
    juce::AudioBuffer<float> workBuffer;   // sized in inputAboutToStart, never on the audio thread
    juce::MidiBuffer midiBuffer;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::StringArray pluginFolders;
    int preferredBufferSize = 0;
    int safetyBufferMs = 0;                // 0 = Auto
    double preferredInputRate = 48000.0;
    LevelMeter inputMeter, outputMeter;

    // Realtime statistics (written by the audio threads)
    std::atomic<bool>     inputRunningFlag { false }, outputRunningFlag { false };
    std::atomic<double>   inputRateNow { 0.0 }, outputRateNow { 0.0 };
    std::atomic<int>      inputBlockNow { 0 }, outputBlockNow { 0 };
    std::atomic<uint64_t> inputBeats { 0 }, outputBeats { 0 };
    std::atomic<uint64_t> overloads { 0 };
    std::atomic<double>   dspPeakPublished { 0.0 }, dspAvgPublished { 0.0 };
    double  dspPeakWindow = 0.0, dspSumWindow = 0.0;   // input thread only
    int     dspCountWindow = 0;                         // input thread only
    std::atomic<uint64_t> watchdogRestarts { 0 };

    // Watchdog / logging state (message thread)
    uint64_t lastInputBeats = 0, lastOutputBeats = 0;
    int      inputStallSeconds = 0, outputStallSeconds = 0;
    juce::uint32 lastRestartMs = 0;
    AudioBridge::Stats lastLoggedStats;
    uint64_t lastLoggedOverloads = 0;

    std::unique_ptr<ScanCoordinator> scanner;
    bool rescanQueued = false;
    juce::Array<SkippedPlugin> skippedPlugins;
    juce::Array<PluginEntryState> pendingPlugins;
    juce::StringArray scanRoots() const;
    juce::StringArray listVst3Files() const;
    void restoreChain (const juce::Array<PluginEntryState>& plugins);
    void handleScanFinished (const ScanOutcome&);
    void pruneOutsideFolders();

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};
