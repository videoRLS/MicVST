#include "ui/MainComponent.h"
#include "ui/HotkeyDialog.h"
#include "ui/DarkTitleBar.h"
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
            "    - \"Ayarlar > Eklentiler\" başka konumlardaki VST3 eklentilerini ekler.\n"
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
            "    \"Ayarlar\"da \"Windows açılışında başlat\" seçiliyse sessizce başlar.\n"
            "\n"
            "Gizlilik\n"
            "    MicVST internete hiç bağlanmaz: güncelleme denetimi, veri toplama ya da\n"
            "    web bağlantısı yoktur.")
             + "\n\n"
             + tr (
            "Sustur, kısayollar, kendini dinleme\n"
            "    - \"Sustur\" sesi tıksız kapatır; F9 (değiştirilebilir) MicVST arka plandayken de\n"
            "      susturur. \"Ayarlar > Kısayollar\" ile kısayolu (klavye ya da fare tuşu) değiştirin\n"
            "      ya da kapatın; saatin yanındaki simgenin menüsünde de \"Sustur\" ve hazır ayarlar vardır.\n"
            "    - \"Ham ses\" kabloya bir anlığına zincirsiz mikrofon sesini verir: önce / sonra.\n"
            "    - \"Ayarlar > Ses > Kulaklık\" ile dinleme cihazını seçin; \"Kendini dinle\" işlenmiş sesi\n"
            "      oradan çalar (hoparlör değil kulaklık kullanın). \"Deneme kaydı\" 10 saniye\n"
            "      kaydeder ve hemen kulaklıktan çalar.\n"
            "    - \"Dahili - Dinamik\": Kompresör ve Gürültü Kapısı. Hazır ayarlar ikisini de\n"
            "      ölçülü kullanır; \"Kaydet\" menüsünden ayarlar dosyaya aktarılıp paylaşılabilir.")
             + "\n\n"
             + tr (
            "Kayıt\n"
            "    - \"Kayıt\" düğmesi kabloya giden sesi (karşı tarafın duyduğunu) dosyaya kaydeder;\n"
            "      tekrar tıklayınca durur, dosya kapatılır ve doğrulanır. Saatin yanındaki simgenin\n"
            "      menüsünden de başlatılıp durdurulabilir.\n"
            "    - \"Ayarlar > Kayıt\": biçim (WAV, FLAC, MP3), kalite, örnekleme hızı, kanal ve\n"
            "      klasör. Varsayılan: WAV 24 bit, 48 kHz, stereo; Belgeler\\MicVST Kayıtları.\n"
            "    - Dosya adı tarih ve saattir; var olan bir kaydın üzerine asla yazılmaz.");
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

        // Footer under the text: a hairline, then the version, small and quiet.
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
    // --- the level card: meters with their peak values, the status line; the big mute button ---
    addAndMakeVisible (inMeter);
    addAndMakeVisible (outMeter);
    inLabel.setText (tr ("Giriş"), juce::dontSendNotification);
    outLabel.setText (tr ("Çıkış"), juce::dontSendNotification);
    for (auto* l : { &inLabel, &outLabel, &inValue, &outValue })
    {
        l->setFont (juce::Font (juce::FontOptions (12.5f)));
        addAndMakeVisible (l);
    }
    inLabel.setColour (juce::Label::textColourId, micvst::theme::textDim);
    outLabel.setColour (juce::Label::textColourId, micvst::theme::textDim);
    for (auto* l : { &inValue, &outValue })
    {
        l->setJustificationType (juce::Justification::centredRight);
        l->setColour (juce::Label::textColourId, micvst::theme::textBright);
    }

    devicePanel = std::make_unique<DevicePanel> (engine, true);
    addAndMakeVisible (*devicePanel);
    addAndMakeVisible (devicePanel->getStatusLabel());   // shown in the level card

    muteBtn.onClick = [this] { engine.setMuted (! engine.isMuted()); updateActionButtons(); };
    addAndMakeVisible (muteBtn);

    pluginList = std::make_unique<PluginListView> (engine);
    addAndMakeVisible (*pluginList);

    // --- bottom bar: Ham ses, Kendini dinle, Deneme kaydı ... ? Ayarlar ---
    for (auto* b : { &compareBtn, &monitorBtn, &testBtn, &recordBtn, &helpBtn, &settingsBtn })
        addAndMakeVisible (b);
    recordBtn.setColour (juce::TextButton::buttonOnColourId, micvst::theme::error.withAlpha (0.55f));
    recordBtn.onClick = [this] { toggleRecording(); };
    engine.onRecordingStopped = [this] (const Recorder::Result& r, const juce::String& why) { showRecordingResult (r, why); updateActionButtons(); };
    compareBtn.setButtonText (tr ("Ham ses"));
    compareBtn.setTooltip (tr ("Önce / sonra: kabloya bir anlığına zincirsiz, ham mikrofon sesi gider (aynı gecikmeyle). "
                               "Tekrar tıklayınca işlenmiş sese döner."));
    compareBtn.setColour (juce::TextButton::buttonOnColourId, micvst::theme::warning);
    compareBtn.onClick = [this] { engine.setRawCompare (! engine.isRawCompare()); updateActionButtons(); };
    monitorBtn.setButtonText (tr ("Kendini dinle"));
    monitorBtn.setTooltip (tr ("İşlenmiş sesi \"Kulaklık\" çıkışından anlık dinlersiniz. Hoparlörle değil kulaklıkla "
                               "kullanın: hoparlörden mikrofona dönen ses yankı ve ıslık yapar."));
    monitorBtn.onClick = [this]
    {
        const auto err = engine.setMonitorEnabled (! engine.isMonitorEnabled());
        if (err.isNotEmpty())
            juce::NativeMessageBox::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                tr ("Kendini dinle"), tr ("Kulaklık çıkışı açılamadı: ") + err, tr ("Tamam")), [] (int) {});
        updateActionButtons();
    };
    testBtn.setTooltip (tr ("10 saniye konuşun; MicVST kaydeder ve işlenmiş sesinizi hemen \"Kulaklık\" çıkışından "
                            "çalar. Kablo bu sırada canlı sesi göndermeye devam eder. Tıklayınca durur."));
    testBtn.setColour (juce::TextButton::buttonOnColourId, micvst::theme::error.withAlpha (0.35f));
    testBtn.onClick = [this]
    {
        if (engine.getTestState() != AudioEngine::TestState::idle) engine.stopTest();
        else if (const auto err = engine.startTestRecording(); err.isNotEmpty())
            juce::NativeMessageBox::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                tr ("Deneme kaydı"), err, tr ("Tamam")), [] (int) {});
        updateActionButtons();
    };
    helpBtn.setButtonText ("?");
    helpBtn.setTooltip (tr ("Hızlı kurulum rehberi"));
    helpBtn.onClick = [this] { showHowTo(); };
    settingsBtn.setButtonText (tr ("Ayarlar"));
    settingsBtn.setTooltip (tr ("Ses, kulaklık, kısayollar, dil, tema, VST3 klasörleri"));
    settingsBtn.onClick = [this] { if (onShowSettings) onShowSettings(); };
    for (auto* b : { &compareBtn, &monitorBtn })
        b->setColour (juce::TextButton::buttonOnColourId, b == &monitorBtn ? micvst::theme::accentDim : micvst::theme::warning);

    cableHint.setText (tr ("Sanal ses kablosu bulunamadı - VB-Cable kurun (vb-audio.com/Cable) ve MicVST'yi yeniden başlatın"),
                       juce::dontSendNotification);
    cableHint.setColour (juce::Label::textColourId, micvst::theme::warning);
    cableHint.setJustificationType (juce::Justification::centredLeft);
    addChildComponent (cableHint);   // Sichtbarkeit steuert updateCableHint()

    engine.onStatusChanged = [this] { updateCableHint(); };
    updateCableHint();
    updateActionButtons();

    setSize (640, 600);
    startTimerHz (30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    engine.onStatusChanged = nullptr;
    engine.onRecordingStopped = nullptr;
}

