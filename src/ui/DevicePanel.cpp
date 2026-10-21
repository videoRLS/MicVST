#include "ui/DevicePanel.h"
#include "Text.h"

using micvst::tr;

DevicePanel::DevicePanel (AudioEngine& e, bool compactMode) : engine (e), compact (compactMode)
{
    inLabel.setText (compact ? tr ("Mikrofon") : tr ("Giriş"), juce::dontSendNotification);
    outLabel.setText (compact ? tr ("Kablo") : tr ("Çıkış"), juce::dontSendNotification);
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

    // Prozess-Priorität (nur Windows). Ändern wirkt sofort, ohne Geräte-Neustart; die Combo zeigt
    // immer die Klasse, die Windows für MicVST gerade wirklich meldet.
    priorityLabel.setText (tr ("İşlem önceliği"), juce::dontSendNotification);
    priorityLabel.setJustificationType (juce::Justification::centredLeft);
    priorityInfo.setTooltip (tr ("MicVST'nin Windows işlem önceliği. Ses iş parçacıkları (mikrofon, eklenti zinciri, "
                                 "kablo) her seçenekte Windows'un gerçek zamanlı ses önceliğinde (MMCSS) çalışır. Bu ayar "
                                 "MicVST'nin diğer iş parçacıklarını (pencere, ölçerler, cihaz değişimleri, eklenti "
                                 "pencereleri, bazı eklentilerin arka plan işleri) diğer programlara göre öne alır. "
                                 "Normal önerilir; ağır bir oyun veya video işleme sırasında MicVST'nin penceresi ya da "
                                 "bir eklenti penceresi takılıyorsa Normalin Üstünde'yi deneyin."));
    priorityBox.addItem (tr ("Normal"), 1);
    priorityBox.addItem (tr ("Normalin Üstünde"), 2);
    priorityBox.addItem (tr ("Yüksek"), 3);
    priorityBox.onChange = [this] { applyPriority(); };
    if (priorityShown())
    {
        addAndMakeVisible (priorityLabel); addAndMakeVisible (priorityBox); addAndMakeVisible (priorityInfo);
    }

    // Kulaklık: where "Kendini dinle" and the test recording play (headphones), and how loud.
    monitorLabel.setText (tr ("Kulaklık"), juce::dontSendNotification);
    monitorLabel.setJustificationType (juce::Justification::centredLeft);
    monitorInfo.setTooltip (tr ("\"Kendini dinle\" ve \"Deneme kaydı\" sesi buradan çalar (kulaklık önerilir). "
                                "Cihaz yalnızca bunlar açıkken kullanılır; diğer programlar onu kullanmaya devam eder. "
                                "Sağdaki kaydırıcı dinleme sesinin seviyesidir (kabloyu etkilemez)."));
    monitorBox.onChange = [this]
    {
        if (updating) return;
        const int id = monitorBox.getSelectedId();
        engine.setMonitorDevice (id >= 2 ? monitorNames[id - 2] : juce::String());
        engine.requestPersist();
    };
    monitorVol.setSliderStyle (juce::Slider::LinearHorizontal);
    monitorVol.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    monitorVol.setRange (MonitorOutput::minVolumeDb, MonitorOutput::maxVolumeDb, 0.5);
    monitorVol.setDoubleClickReturnValue (true, 0.0);
    monitorVol.setValue (engine.getMonitorVolumeDb(), juce::dontSendNotification);
    monitorVol.textFromValueFunction = [] (double v) { return micvst::trNumber (v, 1) + " dB"; };
    monitorVol.setPopupDisplayEnabled (true, true, nullptr);
    monitorVol.onValueChange = [this] { engine.setMonitorVolumeDb ((float) monitorVol.getValue()); };
    monitorVol.onDragEnd = [this] { engine.requestPersist(); };
    addAndMakeVisible (monitorLabel); addAndMakeVisible (monitorInfo); addAndMakeVisible (monitorBox); addAndMakeVisible (monitorVol);

    // Zwei Info-Zeilen: Geräte/Latenz und CPU-Last/Aussetzer.
    for (auto* l : { &statusLabel, &statsLabel })
    {
        l->setJustificationType (juce::Justification::centredLeft);
        l->setColour (juce::Label::textColourId, micvst::theme::textDim);
        l->setFont (juce::Font (juce::FontOptions (13.0f)));
        addAndMakeVisible (*l);
    }

    inBox.onChange  = [this] { apply(); };
    outBox.onChange = [this] { apply(); };

    // one window shows the device combos, the other the settings rows
    for (auto* c : { (juce::Component*) &inLabel, (juce::Component*) &inBox, (juce::Component*) &inInfo,
                     (juce::Component*) &outLabel, (juce::Component*) &outBox, (juce::Component*) &outInfo })
        c->setVisible (compact);
    inInfo.setVisible (false);
    outInfo.setVisible (false);
    inBox.setTooltip (inInfo.getTooltip());
    outBox.setTooltip (outInfo.getTooltip());
    if (compact)
    {
        for (auto* c : { (juce::Component*) &safetyLabel, (juce::Component*) &safetyBox, (juce::Component*) &safetyInfo,
                         (juce::Component*) &monitorLabel, (juce::Component*) &monitorBox, (juce::Component*) &monitorVol,
                         (juce::Component*) &monitorInfo, (juce::Component*) &statsLabel })
            c->setVisible (false);
        statusLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    }
    else
    {
        statusLabel.setVisible (false);
        statsLabel.setVisible (false);
    }

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
    syncPriority();   // außer der Prioritäts-Combo, wenn sie zu ist (z. B. im Task-Manager geändert)
}

