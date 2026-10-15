#include "ui/PluginListView.h"
#include "audio/PluginChain.h"
#include "ui/PluginPicker.h"
#include "Text.h"

using micvst::tr;

namespace
{
    // Anzeigename eines Ketten-Eintrags (VST3: Dateiname ohne Endung; interne Glieder: Funktion).
    juce::String displayName (const PluginChain::Entry& e)
    {
        if (e.fileOrId == PluginChain::monoToStereoId) return "Mono -> Stereo";
        if (e.fileOrId == PluginChain::stereoToMonoId) return "Stereo -> Mono";
        // Name des Plugins selbst (eine Datei kann viele enthalten), sonst der Dateiname
        return e.pluginName.isNotEmpty() ? e.pluginName : juce::File (e.fileOrId).getFileNameWithoutExtension();
    }
}


// ============================ TrashButton ============================

PluginListView::TrashButton::TrashButton() : juce::Button (tr ("Kaldır"))
{
    setTooltip (tr ("Eklentiyi zincirden kaldır"));
}

void PluginListView::TrashButton::paintButton (juce::Graphics& g, bool highlighted, bool /*down*/)
{
    auto b = getLocalBounds().toFloat();
    const auto col = highlighted ? juce::Colours::orangered : juce::Colours::lightgrey;
    g.setColour (col);

    const float cx = b.getCentreX();
    const float iconH = 15.0f;
    const float top = b.getCentreY() - iconH * 0.5f;
    const float lidY = top + 3.0f;
    const float botY = top + iconH;

    // Deckel-Linie + Griff oben.
    g.drawLine (cx - 7.0f, lidY, cx + 7.0f, lidY, 1.3f);
    g.drawLine (cx - 2.5f, lidY, cx - 2.5f, top + 1.0f, 1.3f);
    g.drawLine (cx - 2.5f, top + 1.0f, cx + 2.5f, top + 1.0f, 1.3f);
    g.drawLine (cx + 2.5f, top + 1.0f, cx + 2.5f, lidY, 1.3f);

    // Korpus (leicht trapezförmig).
    g.drawLine (cx - 5.5f, lidY, cx - 4.5f, botY, 1.3f);
    g.drawLine (cx + 5.5f, lidY, cx + 4.5f, botY, 1.3f);
    g.drawLine (cx - 4.5f, botY, cx + 4.5f, botY, 1.3f);

    // Innere Striche.
    g.drawLine (cx - 2.0f, lidY + 2.5f, cx - 2.0f, botY - 1.5f, 1.0f);
    g.drawLine (cx + 2.0f, lidY + 2.5f, cx + 2.0f, botY - 1.5f, 1.0f);
}

// ============================ Row ============================

PluginListView::Row::Row (PluginListView& ownerIn, int idx) : owner (ownerIn), index (idx)
{
    const auto& es = owner.engine.getChain().entries();
    const bool bypassed = juce::isPositiveAndBelow (index, (int) es.size())
                          && es[(size_t) index].bypassed;

    bypassBtn.setButtonText (tr ("Devre dışı"));
    bypassBtn.setTooltip (tr ("Eklentiyi geçici olarak devre dışı bırak (ses onu atlar)"));
    bypassBtn.setClickingTogglesState (true);
    bypassBtn.setToggleState (bypassed, juce::dontSendNotification);
    bypassBtn.setColour (juce::TextButton::buttonOnColourId, juce::Colours::darkorange);
    bypassBtn.onClick = [this] { owner.toggleBypass (index); };
    addAndMakeVisible (bypassBtn);

    trashBtn.onClick = [this] { owner.requestRemove (index); };
    addAndMakeVisible (trashBtn);
}

bool PluginListView::Row::overHandle (juce::Point<int> p) const { return p.x < 24; }

void PluginListView::Row::resized()
{
    auto r = getLocalBounds().reduced (2);
    trashBtn.setBounds (r.removeFromRight (28));
    r.removeFromRight (4);
    bypassBtn.setBounds (r.removeFromRight (80));
}

