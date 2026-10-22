#include "audio/AudioEngine.h"
#include "audio/DeviceFormat.h"
#include "audio/BuiltInAutoGain.h"
#include "audio/BuiltInGain.h"
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
    cues.cableName = [this] { return wantedOutput; };   // a Bildirim sesi never goes into the cable
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
    if (recorder.isRecording()) logRecordingResult (recorder.stop());   // the file is finished and checked
    monitor.close();
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
    const int running = isRunning() ? 1 : 0;
    if (running != lastRunningLogged)
    {
        juce::Logger::writeToLog (running != 0 ? tr ("Ses: çalışıyor") : tr ("Ses: durdu (cihaz bağlı değil mi?)"));
        lastRunningLogged = running;
    }
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
        // After a failed attempt the device waits for its retry time (the timer calls this again).
        const auto nowMs = juce::Time::getMillisecondCounter();
        auto due = [nowMs] (const ReopenBackoff& b) { return b.failures == 0 || (juce::int32) (nowMs - b.nextMs) >= 0; };
        const bool tryIn = in == back && due (inputReopen), tryOut = out == back && due (outputReopen);
        if (! tryIn && ! tryOut)
            return;

        if ((tryIn && inputReopen.failures == 0) || (tryOut && outputReopen.failures == 0))
            juce::Logger::writeToLog (tr ("Cihaz yeniden bağlandı, açılıyor: ") + (tryIn ? wantedInput : juce::String())
                                      + (tryIn && tryOut ? " / " : "") + (tryOut ? wantedOutput : juce::String()));
        openDevices (wantedInput, wantedOutput, preferredBufferSize);
        {
            const ChainEdit edit (*this);
            rebuildGraph();   // the microphone's channel count may differ
        }

        auto result = [nowMs] (ReopenBackoff& b, bool tried, MicVSTDeviceManager& dm, const juce::String& name)
        {
            if (! tried)
                return;
            auto* d = dm.getCurrentAudioDevice();
            if (d != nullptr && d->isOpen())
            {
                if (b.failures > 0)
                    juce::Logger::writeToLog ("'" + name + tr ("' açıldı (") + juce::String (b.failures + 1) + tr (". denemede)"));
                b = {};
                return;
            }
            ++b.failures;
            const int delayMs = juce::jmin (10000, 1000 << juce::jmin (b.failures - 1, 4));
            b.nextMs = nowMs + (juce::uint32) delayMs;
            if (b.failures == 1 || b.failures % 30 == 0)
                juce::Logger::writeToLog ("'" + name + tr ("' listede ama açılamıyor (başka bir program tek başına kullanıyor olabilir) - ")
                                          + juce::String (b.failures) + tr (". deneme; kendiliğinden yeniden denenecek (en çok 10 sn arayla)"));
        };
        result (inputReopen, tryIn, inputDevices, wantedInput);
        result (outputReopen, tryOut, outputDevices, wantedOutput);
        lastMissingState = 0;
        return;
    }
    if (in != back) inputReopen = {};     // open again, unplugged or not wanted: start over next time
    if (out != back) outputReopen = {};

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

    // The real formats: MicVST's streams are 32-bit float (shared mode; JUCE tries float32 first and
    // the Windows audio engine mixes in float32); the endpoints themselves run in the format set in
    // Windows (e.g. 32 bit microphone, 24 bit cable) and Windows converts between the two.
    auto endpoint = [] (juce::AudioIODevice* d, bool capture)
    {
        return d == nullptr ? tr ("(yok)") : devfmt::describe (devfmt::windowsEndpointFormat (d->getName(), capture));
    };
    juce::Logger::writeToLog (tr ("Ses biçimi: MicVST akışı ve iç işlem 32 bit kayan nokta; Windows aygıt biçimi: giriş ")
                              + endpoint (inputDevices.getCurrentAudioDevice(), true) + tr (" | çıkış ")
                              + endpoint (outputDevices.getCurrentAudioDevice(), false));
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

juce::String AudioEngine::priorityName (int choice)
{
    switch (choice)
    {
        case (int) rt::ProcessPriority::normal:      return tr ("Normal");
        case (int) rt::ProcessPriority::aboveNormal: return tr ("Normalin Üstünde");
        case (int) rt::ProcessPriority::high:        return tr ("Yüksek");
        default:                                     return tr ("başka bir seviye");
    }
}

