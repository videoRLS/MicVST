#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/AudioEngine.h"
#include "audio/RealtimeSupport.h"
#include "state/Persistence.h"
#include "state/AutostartRegistry.h"
#include "ui/MainComponent.h"
#include "ui/TrayIcon.h"
#include "ui/GlobalHotkeys.h"
#include "ui/SettingsComponent.h"
#include "ui/Theme.h"
#include "ui/DarkTitleBar.h"
#include "ui/RenderMode.h"
#include "Text.h"

using micvst::tr;

#if JUCE_WINDOWS
 // Bewusst kein <windows.h>: das Header pollutet nachfolgende JUCE-Header (Makrokonflikte
 // z.B. in juce_PushNotifications.h). Stattdessen nur die eine benötigte WinAPI-Funktion
 // per extern "C" deklarieren.
 extern "C" __declspec (dllimport) unsigned int __stdcall SetErrorMode (unsigned int);
 static constexpr unsigned int kSemFailCriticalErrors = 0x0001; // SEM_FAILCRITICALERRORS
 static constexpr unsigned int kSemNoGpFaultErrorBox  = 0x0002; // SEM_NOGPFAULTERRORBOX
 static constexpr unsigned int kSemNoOpenFileErrorBox  = 0x8000; // SEM_NOOPENFILEERRORBOX
 #include <excpt.h>   // SEH-Intrinsics (_exception_code) — bewusst ohne <windows.h>

// Enumeration mit SEH-Guard: Crasht eine Plugin-Klasse (WaveShell enthält hunderte),
// stirbt nicht mehr der ganze Prozess — die bis dahin gefundenen Typen bleiben
// nutzbar. Rückgabe 0 = sauber durchgelaufen, sonst der Exception-Code (NTSTATUS).
static unsigned int findTypesWithSehGuard (juce::VST3PluginFormat& format,
                                           juce::OwnedArray<juce::PluginDescription>& types,
                                           const juce::String& path)
{
    __try
    {
        format.findAllTypesForFile (types, path);
        return 0;
    }
    __except (1 /* EXCEPTION_EXECUTE_HANDLER */)
    {
        return (unsigned int) _exception_code();
    }
}
#endif

// %APPDATA%\MicVST\log.txt: Türkischer Kopf, Uhrzeit vor jeder Zeile (Ereignisse lassen sich so
// zeitlich zuordnen), jede Zeile sofort geschrieben (übersteht einen Absturz), beim Start auf
// die neuesten 256 KB gekürzt - und auch im laufenden Betrieb (Wochen im Tray), sobald die Datei
// 2 MB erreicht. Ersetzt juce::FileLogger (englischer Kopf, keine Uhrzeiten).
class MicVSTLogger final : public juce::Logger
{
public:
    explicit MicVSTLogger (const juce::File& f) : file (f)
    {
        f.getParentDirectory().createDirectory();
        juce::FileLogger::trimFileSize (f, keepBytes);
        stream = std::make_unique<juce::FileOutputStream> (f);   // hängt ans Dateiende an
        if (stream->failedToOpen())
            stream.reset();
        write ("\n**********************************************************\n"
               + tr ("MicVST günlüğü - başlangıç: ") + juce::Time::getCurrentTime().formatted ("%d.%m.%Y %H:%M:%S") + "\n");
    }

    void logMessage (const juce::String& message) override
    {
        // Der Benutzerordner (enthält den Windows-Benutzernamen) steht im Log nur als
        // %USERPROFILE% - das Log wird oft zur Fehlersuche weitergegeben.
        write ("[" + juce::Time::getCurrentTime().formatted ("%H:%M:%S") + "] "
               + (userHome.length() > 3 ? message.replace (userHome, "%USERPROFILE%", true) : message) + "\n");
    }

private:
    void write (const juce::String& text)
    {
        const juce::ScopedLock sl (lock);
        if (stream == nullptr)
            return;
        stream->writeText (text, false, false, "\r\n");   // UTF-8, Windows-Zeilenenden
        stream->flush();
        if (stream->getPosition() > maxBytes)   // langer Betrieb: wieder auf die neuesten 256 KB kürzen
        {
            stream.reset();
            juce::FileLogger::trimFileSize (file, keepBytes);
            stream = std::make_unique<juce::FileOutputStream> (file);
            if (stream->failedToOpen())
                stream.reset();
        }
    }