void PluginListView::Row::paint (juce::Graphics& g)
{
    auto b = getLocalBounds();
    const auto& es = owner.engine.getChain().entries();
    if (! juce::isPositiveAndBelow (index, (int) es.size())) return;
    const auto& e = es[(size_t) index];

    g.setColour (dragging ? juce::Colours::darkblue
                          : (index % 2 ? juce::Colour (0xff2a2a2a) : juce::Colour (0xff242424)));
    g.fillRect (b);

    // Drag-Handle: zwei Spalten à drei Punkte.
    g.setColour (juce::Colours::grey);
    for (int cxn = 0; cxn < 2; ++cxn)
        for (int cyn = 0; cyn < 3; ++cyn)
            g.fillEllipse ((float) (8 + cxn * 6), (float) (b.getCentreY() - 6 + cyn * 6), 2.6f, 2.6f);

    // Latenz-Spalte links vom Bypass: gemeldete Plugin-Latenz in ms (Lookahead etc.).
    constexpr int latW = 72;
    const int latRight = bypassBtn.getX() - 8;
    const int latLeft  = latRight - latW;

    juce::String latText;
    if (auto* node = owner.engine.getGraph().getNodeForId (e.node))
        if (auto* proc = node->getProcessor())
        {
            const double sr = owner.engine.getProcessingSampleRate();   // Kette läuft mit der Mikrofon-Rate
            const double ms = sr > 0.0 ? (double) proc->getLatencySamples() / sr * 1000.0 : 0.0;
            latText = micvst::trNumber (ms, 1) + " ms";
        }

    // Name (auf den Platz links der Latenz-Spalte begrenzt).
    const auto name = displayName (e);
    g.setColour (e.bypassed ? juce::Colours::grey : juce::Colours::white);
    g.drawText (name, 28, 0, latLeft - 6 - 28, b.getHeight(), juce::Justification::centredLeft);

    g.setColour (juce::Colours::grey);
    g.setFont (juce::Font (juce::FontOptions (12.0f)));
    g.drawText (latText, latLeft, 0, latW, b.getHeight(), juce::Justification::centredRight);
}

void PluginListView::Row::mouseDown (const juce::MouseEvent& e)
{
    if (overHandle (e.getPosition()))
    {
        dragging = true;
        grabOffsetY = e.getPosition().y;
        toFront (false);
    }
}

void PluginListView::Row::mouseDrag (const juce::MouseEvent& e)
{
    if (! dragging) return;
    const int n = owner.rows.size();
    const int maxY = juce::jmax (0, (n - 1) * rowH);
    const int newY = juce::jlimit (0, maxY, e.getEventRelativeTo (getParentComponent()).y - grabOffsetY);
    setTopLeftPosition (getX(), newY);
}

void PluginListView::Row::mouseUp (const juce::MouseEvent&)
{
    if (! dragging) return;
    dragging = false;
    const int n = owner.rows.size();
    const int target = juce::jlimit (0, n - 1, (getY() + rowH / 2) / rowH);
    owner.requestMove (index, target);   // rebuildRows() snappt alles zurück an Position
}

void PluginListView::Row::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (! overHandle (e.getPosition()))
        owner.openEditor (index);
}

// ============================ PluginListView ============================

