#include "audio/AudioEngine.h"
#include "audio/RealtimeSupport.h"
#include "Text.h"

using micvst::tr;
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
        if (d == nullptr) return tr ("(yok)");
        return "'" + d->getName() + "' " + juce::String (juce::roundToInt (d->getCurrentSampleRate())) + tr (" Hz, tampon ")
             + juce::String (d->getCurrentBufferSizeSamples());
    }

    void storeMax (std::atomic<double>& a, double v) noexcept
    {
        auto current = a.load (std::memory_order_relaxed);
        while (v > current && ! a.compare_exchange_weak (current, v, std::memory_order_relaxed)) {}
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
    fadeOutOutput();   // no click on the cable when MicVST quits
    inputDevices.removeChangeListener (this);
    outputDevices.removeChangeListener (this);
    inputDevices.removeAudioCallback (&inputSide);
    outputDevices.removeAudioCallback (&outputSide);
    inputDevices.closeAudioDevice();
    outputDevices.closeAudioDevice();
    stopProcessing();
    graph.releaseResources();
}

bool AudioEngine::isRunning() const
{
    auto* in = inputDevices.getCurrentAudioDevice();
    const bool inputOk = in != nullptr && in->isPlaying();
    const bool outputSelected = wantedOutput.isNotEmpty();
    auto* out = outputDevices.getCurrentAudioDevice();
    const bool outputOk = ! outputSelected || (out != nullptr && out->isPlaying());
    return inputOk && outputOk;
}

void AudioEngine::changeListenerCallback (juce::ChangeBroadcaster*)
{
    // A device came or went (or a device was opened/closed).
    reconnectIfNeeded();
    juce::Logger::writeToLog (isRunning() ? tr ("Ses: çalışıyor") : tr ("Ses: durdu (cihaz bağlı değil mi?)"));
    if (onStatusChanged) onStatusChanged();
    if (onDeviceChanged) onDeviceChanged();
}

void AudioEngine::reconnectIfNeeded()
{
    // Only the chosen devices are ever used. If one disappeared (USB microphone unplugged, cable
    // driver restarted), its manager stays closed - no other microphone, never the speakers
    // instead of the virtual cable (JUCE switched to the default device, see PatchJuce.cmake).
    // As soon as it is listed again, it is reopened.
    if (reconnecting)
        return;
    const juce::ScopedValueSetter<bool> guard (reconnecting, true);

    enum { fine = 0, missing = 1, back = 2 };
    auto state = [] (MicVSTDeviceManager& dm, const juce::String& wanted, bool isInput)
    {
        if (wanted.isEmpty())
            return (int) fine;
        auto* d = dm.getCurrentAudioDevice();
        if (d != nullptr && d->getName() == wanted)
            return (int) fine;
        if (d != nullptr)
            dm.closeAudioDevice();   // some other device: never play through it
        auto* type = dm.getCurrentDeviceTypeObject();
        return type != nullptr && type->getDeviceNames (isInput).contains (wanted) ? (int) back : (int) missing;
    };

    const int in = state (inputDevices, wantedInput, true);
    const int out = state (outputDevices, wantedOutput, false);

    if (in == back || out == back)
    {
        juce::Logger::writeToLog (tr ("Cihaz yeniden bağlandı, açılıyor: ") + (in == back ? wantedInput : juce::String())
                                  + (in == back && out == back ? " / " : "") + (out == back ? wantedOutput : juce::String()));
        openDevices (wantedInput, wantedOutput, preferredBufferSize);
        {
            const ChainEdit edit (*this);
            rebuildGraph();   // the microphone's channel count may differ
        }
        lastMissingState = 0;
        return;
    }

    const int missingState = (in == missing ? 1 : 0) | (out == missing ? 2 : 0);
    if (missingState != lastMissingState && missingState != 0)
        juce::Logger::writeToLog (tr ("Seçili cihaz bağlı değil, takılınca yeniden açılacak: ")
                                  + ((missingState & 1) != 0 ? wantedInput : juce::String())
                                  + (missingState == 3 ? " / " : "") + ((missingState & 2) != 0 ? wantedOutput : juce::String()));
    lastMissingState = missingState;
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
    wantedInput = input;
    wantedOutput = output;

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
            juce::Logger::writeToLog ("'" + name + tr ("' düşük gecikme modunda açılmadı (") + micvst::deviceErrorText (err).upToFirstOccurrenceOf ("\n", false, false)
                                      + tr (") - paylaşımlı modda yeniden deneniyor"));
            dm.setCurrentAudioDeviceType (MicVSTDeviceManager::sharedTypeName, true);
            err = dm.setAudioDeviceSetup (makeSetup(), true);
        }
        return err;
    };

    const auto inErr  = open (inputDevices, input, true,
                              [&] { return inputOnlySetup (inputDevices, input, preferredInputRate, bufferSize); });
    const auto outErr = open (outputDevices, output, false,
                              [&] { return outputOnlySetup (outputDevices, output, preferredInputRate, bufferSize); });

    juce::Logger::writeToLog (tr ("Kurulum: giriş ") + describe (inputDevices.getCurrentAudioDevice())
                              + (inErr.isNotEmpty() ? " [" + micvst::deviceErrorText (inErr) + "]" : juce::String())
                              + tr (" | çıkış ") + describe (outputDevices.getCurrentAudioDevice())
                              + (outErr.isNotEmpty() ? " [" + micvst::deviceErrorText (outErr) + "]" : juce::String()));
}

