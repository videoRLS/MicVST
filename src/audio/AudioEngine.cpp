#include "audio/AudioEngine.h"
#include "audio/RealtimeSupport.h"
using IOProc = juce::AudioProcessorGraph::AudioGraphIOProcessor;

namespace
{
    juce::AudioDeviceManager::AudioDeviceSetup inputOnlySetup (juce::AudioDeviceManager& dm,
                                                               const juce::String& input,
                                                               double sampleRate, int bufferSize)
    {
        auto setup = dm.getAudioDeviceSetup();
        setup.inputDeviceName  = input;
        setup.outputDeviceName = {};
        // Channels EXPLICITLY: without deviceManager.initialise() the "default" channel ranges
        // would be empty.
        setup.useDefaultInputChannels  = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.clear();
        setup.inputChannels.setRange (0, 2, true);   // up to 2 mic channels (mono uses ch0)
        setup.outputChannels.clear();
        setup.sampleRate = sampleRate;               // shared mode may force the device mix rate
        if (bufferSize > 0) setup.bufferSize = bufferSize;
        return setup;
    }

    juce::AudioDeviceManager::AudioDeviceSetup outputOnlySetup (juce::AudioDeviceManager& dm,
                                                                const juce::String& output,
                                                                double sampleRate, int bufferSize)
    {
        auto setup = dm.getAudioDeviceSetup();
        setup.inputDeviceName  = {};
        setup.outputDeviceName = output;
        setup.useDefaultInputChannels  = false;
        setup.useDefaultOutputChannels = false;
        setup.inputChannels.clear();
        setup.outputChannels.clear();
        setup.outputChannels.setRange (0, 2, true);
        setup.sampleRate = sampleRate;   // any mismatch with the microphone is resampled by the bridge
        if (bufferSize > 0) setup.bufferSize = bufferSize;
        return setup;
    }

    juce::String describe (juce::AudioIODevice* d)
    {
        if (d == nullptr) return "(none)";
        return "'" + d->getName() + "' " + juce::String (d->getCurrentSampleRate(), 0) + " Hz, buffer "
             + juce::String (d->getCurrentBufferSizeSamples());
    }
}

AudioEngine::AudioEngine()
{
    // IMPORTANT: forces creating + scanning the device types. Without this,
    // getCurrentDeviceTypeObject() is nullptr and setAudioDeviceSetup()/device lookup do nothing.
    inputDevices.getAvailableDeviceTypes();
    outputDevices.getAvailableDeviceTypes();
    inputDevices.addChangeListener (this);
    outputDevices.addChangeListener (this);
    bridge.setSafety (AudioBridge::autoStartMs, true);
    startTimer (1000);
}

AudioEngine::~AudioEngine()
{
    stopTimer();
    inputDevices.removeChangeListener (this);
    outputDevices.removeChangeListener (this);
    inputDevices.removeAudioCallback (&inputSide);
    outputDevices.removeAudioCallback (&outputSide);
    inputDevices.closeAudioDevice();
    outputDevices.closeAudioDevice();
    graph.releaseResources();
}

bool AudioEngine::isRunning() const
{
    auto* in = inputDevices.getCurrentAudioDevice();
    const bool inputOk = in != nullptr && in->isPlaying();
    const bool outputSelected = outputDevices.getAudioDeviceSetup().outputDeviceName.isNotEmpty();
    auto* out = outputDevices.getCurrentAudioDevice();
    const bool outputOk = ! outputSelected || (out != nullptr && out->isPlaying());
    return inputOk && outputOk;
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A device came or went: AudioDeviceManager restores the stored setup by name itself.
    juce::Logger::writeToLog (isRunning() ? "Audio: running" : "Audio: idle (device disconnected?)");
    if (onStatusChanged) onStatusChanged();
    if (onDeviceChanged) onDeviceChanged();
}

