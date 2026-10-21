#include "ui/MainComponent.h"
#include "ui/Theme.h"
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
            "      seviyesi yaklaşık -20 dBFS, bağırırken yaklaşık -15 dBFS olur; hiçbir tepe\n"
            "      -10 dBFS'yi geçmez. Kazanç 0 ile +6 dB arasında değişir,\n"
            "      sessizlikte korunur (noise gate yok). Zincirde istediğiniz yere\n"
            "      taşıyabilirsiniz; satırdaki kutuyla açılıp kapatılır.\n"
            "    - Sıralamak için soldaki tutamağı sürükleyin; çöp kutusu siler (onay sorar).\n"
            "    - Eklentinin penceresini açmak için adına çift tıklayın.\n"
            "    - \"VST3 klasörleri\" başka konumlardaki eklentileri ekler.\n"
            "    - Dahili efektler (EQ, de-esser, saturasyon, limiter, LUFS ölçer...) listede \"Dahili - ...\"\n"
            "      başlıkları altındadır; ayarları adlarına çift tıklayınca açılır.\n"
            "    - \"Hazır ayar\" listesi tüm zinciri tek seçimde değiştirir: dahili hazır ayarlar\n"
            "      yalnızca dahili efektleri kullanır; \"Kaydet\" o anki zinciri (VST3 eklentileri ve\n"
            "      ayarlarıyla) kendi ayarınız olarak saklar. \"Önceki zincire dön\" son seçimi geri alır.\n"
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
            "    \"İşlem önceliği\" (Normal önerilir) MicVST'nin pencere ve cihaz işlerini diğer\n"
            "    programlara göre öne alır; ses her seçenekte Windows'un gerçek zamanlı ses\n"
            "    önceliğinde işlenir.\n"
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
    // The how-to text, laid out by MicVST itself: the text (in every language) is written with
    // hard line breaks and indentation for a fixed width; here the lines of one paragraph or list
    // item are joined again and wrapped to the window width, with a hanging indent for the "- "
    // items. So longer languages never break a line in the middle of the indentation, and each
    // paragraph is shaped as a whole (Devanagari, Arabic, CJK). Arabic runs right to left.
    class HowToBody : public juce::Component
    {
    public:
        HowToBody()
        {
            rtl = micvst::currentLanguage() == "ar";
            parse (howToText());
        }

        // Lays the blocks out for a width; returns the height they need.
        int layoutFor (int width)
        {
            int y = 0;
            for (auto& b : blocks)
            {
                if (b.kind == Block::gap) { b.area = { 0, y, width, gapH }; y += gapH; continue; }
                const int indent = b.kind == Block::item ? itemIndent : b.kind == Block::para ? paraIndent : 0;
                const int w = juce::jmax (40, width - indent);
                juce::AttributedString as;
                as.setWordWrap (juce::AttributedString::byWord);
                as.setLineSpacing (2.0f);   // room for marks above and below the line (Devanagari, Arabic)
                as.setJustification (rtl ? juce::Justification::topRight : juce::Justification::topLeft);
                as.setReadingDirection (rtl ? juce::AttributedString::rightToLeft : juce::AttributedString::leftToRight);
                as.append (b.text, juce::Font (juce::FontOptions (b.kind == Block::heading ? 15.5f : 15.0f)),
                           b.kind == Block::heading ? micvst::theme::textBright : micvst::theme::text);
                b.layout.createLayout (as, (float) w);
                const int h = (int) std::ceil (b.layout.getHeight()) + lineGap;
                b.area = rtl ? juce::Rectangle<int> (0, y, w, h) : juce::Rectangle<int> (indent, y, w, h);
                y += h;
            }
            return y;
        }

        void paint (juce::Graphics& g) override
        {
            for (auto& b : blocks)
            {
                if (b.kind == Block::gap) continue;
                if (! g.clipRegionIntersects (b.area)) continue;
                b.layout.draw (g, b.area.toFloat());
                if (b.kind == Block::item)   // the dash in front of the item
                {
                    g.setColour (micvst::theme::textDim);
                    g.setFont (juce::Font (juce::FontOptions (15.0f)));
                    const auto dash = rtl ? juce::Rectangle<int> (b.area.getRight(), b.area.getY(), itemIndent - paraIndent, 18)
                                          : juce::Rectangle<int> (paraIndent, b.area.getY(), itemIndent - paraIndent, 18);
                    g.drawText (juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x93")), dash, juce::Justification::centredLeft);
                }
            }
        }

    private:
        struct Block
        {
            enum Kind { heading, para, item, gap } kind;
            juce::String text;
            juce::TextLayout layout;
            juce::Rectangle<int> area;
        };
        std::vector<Block> blocks;
        bool rtl = false;
        static constexpr int paraIndent = 22, itemIndent = 36, gapH = 10, lineGap = 2;

        static bool joinsWithoutSpace (juce::juce_wchar a, juce::juce_wchar b)
        {
            auto cjk = [] (juce::juce_wchar c) { return (c >= 0x2E80 && c < 0xA000) || (c >= 0xF900 && c < 0xFB00) || (c >= 0xFF00 && c < 0xFFF0); };
            return cjk (a) || cjk (b);
        }

        void add (Block::Kind k, const juce::String& t) { blocks.push_back ({ k, t, {}, {} }); }

        void appendTo (Block& b, const juce::String& more)
        {
            if (b.text.isEmpty() || more.isEmpty()) { b.text += more; return; }
            if (b.text.endsWithChar ('-') && b.text.length() > 1 && ! juce::CharacterFunctions::isWhitespace (b.text[b.text.length() - 2]))
                b.text += more;   // a word split at a hyphen ("VST3-" + "Plugins")
            else
                b.text += (joinsWithoutSpace (b.text.getLastCharacter(), more[0]) ? "" : " ") + more;
        }

        void parse (const juce::String& text)
        {
            Block* open = nullptr;   // the paragraph or item the next continuation line belongs to
            for (auto line : juce::StringArray::fromLines (text))
            {
                line = line.trimEnd();
                const auto lead = line.length() - line.trimStart().length();
                const auto body = line.trimStart();
                if (body.isEmpty()) { add (Block::gap, {}); open = nullptr; continue; }
                if (lead == 0)      { add (Block::heading, body); open = nullptr; continue; }
                if (body.startsWith ("- ")) { add (Block::item, body.substring (2).trimStart()); open = &blocks.back(); continue; }
                const bool table = body.contains ("  =  ");   // "Giriş  =  mikrofonunuz": one line each
                if (open != nullptr && ! table && (open->kind == Block::item ? lead > 4 : true))
                {
                    appendTo (*open, body);
                    continue;
                }
                add (Block::para, table ? body.replace ("  =  ", " = ") : body);
                open = table ? nullptr : &blocks.back();
            }
        }
    };

    class HowToContent : public juce::Component
    {
    public:
        HowToContent()
        {
            viewport.setViewedComponent (&body, false);
            viewport.setScrollBarsShown (true, false);
            addAndMakeVisible (viewport);
        }

        // Footer under the text: a hairline, then the version and the signature, small and quiet.
        void paint (juce::Graphics& g) override
        {
            g.fillAll (micvst::theme::surface);
            auto foot = getLocalBounds().reduced (12, 0).removeFromBottom (footerH);
            g.setColour (micvst::theme::outline);
            g.fillRect (foot.removeFromTop (1));
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.setColour (micvst::theme::textDim);
            const auto version = "MicVST v" + juce::String (MICVST_VERSION_STRING);
            g.drawText (version, foot, juce::Justification::centredLeft);
            g.setColour (micvst::theme::text);
            g.drawText (tr ("Relios tarafından geliştirilmiştir."), foot, juce::Justification::centredRight);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12);
            r.removeFromBottom (footerH - 12 + 4);
            viewport.setBounds (r);
            const int w = r.getWidth() - viewport.getScrollBarThickness() - 6;
            body.setSize (w, body.layoutFor (w));
        }

    private:
        static constexpr int footerH = 34;
        juce::Viewport viewport;
        HowToBody body;
    };

    // Eigenes Fenster mit der Anleitung. Schließen versteckt nur (Instanz bleibt zum Wieder-Öffnen).
    class HowToWindow : public juce::DocumentWindow
    {
    public:
        HowToWindow()
            : juce::DocumentWindow (MainComponent::howToTitle(), micvst::theme::background,
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
    versionLabel.setColour (juce::Label::textColourId, micvst::theme::textDim);
    versionLabel.setTooltip (tr ("MicVST tamamen çevrimdışı çalışır (internete bağlanmaz)"));
    versionLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (versionLabel);

    {
        int id = 1;
        for (auto& l : micvst::languages())
        {
            // only languages whose translation table is in this build (Türkçe always)
            if (id > 1 && micvst::translationTable (l.id) == nullptr) { ++id; continue; }
            languageBox.addItem (juce::String::fromUTF8 (l.nativeName), id);
            if (micvst::currentLanguage() == l.id) languageBox.setSelectedId (id, juce::dontSendNotification);
            ++id;
        }
    }
    languageBox.setTooltip (tr ("Arayüz dili. Varsayılan: Türkçe."));
    languageBox.onChange = [this]
    {
        const int idx = languageBox.getSelectedId() - 1;
        const auto& list = micvst::languages();
        if (juce::isPositiveAndBelow (idx, (int) list.size()) && onLanguageChosen)
            onLanguageChosen (list[(size_t) idx].id);
    };
    addAndMakeVisible (languageBox);
    languageBox.setVisible (languageBox.getNumItems() > 1);   // nothing to choose yet: hidden
    {
        int id = 1;
        for (auto& p : micvst::theme::palettes())
            themeBox.addItem (tr (p.name), id++);
        id = 1;
        for (auto& p : micvst::theme::palettes())
        {
            if (micvst::theme::currentId == p.id) themeBox.setSelectedId (id, juce::dontSendNotification);
            ++id;
        }
    }
    themeBox.setTooltip (tr ("Pencerenin renk teması. Varsayılan: Gece."));
    themeBox.onChange = [this]
    {
        const int idx = themeBox.getSelectedId() - 1;
        const auto& list = micvst::theme::palettes();
        if (juce::isPositiveAndBelow (idx, (int) list.size()) && onThemeChosen)
            onThemeChosen (list[(size_t) idx].id);
    };
    addAndMakeVisible (themeBox);

    addAndMakeVisible (autostartToggle);
    autostartToggle.setButtonText (tr ("Açılışta başlat"));
    autostartToggle.setTooltip (tr ("Windows açılınca MicVST'yi pencere açmadan, simge olarak başlatır"));
    autostartToggle.setToggleState (AutostartRegistry::isEnabled(), juce::dontSendNotification);
    autostartToggle.onClick = [this] { AutostartRegistry::setEnabled (autostartToggle.getToggleState()); };

    cableHint.setText (tr ("Sanal ses kablosu bulunamadı - VB-Cable kurun (vb-audio.com/Cable) ve MicVST'yi yeniden başlatın"),
                       juce::dontSendNotification);
    cableHint.setColour (juce::Label::textColourId, micvst::theme::warning);
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
    bottomRow.removeFromRight (6);
    themeBox.setBounds (bottomRow.removeFromRight (150));
    bottomRow.removeFromRight (4);
    languageBox.setBounds (bottomRow.removeFromRight (118));
    versionLabel.setBounds (bottomRow);
    r.removeFromBottom (4);

    // Horizontale Meter: In-Zeile, Out-Zeile, gemeinsame dB-Skala darunter.
    // as wide as the longer of "Giriş" / "Çıkış" in the current language (40 px for Turkish)
    auto textW = [] (const juce::Label& l) { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (l.getFont(), l.getText()) + 0.5f); };
    const int labelW = juce::jlimit (40, 96, juce::jmax (textW (inLabel), textW (outLabel)) + 8);
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
