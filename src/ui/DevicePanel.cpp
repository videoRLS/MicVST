#include "ui/DevicePanel.h"
#include "Text.h"

using micvst::tr;

DevicePanel::DevicePanel (AudioEngine& e) : engine (e)
{
    inLabel.setText (tr ("Giriş"), juce::dontSendNotification);
    outLabel.setText (tr ("Çıkış"), juce::dontSendNotification);
    bufLabel.setText (tr ("Tampon"), juce::dontSendNotification);
    safetyLabel.setText (tr ("Güvenlik payı"), juce::dontSendNotification);
    for (auto* l : { &inLabel, &outLabel })
        l->setJustificationType (juce::Justification::centredLeft);
    for (auto* b : { &inBox, &outBox, &bufBox, &safetyBox })
        b->setTextWhenNoChoicesAvailable (tr ("(seçenek yok)"));

    addAndMakeVisible (inLabel);  addAndMakeVisible (inBox);
    addAndMakeVisible (outLabel); addAndMakeVisible (outBox);

    inInfo.setTooltip (tr ("Mikrofonunuzu buradan seçin."));
    outInfo.setTooltip (tr ("Buradan \"CABLE Input\" seçin; Discord, OBS gibi programlarda "
                            "mikrofon olarak \"CABLE Output\" seçin."));
    addAndMakeVisible (inInfo);
    addAndMakeVisible (outInfo);

    bufInfo.setTooltip (tr ("Ses bloklarının boyutu. Küçük değer: daha az gecikme ama işlemciye "
                            "daha çok yük. \"Otomatik\" cihazın kendi değerini kullanır."));
    addChildComponent (bufLabel); addChildComponent (bufBox); addChildComponent (bufInfo);
    bufLabel.setJustificationType (juce::Justification::centredLeft);
    bufBox.onChange = [this] { apply(); };

    // Sicherheitspuffer zwischen Mikrofon und Kabel (AudioBridge) - immer sichtbar. Ändern
    // öffnet die Geräte nicht neu.
    safetyInfo.setTooltip (tr ("Mikrofonla kablo arasında yedekte tutulan ses. Ağır oyun veya video "
                               "işleme gibi anlık yüklerde çıtırtıyı önler. \"Otomatik\" 10 ms ile "
                               "başlar, ses kesilirse kendiliğinden artar. Büyük değer: daha sağlam, "
                               "biraz daha fazla gecikme."));
    safetyLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (safetyLabel); addAndMakeVisible (safetyBox); addAndMakeVisible (safetyInfo);
    safetyBox.addItem (tr ("Otomatik (önerilen)"), 1);
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
        outBox.addItem (tr ("(yok)"), 1);   // leerer Output-Name = kein Host-Output
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
        bufBox.addItem (tr ("Otomatik (varsayılan)"), 1);
        for (int i = 0; i < sizes.size(); ++i)
            bufBox.addItem (juce::String (sizes[i]) + tr (" örnek (")
                                + micvst::trNumber (sizes[i] / sr * 1000.0, 1) + " ms)", i + 2);
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
    juce::String st = engine.isRunning() ? tr ("Çalışıyor")
                                         : tr ("Durdu - cihaz bağlı değil");
    if (d.inputRate > 0.0)
        st << "  |  " << tr ("mikrofon ") << juce::roundToInt (d.inputRate) << " Hz / " << d.inputBlock;
    if (d.outputSelected && d.outputRate > 0.0)
        st << "  |  " << tr ("kablo ") << juce::roundToInt (d.outputRate) << " Hz / " << d.outputBlock;
    if (d.latencyMs > 0.0)
        st << "  |  " << tr ("gecikme ") << micvst::trNumber (d.latencyMs, 1) << " ms";
    statusLabel.setText (st, juce::dontSendNotification);

    // Zeile 2: CPU-Last der Plugin-Kette, Sicherheitspuffer, Taktdrift und JEDER hörbare Aussetzer
    // (orange, sobald einer auftrat; Tooltip schlüsselt die Ursachen auf). Einzelne zu langsame
    // Plugin-Blöcke sind seit 1.2.1 kein Aussetzer mehr (die Eingangs-Warteschlange fängt sie ab)
    // und stehen nur im Tooltip; im Mittel über 100 % = die Kette ist für die aktuelle Last zu schwer.
    const auto dropouts = d.bridge.underruns + d.inputSkips + d.inputQueueOverflows + (uint64_t) d.inputDeviceXruns;
    const bool tooHeavy = d.dspLoadPercent >= 100.0;
    juce::String s2;
    s2 << tr ("İşlemci ") << micvst::trPercent (d.dspLoadPercent)
       << tr (" (tepe ") << micvst::trPercent (d.dspPeakPercent) << ")";
    if (tooHeavy)
        s2 << tr (" - zincir çok ağır");
    if (d.outputSelected)
        s2 << "  |  " << tr ("güvenlik payı ") << juce::roundToInt (d.bridge.safetyMs) << " ms"
           << (engine.getSafetyBufferMs() == 0 ? tr (" (otomatik)") : juce::String());
    s2 << "  |  " << tr ("kesinti ") << juce::String ((juce::int64) dropouts);
    statsLabel.setText (s2, juce::dontSendNotification);
    statsLabel.setColour (juce::Label::textColourId, tooHeavy ? juce::Colours::orangered
                                                   : dropouts > 0 ? juce::Colours::orange : juce::Colours::grey);

    juce::String tip;
    tip << tr ("Çıkışta ses kesildi: ") << (juce::int64) d.bridge.underruns << "\n"
        << tr ("Zincir yetişemedi, ses atlandı: ") << (juce::int64) d.inputSkips;
    if (d.inputSkips > 0)
        tip << " (" << juce::roundToInt (d.skippedMs) << " ms)";
    tip << "\n" << tr ("Mikrofon sürücüsünde kayıp: ") << d.inputDeviceXruns
        << "\n" << tr ("Eklenti işleme takıldı: ") << (juce::int64) d.inputQueueOverflows
        << "\n" << tr ("Gecikme düzeltmesi: ") << (juce::int64) d.bridge.resyncs
        << "\n" << tr ("Cihaz yeniden açıldı: ") << (juce::int64) d.watchdogRestarts
        << "\n" << tr ("Geç kalan eklenti bloğu (kesinti olmadan telafi edildi): ") << (juce::int64) d.overloads
        << "\n" << tr ("Saat farkı mikrofon/kablo: ") << (d.bridge.driftPpm >= 0 ? "+" : "")
        << juce::roundToInt (d.bridge.driftPpm) << " ppm";
    statsLabel.setTooltip (tip);
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
    // labelW: Platz für das längste türkische Label ("Güvenlik payı") plus "?"-Icon.
    constexpr int rowH = 26, statusH = 20, labelW = 118, gap = 4;
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