juce::String AudioEngine::detectCableOutput()
{
    // Render-Endpunkte bekannter virtueller Kabel, nach Priorität (VB-Cable zuerst).
    static const char* const cablePatterns[] = {
        "CABLE Input",          // VB-Audio Virtual Cable (empfohlen)
        "VB-Audio",             // weitere VB-Audio-Kabel (Hi-Fi Cable etc.)
        "VoiceMeeter Input",    // VoiceMeeter VAIO/Aux
        "Virtual Audio Cable"   // VAC ("Line 1 (Virtual Audio Cable)")
    };

    // Nur aufzählen, den Gerätetyp NICHT umschalten: das würde das offene Kabel-Gerät neu öffnen
    // (und einen Shared-Fallback, siehe openDevices, wieder rückgängig machen). Die Namen sind in
    // beiden WASAPI-Typen gleich.
    for (auto* type : outputDevices.getAvailableDeviceTypes())
    {
        if (type->getTypeName() != outputDevices.preferredTypeName())
            continue;

        type->scanForDevices();
        auto outs = type->getDeviceNames (false /* output */);
        for (auto* pat : cablePatterns)
            for (auto& name : outs)
                if (name.containsIgnoreCase (pat))
                    return name;
    }
    return {};
}

//==============================================================================
void AudioEngine::openDevices (const juce::String& input, const juce::String& output, int bufferSize)
{
    // Names VERBATIM, also empty: an empty output deliberately means "no output" ("none").
    // If the low-latency mode (IAudioClient3) refuses a device that IS present, it is retried in
    // classic shared mode - 1.1.1 then simply stayed silent ("Idle").
    auto open = [] (MicVSTDeviceManager& dm, const juce::String& name, bool isInput,
                    const std::function<juce::AudioDeviceManager::AudioDeviceSetup()>& makeSetup)
    {
        auto err = dm.setAudioDeviceSetup (makeSetup(), true);

        auto* type = dm.getCurrentDeviceTypeObject();
        const bool present = type != nullptr && type->getDeviceNames (isInput).contains (name);

        if (dm.getCurrentAudioDevice() == nullptr && name.isNotEmpty() && present
             && dm.getCurrentAudioDeviceType() == MicVSTDeviceManager::lowLatencyTypeName)
        {
            juce::Logger::writeToLog ("'" + name + "' does not open in low-latency mode (" + err.upToFirstOccurrenceOf ("\n", false, false)
                                      + ") - retrying in shared mode");
            dm.setCurrentAudioDeviceType (MicVSTDeviceManager::sharedTypeName, true);
            err = dm.setAudioDeviceSetup (makeSetup(), true);
        }
        return err;
    };

    const auto inErr  = open (inputDevices, input, true,
                              [&] { return inputOnlySetup (inputDevices, input, preferredInputRate, bufferSize); });
    const auto outErr = open (outputDevices, output, false,
                              [&] { return outputOnlySetup (outputDevices, output, preferredInputRate, bufferSize); });

    juce::Logger::writeToLog ("Setup: input " + describe (inputDevices.getCurrentAudioDevice())
                              + (inErr.isNotEmpty() ? " [" + inErr + "]" : juce::String())
                              + " | output " + describe (outputDevices.getCurrentAudioDevice())
                              + (outErr.isNotEmpty() ? " [" + outErr + "]" : juce::String()));
}

juce::String AudioEngine::initialise (const juce::String& inputDeviceName,
                                      const juce::String& outputDeviceName)
{
    inputDevices.removeAudioCallback (&inputSide);    // idempotent: no double registration
    outputDevices.removeAudioCallback (&outputSide);
    inputDevices.closeAudioDevice();
    outputDevices.closeAudioDevice();
    inputDevices.setCurrentAudioDeviceType (inputDevices.preferredTypeName(), true);
    outputDevices.setCurrentAudioDeviceType (outputDevices.preferredTypeName(), true);

    // Errors do NOT return early: graph + chain must always exist, even without an open device
    // (device not there yet / reconnect).
    openDevices (inputDeviceName, outputDeviceName, preferredBufferSize);

    graph.clear();
    auto inNode  = graph.addNode (std::make_unique<IOProc> (IOProc::audioInputNode));
    auto outNode = graph.addNode (std::make_unique<IOProc> (IOProc::audioOutputNode));
    pluginChain = std::make_unique<PluginChain> (graph, inNode->nodeID, outNode->nodeID);
    graph.setPlayHead (&playHead);
    rebuildGraph();

    // Registering the callbacks prepares the graph (inputAboutToStart) and starts processing.
    inputDevices.addAudioCallback (&inputSide);
    outputDevices.addAudioCallback (&outputSide);

    juce::String err;
    if (inputDevices.getCurrentAudioDevice() == nullptr && inputDeviceName.isNotEmpty())
        err = "Couldn't open the input device '" + inputDeviceName + "'";
    if (outputDevices.getCurrentAudioDevice() == nullptr && outputDeviceName.isNotEmpty())
        err << (err.isEmpty() ? "" : " / ") << "Couldn't open the output device '" << outputDeviceName << "'";
    return err;
}

