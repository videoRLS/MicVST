#include "ui/MainComponent.h"
#include "audio/PluginChain.h"
#include "Text.h"

using micvst::tr;

namespace
{
    juce::String howToText()
    {
        return tr (
            "MicVST nasıl kullanılır\n"
            "\n"
            "1)  Sanal ses kablosu kurun\n"
            "    Ücretsiz VB-Cable önerilir: vb-audio.com/Cable. MicVST onu kendiliğinden bulur.\n"
            "\n"
            "2)  Cihazları seçin (pencerenin üstünde)\n"
            "    Giriş  =  mikrofonunuz\n"
            "    Çıkış  =  sanal kablo (ör. \"CABLE Input\")\n"
            "\n"
            "3)  Eklenti zincirinizi kurun\n"
            "    - \"+ Eklenti\" tüm VST3 efektlerini ve dahili araçları üreticiye göre gruplu,\n"
            "      aranabilir bir listede açar. İstediğiniz efektleri ekleyin (EQ, gürültü\n"
            "      giderme, kompresör...).\n"
            "    - Stereo istediğiniz yere \"Mono -> Stereo\" ekleyin; ondan önceki kısım mono\n"
            "      çalışır (daha az işlemci). \"Stereo -> Mono\" da vardır. Mono biten zincir\n"
            "      kablonun iki kanalına da gönderilir.\n"
            "    - \"Kazanç\" (Dahili) sesi -30 ile +12 dB arasında alçaltır/yükseltir; değeri\n"
            "      satırdaki kaydırıcıyla değiştirin. Kırpılmaması için sınırlayıcıdan önce koyun.\n"
            "    - \"Otomatik Kazanç\" (Dahili) sesinizi canlı analiz eder: normal konuşmada tepe\n"
            "      seviyesi yaklaşık -15 dBFS, bağırırken yaklaşık -17,5 dBFS olur. Sessizlikte son\n"
            "      kazancı korur (noise gate yok). Zincirde istediğiniz yere taşıyabilirsiniz;\n"
            "      satırdaki kutuyla açılıp kapatılır.\n"
            "    - Sıralamak için soldaki tutamağı sürükleyin; çöp kutusu siler (onay sorar).\n"
            "    - Eklentinin penceresini açmak için adına çift tıklayın.\n"
            "    - \"VST3 klasörleri\" başka konumlardaki eklentileri ekler.\n"
            "\n"
            "4)  İstediğiniz programda kullanın\n"
            "    Discord, OBS, Zoom vb. programlarda mikrofon olarak \"CABLE Output\" seçin.\n"
            "\n"
            "5)  Kesintisiz ses\n"
            "    Mikrofon, eklenti zinciri ve kablo ayrı ayrı çalışır; aradaki küçük güvenlik\n"
            "    payı anlık yüklenmeleri karşılar. \"Otomatik\" bu payı gerektiğinde kendisi\n"
            "    büyütür. Alttaki satırlar gecikmeyi, işlemci yükünü ve her kesintiyi gösterir.\n"
            "    \"Zincir çok ağır\" yazıyorsa eklentiler o anki yükte yetişemiyordur: kalite\n"
            "    ayarlarını (aşırı örnekleme, ileri bakış) düşürün ya da arka plandaki ağır\n"
            "    programın önceliğini azaltın.\n"
            "\n"
            "6)  Hepsi bu\n"
            "    Ayarlar kendiliğinden kaydedilir. Pencereyi kapatınca MicVST saatin yanındaki\n"
            "    simgede çalışmaya devam eder (seçenekler için simgeye sağ tıklayın).\n"
            "    \"Açılışta başlat\" seçiliyse Windows açılınca sessizce başlar.\n"
            "\n"
            "Gizlilik\n"
            "    MicVST internete hiç bağlanmaz: güncelleme denetimi, veri toplama ya da\n"
            "    web bağlantısı yoktur.");
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
            : juce::DocumentWindow (MainComponent::howToTitle(), juce::Colours::darkgrey,
                                    juce::DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (MainComponent::createHowToContent().release(), false);
            setResizable (true, false);
            centreWithSize (580, 600);
        }
        void closeButtonPressed() override { setVisible (false); }
    };
}

juce::String MainComponent::howToTitle()                         { return tr ("MicVST - Kullanım"); }
std::unique_ptr<juce::Component> MainComponent::createHowToContent() { return std::make_unique<HowToContent>(); }

MainComponent::MainComponent (AudioEngine& e) : engine (e)
{
    devicePanel = std::make_unique<DevicePanel> (engine);
    addAndMakeVisible (*devicePanel);
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    addAndMakeVisible (dbScale);
    inLabel.setText (tr ("Giriş"), juce::dontSendNotification);
    outLabel.setText (tr ("Çıkış"), juce::dontSendNotification);
    addAndMakeVisible (inLabel);
    addAndMakeVisible (outLabel);
    inLabel.setJustificationType (juce::Justification::centredLeft);
    outLabel.setJustificationType (juce::Justification::centredLeft);

    pluginList = std::make_unique<PluginListView> (engine);
    addAndMakeVisible (*pluginList);

    addAndMakeVisible (howToBtn);
    howToBtn.setButtonText (tr ("Kullanım"));
    howToBtn.setTooltip (tr ("Hızlı kurulum rehberi"));
    howToBtn.onClick = [this] { showHowTo(); };

    versionLabel.setText ("v" + juce::JUCEApplication::getInstance()->getApplicationVersion(), juce::dontSendNotification);
    versionLabel.setColour (juce::Label::textColourId, juce::Colours::grey);
    versionLabel.setTooltip (tr ("MicVST tamamen çevrimdışı çalışır (internete bağlanmaz)"));
    versionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (versionLabel);

    addAndMakeVisible (autostartToggle);
    autostartToggle.setButtonText (tr ("Açılışta başlat"));
    autostartToggle.setTooltip (tr ("Windows açılınca MicVST'yi pencere açmadan, simge olarak başlatır"));
    autostartToggle.setToggleState (AutostartRegistry::isEnabled(), juce::dontSendNotification);
    autostartToggle.onClick = [this] { AutostartRegistry::setEnabled (autostartToggle.getToggleState()); };

    cableHint.setText (tr ("Sanal ses kablosu bulunamadı - VB-Cable kurun (vb-audio.com/Cable) ve MicVST'yi yeniden başlatın"),
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

    // Mit der Registry synchron halten (z. B. wenn der Tray den Autostart umschaltet) - einmal
    // pro Sekunde reicht (1.1.1 las die Registry 30-mal pro Sekunde).
    if (++timerTicks >= 30)
    {
        timerTicks = 0;
        autostartToggle.setToggleState (AutostartRegistry::isEnabled(), juce::dontSendNotification);
    }
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

    // DevicePanel meldet seine Höhe selbst (Input/Output/Safety, +1 wenn die Buffer-Zeile
    // sichtbar ist, plus zwei Statuszeilen); die Plugin-Liste bekommt den ganzen Rest.
    devicePanel->setBounds (r.removeFromTop (devicePanel->preferredHeight()));
    r.removeFromTop (8);
    pluginList->setBounds (r);
}