juce::String AudioEngine::initialise (const juce::String& inputDeviceName,
                                      const juce::String& outputDeviceName)
{
    fadeOutOutput();   // both devices are closed below; reopening the output fades in again
    inputDevices.removeAudioCallback (&inputSide);    // idempotent: no double registration
    outputDevices.removeAudioCallback (&outputSide);
    inputDevices.closeAudioDevice();
    outputDevices.closeAudioDevice();
    inputDevices.setCurrentAudioDeviceType (inputDevices.preferredTypeName(), true);
    outputDevices.setCurrentAudioDeviceType (outputDevices.preferredTypeName(), true);

    // Errors do NOT return early: graph + chain must always exist, even without an open device
    // (device not there yet / reconnect).
    openDevices (inputDeviceName, outputDeviceName, preferredBufferSize);

    stopProcessing();   // (normally already stopped with the input callback) - never clear a live graph
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
        err = tr ("Giriş cihazı açılamadı: '") + inputDeviceName + "'";
    if (outputDevices.getCurrentAudioDevice() == nullptr && outputDeviceName.isNotEmpty())
        err << (err.isEmpty() ? "" : " / ") << tr ("Çıkış cihazı açılamadı: '") << outputDeviceName << "'";
    return err;
}

void AudioEngine::setDeviceConfig (const juce::String& input, const juce::String& output,
                                   double sampleRate, int bufferSize)
{
    if (sampleRate > 0.0) preferredInputRate = sampleRate;
    const bool held = fadeOutOutput();   // the output may be restarted: fade out first
    openDevices (input, output, bufferSize);
    {
        const ChainEdit edit (*this);
        rebuildGraph();   // the input node's channel count may have changed
    }
    if (held)
        bridge.resume();   // fades in again (a restarted output has done so already)
}

bool AudioEngine::fadeOutOutput()
{
    // Message thread. Fade the cable out and wait until the faded block and some silence have
    // left the device buffer: whatever is stopped then, the listener hears no cut.
    auto* out = outputDevices.getCurrentAudioDevice();
    if (out == nullptr || ! out->isPlaying() || ! outputRunningFlag.load())
        return false;

    const bool audible = bridge.getStats().running;
    bridge.fadeOutAndHold();
    if (! audible)
        return true;   // silent anyway (no input yet / refilling): nothing to fade or drain

    const double rate = juce::jmax (8000.0, out->getCurrentSampleRate());
    const double blockMs = 1000.0 * juce::jmax (1, out->getCurrentBufferSizeSamples()) / rate;
    const double start = juce::Time::getMillisecondCounterHiRes();
    while (bridge.getStats().running && juce::Time::getMillisecondCounterHiRes() - start < 200.0)
        juce::Thread::sleep (1);
    const double drainMs = juce::jlimit (5.0, 200.0, 1000.0 * out->getOutputLatencyInSamples() / rate + 2.0 * blockMs);
    juce::Thread::sleep (juce::roundToInt (drainMs));
    return true;
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
    // audio threads. The plug-in thread must not run meanwhile.
    stopProcessing();
    graph.setPlayConfigDetails (ins, 2, sr, bs);
    graph.prepareToPlay (sr, bs);
    workBuffer.setSize (2, bs, false, true, false);
    midiBuffer.ensureSize (2048);
    playHead.sampleRate.store (sr);
    bridge.setInputRate (sr);
    inputRateNow.store (sr);
    inputBlockNow.store (bs);
    inputQueue.reset();
    dspPeakWindow = dspSumWindow = backlogWindowMs = 0.0;
    dspCountWindow = 0;
    editGain = chainEditDepth.load() > 0 ? 0.0f : 1.0f;   // plug-in thread is stopped here
    editSettleBlocks = 0;
    seenEditGeneration = chainEditGeneration.load();
    rebuildGraph();   // IO node channel count may have changed (mono <-> stereo microphone)
    inputRunningFlag.store (true);
    startProcessing();

    juce::Logger::writeToLog (tr ("Giriş başladı: ") + describe (device) + tr (" | kanal ") + juce::String (ins)
                              + " / [" + device->getInputChannelNames().joinIntoString (", ") + "]");
}