void AudioEngine::setDeviceConfig (const juce::String& input, const juce::String& output,
                                   double sampleRate, int bufferSize)
{
    if (sampleRate > 0.0) preferredInputRate = sampleRate;
    openDevices (input, output, bufferSize);
    rebuildGraph();   // the input node's channel count may have changed
}

void AudioEngine::setSafetyBufferMs (int ms)
{
    safetyBufferMs = ms > 0 ? ms : 0;
    if (safetyBufferMs == 0) bridge.setSafety (AudioBridge::autoStartMs, true);
    else                     bridge.setSafety ((double) safetyBufferMs, false);
}

void AudioEngine::rebuildGraph()
{
    if (pluginChain != nullptr)
        pluginChain->rebuildConnections();
}

//==============================================================================
double AudioEngine::nowSeconds() noexcept
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;   // QueryPerformanceCounter on Windows
}

void AudioEngine::inputAboutToStart (juce::AudioIODevice* device)
{
    const double sr  = device->getCurrentSampleRate();
    const int    bs  = juce::jmax (1, device->getCurrentBufferSizeSamples());
    const int    ins = juce::jlimit (1, 2, device->getActiveInputChannels().countNumberOfSetBits());

    // Prepare everything here (message thread, before the first callback) - never on the
    // audio thread.
    graph.setPlayConfigDetails (ins, 2, sr, bs);
    graph.prepareToPlay (sr, bs);
    workBuffer.setSize (2, bs, false, true, false);
    midiBuffer.ensureSize (2048);
    playHead.sampleRate.store (sr);
    bridge.setInputRate (sr);
    inputRateNow.store (sr);
    inputBlockNow.store (bs);
    dspPeakWindow = dspSumWindow = 0.0;
    dspCountWindow = 0;
    rebuildGraph();   // IO node channel count may have changed (mono <-> stereo microphone)
    inputRunningFlag.store (true);

    juce::Logger::writeToLog ("Input start: " + describe (device) + " | channels " + juce::String (ins)
                              + " of [" + device->getInputChannelNames().joinIntoString (", ") + "]");
}

void AudioEngine::inputStopped()
{
    inputRunningFlag.store (false);
    graph.releaseResources();
}

void AudioEngine::outputAboutToStart (juce::AudioIODevice* device)
{
    const double sr = device->getCurrentSampleRate();
    bridge.setOutputRate (sr);
    bridge.setConsumerActive (true);
    outputRateNow.store (sr);
    outputBlockNow.store (device->getCurrentBufferSizeSamples());
    outputRunningFlag.store (true);
    juce::Logger::writeToLog ("Output start: " + describe (device) + " | channels "
                              + juce::String (device->getActiveOutputChannels().countNumberOfSetBits()));
}

void AudioEngine::outputStopped()
{
    outputRunningFlag.store (false);
    bridge.setConsumerActive (false);
}