void MainComponent::toggleRecording()
{
    if (engine.isRecording())
        showRecordingResult (engine.stopRecording(), {});
    else if (const auto err = engine.startRecording(); err.isNotEmpty())
        juce::NativeMessageBox::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
            tr ("Kayıt"), err, tr ("Tamam")), [] (int) {});
    updateActionButtons();
}

void MainComponent::showRecordingResult (const Recorder::Result& r, const juce::String& why)
{
    if (r.file == juce::File()) return;
    const bool problem = why.isNotEmpty() || r.problem.isNotEmpty() || ! r.verified || r.droppedFrames > 0;
    if (problem)
    {
        juce::String text;
        if (why.isNotEmpty()) text << why << "\n\n";
        else if (r.problem.isNotEmpty()) text << r.problem << "\n\n";
        text << tr ("Dosya: ") << r.file.getFullPathName() << "\n" << tr ("Süre: ") << juce::String (r.seconds, 1) << " s";
        if (r.droppedFrames > 0) text << "\n" << tr ("Kayıp örnek: ") << juce::String (r.droppedFrames);
        text << "\n" << (r.verified ? tr ("Dosya doğrulandı: kaydedilen kısım eksiksiz.") : tr ("Dosya doğrulanamadı."));
        juce::NativeMessageBox::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
            tr ("Kayıt"), text, tr ("Tamam")), [] (int) {});
        return;
    }
    // fine: a short note at the button (no dialog to click away)
    recordBubble = std::make_unique<juce::BubbleMessageComponent> (300);
    addChildComponent (*recordBubble);
    juce::AttributedString a;
    a.append (tr ("Kaydedildi: ") + r.file.getFileName() + " (" + juce::String (r.seconds, 1) + " s)",
              juce::Font (juce::FontOptions (14.0f)), micvst::theme::text);
    recordBubble->setColour (juce::BubbleComponent::backgroundColourId, micvst::theme::surface);
    recordBubble->setColour (juce::BubbleComponent::outlineColourId, micvst::theme::outline);
    recordBubble->showAt (&recordBtn, a, 4000, true, false);
}

