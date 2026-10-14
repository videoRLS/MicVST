#include "ui/DevicePanel.h"

DevicePanel::DevicePanel (AudioEngine& e) : engine (e)
{
    for (auto* l : { &inLabel, &outLabel })
        l->setJustificationType (juce::Justification::centredLeft);

    addAndMakeVisible (inLabel);  addAndMakeVisible (inBox);
    addAndMakeVisible (outLabel); addAndMakeVisible (outBox);

    inInfo.setTooltip ("Select your microphone here");
    outInfo.setTooltip ("Select \"CABLE Input\" here, "
                        "and choose \"CABLE Output\" as the microphone in Discord etc.");
    addAndMakeVisible (inInfo);
    addAndMakeVisible (outInfo);

    bufInfo.setTooltip ("Buffer size in samples. Smaller = lower latency, higher CPU risk. "
                        "\"Auto\" uses the device default. Only shown when the devices "
                        "support multiple sizes (low-latency mode).");
    addChildComponent (bufLabel); addChildComponent (bufBox); addChildComponent (bufInfo);
    bufLabel.setJustificationType (juce::Justification::centredLeft);
    bufBox.onChange = [this] { apply(); };

    // Sicherheitspuffer zwischen Mikrofon und Kabel (AudioBridge) - immer sichtbar. Ändern
    // öffnet die Geräte nicht neu.
    safetyInfo.setTooltip ("Audio kept in reserve between microphone and cable. It absorbs CPU spikes "
                           "(heavy games, rendering) without crackles. \"Auto\" starts at 10 ms and "
                           "adds 5 ms whenever the output ever runs dry. Larger = more robust, a few ms "
                           "more latency.");
    safetyLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (safetyLabel); addAndMakeVisible (safetyBox); addAndMakeVisible (safetyInfo);
    safetyBox.addItem ("Auto (recommended)", 1);
    for (int i = 0; i < (int) std::size (AudioEngine::safetyChoicesMs); ++i)
        safetyBox.addItem (juce::String (AudioEngine::safetyChoicesMs[i]) + " ms", i + 2);
    safetyBox.onChange = [this] { applySafety(); };

    // Zwei Info-Zeilen: Geräte/Latenz und CPU-Last/Aussetzer.
    for (auto* l : { &statusLabel, &statsLabel })
    {
        l->setJustificationType (juce::Justification::centredLeft);
        l->setColour (juce::Label::textColourId, juce::Colours::grey);
        l->setFont (juce::Font (juce::FontOptions (13.0f)));
        addAndMakeVisible (*l);
    }

    inBox.onChange  = [this] { apply(); };
    outBox.onChange = [this] { apply(); };

    // Seit 1.2 sind Mikrofon und Kabel zwei eigene Geräte (je ein eigener Echtzeit-Thread).
    engine.getInputDeviceManager().addChangeListener (this);
    engine.getOutputDeviceManager().addChangeListener (this);
    refresh();
    startTimer (500);   // Info-Zeilen (Latenz, CPU-Last, Aussetzer) live halten
}

DevicePanel::~DevicePanel()
{
    stopTimer();
    engine.getInputDeviceManager().removeChangeListener (this);
    engine.getOutputDeviceManager().removeChangeListener (this);
}

void DevicePanel::changeListenerCallback (juce::ChangeBroadcaster*)
{
    refresh();   // Gerät extern geändert (z. B. ab-/angesteckt) -> Combos spiegeln
}

void DevicePanel::timerCallback()
{
    updateStatus();   // nur die Info-Zeilen; Combos nicht anfassen (User könnte gerade wählen)
}