void AudioEngine::processInput (const float* const* in, int numIn, int numSamples) noexcept
{
    rt::configureAudioThread();
    const juce::ScopedNoDenormals noDenormals;   // plug-in tails decaying into denormals = CPU spikes
    const auto startTicks = juce::Time::getHighResolutionTicks();

    playHead.samples.fetch_add (numSamples, std::memory_order_relaxed);
    inputBeats.fetch_add (1, std::memory_order_relaxed);

    const int capacity = workBuffer.getNumSamples();
    if (capacity <= 0 || workBuffer.getNumChannels() < 2)
        return;

    const bool mono = pluginChain != nullptr && pluginChain->isOutputMono();

    for (int offset = 0; offset < numSamples; offset += capacity)
    {
        const int len = juce::jmin (capacity, numSamples - offset);
        juce::AudioBuffer<float> block (workBuffer.getArrayOfWritePointers(), 2, len);

        for (int ch = 0; ch < 2; ++ch)
        {
            if (ch < numIn && in[ch] != nullptr)
                juce::FloatVectorOperations::copy (block.getWritePointer (ch), in[ch] + offset, len);
            else
                juce::FloatVectorOperations::clear (block.getWritePointer (ch), len);
        }

        {
            const juce::AudioBuffer<float> inView (workBuffer.getArrayOfWritePointers(), juce::jlimit (1, 2, numIn), len);
            inputMeter.process (inView);
        }

        midiBuffer.clear();
        {
            // Never block the audio thread: if the message thread holds the graph lock right
            // now (rare: suspend/prepare), output one silent block instead of waiting.
            const juce::ScopedTryLock sl (graph.getCallbackLock());
            if (sl.isLocked() && ! graph.isSuspended())
                graph.processBlock (block, midiBuffer);
            else
                block.clear();
        }

        if (mono)   // chain ends mono: the voice belongs on both cable channels
            juce::FloatVectorOperations::copy (block.getWritePointer (1), block.getReadPointer (0), len);

        outputMeter.process (block);

        const float* chans[] = { block.getReadPointer (0), block.getReadPointer (1) };
        bridge.push (chans, 2, len, nowSeconds());
    }

    // DSP load = processing time / real time of the block.
    const double sr = juce::jmax (1.0, inputRateNow.load (std::memory_order_relaxed));
    const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
    const double load = used / ((double) numSamples / sr);

    if (load > 1.0) overloads.fetch_add (1, std::memory_order_relaxed);
    dspPeakWindow = juce::jmax (dspPeakWindow, load);
    dspSumWindow += load;

    if (++dspCountWindow >= juce::jmax (1, (int) (sr / juce::jmax (1, numSamples))))   // ~1 s
    {
        dspPeakPublished.store (dspPeakWindow, std::memory_order_relaxed);
        dspAvgPublished.store (dspSumWindow / dspCountWindow, std::memory_order_relaxed);
        dspPeakWindow = dspSumWindow = 0.0;
        dspCountWindow = 0;
    }
}

void AudioEngine::processOutput (float* const* out, int numOut, int numSamples) noexcept
{
    rt::configureAudioThread();
    const juce::ScopedNoDenormals noDenormals;
    outputBeats.fetch_add (1, std::memory_order_relaxed);
    bridge.pull (out, numOut, numSamples, nowSeconds());
}

//==============================================================================
AudioEngine::Diagnostics AudioEngine::getDiagnostics()
{
    Diagnostics d;
    auto* in  = inputDevices.getCurrentAudioDevice();
    auto* out = outputDevices.getCurrentAudioDevice();

    d.inputRunning   = in != nullptr && in->isPlaying() && inputRunningFlag.load();
    d.outputRunning  = out != nullptr && out->isPlaying() && outputRunningFlag.load();
    d.outputSelected = outputDevices.getAudioDeviceSetup().outputDeviceName.isNotEmpty();
    d.inputRate      = inputRateNow.load();
    d.outputRate     = outputRateNow.load();
    d.inputBlock     = inputBlockNow.load();
    d.outputBlock    = outputBlockNow.load();
    d.dspLoadPercent = 100.0 * dspAvgPublished.load();
    d.dspPeakPercent = 100.0 * dspPeakPublished.load();
    d.overloads      = overloads.load();
    d.inputDeviceXruns = in != nullptr ? juce::jmax (0, in->getXRunCount()) : 0;
    d.watchdogRestarts = watchdogRestarts.load();
    d.bridge = bridge.getStats();

    if (in != nullptr && d.inputRate > 0.0)
    {
        double ms = 1000.0 * (in->getInputLatencyInSamples() + graph.getLatencySamples()
                              + bridge.getResamplerDelayFrames()) / d.inputRate;
        if (d.outputSelected && out != nullptr && d.outputRate > 0.0)
            ms += d.bridge.bufferedMs + 1000.0 * out->getOutputLatencyInSamples() / d.outputRate;
        d.latencyMs = ms;
    }
    return d;
}