rt::PriorityResult AudioEngine::setProcessPriority (int choice)
{
    choice = juce::jlimit (0, rt::numProcessPriorities - 1, choice);
    if (! rt::processPrioritySupported())
    {
        processPriority = choice;   // kept in config.xml; nothing to apply on this system
        return {};
    }

    const auto r = rt::setProcessPriority ((rt::ProcessPriority) choice);
    if (r.ok)
    {
        processPriority = choice;
        juce::Logger::writeToLog (tr ("İşlem önceliği: ") + priorityName (choice) + tr (" (uygulandı, Windows'tan okundu)"));
    }
    else
    {
        // Keep what the process really has (if it is one of the three), so the window, the
        // saved setting and Windows agree.
        if (juce::isPositiveAndBelow (r.actual, rt::numProcessPriorities))
            processPriority = r.actual;
        priorityProblem = tr ("İşlem önceliği \"") + priorityName (choice) + tr ("\" yapılamadı: Windows bu değişikliğe izin vermedi")
                        + (r.error != 0 ? tr (" (hata kodu ") + juce::String ((juce::int64) r.error) + ")" : juce::String())
                        + tr (". Şu anki öncelik: ") + priorityName (r.actual) + ".";
        ++priorityProblemCount;
        juce::Logger::writeToLog (priorityProblem);
    }
    return r;
}

int AudioEngine::chainLatencySamples() const
{
    if (pluginChain == nullptr)
        return graph.getLatencySamples();
    int sum = 0;   // serial chain: the delays add up
    for (auto& e : pluginChain->entries())
        if (! e.bypassed)
            if (auto* n = graph.getNodeForId (e.node))
                sum += juce::jmax (0, n->getProcessor()->getLatencySamples());
    return sum;
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
    rawBlock.setSize (2, bs, false, true, false);
    if (rawDelay.getNumSamples() != rawDelaySize)
        rawDelay.setSize (2, rawDelaySize);
    rawDelay.clear();
    rawDelayPos = 0;
    inputChannelsNow.store (ins);
    inputDeviceForXruns.store (device);
    inputXrunBaselinePending.store (true);
    monitor.setInputRate (sr);
    // the test recording's buffer (10 s at this rate) - allocated here, never while recording
    testMode.store (0);
    testBuffer.setSize (2, (int) std::ceil (testSeconds * sr), false, true, false);
    if (testState != TestState::idle)
    {
        testState = TestState::idle;
        updateMonitorDevice();
    }
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
    inputDeviceForXruns.store (nullptr);
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
    if (inputXrunBaselinePending.load (std::memory_order_relaxed))   // first block after a start: same thread that counts
    {
        if (auto* dev = inputDeviceForXruns.load (std::memory_order_relaxed))
            inputXrunBaseline.store (juce::jmax (0, dev->getXRunCount()), std::memory_order_relaxed);
        inputXrunBaselinePending.store (false, std::memory_order_release);
    }

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
        // the microphone as it came in, for "Ham ses" (A/B); a mono microphone on both channels
        rawBlock.copyFrom (0, 0, block, 0, 0, bs);
        rawBlock.copyFrom (1, 0, block, inputChannelsNow.load (std::memory_order_relaxed) > 1 ? 1 : 0, 0, bs);
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
        applyMuteAndCompare (block, sr);
        outputMeter.process (block);

        const float* chans[] = { block.getReadPointer (0), block.getReadPointer (1) };
        const double pushTime = nowSeconds();
        bridge.push (chans, 2, bs, captureTime, pushTime);
        recorder.push (chans, 2, bs, sr);   // Kayıt: a copy into its FIFO, nothing else (audio/Recorder.h)

        // test recording / playback and the headphones
        const int mode = testMode.load (std::memory_order_acquire);
        if (mode == 1)
        {
            const int at = testFrames.load (std::memory_order_relaxed);
            const int n = juce::jmin (bs, testBuffer.getNumSamples() - at);
            if (n > 0)
            {
                testBuffer.copyFrom (0, at, block, 0, 0, n);
                testBuffer.copyFrom (1, at, block, 1, 0, n);
                testFrames.store (at + n, std::memory_order_release);
            }
        }
        if (mode == 2)
        {
            // the recording instead of the live voice (the headphones only; the cable stays live)
            const int at = testPos.load (std::memory_order_relaxed), total = testFrames.load (std::memory_order_relaxed);
            const int n = juce::jlimit (0, bs, total - at);
            for (int c = 0; c < 2; ++c)
            {
                float* d = rawBlock.getWritePointer (c);   // free again here: reused as the playback block
                if (n > 0) juce::FloatVectorOperations::copy (d, testBuffer.getReadPointer (c, at), n);
                if (n < bs) juce::FloatVectorOperations::clear (d + n, bs - n);
            }
            // 10 ms raised-cosine fade in at the start and out at the end of the recording: a voice
            // cut off mid-wave is a click (and the headphone resampler's overshoot)
            const int fadeLen = juce::jmax (1, (int) (0.01 * sr));
            if (n > 0 && (at < fadeLen || total - (at + n) < fadeLen))
                for (int i = 0; i < n; ++i)
                {
                    const int pos = at + i;
                    const double gIn  = pos < fadeLen ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (pos + 0.5) / fadeLen) : 1.0;
                    const int left = total - 1 - pos;
                    const double gOut = left < fadeLen ? 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * (left + 0.5) / fadeLen) : 1.0;
                    const float g = (float) (gIn * gOut);
                    rawBlock.getWritePointer (0)[i] *= g;
                    rawBlock.getWritePointer (1)[i] *= g;
                }
            testPos.store (at + n, std::memory_order_release);
            const float* rec[] = { rawBlock.getReadPointer (0), rawBlock.getReadPointer (1) };
            monitor.push (rec, bs, captureTime, pushTime);
        }
        else
            monitor.push (chans, bs, captureTime, pushTime);

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