void AudioEngine::inputStopped()
{
    inputRunningFlag.store (false);
    stopProcessing();   // the capture thread has stopped already; let the plug-in thread finish
    graph.releaseResources();
}

void AudioEngine::startProcessing()
{
    if (! processingThread.isThreadRunning())
        processingThread.startThread (juce::Thread::Priority::highest);   // MMCSS takes over inside
}

void AudioEngine::stopProcessing()
{
    if (! processingThread.isThreadRunning())
        return;

    processingThread.signalThreadShouldExit();
    processingWake.signal();
    // Returns as soon as the current block is done. Only a plug-in hanging inside processBlock
    // could run into the timeout (JUCE then terminates the thread, as with its device threads).
    processingThread.stopThread (5000);
}

void AudioEngine::outputAboutToStart (juce::AudioIODevice* device)
{
    const double sr = device->getCurrentSampleRate();
    bridge.setOutputRate (sr);
    bridge.setConsumerActive (true);
    outputRateNow.store (sr);
    outputBlockNow.store (device->getCurrentBufferSizeSamples());
    outputRunningFlag.store (true);
    juce::Logger::writeToLog (tr ("Çıkış başladı: ") + describe (device) + tr (" | kanal ")
                              + juce::String (device->getActiveOutputChannels().countNumberOfSetBits()));
}

void AudioEngine::outputStopped()
{
    outputRunningFlag.store (false);
    bridge.setConsumerActive (false);
}

void AudioEngine::captureInput (const float* const* in, int numIn, int numSamples) noexcept
{
    // Capture thread: copy and return. Nothing here may take long - while this thread is busy,
    // only Windows' small capture buffer (about 20 ms) holds the incoming audio.
    rt::configureAudioThread();
    MICVST_REALTIME_SCOPE;
    inputBeats.fetch_add (1, std::memory_order_relaxed);

    int channels = 0;
    while (in != nullptr && channels < juce::jmin (2, numIn) && in[channels] != nullptr)
        ++channels;

    if (channels > 0)
    {
        // read-only view for the meter (AudioBuffer has no const-referencing constructor)
        const juce::AudioBuffer<float> view (const_cast<float* const*> (in), channels, numSamples);
        inputMeter.process (view);
    }

    inputQueue.write (in, channels, numSamples, nowSeconds());
    processingWake.signal();
}

void AudioEngine::processingLoop()
{
    const rt::ProcessingThreadSetup setup;   // MMCSS "Pro Audio" + no power throttling, while it runs
    processingMmcss.store (setup.mmcssActive());

    while (! processingThread.threadShouldExit())
    {
        processingWake.wait (100);
        processQueuedInput();
    }
}