void AudioEngine::timerCallback()
{
    // --- watchdog: an open device that delivered no callback for 3 s gets reopened ---------
    auto check = [] (juce::AudioDeviceManager& dm, uint64_t beats, uint64_t& last, int& stalled)
    {
        auto* d = dm.getCurrentAudioDevice();
        if (d != nullptr && d->isOpen() && beats == last) ++stalled; else stalled = 0;
        last = beats;
    };
    check (inputDevices,  inputBeats.load(),  lastInputBeats,  inputStallSeconds);
    const bool outputSelected = outputDevices.getAudioDeviceSetup().outputDeviceName.isNotEmpty();
    if (outputSelected) check (outputDevices, outputBeats.load(), lastOutputBeats, outputStallSeconds);
    else                outputStallSeconds = 0;

    const auto nowMs = juce::Time::getMillisecondCounter();
    const auto backoffMs = (juce::uint32) juce::jmin<int64_t> (60000, (int64_t) 10000 << juce::jmin<uint64_t> (3, watchdogRestarts.load()));

    auto restart = [&] (MicVSTDeviceManager& dm, const char* which)
    {
        juce::Logger::writeToLog (juce::String ("Watchdog: ") + which + " device delivered no audio for 3 s - reopening it");
        const auto setup = dm.getAudioDeviceSetup();
        dm.closeAudioDevice();
        dm.setAudioDeviceSetup (setup, true);
        watchdogRestarts.fetch_add (1);
        lastRestartMs = nowMs;
    };

    if (nowMs - lastRestartMs > backoffMs)
    {
        if (inputStallSeconds >= 3)       { restart (inputDevices, "input");   inputStallSeconds = 0; }
        else if (outputStallSeconds >= 3) { restart (outputDevices, "output"); outputStallSeconds = 0; }
    }

    // --- log realtime events once (from the message thread, never from the audio threads) --
    const auto st = bridge.getStats();
    if (st.underruns != lastLoggedStats.underruns)
        juce::Logger::writeToLog ("Bridge: output ran dry " + juce::String ((int) (st.underruns - lastLoggedStats.underruns))
                                  + "x (faded gap). Safety buffer now " + juce::String (st.safetyMs, 0) + " ms");
    if (st.droppedBlocks != lastLoggedStats.droppedBlocks)
        juce::Logger::writeToLog ("Bridge: " + juce::String ((int) (st.droppedBlocks - lastLoggedStats.droppedBlocks))
                                  + " input block(s) dropped (output stalled)");
    if (st.resyncs != lastLoggedStats.resyncs)
        juce::Logger::writeToLog ("Bridge: latency trimmed back to target (" + juce::String ((int) st.resyncs) + " total)");
    const auto ov = overloads.load();
    if (ov != lastLoggedOverloads)
        juce::Logger::writeToLog ("DSP: " + juce::String ((int) (ov - lastLoggedOverloads))
                                  + " block(s) took longer than real time (peak load "
                                  + juce::String (100.0 * dspPeakPublished.load(), 0) + " %)");
    lastLoggedStats = st;
    lastLoggedOverloads = ov;
}

//==============================================================================
juce::File AudioEngine::pluginCacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
              .getChildFile ("MicVST").getChildFile ("plugin_cache.xml");
}

juce::StringArray AudioEngine::scanRoots() const
{
    // JUCE-Default-Orte statt hartkodiertem Pfad: deckt neben Program Files auch
    // %LOCALAPPDATA%\Programs\Common\VST3 und die VST3_PATH-Umgebungsvariable ab.
    MicVST3Format vst3;
    juce::StringArray roots;
    const auto defaults = vst3.getDefaultLocationsToSearch();
    for (int i = 0; i < defaults.getNumPaths(); ++i)
        roots.add (defaults[i].getFullPathName());
    for (auto& f : pluginFolders)
        if (f.isNotEmpty()) roots.add (f);
    return roots;
}

juce::StringArray AudioEngine::listVst3Files() const
{
    MicVST3Format vst3;
    juce::FileSearchPath paths;
    for (auto& r : scanRoots())
        paths.add (juce::File (r));
    paths.removeRedundantPaths();
    return vst3.searchPathsForPlugins (paths, true, true);
}

void AudioEngine::loadPluginCache()
{
    if (! PluginScanCache::load (pluginCacheFile(), knownPlugins, skippedPlugins))
        juce::Logger::writeToLog ("Plugin-Cache fehlt/korrupt -> voller Scan");
}