PluginListView::PluginListView (AudioEngine& e) : engine (e)
{
    addAndMakeVisible (addBtn);
    addBtn.onClick = [this] { showPluginPicker(); };

    addAndMakeVisible (manageFoldersBtn);
    manageFoldersBtn.onClick = [this] { showFolderMenu(); };

    viewport.setViewedComponent (&rowsHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    scanLabel.setFont (juce::Font (juce::FontOptions (13.0f)));
    scanLabel.setColour (juce::Label::textColourId, juce::Colours::lightgrey);
    addChildComponent (scanLabel);       // nur sichtbar während des Scans
    addChildComponent (scanBar);
    addChildComponent (skipScanBtn);
    addBtn.setButtonText (tr ("+ Eklenti"));
    manageFoldersBtn.setButtonText (tr ("VST3 klasörleri"));
    skipScanBtn.setButtonText (tr ("Atla"));
    skipScanBtn.setTooltip (tr ("Şu an taranan eklentiyi atla"));
    skipScanBtn.onClick = [this] { engine.skipCurrentScanFile(); };
    skipLabel.setFont (juce::Font (juce::FontOptions (12.0f)));
    skipLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    addChildComponent (skipLabel);

    engine.onScanProgress = [this] (int cur, int total, juce::String name)
    {
        scanProgress = total > 0 ? (double) (cur - 1) / (double) total : 0.0;
        scanLabel.setText (tr ("Eklentiler taranıyor... ") + juce::String (cur) + "/" + juce::String (total)
                           + " - " + name, juce::dontSendNotification);
        scanBar.setTextToDisplay (micvst::trPercent (100.0 * scanProgress));
        updateScanUi();
    };
    engine.onScanFinished = [this]
    {
        rebuildRows();   // Kette kann per deferred Restore gerade erst entstanden sein
        updateScanUi();
    };
    updateScanUi();

    rebuildRows();
    startTimer (500);   // Plugin-Latenz live halten (z. B. wenn ein Plugin Lookahead an-/abschaltet)
}

// Engine-Callbacks lösen: eine zerstörte View darf nie mehr zurückgerufen werden.
PluginListView::~PluginListView()
{
    engine.onScanProgress = nullptr;
    engine.onScanFinished = nullptr;
}

void PluginListView::timerCallback()
{
    for (auto* r : rows)
        r->repaint();   // nur neu zeichnen; die Latenz wird in Row::paint frisch gelesen
}

void PluginListView::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (30);
    constexpr int btnH = 26;   // einheitliche Button-Höhe (wie Row-/DevicePanel-Buttons)
    addBtn.setBounds (top.removeFromLeft (110).withSizeKeepingCentre (106, btnH));
    manageFoldersBtn.setBounds (top.removeFromRight (200).withSizeKeepingCentre (196, btnH));   // rechtsbündig
    r.removeFromTop (4);

    if (scanLabel.isVisible())
    {
        auto row = r.removeFromTop (22);
        skipScanBtn.setBounds (row.removeFromRight (48).reduced (0, 1));
        row.removeFromRight (4);
        scanBar.setBounds (row.removeFromRight (120).reduced (0, 4));
        scanLabel.setBounds (row);
        r.removeFromTop (2);
    }
    else if (skipLabel.isVisible())
    {
        skipLabel.setBounds (r.removeFromTop (20));
        r.removeFromTop (2);
    }

    viewport.setBounds (r);

    const int vh = viewport.getHeight();
    const int contentH = rows.size() * rowH;
    // Scrollbar-Breite reservieren, wenn die Kette überläuft -> der Mülleimer am
    // rechten Rand verschwindet nie hinter dem Scrollbalken.
    const int w = viewport.getWidth() - (contentH > vh ? viewport.getScrollBarThickness() : 0);
    rowsHolder.setSize (w, juce::jmax (vh, contentH));
    for (int i = 0; i < rows.size(); ++i)
        rows[i]->setBounds (0, i * rowH, w, rowH);
}

void PluginListView::rebuildRows()
{
    rows.clear();
    const int n = (int) engine.getChain().entries().size();
    for (int i = 0; i < n; ++i)
    {
        auto* row = new Row (*this, i);
        rowsHolder.addAndMakeVisible (row);
        rows.add (row);
    }
    resized();
}

void PluginListView::commitChange()
{
    // Läuft NACH dem ChainEdit-Block (Graph ist schon umgebaut): erst dann speichern - das
    // Serialisieren aller Plugin-Zustände soll die Stummschaltung nicht verlängern.
    engine.requestPersist();   // App persistiert den Gesamtzustand (nicht nur captureState)
    rebuildRows();
}

void PluginListView::requestRemove (int index)
{
    const auto& es = engine.getChain().entries();
    if (! juce::isPositiveAndBelow (index, (int) es.size())) return;
    const auto& e = es[(size_t) index];
    const auto name = displayName (e);

    // Sicherheitsabfrage. Der Dialog ist asynchron -> die auslösende Row bleibt am Leben,
    // bis OK gedrückt wird (das eigentliche Entfernen passiert erst im Callback).
    const auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
        juce::MessageBoxIconType::QuestionIcon,
        tr ("Eklentiyi kaldır"),
        "\"" + name + tr ("\" zincirden kaldırılsın mı?"),
        tr ("Kaldır"), tr ("İptal"));
    juce::NativeMessageBox::showAsync (options, [this, index] (int result)
        {
            if (result != 0) return;   // 0 = erster Button ("Kaldır"), 1 = İptal
            if (! juce::isPositiveAndBelow (index, (int) engine.getChain().entries().size())) return;
            editorWindows.clear();     // offene Editoren schließen (sonst Dangling -> Crash)
            {
                const AudioEngine::ChainEdit edit (engine);   // ausblenden - umbauen - einblenden
                engine.getChain().removePlugin (index);
                engine.rebuildGraph();
            }
            commitChange();
        });
}

void PluginListView::requestMove (int from, int to)
{
    juce::MessageManager::callAsync ([this, from, to]
    {
        const int n = (int) engine.getChain().entries().size();
        if (from == to || ! juce::isPositiveAndBelow (from, n)) { rebuildRows(); return; }
        {
            const AudioEngine::ChainEdit edit (engine);
            engine.getChain().movePlugin (from, juce::jlimit (0, n - 1, to));
            engine.rebuildGraph();
        }
        commitChange();
    });
}