void AudioEngine::processQueuedInput() noexcept
{
    const int bs = workBuffer.getNumSamples();
    if (bs <= 0 || workBuffer.getNumChannels() < 2)
        return;

    const double sr = juce::jmax (1.0, inputRateNow.load (std::memory_order_relaxed));
    const double blockSeconds = (double) bs / sr;
    const int64_t maxBacklog = InputQueue::backlogLimit (sr, bs);
    const int blocksPerWindow = juce::jmax (1, (int) (sr / bs));   // ~1 s

    while (! processingThread.threadShouldExit())
    {
        const int64_t queued = inputQueue.available();
        if (queued < bs)
            return;

        MICVST_REALTIME_SCOPE;
        const juce::ScopedNoDenormals noDenormals;   // plug-in tails decaying into denormals = CPU spikes
        const auto startTicks = juce::Time::getHighResolutionTicks();
        const auto times0 = rt::threadTimes();

        float* const dst[] = { workBuffer.getWritePointer (0), workBuffer.getWritePointer (1) };
        bool inputJumped = false;
        const int64_t skipped = inputQueue.readBlock (dst, bs, maxBacklog, InputQueue::alignRange (sr), &inputJumped);
        if (skipped < 0)
            return;
        if (skipped > 0)
        {
            inputSkips.fetch_add (1, std::memory_order_relaxed);
            skippedFrames.fetch_add ((uint64_t) skipped, std::memory_order_relaxed);
        }

        // The bridge models the microphone's clock: stamp the block with its capture time.
        double captureTime = 0.0;
        if (! inputQueue.lastReadCaptureTime (sr, captureTime))
            captureTime = nowSeconds();

        playHead.samples.fetch_add (bs, std::memory_order_relaxed);

        juce::AudioBuffer<float> block (workBuffer.getArrayOfWritePointers(), 2, bs);
        midiBuffer.clear();
        bool threwException = false, silenced = false;
        // diagnostics (chainSilentBlocks): exact zeros the chain adds to a block
        auto zerosIn = [&block, bs]
        {
            int z = 0;
            for (const float* x = block.getReadPointer (0), *e = x + bs; x != e; ++x) z += (*x == 0.0f) ? 1 : 0;
            return z;
        };
        const int inputZeros = zerosIn();
        {
            // Never block: if the message thread holds the graph lock right now (rare:
            // suspend/prepare), output one silent block instead of waiting.
            const juce::ScopedTryLock sl (graph.getCallbackLock());
            if (sl.isLocked() && ! graph.isSuspended())
            {
                // A C++ exception out of a plug-in would end the whole program here (this
                // function is noexcept): silence the block instead and count it.
                try { graph.processBlock (block, midiBuffer); }
                catch (...) { threwException = true; invalidBlocks.fetch_add (1, std::memory_order_relaxed); }
                if (! threwException && chainEditDepth.load (std::memory_order_relaxed) == 0
                     && zerosIn() >= inputZeros + juce::jmin (48, bs / 2))
                    chainSilentBlocks.fetch_add (1, std::memory_order_relaxed);
            }
            else
            {
                silenced = true;
                graphBusyBlocks.fetch_add (1, std::memory_order_relaxed);
            }
        }

        if (pluginChain != nullptr && pluginChain->isOutputMono())   // the voice belongs on both cable channels
            juce::FloatVectorOperations::copy (block.getWritePointer (1), block.getReadPointer (0), bs);

        // A plug-in that produces NaN/infinity (an unstable filter, a bug) or absurd levels would
        // otherwise poison the resampler and everything downstream (OBS, Discord): such a block
        // is silenced as a whole (counted, logged once per second). The bridge is told to hold:
        // it fades out from the valid audio it still has (a silenced block itself would start
        // with a step or a kink) and, once valid audio returns, re-primes from it and fades in.
        if (! silenced && (threwException || ! isValidAudio (block)))
        {
            if (! threwException)
                invalidBlocks.fetch_add (1, std::memory_order_relaxed);
            silenced = true;
        }
        if (silenced)
        {
            block.clear();
            if (! outputWasInvalid)
                bridge.holdInput();
            outputWasInvalid = true;
        }
        else if (outputWasInvalid)
        {
            outputWasInvalid = false;
            bridge.releaseInput();
        }
        if (inputJumped && ! silenced)
            bridge.markDiscontinuity();   // the input queue lost audio here (plug-in thread stuck > 1 s)

        applyEditFade (block, sr);
        outputMeter.process (block);

        const float* chans[] = { block.getReadPointer (0), block.getReadPointer (1) };
        bridge.push (chans, 2, bs, captureTime, nowSeconds());

        // --- statistics: load = processing time / real time of the block ---------------------
        const double used = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startTicks);
        const auto times1 = rt::threadTimes();
        const double load = used / blockSeconds;
        const double waitingMs = 1000.0 * (double) juce::jmax ((int64_t) 0, queued - bs) / sr;

        processedBlocks.fetch_add (1, std::memory_order_relaxed);
        if (load > 1.0) overloads.fetch_add (1, std::memory_order_relaxed);
        busyNanos.fetch_add ((uint64_t) (used * 1.0e9), std::memory_order_relaxed);
        audioNanos.fetch_add ((uint64_t) (blockSeconds * 1.0e9), std::memory_order_relaxed);
        if (times1.wall > times0.wall && times1.cpu >= times0.cpu)
        {
            threadWallTicks.fetch_add (times1.wall - times0.wall, std::memory_order_relaxed);
            threadCpuTicks.fetch_add (times1.cpu - times0.cpu, std::memory_order_relaxed);
        }
        storeMax (logPeakLoad, load);
        storeMax (logPeakBacklogMs, waitingMs);

        dspPeakWindow = juce::jmax (dspPeakWindow, load);
        dspSumWindow += load;
        backlogWindowMs = juce::jmax (backlogWindowMs, waitingMs);
        if (++dspCountWindow >= blocksPerWindow)
        {
            dspPeakPublished.store (dspPeakWindow, std::memory_order_relaxed);
            dspAvgPublished.store (dspSumWindow / dspCountWindow, std::memory_order_relaxed);
            backlogMsPublished.store (backlogWindowMs, std::memory_order_relaxed);
            dspPeakWindow = dspSumWindow = backlogWindowMs = 0.0;
            dspCountWindow = 0;
        }
    }
}