void MainComponent::timerCallback()
{
    const auto in = engine.inputLevel(), out = engine.outputLevel();
    inMeter.setLevel (in);
    outMeter.setLevel (out);
    // the shown peak values fall slowly (readable), rise at once
    auto show = [] (juce::Label& l, float& held, float peak)
    {
        const float db = peak > 1.0e-6f ? juce::Decibels::gainToDecibels (peak) : -120.0f;
        held = db > held ? db : juce::jmax (db, held - 0.5f);
        const auto t = held <= -60.0f ? juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94")) : micvst::trNumber (held, 0) + " dB";
        if (l.getText() != t) l.setText (t, juce::dontSendNotification);
    };
    show (inValue, inHeld, in.peak);
    show (outValue, outHeld, out.peak);

    engine.testTick();
    updateActionButtons();
}

void MainComponent::setHotkeys (GlobalHotkeys* keys)
{
    hotkeys = keys;
    updateActionButtons();
}

void MainComponent::updateActionButtons()
{
    juce::String key;
    if (hotkeys != nullptr && hotkeys->get (GlobalHotkeys::mute).enabled && hotkeys->isActive (GlobalHotkeys::mute))
        key = GlobalHotkeys::describe (hotkeys->get (GlobalHotkeys::mute));
    muteBtn.setState (engine.isMuted(), key);

    juce::String cmpText = tr ("Ham ses");
    if (hotkeys != nullptr && hotkeys->get (GlobalHotkeys::rawCompare).enabled && hotkeys->isActive (GlobalHotkeys::rawCompare))
        cmpText << " (" << GlobalHotkeys::describe (hotkeys->get (GlobalHotkeys::rawCompare)) << ")";
    if (compareBtn.getButtonText() != cmpText) { compareBtn.setButtonText (cmpText); resized(); }
    compareBtn.setToggleState (engine.isRawCompare(), juce::dontSendNotification);
    monitorBtn.setToggleState (engine.isMonitorEnabled(), juce::dontSendNotification);

    juce::String t;
    const int secs = juce::roundToInt (AudioEngine::testSeconds * (1.0 - engine.getTestProgress()) + 0.49);
    switch (engine.getTestState())
    {
        case AudioEngine::TestState::idle:      t = tr ("Deneme kaydı"); break;
        case AudioEngine::TestState::recording: t = tr ("Kaydediliyor... ") + juce::String (secs); break;
        case AudioEngine::TestState::playing:   t = tr ("Çalınıyor... ") + juce::String (secs); break;
    }
    if (testBtn.getButtonText() != t) testBtn.setButtonText (t);
    testBtn.setToggleState (engine.getTestState() != AudioEngine::TestState::idle, juce::dontSendNotification);

    // Kayıt: a dot while idle, a square and the running time while recording
    juce::String rt;
    if (engine.isRecording())
    {
        const int s = (int) engine.getRecordingSeconds();
        rt = juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 "))
             + (s >= 3600 ? juce::String (s / 3600) + ":" + juce::String ((s / 60) % 60).paddedLeft ('0', 2)
                          : juce::String (s / 60).paddedLeft ('0', 2))
             + ":" + juce::String (s % 60).paddedLeft ('0', 2);
    }
    else
        rt = juce::String (juce::CharPointer_UTF8 ("\xe2\x97\x8f ")) + tr ("Kayıt");
    if (recordBtn.getButtonText() != rt) recordBtn.setButtonText (rt);
    recordBtn.setToggleState (engine.isRecording(), juce::dontSendNotification);
    const auto& rs = engine.getRecordSettings();
    auto used = rs;
    used.sampleRate = rs.effectiveRate (engine.getProcessingSampleRate());
    const auto tip = (engine.isRecording() ? tr ("Kaydediliyor: ") + engine.getRecordingFile().getFullPathName() + "\n" + tr ("Tıklayınca durur ve dosya kapatılır.")
                                           : tr ("Kabloya giden sesi (karşı tarafın duyduğunu) dosyaya kaydeder. Tıklayınca başlar, tekrar tıklayınca durur."))
                     + "\n" + used.describe() + "\n" + rs.folderOrDefault().getFullPathName();
    if (recordBtn.getTooltip() != tip) recordBtn.setTooltip (tip);
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

void MainComponent::paint (juce::Graphics& g)
{
    // the level card behind the meters and the status line
    g.setColour (micvst::theme::rowA);
    g.fillRoundedRectangle (levelCard.toFloat(), 10.0f);
    g.setColour (micvst::theme::outline);
    g.drawRoundedRectangle (levelCard.toFloat().reduced (0.5f), 10.0f, 1.0f);
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (14, 12);

    // bottom bar
    {
        auto bar = r.removeFromBottom (34);
        auto textW = [] (juce::TextButton& b) { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (15.0f)), b.getButtonText())) + 26; };
        settingsBtn.setBounds (bar.removeFromRight (juce::jmax (96, textW (settingsBtn))));
        bar.removeFromRight (6);
        helpBtn.setBounds (bar.removeFromRight (34));
        bar.removeFromRight (12);
        const int testW = juce::jmax (textW (testBtn), juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (15.0f)),
                                                                          tr ("Kaydediliyor... ") + "10")) + 26);
        const int recW = juce::jmax (textW (recordBtn), juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (15.0f)),
                                                                            juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xa0 ")) + "0:00:00")) + 26);
        int widths[] = { textW (compareBtn), textW (monitorBtn), testW, recW };
        const int total = widths[0] + widths[1] + widths[2] + widths[3] + 18;
        const double scale = total > bar.getWidth() ? (double) bar.getWidth() / total : 1.0;
        juce::TextButton* btns[] = { &compareBtn, &monitorBtn, &testBtn, &recordBtn };
        for (int i = 0; i < 4; ++i)
        {
            btns[i]->setBounds (bar.removeFromLeft ((int) (widths[i] * scale)));
            bar.removeFromLeft (6);
        }
        r.removeFromBottom (12);
    }

    // level card + mute button
    {
        auto top = r.removeFromTop (88);
        muteBtn.setBounds (top.removeFromRight (100));
        top.removeFromRight (12);
        levelCard = top;
        auto c = top.reduced (12, 10);
        auto textW = [] (const juce::Label& l) { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (l.getFont(), l.getText()) + 0.5f); };
        const int labelW = juce::jlimit (40, 96, juce::jmax (textW (inLabel), textW (outLabel)) + 8);
        auto meterRow = [&] (juce::Label& label, LevelMeterComponent& meter, juce::Label& value)
        {
            auto row = c.removeFromTop (18);
            label.setBounds (row.removeFromLeft (labelW));
            value.setBounds (row.removeFromRight (56));
            row.removeFromRight (6);
            meter.setBounds (row.withSizeKeepingCentre (row.getWidth(), 12));
            c.removeFromTop (6);
        };
        meterRow (inLabel, inMeter, inValue);
        meterRow (outLabel, outMeter, outValue);
        devicePanel->getStatusLabel().setBounds (c.removeFromTop (20).withTrimmedLeft (-4));
        r.removeFromTop (12);
    }

    // devices
    devicePanel->setBounds (r.removeFromTop (devicePanel->preferredHeight()));
    r.removeFromTop (8);

    if (cableHint.isVisible())
    {
        cableHint.setBounds (r.removeFromTop (22));
        r.removeFromTop (4);
    }
    r.removeFromTop (4);
    pluginList->setBounds (r);
}