void PluginListView::toggleBypass (int index)
{
    juce::MessageManager::callAsync ([this, index]
    {
        const auto& es = engine.getChain().entries();
        if (! juce::isPositiveAndBelow (index, (int) es.size())) return;
        {
            const AudioEngine::ChainEdit edit (engine);
            engine.getChain().setBypass (index, ! es[(size_t) index].bypassed);
            engine.rebuildGraph();
        }
        commitChange();
    });
}

void PluginListView::showPluginPicker()
{
    // Built-ins als Pseudo-Descriptions: laufen so durch dieselbe Suche/Gruppierung.
    juce::Array<juce::PluginDescription> entries;
    juce::PluginDescription m2s;
    m2s.name = "Mono -> Stereo"; m2s.manufacturerName = tr ("Dahili");
    m2s.fileOrIdentifier = PluginChain::monoToStereoId;
    entries.add (m2s);
    juce::PluginDescription s2m;
    s2m.name = "Stereo -> Mono"; s2m.manufacturerName = tr ("Dahili");
    s2m.fileOrIdentifier = PluginChain::stereoToMonoId;
    entries.add (s2m);
    entries.addArray (engine.getKnownPlugins().getTypes());

    // SafePointer: die CallOutBox lebt auf dem Desktop und kann die View überleben.
    juce::Component::SafePointer<PluginListView> safe (this);
    auto picker = std::make_unique<PluginPickerComponent> (entries,
        [safe] (const juce::PluginDescription& d)
        {
            if (auto* self = safe.getComponent()) self->addFromPicker (d);
        });
    juce::CallOutBox::launchAsynchronously (std::move (picker), addBtn.getScreenBounds(), nullptr);
}

void PluginListView::addFromPicker (const juce::PluginDescription& desc)
{
    if (desc.fileOrIdentifier == PluginChain::monoToStereoId
         || desc.fileOrIdentifier == PluginChain::stereoToMonoId)
    {
        {
            const AudioEngine::ChainEdit edit (engine);
            if (desc.fileOrIdentifier == PluginChain::monoToStereoId) engine.getChain().addMonoToStereo();
            else                                                      engine.getChain().addStereoToMono();
            engine.rebuildGraph();
        }
        commitChange();
        return;
    }

    // Laden (kann dauern) bei laufendem Ton: der neue Knoten hängt noch an keiner Verbindung,
    // ist also unhörbar. Nur das Einhängen in die Kette passiert stummgeschaltet.
    juce::String err;
    if (engine.getChain().addPlugin (engine.getFormatManager(), desc, engine.getProcessingSampleRate(),
                                     engine.getProcessingBlockSize(), err))
    {
        {
            const AudioEngine::ChainEdit edit (engine);
            engine.rebuildGraph();
        }
        commitChange();
    }
    else
    {
        juce::Logger::writeToLog (tr ("Eklenti yüklenemedi: ") + desc.name + " - " + err);
        juce::NativeMessageBox::showAsync (juce::MessageBoxOptions::makeOptionsOk (
                                               juce::MessageBoxIconType::WarningIcon,
                                               tr ("Eklenti yüklenemedi"),
                                               "\"" + desc.name + tr ("\" yüklenemedi. Ayrıntılar günlük dosyasında "
                                                                      "(%APPDATA%\\MicVST\\log.txt)."),
                                               tr ("Tamam")),
                                           [] (int) {});
    }
}