bool AudioEngine::isValidAudio (const juce::AudioBuffer<float>& block) noexcept
{
    // > +24 dBFS is no signal but a plug-in out of control (an unstable filter).
    constexpr float limit = 16.0f;
    for (int c = 0; c < block.getNumChannels(); ++c)
    {
        const float* x = block.getReadPointer (c);
        for (int i = 0; i < block.getNumSamples(); ++i)
            if (! (std::abs (x[i]) <= limit))   // also true for NaN
                return false;
    }
    return true;
}

void AudioEngine::applyEditFade (juce::AudioBuffer<float>& block, double sr) noexcept
{
    // Plug-in thread. During a chain edit: fade out, then silence (acknowledged to the message
    // thread, which only then switches the graph); after the edit one more silent block (the
    // graph hands the new render sequence over at the start of a block), then fade back in.
    const bool editing = chainEditDepth.load (std::memory_order_acquire) > 0;
    const auto generation = chainEditGeneration.load (std::memory_order_acquire);
    if (generation != seenEditGeneration)
    {
        seenEditGeneration = generation;
        if (! editing && editGain <= 0.0f)
            editSettleBlocks = 1;
    }

    const float step = 1.0f / (float) juce::jmax (1.0, editFadeSeconds * sr);
    const int n = block.getNumSamples();

    if (editing || editSettleBlocks > 0)
    {
        if (editGain > 0.0f)
        {
            for (int i = 0; i < n; ++i)
            {
                editGain = juce::jmax (0.0f, editGain - step);
                for (int c = 0; c < block.getNumChannels(); ++c)
                    block.getWritePointer (c)[i] *= editGain;
            }
        }
        else
        {
            block.clear();
            if (! editing) --editSettleBlocks;
        }
        outputMutedForEdit.store (editing && editGain <= 0.0f, std::memory_order_release);
        return;
    }

    outputMutedForEdit.store (false, std::memory_order_release);
    if (editGain >= 1.0f)
        return;

    for (int i = 0; i < n; ++i)
    {
        editGain = juce::jmin (1.0f, editGain + step);
        for (int c = 0; c < block.getNumChannels(); ++c)
            block.getWritePointer (c)[i] *= editGain;
    }
}

void AudioEngine::beginChainEdit()
{
    if (chainEditDepth.fetch_add (1, std::memory_order_acq_rel) > 0)
        return;   // nested: already muted

    // Wait (briefly) until the plug-in thread has faded the output out - only if it runs and
    // gets audio; otherwise nothing is audible anyway.
    if (! processingThread.isThreadRunning() || ! inputRunningFlag.load() || inputStallSeconds > 0)
        return;

    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 150.0;
    while (! outputMutedForEdit.load (std::memory_order_acquire)
           && juce::Time::getMillisecondCounterHiRes() < deadline)
        juce::Thread::sleep (1);
}

void AudioEngine::endChainEdit()
{
    if (chainEditDepth.fetch_sub (1, std::memory_order_acq_rel) == 1)
        chainEditGeneration.fetch_add (1, std::memory_order_acq_rel);
}