void AudioEngine::startBackgroundScan (int timeoutMs, const juce::StringArray& forceRescan)
{
    if (isScanning()) { rescanQueued = true; return; }

    MicVST3Format vst3;
    auto files = filterFilesNeedingScan (listVst3Files(), knownPlugins, vst3, skippedPlugins, forceRescan);
    juce::Logger::writeToLog ("Scan: " + juce::String (files.size()) + " Datei(en) zu scannen");
    if (files.isEmpty())
    {
        PluginScanCache::save (pluginCacheFile(), knownPlugins, skippedPlugins);
        if (! pendingPlugins.isEmpty()) { restoreChain (pendingPlugins); pendingPlugins.clear(); }
        if (onScanFinished) onScanFinished();
        return;
    }

    scanner = std::make_unique<ScanCoordinator> (files,
        [this] (int cur, int total, juce::String name)
        {
            if (onScanProgress) onScanProgress (cur, total, name);
        },
        [this] (ScanOutcome outcome) { handleScanFinished (outcome); },
        timeoutMs);
}

void AudioEngine::handleScanFinished (const ScanOutcome& outcome)
{
    scanner = nullptr;   // Callback kommt via callAsync -> wir sind auf dem Message-Thread

    mergeScanResults (knownPlugins, outcome);
    for (auto& s : outcome.skipped)
    {
        skippedPlugins.add (s);
        juce::Logger::writeToLog ("Scan übersprungen (" + s.reason + "): " + s.file);
    }

    // Ordner können während des Scans entfernt worden sein -> NACH dem Übernehmen wegputzen,
    // damit auch frisch gescannte Fremd-Ergebnisse rausfliegen.
    pruneOutsideFolders();

    PluginScanCache::save (pluginCacheFile(), knownPlugins, skippedPlugins);

    if (! pendingPlugins.isEmpty())
    {
        restoreChain (pendingPlugins);
        pendingPlugins.clear();
    }
    if (onScanFinished) onScanFinished();

    if (rescanQueued) { rescanQueued = false; startBackgroundScan(); }
}

void AudioEngine::rescanAllPlugins()
{
    if (isScanning()) return;
    knownPlugins.clear();
    skippedPlugins.clear();
    pluginCacheFile().deleteFile();
    startBackgroundScan();
}

void AudioEngine::retrySkippedPlugins()
{
    if (isScanning() || skippedPlugins.isEmpty()) return;

    // Crash-Rescue: Gerettete Typen sind im Cache schon mit AKTUELLER effectiveModTime
    // gestempelt (mergeScanResults) -> ohne forceRescan hielte filterFilesNeedingScan die
    // Datei für up-to-date und der Retry würde für sie stillschweigend nichts tun. Vor dem
    // Leeren einsammeln (skippedPlugins ist danach weg), nur noch existierende Dateien.
    juce::StringArray forceRescan;
    for (auto& s : skippedPlugins)
        if (juce::File (s.file).exists())
            forceRescan.add (s.file);

    skippedPlugins.clear();   // Cache/Fundliste bleiben -> nur die Geskippten werden gescannt
    startBackgroundScan (ScanCoordinator::retryTimeoutMs, forceRescan);
}

void AudioEngine::skipCurrentScanFile()
{
    if (scanner != nullptr) scanner->skipCurrentFile();
}

void AudioEngine::pruneOutsideFolders()
{
    const auto roots = scanRoots();
    for (auto& t : knownPlugins.getTypes())
        if (! pathIsInsideAnyFolder (t.fileOrIdentifier, roots)) knownPlugins.removeType (t);
    for (int i = skippedPlugins.size(); --i >= 0;)
        if (! pathIsInsideAnyFolder (skippedPlugins[i].file, roots)) skippedPlugins.remove (i);
}

void AudioEngine::addPluginFolder (const juce::String& folder)
{
    if (folder.isNotEmpty() && ! pluginFolders.contains (folder))
        pluginFolders.add (folder);
    startBackgroundScan();
}

void AudioEngine::removePluginFolder (const juce::String& folder)
{
    pluginFolders.removeString (folder);
    pruneOutsideFolders();
    PluginScanCache::save (pluginCacheFile(), knownPlugins, skippedPlugins);
    startBackgroundScan();
}