    static constexpr juce::int64 keepBytes = 256 * 1024, maxBytes = 2 * 1024 * 1024;
    const juce::File file;
    std::unique_ptr<juce::FileOutputStream> stream;
    juce::CriticalSection lock;
    const juce::String userHome { juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName() };
};

class MicVSTApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "MicVST"; }
    const juce::String getApplicationVersion() override { return MICVST_VERSION_STRING; }   // aus CMake project(VERSION)

    // Kind-Scanprozesse (--scan) laufen parallel zur Haupt-Instanz und dürfen nicht
    // von der Single-Instance-Logik weggefangen werden.
    bool moreThanOneInstanceAllowed() override
    {
        return juce::JUCEApplicationBase::getCommandLineParameterArray().contains ("--scan");
    }

    // Kindmodus: genau EIN VST3 scannen und die Beschreibungen als XML in die --out-Datei
    // schreiben. Läuft in einem eigenen Prozess -> Hänger/Crashes können die App nie blockieren.
    // Rückgabe = Exit-Code (0 nur bei mindestens einem gefundenen Plugin-Typ).
    static int runScanChildMode()
    {
       #if JUCE_WINDOWS
        // Windows-Loader-Fehlerdialoge ("Ungültiges Bild", 0xC0000020) und Crash-Dialoge unterdrücken:
        // eine kaputte Plugin-DLL soll still als Fehler enden, nicht als Modal-Dialog.
        SetErrorMode (kSemFailCriticalErrors | kSemNoGpFaultErrorBox | kSemNoOpenFileErrorBox);
       #endif
        // Scans laufen parallel zum Audio: Priorität "unter normal", damit ein schwerer
        // Plugin-Scan nicht mit Spiel, OBS oder MicVST selbst um CPU konkurriert.
        rt::configureBackgroundProcess();

        auto args = juce::JUCEApplicationBase::getCommandLineParameterArray();
        juce::String pluginPath, outPath;
        for (int i = 0; i < args.size() - 1; ++i)
        {
            if (args[i] == "--scan") pluginPath = args[i + 1].unquoted();
            if (args[i] == "--out")  outPath    = args[i + 1].unquoted();
        }
        if (pluginPath.isEmpty() || outPath.isEmpty()) return 2;

        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> types;
       #if JUCE_WINDOWS
        const unsigned int crashCode = findTypesWithSehGuard (format, types, pluginPath);
       #else
        const unsigned int crashCode = 0;
        format.findAllTypesForFile (types, pluginPath);
       #endif
        if (crashCode == 0 && types.isEmpty()) return 1;

        juce::XmlElement root ("MicVSTScanResult");
        if (crashCode != 0)
            root.setAttribute ("crashCode",
                               "0x" + juce::String::toHexString ((int) crashCode).toUpperCase());
        for (auto* t : types)
            root.addChildElement (t->createXml().release());
        if (! root.writeTo (juce::File (outPath))) return 1;
        return crashCode != 0 ? 3 : 0;
    }

    void initialise (const juce::String& commandLine) override
    {
        if (juce::JUCEApplicationBase::getCommandLineParameterArray().contains ("--scan"))
        {
            setApplicationReturnValue (runScanChildMode());
            quit();
            return;
        }

        // Sprache (Standard: Türkçe) - auch für JUCEs eigene Beschriftungen (Dialog-Buttons,
        // Kontextmenü des Suchfelds) - vor dem Aufbau jeder Oberfläche.
        micvst::setLanguage (loadState().language);
        // Farbthema (Standard: Gece - dunkel, blendfrei) für alle Fenster, Menüs, Tooltips und
        // Dialoge; die Wahl steht in config.xml (gelesen, bevor irgendeine Oberfläche entsteht).
        micvst::theme::applyTheme (loadState().theme);
        lookAndFeel = std::make_unique<micvst::theme::NightLookAndFeel>();
        juce::LookAndFeel::setDefaultLookAndFeel (lookAndFeel.get());

        auto logFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                          .getChildFile ("MicVST").getChildFile ("log.txt");
        logger = std::make_unique<MicVSTLogger> (logFile);
        juce::Logger::setCurrentLogger (logger.get());
        juce::Logger::writeToLog ("MicVST " + getApplicationVersion() + " (" + juce::SystemStats::getJUCEVersion()
                                  + tr (", internet erişimi yok)"));

        // Echtzeit-Prozess: Windows darf den (oft nur im Tray laufenden) Audio-Prozess nicht als
        // Hintergrundarbeit drosseln (EcoQoS / Timer-Drosselung). Die Prioritätsklasse wählt der
        // Nutzer (Normal / Normalin Üstünde / Yüksek, Standard Normal); applyState() setzt sie unten.
        const auto rtSetup = rt::configureRealtimeProcess();
        juce::Logger::writeToLog (tr ("İşlem: güç kısıtlaması kapalı ") + (rtSetup.executionThrottlingOff ? tr ("tamam") : tr ("desteklenmiyor"))
                                  + tr (", zamanlayıcı kısıtlaması kapalı ") + (rtSetup.timerThrottlingOff ? tr ("tamam") : tr ("desteklenmiyor")));
        // Arayüz çizimi (Otomatik / Yalnızca ekran kartı / Yalnızca işlemci): decided once per start,
        // logged, fixed until MicVST closes
        micvst::render::startSession (micvst::render::fromId (loadState().uiRenderer));
        micvst::render::setFps (loadState().uiFps.getIntValue());
        micvst::render::setMetersVisible (loadState().hideMeters != "1");

        const bool silent = commandLine.contains ("--tray");

        engine = std::make_unique<AudioEngine>();

        // Gespeicherten Zustand laden; NUR beim allerersten Start (noch keine config.xml)
        // Devices vorbelegen. Sobald eine Config existiert, gilt sie verbatim — sonst würde
        // eine bewusst auf "none" gesetzte Output-Auswahl beim Neustart wieder überschrieben.
        const bool firstRun = ! configFile().existsAsFile();
        MicVSTState state = loadState();

        // Custom-VST3-Ordner übernehmen, dann Cache laden (vor applyState und vor der UI).
        engine->setPluginFolders (state.pluginFolders);
        engine->loadPluginCache();   // KEIN synchroner Scan mehr — Cache reicht zum Start
        if (firstRun)
        {
            // Output bevorzugt ein virtuelles Kabel (VB-Cable & Co): die App liefert dann
            // direkt an ein virtuelles Mikro, ohne zusätzliches Ausgabe-Plugin. Kein Kabel
            // gefunden -> Output bleibt leer ("none"), die UI weist auf den Download hin.
            state.outputDevice = engine->detectCableOutput();
            juce::Logger::writeToLog (state.outputDevice.isEmpty()
                ? tr ("Sanal kablo bulunamadı -> çıkış 'yok'")
                : tr ("Çıkış (kablo) = ") + state.outputDevice);

            // Input: System-Standardmikrofon, sonst erstes verfügbares Eingangsgerät.
            auto& dm = engine->getInputDeviceManager();
            dm.setCurrentAudioDeviceType (dm.preferredTypeName(), true);
            if (auto* type = dm.getCurrentDeviceTypeObject())
            {
                type->scanForDevices();
                auto ins = type->getDeviceNames (true);
                const int def = type->getDefaultDeviceIndex (true);
                state.inputDevice = juce::isPositiveAndBelow (def, ins.size()) ? ins[def]
                                  : (ins.isEmpty() ? juce::String() : ins[0]);
            }
            juce::Logger::writeToLog (state.inputDevice.isEmpty()
                ? tr ("Mikrofon bulunamadı -> giriş 'yok'")
                : tr ("Giriş (mikrofon) = ") + state.inputDevice);
        }
        engine->applyState (state);

        // Shortcut keys (system-wide): from config.xml, otherwise the defaults (Sustur = F9)
        hotkeys = std::make_unique<GlobalHotkeys>();
        hotkeys->set (GlobalHotkeys::mute, GlobalHotkeys::decode (state.hotkeyMute, GlobalHotkeys::mute));
        hotkeys->set (GlobalHotkeys::rawCompare, GlobalHotkeys::decode (state.hotkeyCompare, GlobalHotkeys::rawCompare));
        hotkeys->onPressed = [this] (GlobalHotkeys::Action a)
        {
            if (engine == nullptr) return;
            if (a == GlobalHotkeys::mute) engine->setMuted (! engine->isMuted());
            else                          engine->setRawCompare (! engine->isRawCompare());
        };

        mainWindow = std::make_unique<MainWindow> ("MicVST", *engine, state.windowState, hotkeys.get(),
            [this] (GlobalHotkeys::Action a, const GlobalHotkeys::Binding& b)
            {
                const bool ok = hotkeys->set (a, b);
                persistState();
                return ok;
            });
        mainWindow->onThemeApplied = [this]
        {
            if (lookAndFeel != nullptr) lookAndFeel->refresh();
            persistState();
        };
        mainWindow->setVisible (! silent);   // Autostart (--tray): unsichtbar, nur Tray

        tray = std::make_unique<TrayIcon>();
        tray->onToggleWindow = [this] { toggleWindow(); };
        tray->onQuit         = [this] { systemRequestedQuit(); };
        tray->isAutostartOn  = [] { return AutostartRegistry::isEnabled(); };
        tray->setAutostart   = [] (bool on) { AutostartRegistry::setEnabled (on); };
        tray->isMuted        = [this] { return engine != nullptr && engine->isMuted(); };
        tray->toggleMute     = [this] { if (engine != nullptr) engine->setMuted (! engine->isMuted()); };
        tray->isCueOn        = [this] { return engine != nullptr && engine->getCues().getSettings().enabled; };
        tray->toggleCue      = [this]
        {
            if (engine == nullptr) return;
            auto s = engine->getCues().getSettings();
            s.enabled = ! s.enabled;
            engine->getCues().setSettings (s);
            engine->requestPersist();
        };
        tray->isRecording    = [this] { return engine != nullptr && engine->isRecording(); };
        tray->toggleRecording = [this]
        {
            if (auto* c = mainWindow != nullptr ? mainWindow->getContent() : nullptr)
            {
                const bool was = engine->isRecording();
                c->toggleRecording();
                if (was && ! mainWindow->isVisible())   // the window's own note is not seen: the tray's
                    tray->showInfoBubble (tr ("Kayıt"), tr ("Kayıt bitti ve kaydedildi."));
            }
        };
        tray->addPresetMenus = [this] (juce::PopupMenu& m, int offset)
        {
            if (auto* c = mainWindow != nullptr ? mainWindow->getContent() : nullptr)
                c->getPluginList().addPresetMenus (m, offset);
        };
        tray->loadPreset     = [this] (int id)
        {
            if (auto* c = mainWindow != nullptr ? mainWindow->getContent() : nullptr)
                c->getPluginList().loadPresetById (id);
        };

        // Beim Verstecken: einmaligen Tray-Hinweis zeigen und die aktuelle Fenstergröße sichern
        // (X beendet nicht, daher hier persistieren, damit eine geänderte Größe nicht verloren geht).
        mainWindow->onHide = [this] { maybeShowTrayHint(); persistState(); };

        // Erst JETZT (nach applyState) das Persistieren bei Geräte-Änderungen aktivieren,
        // damit In-/Output-Auswahl gemerkt wird — auch ohne sauberes Beenden (X versteckt nur).
        engine->onDeviceChanged = [this] { persistState(); };
        // UI-Änderungen (Plugin-Kette, Ordner) persistieren ebenfalls den Gesamtzustand —
        // ein direktes saveState(captureState()) in der UI würde windowState resetten.
        engine->onStateChanged  = [this] { persistState(); };

        engine->onFactoryResetRequested = [this] { factoryReset(); };

        // Altes Dead-Man's-Pedal aufräumen (ersetzt durch Skip-Liste im plugin_cache.xml).
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("MicVST").getChildFile ("plugin_scan.tmp").deleteFile();

        // Fenster + Tray stehen -> jetzt (und erst jetzt) im Hintergrund scannen.
        engine->startBackgroundScan();
    }

    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr) { mainWindow->setVisible (true); mainWindow->toFront (true); }
    }

    void systemRequestedQuit() override
    {
        persistState();
        quit();
    }

    void shutdown() override
    {
        tray = nullptr;
        mainWindow = nullptr;
        hotkeys = nullptr;
        engine = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        lookAndFeel = nullptr;
        juce::Logger::setCurrentLogger (nullptr);
        logger = nullptr;
    }