void AudioEngine::applyMuteAndCompare (juce::AudioBuffer<float>& block, double sr) noexcept
{
    const int bs = block.getNumSamples();
    // keep the raw ring fed all the time: "Ham ses" then has the right moment at once
    const int L = juce::jlimit (0, rawDelaySize - bs - 1, chainLatencyNow.load (std::memory_order_relaxed));
    float* const d0 = rawDelay.getWritePointer (0);
    float* const d1 = rawDelay.getWritePointer (1);
    float* const r0 = rawBlock.getWritePointer (0);
    float* const r1 = rawBlock.getWritePointer (1);
    for (int i = 0; i < bs; ++i)
    {
        const int w = (rawDelayPos + i) & (rawDelaySize - 1);
        d0[w] = r0[i];
        d1[w] = r1[i];
        const int rd = (w - L) & (rawDelaySize - 1);
        r0[i] = d0[rd];   // rawBlock now holds the delayed raw audio
        r1[i] = d1[rd];
    }
    rawDelayPos = (rawDelayPos + bs) & (rawDelaySize - 1);

    const bool wantRaw = rawCompare.load (std::memory_order_relaxed), wantMute = muted.load (std::memory_order_relaxed);
    const double cmpStep = 1.0 / juce::jmax (1.0, 0.030 * sr), muteStep = 1.0 / juce::jmax (1.0, 0.015 * sr);
    if (! wantRaw && compareMix <= 0.0 && ! wantMute && muteGain >= 1.0)
        return;   // the usual case: nothing to do

    float* const o0 = block.getWritePointer (0);
    float* const o1 = block.getWritePointer (1);
    const double pi = juce::MathConstants<double>::pi;
    for (int i = 0; i < bs; ++i)
    {
        compareMix = wantRaw ? juce::jmin (1.0, compareMix + cmpStep) : juce::jmax (0.0, compareMix - cmpStep);
        muteGain = wantMute ? juce::jmax (0.0, muteGain - muteStep) : juce::jmin (1.0, muteGain + muteStep);
        const double x = 0.5 - 0.5 * std::cos (pi * compareMix);   // raised cosine
        const double g = 0.5 - 0.5 * std::cos (pi * muteGain);
        const double a = std::sqrt (1.0 - x), b = std::sqrt (x);   // equal power (raw and processed differ)
        o0[i] = (float) ((a * o0[i] + b * r0[i]) * g);
        o1[i] = (float) ((a * o1[i] + b * r1[i]) * g);
    }
}