//==============================================================================
MicVSTState AudioEngine::captureState()
{
    MicVSTState s;
    s.inputDevice   = inputDevices.getAudioDeviceSetup().inputDeviceName;
    s.outputDevice  = outputDevices.getAudioDeviceSetup().outputDeviceName;
    s.sampleRate    = preferredInputRate;
    s.bufferSize    = preferredBufferSize;
    s.safetyBufferMs = safetyBufferMs;
    s.pluginFolders = pluginFolders;

    // Ketten-Restore steht noch aus -> gemerkten Zustand verbatim zurückgeben,
    // sonst würde persistState() die gespeicherte Kette mit "leer" überschreiben.
    if (! pendingPlugins.isEmpty()) { s.plugins = pendingPlugins; return s; }
    if (pluginChain == nullptr) return s;

    for (auto& e : pluginChain->entries())
    {
        PluginEntryState p;
        p.fileOrId = e.fileOrId;
        p.bypassed = e.bypassed;
        // Bewusst OHNE Callback-Lock (1.1.1 hielt ihn): der Graph nimmt in jedem Block den
        // Callback-Lock jedes Plugins. Solange ein Plugin hier seinen Zustand serialisierte,
        // stand der Audio-Thread still - und unter Last kann der (normal priorisierte)
        // Message-Thread mitten darin verdrängt werden (Prioritätsinversion -> Aussetzer).
        // VST3 definiert getState() als Message-Thread-Aufruf, der auch während process()
        // erlaubt ist; JUCEs eigener AudioPluginHost speichert genauso.
        if (auto* node = graph.getNodeForId (e.node))
            node->getProcessor()->getStateInformation (p.state);
        s.plugins.add (p);
    }
    return s;
}

void AudioEngine::applyState (const MicVSTState& s)
{
    setPreferredBufferSize (s.bufferSize);
    if (s.sampleRate > 0.0) preferredInputRate = s.sampleRate;
    setSafetyBufferMs (s.safetyBufferMs);
    initialise (s.inputDevice, s.outputDevice);

    // Kette nur wiederherstellen, wenn alle Nicht-Builtin-Plugins im Cache auflösbar sind.
    // Sonst bis Scan-Ende zurückstellen (captureState liefert solange pendingPlugins,
    // damit persistState die Kette nicht mit "leer" überschreibt).
    bool allResolvable = true;
    for (auto& p : s.plugins)
        if (! p.fileOrId.startsWith ("builtin:") && knownPlugins.getTypeForFile (p.fileOrId) == nullptr)
            { allResolvable = false; break; }

    if (allResolvable) restoreChain (s.plugins);
    else               { pendingPlugins = s.plugins;
                         juce::Logger::writeToLog ("Ketten-Restore wartet auf Plugin-Scan"); }
    rebuildGraph();
}

void AudioEngine::restoreChain (const juce::Array<PluginEntryState>& plugins)
{
    const double sr = getProcessingSampleRate();
    const int    bs = getProcessingBlockSize();

    for (auto& p : plugins)
    {
        if (p.fileOrId == PluginChain::monoToStereoId || p.fileOrId == PluginChain::stereoToMonoId)
        {
            if (p.fileOrId == PluginChain::monoToStereoId) pluginChain->addMonoToStereo();
            else                                           pluginChain->addStereoToMono();
            pluginChain->setBypass ((int) pluginChain->entries().size() - 1, p.bypassed);
            continue;
        }

        auto type = knownPlugins.getTypeForFile (p.fileOrId);
        if (type == nullptr) { juce::Logger::writeToLog ("Plugin fehlt: " + p.fileOrId); continue; }
        juce::String err;
        if (pluginChain->addPlugin (formatManager, *type, sr, bs, err))
        {
            const int idx = (int) pluginChain->entries().size() - 1;
            if (auto* node = graph.getNodeForId (pluginChain->entries()[(size_t) idx].node))
                node->getProcessor()->setStateInformation (p.state.getData(), (int) p.state.getSize());
            pluginChain->setBypass (idx, p.bypassed);
        }
        else juce::Logger::writeToLog ("Plugin-Load: " + err);
    }
    rebuildGraph();
}