private:
    // Engine-Zustand (Geräte/Plugins) + Fenstergröße/-position speichern.
    void persistState()
    {
        if (suppressPersist || engine == nullptr) return;
        auto s = engine->captureState();
        if (mainWindow != nullptr) s.windowState = mainWindow->getWindowStateAsString();
        s.theme = micvst::theme::currentId;
        s.language = micvst::currentLanguage();
        s.uiRenderer = micvst::render::toId (micvst::render::current());
        s.uiFps = micvst::render::savedFps() > 0 ? juce::String (micvst::render::savedFps()) : juce::String();
        s.hideMeters = micvst::render::metersVisible() ? juce::String() : juce::String ("1");
        if (hotkeys != nullptr)
        {
            s.hotkeyMute = GlobalHotkeys::encode (hotkeys->get (GlobalHotkeys::mute));
            s.hotkeyCompare = GlobalHotkeys::encode (hotkeys->get (GlobalHotkeys::rawCompare));
        }
        saveState (s);
    }

    // "Reset app (clear all data)": Einstellungen + Plugin-Cache + Tray-Hinweis-Marker
    // löschen und die App beenden. Kein Auto-Relaunch (Single-Instance-Logik würde die
    // neue Instanz blocken); der Dialog kündigt den manuellen Neustart an. Der
    // Autostart-Registry-Eintrag bleibt bewusst erhalten (sichtbare, eigene Einstellung).
    void factoryReset()
    {
        suppressPersist = true;
        configFile().deleteFile();
        AudioEngine::pluginCacheFile().deleteFile();
        juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
            .getChildFile ("MicVST").getChildFile ("tray_hint_shown").deleteFile();
        juce::Logger::writeToLog (tr ("Sıfırlama: tüm veriler silindi, program kapanıyor"));
        systemRequestedQuit();
    }

    void toggleWindow()
    {
        if (mainWindow == nullptr) return;
        const bool show = ! mainWindow->isVisible();
        mainWindow->setVisible (show);
        if (show) mainWindow->toFront (true);
        else      maybeShowTrayHint();
    }

    // Einmaliger Tray-Hinweis (Marker in %APPDATA%\MicVST), damit Nutzer nicht denken,
    // sie hätten die App beendet, wenn das Fenster verschwindet.
    void maybeShowTrayHint()
    {
        if (tray == nullptr) return;
        auto marker = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                        .getChildFile ("MicVST").getChildFile ("tray_hint_shown");
        if (marker.existsAsFile()) return;
        marker.create();
        tray->showInfoBubble ("MicVST", tr ("MicVST arka planda çalışmaya devam ediyor. Seçenekler için simgeye sağ tıklayın."));
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        using HotkeySetter = std::function<bool (GlobalHotkeys::Action, const GlobalHotkeys::Binding&)>;
        MainWindow (juce::String name, AudioEngine& engine, const juce::String& windowState,
                    GlobalHotkeys* keys, HotkeySetter setter)
            : DocumentWindow (name, micvst::theme::background, DocumentWindow::allButtons), engineRef (engine),
              hotkeys (keys), setHotkey (std::move (setter))
        {
            setUsingNativeTitleBar (true);
            // resize-to-fit AUS: die Fenstergröße diktiert centreWithSize unten, nicht die
            // Content-Größe. Sonst zieht der MainComponent (setSize im Ctor) die Startgröße
            // über das Minimum hinaus.
            createContent();
            setResizable (true, false);
            // Mindestgröße 600x540, Start 640x600; zusätzliche Höhe verlängert die Plugin-Liste.
            setResizeLimits (600, 540, 1600, 1400);
            // Letzte Größe/Position wiederherstellen (innerhalb der Resize-Limits), sonst zentriert
            // auf Startgröße. restoreWindowStateFromString respektiert die gesetzten Limits.
            if (windowState.isNotEmpty())
                restoreWindowStateFromString (windowState);
            else
                centreWithSize (640, 600);
        }
        std::function<void()> onHide;
        std::function<void()> onThemeApplied;   // the app: LookAndFeel refresh + save
        void closeButtonPressed() override { setVisible (false); if (onHide) onHide(); }
        void visibilityChanged() override
        {
            DocumentWindow::visibilityChanged();
            if (isVisible())
            {
                micvst::render::attach (*this);   // GPU / CPU drawing (Ayarlar > Arayüz çizimi)
                micvst::useDarkTitleBar (*this, micvst::theme::dark);   // native title bar like the theme
            }
        }
        MainComponent* getContent() const { return content; }
    private:
        void createContent()
        {
            content = new MainComponent (engineRef);
            content->setHotkeys (hotkeys);
            content->onShowSettings = [this] { showSettings(); };
            setContentOwned (content, false);
        }

        // "Ayarlar": its own window (kept by this window: it survives the content's rebuild and is
        // rebuilt with it when the theme or the language changes).
        struct SettingsWindow : juce::DocumentWindow
        {
            SettingsWindow() : juce::DocumentWindow (SettingsComponent::windowTitle(), micvst::theme::background,
                                                     juce::DocumentWindow::closeButton) {}
            void closeButtonPressed() override { setVisible (false); }
            void visibilityChanged() override { DocumentWindow::visibilityChanged(); if (isVisible()) micvst::render::attach (*this); }
        };
        std::unique_ptr<SettingsWindow> settingsWindow;

    public:
        // bounds / view: where and at which section a rebuilt settings window appears (rebuild()); they are
        // set before the window is shown - shown first at the default place and moved afterwards, it
        // flickered like a second window ("hayalet") when the theme or the language changed
        void showSettings (juce::Rectangle<int> bounds = {}, const SettingsComponent::ViewState* view = nullptr)
        {
            if (settingsWindow == nullptr)
            {
                settingsWindow = std::make_unique<SettingsWindow>();
                settingsWindow->setUsingNativeTitleBar (true);
                SettingsComponent::Callbacks cb;
                juce::Component::SafePointer<MainWindow> safe (this);
                // later, outside the combo box's own callback: both windows are rebuilt
                cb.chooseTheme = [safe] (const juce::String& id)
                    { juce::MessageManager::callAsync ([safe, id] { if (safe != nullptr) safe->applyTheme (id); }); };
                cb.chooseLanguage = [safe] (const juce::String& id)
                    { juce::MessageManager::callAsync ([safe, id] { if (safe != nullptr) safe->applyLanguage (id); }); };
                cb.showFolderMenu = [safe] (juce::Component* target)
                    { if (safe != nullptr && safe->content != nullptr) safe->content->getPluginList().showFolderMenu (target); };
                cb.showHowTo = [safe] { if (safe != nullptr && safe->content != nullptr) safe->content->showHowTo(); };
                cb.persist = [safe] { juce::MessageManager::callAsync ([safe] { if (safe != nullptr && safe->onThemeApplied) safe->onThemeApplied(); }); };
                cb.chooseRenderer = [safe] (const juce::String& id)
                    { juce::MessageManager::callAsync ([safe, id] { if (safe != nullptr) safe->applyRenderer (id); }); };
                cb.setHotkey = setHotkey;
                settingsWindow->setContentOwned (new SettingsComponent (engineRef, hotkeys, cb), true);
                settingsWindow->setResizable (true, false);
                settingsWindow->setResizeLimits (560, 420, 1200, 1400);
                if (bounds.isEmpty())
                    settingsWindow->centreAroundComponent (this, settingsWindow->getWidth(), settingsWindow->getHeight());
                else
                    settingsWindow->setBounds (bounds);
                if (view != nullptr)
                    if (auto* sc = dynamic_cast<SettingsComponent*> (settingsWindow->getContentComponent()))
                        sc->setViewState (*view);
            }
            micvst::useDarkTitleBar (*settingsWindow, micvst::theme::dark);   // before it shows: no light frame first
            settingsWindow->setVisible (true);
            settingsWindow->toFront (true);
        }
    private:
        void applyLanguage (const juce::String& id)
        {
            if (id == micvst::currentLanguage()) return;
            micvst::setLanguage (id);
            juce::Logger::writeToLog (micvst::tr ("Dil: ") + id);
            rebuild();
        }
        void applyRenderer (const juce::String& id)
        {
            micvst::render::setMode (micvst::render::fromId (id));   // used from the next start
            if (onThemeApplied) onThemeApplied();   // saved with the other appearance settings
        }
        void applyTheme (const juce::String& id)
        {
            if (id == micvst::theme::currentId) return;
            micvst::theme::applyTheme (id);
            juce::Logger::writeToLog (micvst::tr ("Tema: ") + micvst::tr (micvst::theme::paletteFor (id).name));
            rebuild();
        }
        // The window's content again, with the current colours and language.
        void rebuild()
        {
            if (onThemeApplied) onThemeApplied();   // LookAndFeel takes the new colours; saved
            setBackgroundColour (micvst::theme::background);
            juce::Rectangle<int> settingsBounds;
            SettingsComponent::ViewState settingsView;
            const bool settingsOpen = settingsWindow != nullptr && settingsWindow->isVisible();
            if (settingsWindow != nullptr)
            {
                settingsBounds = settingsWindow->getBounds();
                if (auto* sc = dynamic_cast<SettingsComponent*> (settingsWindow->getContentComponent()))
                    settingsView = sc->getViewState();
            }
            // the old settings window stays on screen until the new one is there (its panels add and remove
            // their own listeners, so both may exist for a moment): no gap, no jump
            auto oldSettings = std::move (settingsWindow);
            clearContentComponent();                // the old content (and its engine callbacks) first ...
            content = nullptr;
            createContent();                        // ... then the new one, built with the new colours
            micvst::useDarkTitleBar (*this, micvst::theme::dark);
            repaint();
            if (settingsOpen)                       // the settings window again, where it was, at the same section
                showSettings (settingsBounds, &settingsView);
            oldSettings = nullptr;
        }
        AudioEngine& engineRef;
        GlobalHotkeys* hotkeys = nullptr;
        HotkeySetter setHotkey;
        MainComponent* content = nullptr;   // gehört dem Fenster (setContentOwned), nur Zugriffszeiger
    };

    std::unique_ptr<MicVSTLogger> logger;
    std::unique_ptr<micvst::theme::NightLookAndFeel> lookAndFeel;   // default for every window (shutdown resets it)
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<TrayIcon> tray;
    std::unique_ptr<GlobalHotkeys> hotkeys;   // reset in shutdown() after the window that points to it
    bool suppressPersist = false;   // Factory-Reset: nichts darf die gelöschte Config neu anlegen
};

START_JUCE_APPLICATION (MicVSTApplication)