void AudioEngine::applyEditFade (juce::AudioBuffer<float>& block, double sr) noexcept
{
    // Plug-in thread. During a chain edit: fade out, then silence (acknowledged to the message
    // thread, which only then switches the graph); after the edit one more silent block (the
    // graph hands the new render sequence over at the start of a block, so the new chain runs
    // from this block or the next) plus as long as the chain delays the audio, then fade back in.
    // The wait for the delay: elements with latency (Otomatik Kazanç's 3 ms look-ahead, a
    // look-ahead limiter, a linear-phase EQ) still play audio from before the edit until their
    // delay lines have run through - e.g. at the level before a bypass. Fading in while that
    // seam is still to come would put it into the audible audio as a click.
    const int n = block.getNumSamples();
    const bool editing = chainEditDepth.load (std::memory_order_acquire) > 0;
    const auto generation = chainEditGeneration.load (std::memory_order_acquire);
    if (generation != seenEditGeneration)
    {
        seenEditGeneration = generation;
        if (! editing && editGain <= 0.0f)
        {
            const int drain = juce::jlimit (0, (int) sr, editDrainSamples.load (std::memory_order_relaxed));   // at most 1 s
            editSettleBlocks = 1 + (drain + n - 1) / juce::jmax (1, n);
        }
    }

    const float step = 1.0f / (float) juce::jmax (1.0, editFadeSeconds * sr);

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

    editLatencyBefore = chainLatencySamples();   // the chain's delay before the edit (endChainEdit)

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
    // The outermost edit ends: how long the chain still plays audio from before the edit (the
    // larger delay of the old and the new chain), published before the generation that the
    // plug-in thread waits for.
    if (chainEditDepth.load (std::memory_order_relaxed) == 1)
        editDrainSamples.store (juce::jmax (editLatencyBefore, chainLatencySamples()), std::memory_order_relaxed);
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
    d.inputDeviceXruns = (in == nullptr || inputXrunBaselinePending.load (std::memory_order_acquire)) ? 0
                       : juce::jmax (0, in->getXRunCount() - inputXrunBaseline.load (std::memory_order_relaxed));
    d.watchdogRestarts = watchdogRestarts.load();
    d.bridge = bridge.getStats();

    if (in != nullptr && d.inputRate > 0.0)
    {
        double ms = 1000.0 * (in->getInputLatencyInSamples() + chainLatencySamples()
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
    chainLatencyNow.store (chainLatencySamples());
    testTick();

    // --- Kayıt: a write error, a full disk or a new rate ends the recording (file kept, checked) --
    if (recorder.hasFailed())
    {
        const auto why = recorder.getFailure();
        const auto r = recorder.stop();
        logRecordingResult (r);
        if (onRecordingStopped) onRecordingStopped (r, why);
    }

    // --- a chosen device that is listed but could not be opened: retry when its pause is over ----
    if ((inputReopen.failures > 0 || outputReopen.failures > 0) && ! reconnecting)
        reconnectIfNeeded();

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
    s.processPriority = processPriority;
    s.pluginFolders = pluginFolders;
    s.monitorDevice = monitor.getDeviceName();
    s.monitorVolumeDb = monitor.getVolumeDb();
    s.recordFormat = recordSettings.encode();
    s.recordFolder = recordSettings.folder == juce::File() ? juce::String() : recordSettings.folder.getFullPathName();
    s.cueSounds = cues.getSettings().isDefault() ? juce::String() : cues.getSettings().encode();

    // Ketten-Restore steht noch aus -> gemerkten Zustand verbatim zurückgeben,
    // sonst würde persistState() die gespeicherte Kette mit "leer" überschreiben.
    s.presetName = presetName;
    s.presetModified = presetModified;
    s.plugins = captureChain();
    return s;
}

juce::Array<PluginEntryState> AudioEngine::captureChain()
{
    if (! pendingPlugins.isEmpty()) return pendingPlugins;
    juce::Array<PluginEntryState> list;
    if (pluginChain == nullptr) return list;

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
        list.add (p);
    }
    return list;
}

void AudioEngine::applyState (const MicVSTState& s)
{
    setPreferredBufferSize (s.bufferSize);
    if (s.sampleRate > 0.0) preferredInputRate = s.sampleRate;
    setSafetyBufferMs (s.safetyBufferMs);
    setProcessPriority (s.processPriority);   // Normal unless the user chose otherwise
    monitor.setDeviceName (s.monitorDevice);
    monitor.setVolumeDb (s.monitorVolumeDb);
    recordSettings = RecordSettings::decode (s.recordFormat, s.recordFolder);
    cues.setSettings (CueSettings::decode (s.cueSounds));
    presetName = s.presetName;
    presetModified = s.presetModified;
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
    const ChainEdit edit (*this);   // muted until the whole chain is in place
    for (auto& p : plugins)
        addElement (p, false);
    rebuildGraph();
}

bool AudioEngine::addElement (const PluginEntryState& p, bool forPreset, juce::StringArray* missing)
{
    const double sr = getProcessingSampleRate();
    const int    bs = getProcessingBlockSize();
    const auto before = pluginChain->entries().size();

    auto applyStateAndBypass = [&]
    {
        const int idx = (int) pluginChain->entries().size() - 1;
        if (auto* node = graph.getNodeForId (pluginChain->entries()[(size_t) idx].node))
            node->getProcessor()->setStateInformation (p.state.getData(), (int) p.state.getSize());
        pluginChain->setBypass (idx, p.bypassed);
    };

    if (p.fileOrId == PluginChain::monoToStereoId || p.fileOrId == PluginChain::stereoToMonoId
         || p.fileOrId == PluginChain::gainId || p.fileOrId == PluginChain::autoGainId)
    {
        if (p.fileOrId == PluginChain::monoToStereoId)      pluginChain->addMonoToStereo();
        else if (p.fileOrId == PluginChain::stereoToMonoId) pluginChain->addStereoToMono();
        else if (p.fileOrId == PluginChain::gainId)         pluginChain->addGain();
        else if (forPreset)                                 pluginChain->addAutoGainForPreset (true);
        else                                                pluginChain->addAutoGain();
        if (pluginChain->entries().size() == before)
            return false;   // nicht angelegt: nie den Zustand/Bypass des vorherigen Glieds ändern
        if (p.fileOrId == PluginChain::gainId || p.fileOrId == PluginChain::autoGainId)   // dB-Wert bzw. Ein/Aus
            applyStateAndBypass();
        else
            pluginChain->setBypass ((int) before, p.bypassed);
        return true;
    }

    if (PluginChain::isBuiltInEffectId (p.fileOrId))
    {
        if (! pluginChain->addBuiltInEffect (p.fileOrId))
            return false;
        applyStateAndBypass();
        return true;
    }
    if (p.fileOrId.startsWith ("builtin:"))   // a built-in element of a newer MicVST: skipped, the rest stays
    {
        juce::Logger::writeToLog (tr ("Bilinmeyen dahili eleman atlandı: ") + p.fileOrId);
        if (missing != nullptr) missing->add (p.fileOrId);
        return false;
    }

    auto type = findPluginType (p, true);
    if (type == nullptr)
    {
        juce::Logger::writeToLog (tr ("Eklenti bulunamadı: ") + (p.pluginName.isNotEmpty() ? p.pluginName + " - " : juce::String())
                                  + p.fileOrId);
        if (missing != nullptr) missing->add (p.pluginName.isNotEmpty() ? p.pluginName : juce::File (p.fileOrId).getFileNameWithoutExtension());
        return false;
    }
    juce::String err;
    if (pluginChain->addPlugin (formatManager, *type, sr, bs, err))
    {
        applyStateAndBypass();
        return true;
    }
    juce::Logger::writeToLog (tr ("Eklenti yüklenemedi: ") + p.fileOrId + " - " + err);
    if (missing != nullptr) missing->add (type->name);
    return false;
}

AudioEngine::PresetResult AudioEngine::applyPreset (const juce::String& name, const juce::Array<PluginEntryState>& elements)
{
    PresetResult result;
    undoChain = captureChain();
    undoPresetName = presetName;
    undoPresetModified = presetModified;
    hasUndoChain = true;
    presetName = name;
    presetModified = false;

    // The chain is still waiting for the plug-in scan: the preset takes its place.
    if (! pendingPlugins.isEmpty())
    {
        pendingPlugins = elements;
        return result;
    }

    // Same elements, same order, same on/off, all built-in: the settings glide (no chain edit).
    const auto& es = pluginChain->entries();
    bool sameShape = (int) es.size() == elements.size();
    for (int i = 0; sameShape && i < elements.size(); ++i)
    {
        const auto& p = elements.getReference (i);
        sameShape = es[(size_t) i].fileOrId == p.fileOrId && es[(size_t) i].bypassed == p.bypassed
                    && p.fileOrId.startsWith ("builtin:")
                    && (PluginChain::isBuiltInEffectId (p.fileOrId) || p.fileOrId == PluginChain::gainId
                        || p.fileOrId == PluginChain::autoGainId || p.fileOrId == PluginChain::monoToStereoId
                        || p.fileOrId == PluginChain::stereoToMonoId);
    }
    if (sameShape)
    {
        for (int i = 0; i < elements.size(); ++i)
        {
            const auto& p = elements.getReference (i);
            if (auto* fxProc = pluginChain->getBuiltInEffect (i))
                fxProc->setStateInformation (p.state.getData(), (int) p.state.getSize());   // parameters: they glide
            else if (p.fileOrId == PluginChain::gainId)
            {
                BuiltInGainProcessor parsed;   // read the value, then glide to it (setState would jump)
                parsed.setStateInformation (p.state.getData(), (int) p.state.getSize());
                pluginChain->setGainDb (i, parsed.getGainDb());
            }
            else if (p.fileOrId == PluginChain::autoGainId)
            {
                BuiltInAutoGainProcessor parsed;
                parsed.setStateInformation (p.state.getData(), (int) p.state.getSize());
                pluginChain->setAutoGainEnabled (i, parsed.isAutoGainEnabled());
            }
        }
        result.morphed = true;
        return result;
    }

    // Different elements: create and load them now (unconnected = silent), then swap in one edit.
    std::vector<PluginChain::Entry> prepared;
    for (auto& p : elements)
        if (addElement (p, true, &result.missing))
            prepared.push_back (pluginChain->takeLast());
    {
        const ChainEdit edit (*this);
        while (! pluginChain->entries().empty())
            pluginChain->removePlugin (0);
        for (auto& e : prepared)
            pluginChain->appendEntry (e);
        rebuildGraph();
    }
    return result;
}

void AudioEngine::undoPreset()
{
    if (! hasUndoChain) return;
    const auto chainBack = undoChain;
    const auto nameBack = undoPresetName;
    const bool modifiedBack = undoPresetModified;
    applyPreset (nameBack, chainBack);   // (which keeps the preset's chain for another undo)
    presetModified = modifiedBack;
}

//==============================================================================
void AudioEngine::setMuted (bool m)
{
    if (muted.exchange (m) == m) return;
    cues.play (m);   // the sound first: nothing between the state change and the start of the sound
    const auto cue = cues.getSettings().enabled ? " - " + tr ("bildirim sesi: ") + cues.describe (m) : juce::String();
    juce::Logger::writeToLog ((m ? tr ("Ses kapatıldı (sustur)") : tr ("Ses açıldı")) + cue);
}

void AudioEngine::setRawCompare (bool on)
{
    if (rawCompare.exchange (on) == on) return;
    juce::Logger::writeToLog (on ? tr ("Ham ses: açık (zincir atlanıyor)") : tr ("Ham ses: kapalı (işlenmiş ses)"));
}

juce::String AudioEngine::startRecording()
{
    const double rate = getProcessingSampleRate();
    const auto err = isRunning() ? recorder.start (recordSettings, rate, 2)
                                 : tr ("Ses çalışmıyor: önce mikrofon ve kablo açık olmalı");
    if (err.isEmpty())
    {
        auto used = recordSettings;
        used.sampleRate = recordSettings.effectiveRate (rate);
        juce::Logger::writeToLog (tr ("Kayıt başladı: ") + recorder.getFile().getFullPathName() + " (" + used.describe()
                                  + tr (", işlem hızı ") + juce::String ((int) std::lround (rate)) + " Hz)");
    }
    else
        juce::Logger::writeToLog (tr ("Kayıt başlatılamadı: ") + err);
    return err;
}

Recorder::Result AudioEngine::stopRecording()
{
    const auto r = recorder.stop();
    logRecordingResult (r);
    return r;
}

void AudioEngine::logRecordingResult (const Recorder::Result& r)
{
    if (r.file == juce::File()) return;
    juce::Logger::writeToLog (tr ("Kayıt bitti: ") + r.file.getFullPathName() + ", " + juce::String (r.seconds, 1) + " s, "
                              + (r.verified ? tr ("dosya doğrulandı") : tr ("dosya DOĞRULANAMADI"))
                              + (r.droppedFrames > 0 ? tr (", kayıp örnek: ") + juce::String (r.droppedFrames) : juce::String())
                              + (r.problem.isNotEmpty() ? " - " + r.problem : juce::String()));
}

void AudioEngine::setMonitorDevice (const juce::String& name)
{
    monitor.setDeviceName (name);
}

juce::String AudioEngine::setMonitorEnabled (bool on)
{
    monitorEnabled = on;
    updateMonitorDevice();
    if (on && ! monitor.isOpen())
    {
        monitorEnabled = false;
        const auto err = monitor.open();   // (fails again: gives the reason)
        monitor.close();
        return err.isNotEmpty() ? err : tr ("Ses cihazı açılamadı");
    }
    return {};
}

void AudioEngine::updateMonitorDevice()
{
    const bool needed = monitorEnabled || testState == TestState::playing;
    if (needed && ! monitor.isOpen())
        monitor.open();
    else if (! needed && monitor.isOpen())
        monitor.close();
    monitor.setAudible (needed);
}

juce::String AudioEngine::startTestRecording()
{
    if (! inputRunningFlag.load())
        return tr ("Mikrofon çalışmıyor");
    stopTest();
    if (testState != TestState::idle) stopTest();
    testTotalFrames = testBuffer.getNumSamples();
    if (testTotalFrames <= 0)
        return tr ("Mikrofon çalışmıyor");
    testMode.store (0);
    testFrames.store (0);
    testPos.store (0);
    testMode.store (1, std::memory_order_release);
    testState = TestState::recording;
    return {};
}

void AudioEngine::stopTest()
{
    if (testState == TestState::recording)
    {
        testTotalFrames = testFrames.load();
        testTick();   // plays what there is
        if (testState == TestState::recording)   // nothing recorded
        {
            testMode.store (0);
            testState = TestState::idle;
        }
        return;
    }
    if (testState == TestState::playing)
    {
        testMode.store (0);
        testState = TestState::idle;
        updateMonitorDevice();
    }
}

double AudioEngine::getTestProgress() const
{
    const int total = juce::jmax (1, testState == TestState::playing ? testFrames.load() : testTotalFrames);
    const int at = testState == TestState::playing ? testPos.load() : testFrames.load();
    return juce::jlimit (0.0, 1.0, (double) at / (double) total);
}

void AudioEngine::testTick()
{
    if (testState == TestState::recording && testFrames.load (std::memory_order_acquire) >= juce::jmax (1, testTotalFrames))
    {
        testMode.store (0);
        if (testFrames.load() == 0) { testState = TestState::idle; return; }
        testPos.store (0);
        testState = TestState::playing;
        updateMonitorDevice();
        testMode.store (2, std::memory_order_release);
        juce::Logger::writeToLog (tr ("Deneme kaydı çalınıyor: ") + monitor.getOpenDeviceName());
    }
    else if (testState == TestState::playing && testPos.load (std::memory_order_acquire) >= testFrames.load())
    {
        // let the headphones play out what is still in their buffer (~0.3 s) before closing them
        const auto now = juce::Time::getMillisecondCounter();
        if (playbackEndMs == 0) playbackEndMs = now;
        if (now - playbackEndMs < 400) return;
        playbackEndMs = 0;
        testMode.store (0);
        testState = TestState::idle;
        updateMonitorDevice();
    }
}