void PluginListView::showFolderMenu()
{
    juce::PopupMenu m;
    m.addItem (1, tr ("Klasör ekle..."));
    m.addSeparator();

    const auto& folders = engine.getPluginFolders();
    if (folders.isEmpty())
    {
        m.addItem (2, tr ("(ek klasör yok)"), false /*enabled*/, false);
    }
    else
    {
        // Pro Ordner ein Untermenü mit "Remove". IDs ab 1000 = Index in der Ordnerliste.
        for (int i = 0; i < folders.size(); ++i)
        {
            juce::PopupMenu sub;
            sub.addItem (1000 + i, tr ("Kaldır"));
            m.addSubMenu (folders[i], sub);
        }
    }

    // Rescan auch hier anbieten (nicht nur im "+ Plugin"-Menü): auch ohne Custom-Ordner
    // muss man neu scannen können. Während eines laufenden Scans deaktiviert, weil
    // rescanAllPlugins dann bewusst nichts tut.
    constexpr int rescanItemId = 3;
    m.addSeparator();
    m.addItem (rescanItemId, tr ("Tüm eklentileri yeniden tara"), ! engine.isScanning());

    constexpr int retryItemId = 4;
    m.addItem (retryItemId, tr ("Atlanan eklentileri yeniden dene"),
               ! engine.isScanning() && ! engine.getSkippedPlugins().isEmpty());

    constexpr int resetItemId = 5;
    m.addSeparator();
    // Reset ist nur außerhalb eines Scans erlaubt, um zu verhindern, dass eine wartende
    // scan-finished-Message die gerade gelöschte plugin_cache.xml neu anlegt. Das ITEM wird
    // hier zur Menü-Bauzeit deaktiviert, wenn ein Scan läuft -> ist es klickbar, kann keine
    // scan-finished-Message mehr ausstehen. Startet der User über den (nicht-modalen)
    // Bestätigungsdialog dennoch einen Scan (zweite Menü-Runde), ist das unschädlich: Quit
    // zerstört den ScanCoordinator, bevor je wieder etwas den Cache speichert.
    m.addItem (resetItemId, tr ("Programı sıfırla (tüm veriler silinir)..."), ! engine.isScanning());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&manageFoldersBtn),
        [this, rescanItemId, retryItemId, resetItemId] (int res)
        {
            if (res == 1) { chooseFolder(); return; }
            if (res == rescanItemId) { engine.rescanAllPlugins(); updateScanUi(); return; }
            if (res == retryItemId) { engine.retrySkippedPlugins(); updateScanUi(); return; }
            if (res == resetItemId)
            {
                const auto options = juce::MessageBoxOptions::makeOptionsOkCancel (
                    juce::MessageBoxIconType::WarningIcon,
                    tr ("MicVST'yi sıfırla"),
                    tr ("Tüm ayarlar, eklenti listesi ve eklenti zinciri silinir, ardından MicVST "
                        "kapanır. Yeniden açtığınızda ilk kurulum yapılır. Devam edilsin mi?"),
                    tr ("Sıfırla"), tr ("İptal"));
                juce::NativeMessageBox::showAsync (options, [this] (int result)
                    {
                        if (result == 0) engine.requestFactoryReset();   // 0 = "Sıfırla", 1 = İptal
                    });
                return;
            }
            if (res >= 1000)
            {
                const auto& f = engine.getPluginFolders();
                const int idx = res - 1000;
                if (juce::isPositiveAndBelow (idx, f.size()))
                {
                    engine.removePluginFolder (f[idx]);
                    engine.requestPersist();
                }
            }
        });
}

void PluginListView::chooseFolder()
{
    auto chooser = std::make_shared<juce::FileChooser> (tr ("VST3 klasörü seçin"));
    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto dir = fc.getResult();
            if (! dir.isDirectory()) return;
            engine.addPluginFolder (dir.getFullPathName());   // hinzufügen + rescan
            engine.requestPersist();                          // Ordner persistieren
        });
}

void PluginListView::openEditor (int row)
{
    const auto& es = engine.getChain().entries();
    if (! juce::isPositiveAndBelow (row, (int) es.size())) return;
    auto* node = engine.getGraph().getNodeForId (es[(size_t) row].node);
    if (node == nullptr) return;
    auto* proc = node->getProcessor();
    if (proc == nullptr || ! proc->hasEditor()) return;

    auto* w = new EditorWindow (proc->getName(),
                                [this] (EditorWindow* self)
                                {
                                    editorWindows.removeObject (self);
                                    // Parameter-Änderungen im Editor sofort sichern (1.1.1: erst beim
                                    // Verstecken/Beenden -> nach Absturz/Stromausfall verloren).
                                    engine.requestPersist();
                                });
    w->setUsingNativeTitleBar (true);
    w->setContentOwned (proc->createEditorAndMakeActive(), true);
    w->centreWithSize (w->getWidth(), w->getHeight());
    w->setVisible (true);
    editorWindows.add (w);
}

void PluginListView::updateScanUi()
{
    const bool scanning = engine.isScanning();
    addBtn.setEnabled (! scanning);
    scanLabel.setVisible (scanning);
    scanBar.setVisible (scanning);
    skipScanBtn.setVisible (scanning);

    const auto& skips = engine.getSkippedPlugins();
    skipLabel.setVisible (! scanning && ! skips.isEmpty());
    if (skipLabel.isVisible())
    {
        skipLabel.setText (juce::String (skips.size()) + tr (" eklenti atlandı - ayrıntılar için üzerine gelin"),
                           juce::dontSendNotification);
        juce::String tip;
        for (auto& s : skips)
            tip << juce::File (s.file).getFileNameWithoutExtension() << " (" << micvst::scanSkipReasonText (s.reason) << ")\n";
        skipLabel.setTooltip (tip.trimEnd());
    }
    resized();
}