void DevicePanel::refresh()
{
    updating = true;   // verhindert, dass das Befüllen apply() auslöst

    inBox.clear (juce::dontSendNotification);
    outBox.clear (juce::dontSendNotification);

    monitorBox.clear (juce::dontSendNotification);
    monitorNames = engine.getMonitorDeviceNames();
    monitorBox.addItem (tr ("Windows varsayılanı"), 1);
    for (int i = 0; i < monitorNames.size(); ++i) monitorBox.addItem (monitorNames[i], i + 2);
    {
        const auto& chosen = engine.getMonitorDevice();
        const int mi = monitorNames.indexOf (chosen);
        if (chosen.isEmpty()) monitorBox.setSelectedId (1, juce::dontSendNotification);
        else if (mi >= 0)     monitorBox.setSelectedId (mi + 2, juce::dontSendNotification);
        else                  monitorBox.setText (chosen + tr (" (bağlı değil)"), juce::dontSendNotification);
    }

    auto& inDm  = engine.getInputDeviceManager();
    auto& outDm = engine.getOutputDeviceManager();

    // The chosen devices stay selected while unplugged ("... (bağlı değil)"); the engine reopens
    // them when they return and never switches to another device meanwhile.
    if (auto* type = inDm.getCurrentDeviceTypeObject())
    {
        const auto ins = type->getDeviceNames (true);
        for (int i = 0; i < ins.size(); ++i) inBox.addItem (ins[i], i + 1);
        const auto& chosen = engine.getChosenInput();
        const int si = ins.indexOf (chosen);
        if (si >= 0)
            inBox.setSelectedId (si + 1, juce::dontSendNotification);
        else if (chosen.isNotEmpty())
        {
            inBox.addItem (chosen + tr (" (bağlı değil)"), ins.size() + 1);
            inBox.setSelectedId (ins.size() + 1, juce::dontSendNotification);
        }
    }

    if (auto* type = outDm.getCurrentDeviceTypeObject())
    {
        const auto& outName = engine.getChosenOutput();
        outBox.addItem (tr ("(yok)"), 1);   // leerer Output-Name = kein Host-Output
        const auto outs = type->getDeviceNames (false);
        for (int i = 0; i < outs.size(); ++i) outBox.addItem (outs[i], i + 2);
        const int so = outs.indexOf (outName);
        if (outName.isNotEmpty() && so < 0)
        {
            outBox.addItem (outName + tr (" (bağlı değil)"), outs.size() + 2);
            outBox.setSelectedId (outs.size() + 2, juce::dontSendNotification);
        }
        else
            outBox.setSelectedId (outName.isNotEmpty() ? so + 2 : 1, juce::dontSendNotification);
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
    if (showBuf != bufShown)
    {
        bufShown = showBuf;
        if (! compact)
        {
            bufLabel.setVisible (showBuf); bufBox.setVisible (showBuf); bufInfo.setVisible (showBuf);
            resized();
            if (auto* p = getParentComponent()) p->resized();   // Layout nachziehen
        }
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
    syncPriority();
}

void DevicePanel::syncPriority()
{
    if (! priorityShown() || priorityBox.isPopupActive())
        return;

    // Shows what Windows reports right now - after MicVST applied the saved choice at start, and
    // also when the class was changed from outside (Task Manager): then possibly "another level".
    const int actual = rt::currentProcessPriority();
    const int id = juce::isPositiveAndBelow (actual, rt::numProcessPriorities) ? actual + 1 : 4;
    if (id == 4 && priorityBox.indexOfItemId (4) < 0)
        priorityBox.addItem (tr ("Başka bir seviye (dışarıdan değiştirildi)"), 4);
    if (priorityBox.getSelectedId() != id)
    {
        const juce::ScopedValueSetter<bool> guard (updating, true);
        priorityBox.setSelectedId (id, juce::dontSendNotification);
    }

    // A refused change (also the one at program start) is told once, when the window is visible.
    if (engine.getPriorityProblemCount() != shownPriorityProblems && isShowing())
    {
        shownPriorityProblems = engine.getPriorityProblemCount();
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("İşlem önceliği"),
                                                engine.getPriorityProblem(), {}, this);
    }
}

void DevicePanel::applyPriority()
{
    if (updating) return;
    const int choice = priorityBox.getSelectedId() - 1;
    if (! juce::isPositiveAndBelow (choice, rt::numProcessPriorities))
        return;   // "another level" is only shown, never chosen
    engine.setProcessPriority (choice);   // applies it, reads it back; a refusal is logged and kept
    engine.requestPersist();
    syncPriority();                       // shows the class the process really has (+ the message)
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
    const auto dropouts = d.bridge.underruns + d.inputSkips + d.inputQueueOverflows + (uint64_t) d.inputDeviceXruns
                          + d.invalidBlocks;
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
    statsLabel.setColour (juce::Label::textColourId, tooHeavy ? micvst::theme::error
                                                   : dropouts > 0 ? micvst::theme::warning : micvst::theme::textDim);

    if (compact)
    {
        // one line for the main window: state, latency, CPU, dropouts (the rest in the tooltip)
        juce::String line = engine.isRunning() ? tr ("Çalışıyor") : tr ("Durdu - cihaz bağlı değil");
        if (d.latencyMs > 0.0) line << "  ·  " << tr ("gecikme ") << micvst::trNumber (d.latencyMs, 1) << " ms";
        line << "  ·  " << tr ("İşlemci ") << micvst::trPercent (d.dspLoadPercent);
        if (tooHeavy) line << tr (" - zincir çok ağır");
        line << "  ·  " << tr ("kesinti ") << juce::String ((juce::int64) dropouts);
        statusLabel.setText (line, juce::dontSendNotification);
        statusLabel.setColour (juce::Label::textColourId, tooHeavy ? micvst::theme::error
                                                        : dropouts > 0 ? micvst::theme::warning
                                                        : engine.isRunning() ? micvst::theme::textDim : micvst::theme::warning);
    }

    juce::String tip;
    tip << tr ("Çıkışta ses kesildi: ") << (juce::int64) d.bridge.underruns << "\n"
        << tr ("Zincir yetişemedi, ses atlandı: ") << (juce::int64) d.inputSkips;
    if (d.inputSkips > 0)
        tip << " (" << juce::roundToInt (d.skippedMs) << " ms)";
    tip << "\n" << tr ("Mikrofon sürücüsünde kayıp: ") << d.inputDeviceXruns
        << "\n" << tr ("Eklenti işleme takıldı: ") << (juce::int64) d.inputQueueOverflows
        << "\n" << tr ("Eklentiden geçersiz ses (susturuldu): ") << (juce::int64) d.invalidBlocks
        << "\n" << tr ("Gecikme düzeltmesi: ") << (juce::int64) d.bridge.resyncs
        << "\n" << tr ("Cihaz yeniden açıldı: ") << (juce::int64) d.watchdogRestarts
        << "\n" << tr ("Geç kalan eklenti bloğu (kesinti olmadan telafi edildi): ") << (juce::int64) d.overloads
        << "\n" << tr ("Saat farkı mikrofon/kablo: ") << (d.bridge.driftPpm >= 0 ? "+" : "")
        << juce::roundToInt (d.bridge.driftPpm) << " ppm";
    statsLabel.setTooltip (tip);
    if (compact) statusLabel.setTooltip (st + "\n" + s2 + "\n\n" + tip);
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
    juce::String input = juce::isPositiveAndBelow (ii, ins.size()) ? ins[ii]
                       : ii == ins.size() ? engine.getChosenInput()     // chosen but unplugged
                       : juce::String();

    juce::String output;   // "(none)" (id 1) -> leer
    const int oid = outBox.getSelectedId();
    if (oid >= 2 && juce::isPositiveAndBelow (oid - 2, outs.size())) output = outs[oid - 2];
    else if (oid == outs.size() + 2) output = engine.getChosenOutput();  // chosen but unplugged

    // Buffer-Wunsch: id 1 = Auto (0). Bei Auto den Geräte-Default explizit setzen,
    // weil setDeviceConfig 0 als "unverändert" interpretiert (sonst kein Zurückstellen).
    auto* dev = engine.getInputDeviceManager().getCurrentAudioDevice();
    int buf = 0;
    if (bufShown && bufBox.getSelectedId() >= 2)
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
    if (compact)
    {
        // Mikrofon | Kablo side by side; each label inside its half, then the combo
        auto r = getLocalBounds();
        auto left = r.removeFromLeft ((r.getWidth() - 8) / 2);
        r.removeFromLeft (8);
        auto place = [] (juce::Rectangle<int> area, juce::Label& l, juce::ComboBox& box)
        {
            const int w = juce::jlimit (50, 110, juce::roundToInt (juce::GlyphArrangement::getStringWidth (l.getFont(), l.getText())) + 10);
            l.setBounds (area.removeFromLeft (w));
            box.setBounds (area);
        };
        place (left, inLabel, inBox);
        place (r, outLabel, outBox);
        return;
    }

    // labelW: Platz für das längste Label ("Güvenlik payı" auf Türkisch: 118) plus "?"-Icon; in
    // anderen Sprachen so breit wie das längste Label (höchstens 190), damit nichts gekürzt wird.
    constexpr int rowH = 30, gap = 6;
    int labelW = 118;
    for (auto* l : { &bufLabel, &safetyLabel, &priorityLabel, &monitorLabel })
        if (l->isVisible())
            labelW = juce::jmax (labelW, juce::roundToInt (juce::GlyphArrangement::getStringWidth (l->getFont(), l->getText()) + 0.5f) + 18 + 8);
    labelW = juce::jmin (labelW, 190);
    auto r = getLocalBounds();

    auto row = [&] (int h)
    {
        auto a = r.removeFromTop (h);
        r.removeFromTop (gap);
        return a;
    };
    auto labelWithInfo = [labelW] (juce::Rectangle<int> area, juce::Label& lbl, InfoIcon& info)
    {
        auto col = area.removeFromLeft (labelW);
        info.setBounds (col.removeFromRight (18).reduced (2));
        lbl.setBounds (col);
        return area;
    };

    if (bufBox.isVisible())
        bufBox.setBounds (labelWithInfo (row (rowH), bufLabel, bufInfo));
    safetyBox.setBounds (labelWithInfo (row (rowH), safetyLabel, safetyInfo));
    if (priorityBox.isVisible())
        priorityBox.setBounds (labelWithInfo (row (rowH), priorityLabel, priorityInfo));
    auto monitorRow = labelWithInfo (row (rowH), monitorLabel, monitorInfo);
    monitorVol.setBounds (monitorRow.removeFromRight (110));
    monitorRow.removeFromRight (6);
    monitorBox.setBounds (monitorRow);
}

int DevicePanel::preferredHeight() const
{
    if (compact) return 34;
    // Safety, Kulaklık (+ Buffer, + Priorität unter Windows) à 30, Lücken à 6
    return 2 * 36 + (bufBox.isVisible() ? 36 : 0) + (priorityBox.isVisible() ? 36 : 0) - 6;
}
