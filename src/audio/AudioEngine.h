#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "audio/AudioBridge.h"
#include "audio/InputQueue.h"
#include "audio/Metering.h"
#include "audio/PluginChain.h"
#include "audio/MicVSTDeviceManager.h"
#include "audio/MonitorOutput.h"
#include "audio/Recorder.h"
#include "audio/CueSounds.h"
#include "audio/RealtimeSupport.h"
#include "audio/ScanCoordinator.h"
#include "Text.h"
#include "state/Persistence.h"

// Owns the two audio devices, the plug-in graph and the bridge between them.
//
//   microphone (input-only WASAPI device, own MMCSS thread: only copies each block)
//        -> InputQueue (lock-free FIFO, bounded backlog)
//        -> plug-in thread (MicVST's own MMCSS thread: AudioProcessorGraph)
//        -> AudioBridge (lock-free FIFO + drift-compensating resampler)
//   virtual cable (output-only WASAPI device, own MMCSS thread) <- AudioBridge
//
// MicVST <= 1.1.1 drove both devices from ONE JUCE device (one thread, output clock): input
// shortfalls were zero-padded, clock drift was never compensated and a processing spike went
// straight into the output. See AudioBridge.h. In 1.2.0 the plug-ins still ran on the capture
// thread, so slow plug-in blocks overflowed Windows' small capture buffer; see InputQueue.h.
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
    // The devices the user chose. They stay chosen while unplugged: MicVST never switches to
    // another microphone or to the speakers, and reopens them when they are listed again.
    const juce::String& getChosenInput() const  { return wantedInput; }
    const juce::String& getChosenOutput() const { return wantedOutput; }

    // Safety buffer between microphone and cable in ms; 0 = Auto (adaptive).
    void setSafetyBufferMs (int ms);
    int  getSafetyBufferMs() const { return safetyBufferMs; }
    static constexpr int safetyChoicesMs[] = { 5, 10, 20, 40, 80 };

    // Process priority class chosen by the user (0 Normal = default, 1 above normal, 2 high;
    // rt::ProcessPriority). Applied at once and read back from Windows; the setting keeps the
    // class the process really has afterwards (a refused change keeps the previous one). The
    // audio threads are MMCSS in every case. Message thread. Logged; a refusal is also kept in
    // getPriorityProblem() for the window to show.
    rt::PriorityResult setProcessPriority (int choice);
    int  getProcessPriority() const { return processPriority; }
    // Text of the last refused change (empty = none) and a counter that changes with every refusal.
    const juce::String& getPriorityProblem() const { return priorityProblem; }
    int  getPriorityProblemCount() const { return priorityProblemCount; }
    static juce::String priorityName (int choice);

    // Finds an installed virtual audio cable as output (VB-Cable, VoiceMeeter, VAC).
    juce::String detectCableOutput();

    MicVSTState captureState();
    void        applyState (const MicVSTState&);

    // --- Hazır ayarlar (presets, state/Presets.h) ---
    // The chain as it is now (every element with its state) - what a user preset saves.
    juce::Array<PluginEntryState> captureChain();
    struct PresetResult
    {
        bool morphed = false;          // same elements: only the settings glided (no chain edit)
        juce::StringArray missing;     // plug-ins of the preset that are not installed (skipped)
    };
    // Makes the chain the preset's. Same elements in the same order (e.g. between the factory
    // presets): every setting glides to its new value, nothing is rebuilt. Otherwise the new
    // elements are created and loaded first (unconnected, silent - the old chain keeps playing),
    // then swapped in one chain edit (short fade). The chain before is kept for undoPreset().
    // Message thread.
    PresetResult applyPreset (const juce::String& name, const juce::Array<PluginEntryState>& elements);
    bool canUndoPreset() const { return hasUndoChain; }
    void undoPreset();
    const juce::String& getPresetName() const { return presetName; }
    bool isPresetModified() const { return presetModified; }
    void setPresetName (const juce::String& name, bool modified) { presetName = name; presetModified = modified; }
    void markPresetModified() { if (presetName.isNotEmpty()) presetModified = true; }

    // --- Sustur, Önce/Sonra, Kendini dinle, Deneme kaydı (message thread) ---
    // Mute: what goes to the cable (and the headphones) fades to silence in 15 ms; the meter of the
    // microphone keeps showing the voice. Not saved: MicVST always starts unmuted.
    void setMuted (bool m);   // also plays the Bildirim sesi (same call: in sync with the mute itself)
    CuePlayer& getCues() { return cues; }
    bool isMuted() const { return muted.load(); }
    // "Ham ses" (A/B): the cable gets the microphone without the chain - delayed by the chain's own
    // latency, cross-faded in 30 ms, so switching back and forth compares the same moment.
    void setRawCompare (bool on);
    bool isRawCompare() const { return rawCompare.load(); }

    // Kendini dinle: the processed audio on a second output (headphones), see MonitorOutput.h.
    void setMonitorDevice (const juce::String& name);   // "" = Windows' default output
    const juce::String& getMonitorDevice() const { return monitor.getDeviceName(); }
    juce::String setMonitorEnabled (bool on);          // error text ("" = fine)
    bool isMonitorEnabled() const { return monitorEnabled; }
    void setMonitorVolumeDb (float db) { monitor.setVolumeDb (db); }
    float getMonitorVolumeDb() const { return monitor.getVolumeDb(); }
    juce::StringArray getMonitorDeviceNames() { return monitor.getDeviceNames(); }

    // Deneme kaydı: records 10 s of what the cable gets, then plays it back on the headphones
    // (the monitor device, opened for the playback if monitoring is off). The cable keeps getting
    // the live voice all the time.
    static constexpr double testSeconds = 10.0;
    enum class TestState { idle, recording, playing };
    juce::String startTestRecording();   // error text ("" = fine)
    void stopTest();                      // ends recording (and plays what there is) or playback
    TestState getTestState() const { return testState; }
    double getTestProgress() const;       // 0..1 of the recording / playback
    void testTick();                      // the window's timer (30 Hz) and the engine's (1 Hz): next step when due

    // Kayıt: records what the cable gets into a file (audio/Recorder.h). The settings are saved
    // with the rest (config.xml). startRecording: "" = recording, otherwise why not.
    void setRecordSettings (const RecordSettings& s) { recordSettings = s; }
    const RecordSettings& getRecordSettings() const { return recordSettings; }
    juce::String startRecording();
    Recorder::Result stopRecording();
    bool isRecording() const { return recorder.isRecording(); }
    double getRecordingSeconds() const { return recorder.getSeconds(); }
    juce::File getRecordingFile() const { return recorder.getFile(); }
    // the engine stopped a recording by itself (write error, disk full, rate change, quitting)
    std::function<void (const Recorder::Result&, const juce::String& why)> onRecordingStopped;

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

    // The chain's delay in samples: the sum of the latencies the active (not bypassed) elements
    // report right now. Message thread. (The graph's own figure is only updated when it is rebuilt:
    // a plug-in that changes its latency while running - an EQ switched to linear phase in its
    // window - was missing from the shown latency until the next chain edit.)
    int chainLatencySamples() const;

    // Every change of the plug-in chain while audio runs (add, remove, move, bypass, restore)
    // happens inside one of these: the processed audio fades out (5 ms), the graph is switched
    // while the output is silent, then it fades back in. Without it the sudden jump between the
    // old and the new sound - or a block of silence from a half-rewired graph - is a click.
    // Message thread only; nesting is fine.
    class ChainEdit
    {
    public:
        explicit ChainEdit (AudioEngine& e) : engine (e) { engine.beginChainEdit(); }
        ~ChainEdit() { engine.endChainEdit(); }
    private:
        AudioEngine& engine;
        JUCE_DECLARE_NON_COPYABLE (ChainEdit)
    };

    // Live diagnostics (UI status line, log).
    struct Diagnostics
    {
        bool   inputRunning = false, outputRunning = false, outputSelected = false;
        double inputRate = 0.0, outputRate = 0.0;
        int    inputBlock = 0, outputBlock = 0;
        double dspLoadPercent = 0.0;      // average plug-in chain load (share of the block time)
        double dspPeakPercent = 0.0;      // worst block during the last second
        double backlogMs = 0.0;           // most audio waiting for the plug-ins during the last second
        uint64_t overloads = 0;           // blocks that took longer than real time (absorbed by the queue)
        uint64_t inputSkips = 0;          // chain fell behind > 120 ms: oldest input skipped (crossfaded)
        double skippedMs = 0.0;
        uint64_t inputQueueOverflows = 0; // plug-in thread stuck for > 1 s: captured blocks dropped
        uint64_t invalidBlocks = 0;       // blocks with NaN/infinite/absurd plug-in output (silenced)
        uint64_t graphBusyBlocks = 0;     // blocks output silent because the graph was busy (prepare/suspend)
        uint64_t chainSilentBlocks = 0;   // blocks to which the chain added >= 1 ms of exact zeros (diagnostics)
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
            { engine.captureInput (in, numIn, numSamples); }
        void audioDeviceAboutToStart (juce::AudioIODevice* d) override { engine.inputAboutToStart (d); }
        void audioDeviceStopped() override                             { engine.inputStopped(); }
        void audioDeviceError (const juce::String& e) override         { juce::Logger::writeToLog (micvst::tr ("Giriş cihazı hatası: ") + micvst::deviceErrorText (e)); }
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
        void audioDeviceError (const juce::String& e) override         { juce::Logger::writeToLog (micvst::tr ("Çıkış cihazı hatası: ") + micvst::deviceErrorText (e)); }
        AudioEngine& engine;
    };

    // MicVST's plug-in thread (MMCSS "Pro Audio", one step below the device threads).
    struct ProcessingThread final : juce::Thread
    {
        explicit ProcessingThread (AudioEngine& e) : juce::Thread ("MicVST plug-in chain"), engine (e) {}
        void run() override { engine.processingLoop(); }
        AudioEngine& engine;
    };

    void captureInput (const float* const* in, int numIn, int numSamples) noexcept;   // capture thread
    void processingLoop();                                                             // plug-in thread
    void processQueuedInput() noexcept;                                                // plug-in thread
    void processOutput (float* const* out, int numOut, int numSamples) noexcept;
    void startProcessing();
    void stopProcessing();
    void applyEditFade (juce::AudioBuffer<float>& block, double sampleRate) noexcept;   // plug-in thread
    static bool isValidAudio (const juce::AudioBuffer<float>& block) noexcept;          // plug-in thread
    bool outputWasInvalid = false;                                                     // plug-in thread
    void beginChainEdit();
    void endChainEdit();
    void inputAboutToStart (juce::AudioIODevice*);
    void inputStopped();
    void outputAboutToStart (juce::AudioIODevice*);
    void outputStopped();

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;   // watchdog + diagnostics log (1 Hz)
    void openDevices (const juce::String& input, const juce::String& output, int bufferSize);
    bool fadeOutOutput();   // before stopping/switching devices; true = the bridge now holds silence
    static double nowSeconds() noexcept;

    MicVSTDeviceManager inputDevices, outputDevices;
    MonitorOutput monitor;
    Recorder recorder;
    RecordSettings recordSettings;
    CuePlayer cues;
    void logRecordingResult (const Recorder::Result&);
    bool monitorEnabled = false;   // message thread
    void updateMonitorDevice();    // opens / closes the monitor device as needed (message thread)

    // mute / A-B (message thread writes, plug-in thread reads)
    std::atomic<bool> muted { false }, rawCompare { false };
    std::atomic<int>  chainLatencyNow { 0 };       // samples, updated by the timer
    std::atomic<int>  inputChannelsNow { 2 };
    double muteGain = 1.0, compareMix = 0.0;       // plug-in thread
    juce::AudioBuffer<float> rawBlock;             // the microphone before the chain (plug-in thread)
    static constexpr int rawDelaySize = 1 << 17;   // frames (2.7 s at 48 kHz; longer chain delays are clamped)
    juce::AudioBuffer<float> rawDelay;             // ring of raw input, sized in inputAboutToStart
    int rawDelayPos = 0;                           // plug-in thread
    void applyMuteAndCompare (juce::AudioBuffer<float>& block, double sampleRate) noexcept;   // plug-in thread

    // test recording (state: message thread; positions: plug-in thread / atomics)
    TestState testState = TestState::idle;
    juce::AudioBuffer<float> testBuffer;           // allocated by startTestRecording (message thread)
    std::atomic<int> testMode { 0 };               // 0 idle, 1 recording, 2 playing (plug-in thread follows)
    std::atomic<int> testFrames { 0 };             // recorded frames
    std::atomic<int> testPos { 0 };                // playback position
    int testTotalFrames = 0;
    juce::uint32 playbackEndMs = 0;

    InputSide  inputSide  { *this };
    OutputSide outputSide { *this };
    AudioBridge bridge;
    InputQueue inputQueue;                 // capture thread -> plug-in thread
    rt::WakeEvent processingWake;

    juce::AudioProcessorGraph graph;
    std::unique_ptr<PluginChain> pluginChain;
    juce::AudioBuffer<float> workBuffer;   // sized in inputAboutToStart, never on the audio thread
    juce::MidiBuffer midiBuffer;

    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::StringArray pluginFolders;
    int preferredBufferSize = 0;
    juce::String wantedInput, wantedOutput;   // chosen devices (also while unplugged)
    bool reconnecting = false;
    int lastMissingState = 0;                 // for logging changes only
    int lastRunningLogged = -1;               // "Ses: çalışıyor / durdu" logged on changes only
    void reconnectIfNeeded();
    // A chosen device that is listed but cannot be opened (another program holds it exclusively, a
    // driver error) is retried with a growing pause (1, 2, 4, 8, then every 10 s) from the timer -
    // not on every change message: each failed attempt sends one itself, which used to start the
    // next attempt at once (an endless loop of log lines and config.xml writes).
    struct ReopenBackoff { juce::uint32 nextMs = 0; int failures = 0; };
    ReopenBackoff inputReopen, outputReopen;
    int safetyBufferMs = 0;                // 0 = Auto
    int processPriority = 0;               // 0 Normal (default), 1 above normal, 2 high
    juce::String priorityProblem;
    int priorityProblemCount = 0;
    double preferredInputRate = 48000.0;
    LevelMeter inputMeter, outputMeter;

    // Realtime statistics (written by the audio threads)
    std::atomic<bool>     inputRunningFlag { false }, outputRunningFlag { false };
    std::atomic<double>   inputRateNow { 0.0 }, outputRateNow { 0.0 };
    std::atomic<int>      inputBlockNow { 0 }, outputBlockNow { 0 };
    std::atomic<uint64_t> inputBeats { 0 }, outputBeats { 0 };
    std::atomic<uint64_t> overloads { 0 }, processedBlocks { 0 };
    std::atomic<uint64_t> inputSkips { 0 }, skippedFrames { 0 };
    std::atomic<uint64_t> invalidBlocks { 0 };   // blocks with NaN/infinite/absurd samples from the chain
    std::atomic<uint64_t> graphBusyBlocks { 0 }; // silent blocks: graph locked/suspended by the message thread
    std::atomic<uint64_t> chainSilentBlocks { 0 }; // the graph added exact zeros to a block (not muted by MicVST)
    uint64_t lastLoggedInvalid = 0;
    std::atomic<double>   dspPeakPublished { 0.0 }, dspAvgPublished { 0.0 }, backlogMsPublished { 0.0 };
    double  dspPeakWindow = 0.0, dspSumWindow = 0.0, backlogWindowMs = 0.0;   // plug-in thread only
    int     dspCountWindow = 0;                                               // plug-in thread only
    // For the log (cumulative, read as differences once per second by the timer)
    std::atomic<uint64_t> busyNanos { 0 }, audioNanos { 0 };       // plug-in time / audio time processed
    std::atomic<uint64_t> threadWallTicks { 0 }, threadCpuTicks { 0 };
    std::atomic<double>   logPeakLoad { 0.0 }, logPeakBacklogMs { 0.0 };   // max since the last log line
    std::atomic<bool>     processingMmcss { false };

    // Chain edits (see ChainEdit)
    static constexpr double editFadeSeconds = 0.005;
    std::atomic<int>      chainEditDepth { 0 };
    std::atomic<uint32_t> chainEditGeneration { 0 };
    std::atomic<bool>     outputMutedForEdit { false };
    std::atomic<int>      editDrainSamples { 0 };   // the chain's delay around the last edit (applyEditFade)
    int      editLatencyBefore = 0;    // message thread only
    float    editGain = 1.0f;          // plug-in thread only
    int      editSettleBlocks = 0;     // plug-in thread only
    uint32_t seenEditGeneration = 0;   // plug-in thread only
    std::atomic<uint64_t> watchdogRestarts { 0 };

    // Watchdog / logging state (message thread)
    uint64_t lastInputBeats = 0, lastOutputBeats = 0;
    int      inputStallSeconds = 0, outputStallSeconds = 0;
    juce::uint32 lastRestartMs = 0;
    AudioBridge::Stats lastLoggedStats;
    uint64_t lastLoggedOverloads = 0, lastLoggedBlocks = 0, lastLoggedSkips = 0, lastLoggedSkippedFrames = 0;
    uint64_t lastLoggedQueueOverflows = 0, lastBusyNanos = 0, lastAudioNanos = 0;
    uint64_t lastWallTicks = 0, lastCpuTicks = 0;
    int      processingStallSeconds = 0;
    bool     processingThreadLogged = false;

    ProcessingThread processingThread { *this };   // stopped before anything above is destroyed

    std::unique_ptr<ScanCoordinator> scanner;
    bool rescanQueued = false;
    juce::Array<SkippedPlugin> skippedPlugins;
    juce::Array<PluginEntryState> pendingPlugins;
    juce::StringArray scanRoots() const;
    juce::StringArray listVst3Files() const;
    void restoreChain (const juce::Array<PluginEntryState>& plugins);
    // Creates one element at the end of the chain list with its state and bypass; false = skipped
    // (unknown, not installed or not loadable - logged; missing gets its name). forPreset: a second
    // Otomatik Kazanç is allowed (the running chain may still have one).
    bool addElement (const PluginEntryState&, bool forPreset, juce::StringArray* missing = nullptr);
    juce::String presetName;
    bool presetModified = false;
    juce::Array<PluginEntryState> undoChain;
    juce::String undoPresetName;
    bool undoPresetModified = false, hasUndoChain = false;
    std::unique_ptr<juce::PluginDescription> findPluginType (const PluginEntryState&, bool warnIfAmbiguous) const;
    void handleScanFinished (const ScanOutcome&);
    void pruneOutsideFolders();

    JUCE_DECLARE_NON_COPYABLE (AudioEngine)
};