void AudioEngine::processOutput (float* const* out, int numOut, int numSamples) noexcept
{
    rt::configureAudioThread();
    MICVST_REALTIME_SCOPE;
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
    d.outputSelected = wantedOutput.isNotEmpty();
    d.inputRate      = inputRateNow.load();
    d.outputRate     = outputRateNow.load();
    d.inputBlock     = inputBlockNow.load();
    d.outputBlock    = outputBlockNow.load();
    d.dspLoadPercent = 100.0 * dspAvgPublished.load();
    d.dspPeakPercent = 100.0 * dspPeakPublished.load();
    d.backlogMs      = backlogMsPublished.load();
    d.overloads      = overloads.load();
    d.inputSkips     = inputSkips.load();
    d.skippedMs      = d.inputRate > 0.0 ? 1000.0 * (double) skippedFrames.load() / d.inputRate : 0.0;
    d.inputQueueOverflows = inputQueue.getOverflows();
    d.invalidBlocks  = invalidBlocks.load();
    d.graphBusyBlocks = graphBusyBlocks.load();
    d.chainSilentBlocks = chainSilentBlocks.load();
    d.inputDeviceXruns = in != nullptr ? juce::jmax (0, in->getXRunCount()) : 0;
    d.watchdogRestarts = watchdogRestarts.load();
    d.bridge = bridge.getStats();

    if (in != nullptr && d.inputRate > 0.0)
    {
        double ms = 1000.0 * (in->getInputLatencyInSamples() + graph.getLatencySamples()
                              + bridge.getResamplerDelayFrames()) / d.inputRate;
        ms += d.backlogMs;   // audio still waiting for the plug-ins (0 while the chain keeps up)
        ms += d.bridge.inputDelayMs;   // plug-in thread: wake-up + processing of a block
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
    const bool outputSelected = wantedOutput.isNotEmpty();
    if (outputSelected) check (outputDevices, outputBeats.load(), lastOutputBeats, outputStallSeconds);
    else                outputStallSeconds = 0;

    const auto nowMs = juce::Time::getMillisecondCounter();
    const auto backoffMs = (juce::uint32) juce::jmin<int64_t> (60000, (int64_t) 10000 << juce::jmin<uint64_t> (3, watchdogRestarts.load()));

    auto restart = [&] (MicVSTDeviceManager& dm, const juce::String& which)
    {
        juce::Logger::writeToLog (tr ("Denetim: ") + which + tr (" cihazı 3 sn boyunca ses vermedi - yeniden açılıyor"));
        const auto setup = dm.getAudioDeviceSetup();
        dm.closeAudioDevice();
        dm.setAudioDeviceSetup (setup, true);
        watchdogRestarts.fetch_add (1);
        lastRestartMs = nowMs;
    };

    if (nowMs - lastRestartMs > backoffMs)
    {
        if (inputStallSeconds >= 3)       { restart (inputDevices, tr ("giriş"));   inputStallSeconds = 0; }
        else if (outputStallSeconds >= 3) { restart (outputDevices, tr ("çıkış")); outputStallSeconds = 0; }
    }

    // --- log realtime events once (from the message thread, never from the audio threads) --
    const auto st = bridge.getStats();
    if (st.underruns != lastLoggedStats.underruns)
        juce::Logger::writeToLog (tr ("Köprü: çıkışta ses kesildi, ") + juce::String ((int) (st.underruns - lastLoggedStats.underruns))
                                  + tr (" kez (yumuşak geçişle). Güvenlik payı şimdi ") + juce::String (juce::roundToInt (st.safetyMs)) + " ms");
    if (st.droppedBlocks != lastLoggedStats.droppedBlocks)
        juce::Logger::writeToLog (tr ("Köprü: ") + juce::String ((int) (st.droppedBlocks - lastLoggedStats.droppedBlocks))
                                  + tr (" giriş bloğu atıldı (çıkış durmuştu)"));
    if (st.resyncs != lastLoggedStats.resyncs)
        juce::Logger::writeToLog (tr ("Köprü: gecikme hedefe geri çekildi (toplam ") + juce::String ((int) st.resyncs) + ")");
    lastLoggedStats = st;

    const auto invalid = invalidBlocks.load();
    if (invalid != lastLoggedInvalid)
    {
        juce::Logger::writeToLog (tr ("Eklenti zinciri kullanılamaz ses verdi (NaN, sonsuz, aşırı yüksek değer ya da hata): ")
                                  + juce::String ((int) (invalid - lastLoggedInvalid))
                                  + tr (" blokta bu örnekler susturuldu. Sorun sürerse son eklediğiniz eklentiyi kaldırıp yeniden ekleyin."));
        lastLoggedInvalid = invalid;
    }

    // Plug-in thread over the last second. Read every tick, logged only when something happened.
    const auto blocks = processedBlocks.load(), ov = overloads.load();
    const auto busy = busyNanos.load(), audio = audioNanos.load();
    const auto wall = threadWallTicks.load(), cpu = threadCpuTicks.load();
    const auto skips = inputSkips.load(), skipped = skippedFrames.load();
    const auto queueOverflows = inputQueue.getOverflows();
    const double peakLoad = logPeakLoad.exchange (0.0), peakWaitMs = logPeakBacklogMs.exchange (0.0);

    if (ov != lastLoggedOverloads || skips != lastLoggedSkips)
    {
        const auto dAudio = audio - lastAudioNanos, dBusy = busy - lastBusyNanos, dWall = wall - lastWallTicks;
        juce::String line (tr ("İşleme: ") + juce::String ((int) (ov - lastLoggedOverloads)) + " / "
                           + juce::String ((int) (blocks - lastLoggedBlocks)) + tr (" blok gerçek zamandan uzun sürdü (eklenti yükü ort. ")
                           + micvst::trPercent (dAudio > 0 ? 100.0 * (double) dBusy / (double) dAudio : 0.0)
                           + tr (", tepe ") + micvst::trPercent (100.0 * peakLoad));
        // Laufanteil deutlich unter 100 %: der Thread war bereit, Windows rechnete aber anderes
        // (Treiber-DPC/Interrupts, höher priorisierte Threads); ~100 %: die Plugins selbst waren
        // zu langsam. Nur aussagekräftig, wenn in der Sekunde nennenswert gerechnet wurde.
        if (dWall > 0 && dBusy > 50000000)
            line << tr ("; iş parçacığının çalışma oranı ")
                 << micvst::trPercent (juce::jmin (100.0, 100.0 * (double) (cpu - lastCpuTicks) / (double) dWall))
                 << tr (", kalan sürede işlemci bekledi");
        line << tr ("; ses en çok ") << juce::roundToInt (peakWaitMs) << tr (" ms bekledi)");
        juce::Logger::writeToLog (line);
    }

    if (skips != lastLoggedSkips)
    {
        const double sr = juce::jmax (1.0, inputRateNow.load());
        juce::Logger::writeToLog (tr ("İşleme: eklenti zinciri gerçek zamanın ")
                                  + juce::String (juce::roundToInt (1000.0 * InputQueue::maxBacklogSeconds))
                                  + tr (" ms'den fazla gerisine düştü - mikrofon sesinden ")
                                  + juce::String (juce::roundToInt (1000.0 * (double) (skipped - lastLoggedSkippedFrames) / sr))
                                  + tr (" ms atlandı (") + juce::String ((int) (skips - lastLoggedSkips))
                                  + tr (" kez, yumuşak geçişle). Zincir şu anki yükte gerçek zamandan fazla işlemci süresi istiyor."));
    }

    if (queueOverflows != lastLoggedQueueOverflows)
        juce::Logger::writeToLog (tr ("İşleme: giriş kuyruğu doldu - ")
                                  + juce::String ((int) (queueOverflows - lastLoggedQueueOverflows))
                                  + tr (" mikrofon bloğu atıldı (eklenti işleme bir saniyeden uzun durdu)"));

    // A plug-in stuck inside processBlock: the microphone delivers, nothing gets processed.
    const bool inputFlowing = inputBeats.load() != 0 && inputStallSeconds == 0 && inputRunningFlag.load();
    processingStallSeconds = (inputFlowing && blocks == lastLoggedBlocks && processingThread.isThreadRunning())
                               ? processingStallSeconds + 1 : 0;
    if (processingStallSeconds == 3)
        juce::Logger::writeToLog (tr ("İşleme: mikrofon ses verdiği hâlde 3 sn boyunca hiçbir şey işlenmedi - bir eklenti takılmış olabilir"));

    if (! processingThreadLogged && blocks > 0)
    {
        processingThreadLogged = true;
        juce::Logger::writeToLog (tr ("Eklenti iş parçacığı: ")
                                  + (processingMmcss.load() ? tr ("MMCSS 'Pro Audio' (cihaz iş parçacıklarının bir altında)")
                                                            : tr ("MMCSS kullanılamıyor - zaman-kritik öncelik")));
    }

    lastLoggedOverloads = ov;
    lastLoggedBlocks = blocks;
    lastBusyNanos = busy;
    lastAudioNanos = audio;
    lastWallTicks = wall;
    lastCpuTicks = cpu;
    lastLoggedSkips = skips;
    lastLoggedSkippedFrames = skipped;
    lastLoggedQueueOverflows = queueOverflows;
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
        juce::Logger::writeToLog (tr ("Eklenti önbelleği yok ya da bozuk -> tam tarama"));
}

void AudioEngine::startBackgroundScan (int timeoutMs, const juce::StringArray& forceRescan)
{
    if (isScanning()) { rescanQueued = true; return; }

    MicVST3Format vst3;
    auto files = filterFilesNeedingScan (listVst3Files(), knownPlugins, vst3, skippedPlugins, forceRescan);
    juce::Logger::writeToLog (tr ("Tarama: ") + juce::String (files.size()) + tr (" dosya taranacak"));
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
        juce::Logger::writeToLog (tr ("Tarama atlandı (") + micvst::scanSkipReasonText (s.reason) + "): " + s.file);
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
    s.inputDevice   = wantedInput;    // also while unplugged: stays the user's choice
    s.outputDevice  = wantedOutput;
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
        p.pluginId = e.pluginId;
        p.pluginName = e.pluginName;
        p.pluginUid = e.pluginUid;
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
        if (! p.fileOrId.startsWith ("builtin:") && findPluginType (p, false) == nullptr)
            { allResolvable = false; break; }

    if (allResolvable) restoreChain (s.plugins);
    else               { pendingPlugins = s.plugins;
                         juce::Logger::writeToLog (tr ("Zincirin geri yüklenmesi eklenti taramasını bekliyor")); }
    rebuildGraph();
}

std::unique_ptr<juce::PluginDescription> AudioEngine::findPluginType (const PluginEntryState& p, bool warnIfAmbiguous) const
{
    // 1) exakte Kennung (Format + Name + Datei + uid); 2) gleicher Name + uid an anderer Stelle
    // (Plugin neu installiert/verschoben); 3) ältere config.xml (<= 1.2.0) kennen nur die Datei.
    if (p.pluginId.isNotEmpty())
        if (auto t = knownPlugins.getTypeForIdentifierString (p.pluginId))
            return t;

    if (p.pluginName.isNotEmpty())
    {
        for (auto& t : knownPlugins.getTypes())
            if (t.name == p.pluginName && t.uniqueId == p.pluginUid)
                return std::make_unique<juce::PluginDescription> (t);
        return nullptr;   // bekanntes Plugin, aber nicht (mehr) installiert - nicht raten
    }

    auto t = knownPlugins.getTypeForFile (p.fileOrId);
    if (t != nullptr && warnIfAmbiguous)
    {
        int inFile = 0;
        for (auto& k : knownPlugins.getTypes())
            if (k.fileOrIdentifier == p.fileOrId) ++inFile;
        if (inFile > 1)
            juce::Logger::writeToLog (tr ("Uyarı: '") + p.fileOrId + tr ("' birden çok eklenti içeriyor; eski ayar dosyası hangisinin "
                                      "kullanıldığını kaydetmemiş - ilki yüklendi. Doğru eklentiyi bir kez yeniden ekleyin."));
    }
    return t;
}

void AudioEngine::restoreChain (const juce::Array<PluginEntryState>& plugins)
{
    const double sr = getProcessingSampleRate();
    const int    bs = getProcessingBlockSize();
    const ChainEdit edit (*this);   // muted until the whole chain is in place

    for (auto& p : plugins)
    {
        if (p.fileOrId == PluginChain::monoToStereoId || p.fileOrId == PluginChain::stereoToMonoId)
        {
            if (p.fileOrId == PluginChain::monoToStereoId) pluginChain->addMonoToStereo();
            else                                           pluginChain->addStereoToMono();
            pluginChain->setBypass ((int) pluginChain->entries().size() - 1, p.bypassed);
            continue;
        }

        auto type = findPluginType (p, true);
        if (type == nullptr)
        {
            juce::Logger::writeToLog (tr ("Eklenti bulunamadı: ") + (p.pluginName.isNotEmpty() ? p.pluginName + " - " : juce::String())
                                      + p.fileOrId);
            continue;
        }
        juce::String err;
        if (pluginChain->addPlugin (formatManager, *type, sr, bs, err))
        {
            const int idx = (int) pluginChain->entries().size() - 1;
            if (auto* node = graph.getNodeForId (pluginChain->entries()[(size_t) idx].node))
                node->getProcessor()->setStateInformation (p.state.getData(), (int) p.state.getSize());
            pluginChain->setBypass (idx, p.bypassed);
        }
        else juce::Logger::writeToLog (tr ("Eklenti yüklenemedi: ") + p.fileOrId + " - " + err);
    }
    rebuildGraph();
}
