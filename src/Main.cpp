#include <juce_gui_basics/juce_gui_basics.h>
#include "audio/AudioEngine.h"
#include "audio/RealtimeSupport.h"
#include "state/Persistence.h"
#include "state/AutostartRegistry.h"
#include "ui/MainComponent.h"
#include "ui/TrayIcon.h"

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
        // Scans laufen parallel zum Audio: niedrige Priorität + EcoQoS, damit ein schwerer
        // Plugin-Scan nie mit den Audio-Threads um CPU konkurriert.
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

        auto logFile = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                          .getChildFile ("MicVST").getChildFile ("log.txt");
        logFile.getParentDirectory().createDirectory();
        logger.reset (new juce::FileLogger (logFile, "MicVST Log"));
        juce::Logger::setCurrentLogger (logger.get());
        juce::Logger::writeToLog ("MicVST " + getApplicationVersion() + " (" + juce::SystemStats::getJUCEVersion()
                                  + ", no network access)");

        // Echtzeit-Prozess: Windows darf den (oft nur im Tray laufenden) Audio-Prozess nicht als
        // Hintergrundarbeit drosseln (EcoQoS / Timer-Drosselung), Priorität "über normal".
        const auto rtSetup = rt::configureRealtimeProcess();
        juce::Logger::writeToLog (juce::String ("Process: priority above normal ") + (rtSetup.priorityRaised ? "ok" : "FAILED")
                                  + ", power throttling off " + (rtSetup.executionThrottlingOff ? "ok" : "n/a")
                                  + ", timer throttling off " + (rtSetup.timerThrottlingOff ? "ok" : "n/a"));

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
                ? "No virtual cable found -> output 'none'"
                : "Host output (cable) = " + state.outputDevice);

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
            juce::Logger::writeToLog ("Input = " + state.inputDevice);
        }
        engine->applyState (state);

        mainWindow = std::make_unique<MainWindow> ("MicVST", *engine, state.windowState);
        mainWindow->setVisible (! silent);   // Autostart (--tray): unsichtbar, nur Tray

        tray = std::make_unique<TrayIcon>();
        tray->onToggleWindow = [this] { toggleWindow(); };
        tray->onQuit         = [this] { systemRequestedQuit(); };
        tray->isAutostartOn  = [] { return AutostartRegistry::isEnabled(); };
        tray->setAutostart   = [] (bool on) { AutostartRegistry::setEnabled (on); };

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
        engine = nullptr;
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
        juce::Logger::writeToLog ("Factory-Reset: Daten gelöscht, App beendet sich");
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
        tray->showInfoBubble ("MicVST", "Still running in the tray - right-click the icon for options.");
    }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (juce::String name, AudioEngine& engine, const juce::String& windowState)
            : DocumentWindow (name, juce::Colours::darkgrey, DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            // resize-to-fit AUS: die Fenstergröße diktiert centreWithSize unten, nicht die
            // Content-Größe. Sonst zieht der MainComponent (setSize im Ctor) die Startgröße
            // über das Minimum hinaus.
            content = new MainComponent (engine);
            setContentOwned (content, false);
            setResizable (true, false);
            // Mindestgröße = Startgröße (600x560); zusätzliche Höhe verlängert die Plugin-Liste.
            setResizeLimits (600, 560, 1600, 1400);
            // Letzte Größe/Position wiederherstellen (innerhalb der Resize-Limits), sonst zentriert
            // auf Startgröße. restoreWindowStateFromString respektiert die gesetzten Limits.
            if (windowState.isNotEmpty())
                restoreWindowStateFromString (windowState);
            else
                centreWithSize (600, 560);
        }
        std::function<void()> onHide;
        void closeButtonPressed() override { setVisible (false); if (onHide) onHide(); }
        MainComponent* getContent() const { return content; }
    private:
        MainComponent* content = nullptr;   // gehört dem Fenster (setContentOwned), nur Zugriffszeiger
    };

    std::unique_ptr<juce::FileLogger> logger;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> mainWindow;
    std::unique_ptr<TrayIcon> tray;
    bool suppressPersist = false;   // Factory-Reset: nichts darf die gelöschte Config neu anlegen
};

START_JUCE_APPLICATION (MicVSTApplication)