//==============================================================================
MuteButton::MuteButton() : juce::Button ("mute") {}

void MuteButton::setState (bool m, const juce::String& key)
{
    const auto tip = m ? tr ("Ses kapalı: kabloya sessizlik gidiyor. Tıklayınca ses açılır.")
                       : tr ("Sesi tıksız kapatır (15 ms yumuşak geçiş); tekrar tıklayınca açılır.");
    if (m == muted && key == keyText && getTooltip() == tip) return;
    muted = m;
    keyText = key;
    setTooltip (tip);
    setTitle (m ? tr ("Sesi aç") : tr ("Sustur"));
    repaint();
}

void MuteButton::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    auto b = getLocalBounds().toFloat().reduced (0.5f);
    const auto fill = muted ? micvst::theme::error : micvst::theme::surface;
    g.setColour (down ? fill.darker (0.15f) : highlighted ? fill.brighter (0.08f) : fill);
    g.fillRoundedRectangle (b, 10.0f);
    g.setColour (muted ? micvst::theme::error : micvst::theme::outline);
    g.drawRoundedRectangle (b, 10.0f, 1.0f);

    const auto ink = muted ? juce::Colours::white : micvst::theme::textBright;
    // microphone icon
    const float cx = b.getCentreX(), top = b.getY() + 14.0f;
    juce::Path p;
    p.addRoundedRectangle (cx - 5.5f, top, 11.0f, 19.0f, 5.5f);
    g.setColour (ink);
    g.strokePath (p, juce::PathStrokeType (1.8f));
    juce::Path arc;
    arc.addCentredArc (cx, top + 11.0f, 10.0f, 10.0f, 0.0f, juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi * 3.0f, true);
    arc.startNewSubPath (cx, top + 21.0f);
    arc.lineTo (cx, top + 26.0f);
    if (muted)
    {
        arc.startNewSubPath (cx - 12.0f, top - 2.0f);
        arc.lineTo (cx + 12.0f, top + 26.0f);
    }
    g.strokePath (arc, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
    g.drawFittedText (muted ? tr ("Sesi aç") : tr ("Sustur"), b.toNearestInt().withTrimmedTop (44).withHeight (20), juce::Justification::centred, 1);
    if (keyText.isNotEmpty())
    {
        g.setColour (muted ? juce::Colours::white.withAlpha (0.8f) : micvst::theme::textDim);
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.drawFittedText (keyText, b.toNearestInt().withTrimmedTop (64).withHeight (16), juce::Justification::centred, 1);
    }
}