void DevicePanel::refresh()
{
    updating = true;   // verhindert, dass das Befüllen apply() auslöst

    inBox.clear (juce::dontSendNotification);
    outBox.clear (juce::dontSendNotification);

    auto& inDm  = engine.getInputDeviceManager();
    auto& outDm = engine.getOutputDeviceManager();

    if (auto* type = inDm.getCurrentDeviceTypeObject())
    {
        const auto ins = type->getDeviceNames (true);
        for (int i = 0; i < ins.size(); ++i) inBox.addItem (ins[i], i + 1);
        const int si = ins.indexOf (inDm.getAudioDeviceSetup().inputDeviceName);
        if (si >= 0) inBox.setSelectedId (si + 1, juce::dontSendNotification);
    }

    if (auto* type = outDm.getCurrentDeviceTypeObject())
    {
        const auto outName = outDm.getAudioDeviceSetup().outputDeviceName;
        outBox.addItem ("(none)", 1);   // leerer Output-Name = kein Host-Output
        const auto outs = type->getDeviceNames (false);
        for (int i = 0; i < outs.size(); ++i) outBox.addItem (outs[i], i + 2);
        const int so = outs.indexOf (outName);
        outBox.setSelectedId (outName.isNotEmpty() && so >= 0 ? so + 2 : 1,
                              juce::dontSendNotification);
    }

    // Buffer-Zeile: nur wenn das Mikrofon mehrere Größen anbietet (Low-Latency-Modus). Die
    // Wahl gilt für Mikrofon und Kabel.
    bufBox.clear (juce::dontSendNotification);
    auto* dev = inDm.getCurrentAudioDevice();
    const auto sizes = dev != nullptr ? dev->getAvailableBufferSizes() : juce::Array<int>();
    const bool showBuf = sizes.size() > 1;
    if (showBuf)
    {
        const double sr = dev->getCurrentSampleRate() > 0.0 ? dev->getCurrentSampleRate() : 48000.0;
        bufBox.addItem ("Auto (default)", 1);
        for (int i = 0; i < sizes.size(); ++i)
            bufBox.addItem (juce::String (sizes[i]) + " samples ("
                                + juce::String (sizes[i] / sr * 1000.0, 1) + " ms)", i + 2);
        const int pref = engine.getPreferredBufferSize();
        const int idx = sizes.indexOf (pref);
        bufBox.setSelectedId (pref > 0 && idx >= 0 ? idx + 2 : 1, juce::dontSendNotification);

        // Der gewählte Wert existiert beim aktuellen Gerät nicht mehr (z. B. Gerät
        // gewechselt) -> Anzeige fällt auf "Auto" zurück, also muss auch der Engine-
        // Zustand nachziehen, sonst würde captureState() einen nie bestätigten Wert
        // persistieren. Nur bei sichtbarer Zeile: bei !showBuf bleibt die Wahl erhalten
        // (z. B. USB-Interface kurz getrennt) -- JUCE klemmt unpassende Größen ohnehin.
        if (pref > 0 && idx < 0)
            engine.setPreferredBufferSize (0);
    }
    if (showBuf != bufBox.isVisible())
    {
        bufLabel.setVisible (showBuf); bufBox.setVisible (showBuf); bufInfo.setVisible (showBuf);
        if (auto* p = getParentComponent()) p->resized();   // MainComponent-Layout nachziehen
    }

    {
        // Safety-Combo spiegeln (0 = Auto).
        const int ms = engine.getSafetyBufferMs();
        int id = 1;
        for (int i = 0; i < (int) std::size (AudioEngine::safetyChoicesMs); ++i)
            if (AudioEngine::safetyChoicesMs[i] == ms) id = i + 2;
        safetyBox.setSelectedId (id, juce::dontSendNotification);
    }

    updateStatus();

    updating = false;
}

void DevicePanel::updateStatus()
{
    const auto d = engine.getDiagnostics();

    // Zeile 1: Geräte + Ende-zu-Ende-Latenz durch MicVST (Geräte-Latenz In+Out, Plugin-Latenz
    // des Graphen, Resampler und der tatsächlich gepufferte Vorrat der Bridge).
    juce::String st = engine.isRunning() ? juce::String ("Active")
                                         : juce::String ("Idle - device disconnected");
    if (d.inputRate > 0.0)
        st << "   |   mic " << juce::String (d.inputRate, 0) << " Hz / " << d.inputBlock;
    if (d.outputSelected && d.outputRate > 0.0)
        st << "   |   cable " << juce::String (d.outputRate, 0) << " Hz / " << d.outputBlock;
    if (d.latencyMs > 0.0)
        st << "   |   latency " << juce::String (d.latencyMs, 1) << " ms";
    statusLabel.setText (st, juce::dontSendNotification);

    // Zeile 2: CPU-Last der Plugin-Kette, Sicherheitspuffer, Taktdrift und JEDER Aussetzer
    // (orange, sobald einer auftrat; Tooltip schlüsselt die Ursachen auf).
    const auto dropouts = d.bridge.underruns + d.overloads + (uint64_t) d.inputDeviceXruns;
    juce::String s2;
    s2 << "CPU " << juce::String (d.dspLoadPercent, 0) << " % (peak " << juce::String (d.dspPeakPercent, 0) << " %)";
    if (d.outputSelected)
        s2 << "   |   safety " << juce::String (d.bridge.safetyMs, 0) << " ms"
           << (engine.getSafetyBufferMs() == 0 ? " (auto)" : "")
           << "   |   drift " << (d.bridge.driftPpm >= 0 ? "+" : "") << juce::String (d.bridge.driftPpm, 0) << " ppm";
    s2 << "   |   dropouts " << juce::String ((juce::int64) dropouts);
    statsLabel.setText (s2, juce::dontSendNotification);
    statsLabel.setColour (juce::Label::textColourId, dropouts > 0 ? juce::Colours::orange : juce::Colours::grey);
    statsLabel.setTooltip ("Output ran dry: " + juce::String ((juce::int64) d.bridge.underruns)
                           + "   |   plug-in chain slower than real time: " + juce::String ((juce::int64) d.overloads)
                           + "   |   microphone driver gaps: " + juce::String (d.inputDeviceXruns)
                           + "   |   latency trims: " + juce::String ((juce::int64) d.bridge.resyncs)
                           + "   |   device restarts: " + juce::String ((juce::int64) d.watchdogRestarts));
}

