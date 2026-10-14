#include "ui/MainComponent.h"
#include "audio/PluginChain.h"

namespace
{
    juce::String howToText()
    {
        return
            "How to use MicVST\n"
            "\n"
            "1)  Install a virtual audio cable\n"
            "    VB-Cable (free) is recommended: vb-audio.com/Cable. MicVST detects it automatically.\n"
            "\n"
            "2)  Pick your devices (top of the window)\n"
            "    Input   =  your microphone\n"
            "    Output  =  the virtual cable (e.g. \"CABLE Input\")\n"
            "\n"
            "3)  Build your plugin chain\n"
            "    - \"+ Plugin\" opens a searchable list (grouped by manufacturer) of all VST3 effects\n"
            "      and built-in nodes. Add effects you like (EQ, de-noise, compressor, ...).\n"
            "    - Insert \"Mono -> Stereo\" where you want stereo (before it the chain runs\n"
            "      mono = less CPU). \"Stereo -> Mono\" is available too.\n"
            "    - Drag the handle on the left to reorder; the trash icon removes (with a prompt).\n"
            "    - Double-click a plugin to open its editor.\n"
            "    - \"Manage VST3 Folders\" adds plugins from other locations.\n"
            "\n"
            "4)  Use it in any app\n"
            "    In Discord / OBS / Zoom / ..., select \"CABLE Output\" as the microphone.\n"
            "\n"
            "5)  That's it\n"
            "    Settings are saved automatically. Closing the window keeps MicVST running in the\n"
            "    tray (right-click the tray icon for options). Enable \"Run at startup\" to launch\n"
            "    it silently on boot.\n"
            "\n"
            "Privacy\n"
            "    MicVST never accesses the internet: no update check, no telemetry, no web links.";
    }

    // Inhalt des How-To-Fensters: nur Anleitungstext (keine klickbaren Links mehr - MicVST
    // öffnet keine Webseiten).
    class HowToContent : public juce::Component
    {
    public:
        HowToContent()
        {
            body.setMultiLine (true);
            body.setReadOnly (true);
            body.setCaretVisible (false);
            body.setScrollbarsShown (true);
            body.setPopupMenuEnabled (false);
            body.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1e1e1e));
            body.setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            body.setColour (juce::TextEditor::textColourId, juce::Colours::white);
            body.setFont (juce::Font (juce::FontOptions (15.0f)));
            body.setText (howToText(), false);
            addAndMakeVisible (body);
        }

        void resized() override { body.setBounds (getLocalBounds().reduced (12)); }

    private:
        juce::TextEditor body;
    };

    // Eigenes Fenster mit der Anleitung. Schließen versteckt nur (Instanz bleibt zum Wieder-Öffnen).
    class HowToWindow : public juce::DocumentWindow
    {
    public:
        HowToWindow()
            : juce::DocumentWindow ("MicVST - How To", juce::Colours::darkgrey,
                                    juce::DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new HowToContent(), false);
            setResizable (true, false);
            centreWithSize (560, 560);
        }
        void closeButtonPressed() override { setVisible (false); }
    };
}

MainComponent::MainComponent (AudioEngine& e) : engine (e)
{
    devicePanel = std::make_unique<DevicePanel> (engine);
    addAndMakeVisible (*devicePanel);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (dbScale);
    addAndMakeVisible (inLabel);
    addAndMakeVisible (outLabel);
    inLabel.setJustificationType (juce::Justification::centredLeft);
    outLabel.setJustificationType (juce::Justification::centredLeft);

    pluginList = std::make_unique<PluginListView> (engine);
    addAndMakeVisible (*pluginList);

    addAndMakeVisible (howToBtn);
    howToBtn.setTooltip ("Quick setup guide");
    howToBtn.onClick = [this] { showHowTo(); };

    versionLabel.setText ("v" + juce::JUCEApplication::getInstance()->getApplicationVersion(), juce::dontSendNotification);
    versionLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    versionLabel.setTooltip ("MicVST works fully offline (no network access)");
    versionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (versionLabel);

    addAndMakeVisible (autostartToggle);
    autostartToggle.setTooltip ("Launch MicVST silently into the tray when Windows starts");
    autostartToggle.setToggleState (AutostartRegistry::isEnabled(), juce::dontSendNotification);
    autostartToggle.onClick = [this] { AutostartRegistry::setEnabled (autostartToggle.getToggleState()); };

    cableHint.setText ("No virtual audio cable found - install VB-Cable (vb-audio.com/Cable) and restart MicVST",
                       juce::dontSendNotification);
    cableHint.setColour (juce::Label::textColourId, juce::Colours::orange);
    cableHint.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (cableHint);   // Sichtbarkeit steuert updateCableHint()

    engine.onStatusChanged = [this] { updateCableHint(); };
    updateCableHint();

    setSize (560, 560);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.onStatusChanged = nullptr;
}

void MainComponent::timerCallback()
{
    inMeter.setLevel (engine.inputLevel());
    outMeter.setLevel (engine.outputLevel());
    // Mit der Registry synchron halten (z. B. wenn der Tray den Autostart umschaltet).
    autostartToggle.setToggleState (AutostartRegistry::isEnabled(), juce::dontSendNotification);
}

void MainComponent::showHowTo()
{
    if (howToWindow == nullptr)
        howToWindow.reset (new HowToWindow());
    howToWindow->setVisible (true);
    howToWindow->toFront (true);
}

void MainComponent::updateCableHint()
{
    // Hinweis nur zeigen, wenn KEIN virtuelles Kabel installiert ist. Wird bei Geräte-
    // Änderungen aufgerufen (z. B. nach VB-Cable-Installation verschwindet er von selbst).
    const bool noCable = engine.detectCableOutput().isEmpty();
    if (noCable != cableHint.isVisible())
    {
        cableHint.setVisible (noCable);
        resized();
    }
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto bottomRow = r.removeFromBottom (24);
    // Rechts, von außen nach innen: Run at startup (Rand) | How To. Links die Version.
    autostartToggle.setBounds (bottomRow.removeFromRight (120));
    bottomRow.removeFromRight (6);
    howToBtn.setBounds (bottomRow.removeFromRight (80));
    versionLabel.setBounds (bottomRow);
    r.removeFromBottom (4);

    // Horizontale Meter: In-Zeile, Out-Zeile, gemeinsame dB-Skala darunter.
    constexpr int labelW = 40;
    auto inRow = r.removeFromTop (22);
    inLabel.setBounds (inRow.removeFromLeft (labelW));
    inMeter.setBounds (inRow.reduced (2, 1));
    r.removeFromTop (4);
    auto outRow = r.removeFromTop (22);
    outLabel.setBounds (outRow.removeFromLeft (labelW));
    outMeter.setBounds (outRow.reduced (2, 1));
    auto scaleRow = r.removeFromTop (16);
    scaleRow.removeFromLeft (labelW);                 // bündig unter den Metern
    dbScale.setBounds (scaleRow.reduced (2, 0));
    r.removeFromTop (8);

    if (cableHint.isVisible())
    {
        cableHint.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
    }

    // DevicePanel meldet seine Höhe selbst (3 Zeilen, +1 wenn die Buffer-Zeile sichtbar
    // ist); die Plugin-Liste bekommt den ganzen Rest.
    devicePanel->setBounds (r.removeFromTop (devicePanel->preferredHeight()));
    r.removeFromTop (8);
    pluginList->setBounds (r);
}