void DevicePanel::apply()
{
    if (updating) return;

    auto* inType  = engine.getInputDeviceManager().getCurrentDeviceTypeObject();
    auto* outType = engine.getOutputDeviceManager().getCurrentDeviceTypeObject();
    if (inType == nullptr || outType == nullptr) return;

    const auto ins  = inType->getDeviceNames (true);
    const auto outs = outType->getDeviceNames (false);

    const int ii = inBox.getSelectedId() - 1;
    juce::String input = juce::isPositiveAndBelow (ii, ins.size()) ? ins[ii] : juce::String();

    juce::String output;   // "(none)" (id 1) -> leer
    const int oid = outBox.getSelectedId();
    if (oid >= 2 && juce::isPositiveAndBelow (oid - 2, outs.size())) output = outs[oid - 2];

    // Buffer-Wunsch: id 1 = Auto (0). Bei Auto den Geräte-Default explizit setzen,
    // weil setDeviceConfig 0 als "unverändert" interpretiert (sonst kein Zurückstellen).
    auto* dev = engine.getInputDeviceManager().getCurrentAudioDevice();
    int buf = 0;
    if (bufBox.isVisible() && bufBox.getSelectedId() >= 2)
    {
        const auto sizes = dev != nullptr ? dev->getAvailableBufferSizes() : juce::Array<int>();
        const int idx = bufBox.getSelectedId() - 2;
        if (juce::isPositiveAndBelow (idx, sizes.size())) buf = sizes[idx];
    }
    engine.setPreferredBufferSize (buf);
    const int effective = buf > 0 ? buf : (dev != nullptr ? dev->getDefaultBufferSize() : 0);
    engine.setDeviceConfig (input, output, 0.0, effective);
}

void DevicePanel::applySafety()
{
    if (updating) return;
    // id 1 = Auto (0), sonst feste Größe aus safetyChoicesMs. Wirkt sofort, ohne Geräte-Neustart.
    const int idx = safetyBox.getSelectedId() - 2;
    engine.setSafetyBufferMs (juce::isPositiveAndBelow (idx, (int) std::size (AudioEngine::safetyChoicesMs))
                                  ? AudioEngine::safetyChoicesMs[idx] : 0);
    engine.requestPersist();
    updateStatus();
}

void DevicePanel::resized()
{
    constexpr int rowH = 26, statusH = 20, labelW = 84, gap = 4;
    auto r = getLocalBounds();

    auto row = [&] (int h)
    {
        auto a = r.removeFromTop (h);
        r.removeFromTop (gap);
        return a;
    };

    // Label + "?"-Icon in der Label-Spalte, danach die ComboBox.
    auto labelWithInfo = [labelW] (juce::Rectangle<int> r, juce::Label& lbl, InfoIcon& info)
    {
        auto col = r.removeFromLeft (labelW);
        info.setBounds (col.removeFromRight (18).reduced (2));
        lbl.setBounds (col);
        return r;
    };

    auto inRow = row (rowH);
    inBox.setBounds (labelWithInfo (inRow, inLabel, inInfo));
    auto outRow = row (rowH);
    outBox.setBounds (labelWithInfo (outRow, outLabel, outInfo));

    if (bufBox.isVisible())
    {
        auto bufRow = row (rowH);
        bufBox.setBounds (labelWithInfo (bufRow, bufLabel, bufInfo));
    }

    auto safetyRow = row (rowH);
    safetyBox.setBounds (labelWithInfo (safetyRow, safetyLabel, safetyInfo));

    statusLabel.setBounds (row (statusH));   // dauerhafte Info-Zeilen unter den Geräten
    statsLabel.setBounds (row (statusH));
}

int DevicePanel::preferredHeight() const
{
    // Input, Output, Safety (+ Buffer) à 26, zwei Info-Zeilen à 20, Lücken à 4 dazwischen:
    // 3 Zeilen -> 3*26 + 2*20 + 4*4 = 134; Buffer-Zeile sichtbar -> 4*26 + 2*20 + 5*4 = 164.
    return bufBox.isVisible() ? 164 : 134;
}
