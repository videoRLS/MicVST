#include "ui/RecordingPanel.h"
#include "ui/Theme.h"
#include "Text.h"

using micvst::tr;

namespace
{
    // quality list ids: WAV 1-4, FLAC 11-12, MP3 CBR 100+kbps, MP3 VBR 50+level
    int qualityId (const RecordSettings& s)
    {
        switch (s.format)
        {
            case RecordSettings::Format::flac: return s.flacBits == 16 ? 11 : 12;
            case RecordSettings::Format::mp3:  return s.mp3Mode == RecordSettings::Mp3Mode::cbr ? 100 + s.mp3Kbps : 50 + s.mp3Vbr;
            default: return s.wavFloat ? 4 : (s.wavBits == 16 ? 1 : s.wavBits == 32 ? 3 : 2);
        }
    }

    juce::String khz (int rate)
    {
        return rate % 1000 == 0 ? juce::String (rate / 1000) + " kHz" : juce::String (rate / 1000.0, 1) + " kHz";
    }
}

RecordingPanel::RecordingPanel (AudioEngine& e) : engine (e)
{
    const std::pair<juce::Label*, const char*> labels[] = {
        { &formatLabel, TRK ("Biçim") }, { &qualityLabel, TRK ("Kalite") }, { &levelLabel, TRK ("Sıkıştırma") },
        { &rateLabel, TRK ("Örnekleme hızı") }, { &channelLabel, TRK ("Kanal") }, { &folderLabel, TRK ("Klasör") } };
    for (auto& [l, text] : labels)
    {
        l->setText (tr (juce::String::fromUTF8 (text)), juce::dontSendNotification);
        l->setColour (juce::Label::textColourId, micvst::theme::textDim);
        addAndMakeVisible (l);
    }

    formatBox.addItem ("WAV", 1);
    formatBox.addItem ("FLAC", 2);
    formatBox.addItem ("MP3", 3);
    formatBox.setTooltip (tr ("WAV: kayıpsız, her programda açılır (önerilen). FLAC: kayıpsız ve yaklaşık yarı boyut. "
                              "MP3: kayıplı, en küçük dosya; paylaşmak için."));
    formatBox.onChange = [this]
    {
        auto s = engine.getRecordSettings();
        s.format = formatBox.getSelectedId() == 2 ? RecordSettings::Format::flac
                 : formatBox.getSelectedId() == 3 ? RecordSettings::Format::mp3 : RecordSettings::Format::wav;
        if (s.format == RecordSettings::Format::mp3 && ! RecordSettings::allowedRates (s.format, 0.0).contains (s.sampleRate))
            s.sampleRate = 48000;
        apply (s);
    };
    qualityBox.onChange = [this]
    {
        auto s = engine.getRecordSettings();
        const int id = qualityBox.getSelectedId();
        if (id >= 1 && id <= 4) { s.wavBits = id == 1 ? 16 : id == 2 ? 24 : 32; s.wavFloat = id == 4; }
        else if (id == 11 || id == 12) s.flacBits = id == 11 ? 16 : 24;
        else if (id >= 100) { s.mp3Mode = RecordSettings::Mp3Mode::cbr; s.mp3Kbps = id - 100; }
        else if (id >= 50) { s.mp3Mode = RecordSettings::Mp3Mode::vbr; s.mp3Vbr = id - 50; }
        apply (s);
    };
    for (int l = 0; l <= 8; ++l)
        levelBox.addItem (l == 0 ? "0 - " + tr ("en hızlı") : l == 5 ? "5 - " + tr ("varsayılan") : l == 8 ? "8 - " + tr ("en küçük dosya") : juce::String (l), l + 1);
    levelBox.setTooltip (tr ("FLAC her seviyede kayıpsızdır; yalnızca dosya boyutu ve işlemci süresi değişir."));
    levelBox.onChange = [this] { auto s = engine.getRecordSettings(); s.flacLevel = levelBox.getSelectedId() - 1; apply (s); };
    rateBox.setTooltip (tr ("44,1 kHz müzik/CD, 48 kHz video ve yayın standardıdır. Daha yüksek hızlar yalnızca mikrofon "
                            "o hızda çalışıyorsa sunulur: düşük hızdaki sesi yükseltmek kaliteye bir şey katmaz."));
    rateBox.onChange = [this]
    {
        const int i = rateBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (i, rateItems.size())) return;
        auto s = engine.getRecordSettings();
        s.sampleRate = rateItems[i];
        apply (s);
    };
    channelBox.addItem (tr ("Stereo"), 2);
    channelBox.addItem (tr ("Mono (konuşma için yeterli, yarı boyut)"), 1);
    channelBox.setTooltip (tr ("Mono: iki kanalın ortalaması. Mikrofon tek kanallıysa ses aynen korunur."));
    channelBox.onChange = [this] { auto s = engine.getRecordSettings(); s.channels = channelBox.getSelectedId() == 1 ? 1 : 2; apply (s); };
    for (auto* b : { &formatBox, &qualityBox, &levelBox, &rateBox, &channelBox })
        addAndMakeVisible (b);

    folderPath.setColour (juce::Label::textColourId, micvst::theme::text);
    folderPath.setMinimumHorizontalScale (0.6f);
    addAndMakeVisible (folderPath);
    folderBtn.setButtonText (tr ("Değiştir..."));
    folderBtn.onClick = [this] { chooseFolder(); };
    openBtn.setButtonText (tr ("Klasörü aç"));
    openBtn.onClick = [this]
    {
        const auto f = engine.getRecordSettings().folderOrDefault();
        f.createDirectory();
        f.startAsProcess();
    };
    defaultBtn.setButtonText (tr ("Varsayılan"));
    defaultBtn.setTooltip (RecordSettings::defaultFolder().getFullPathName());
    defaultBtn.onClick = [this] { auto s = engine.getRecordSettings(); s.folder = juce::File(); apply (s); };
    for (auto* b : { &folderBtn, &openBtn, &defaultBtn })
        addAndMakeVisible (b);

    info.setFont (juce::Font (juce::FontOptions (13.0f)));
    info.setColour (juce::Label::textColourId, micvst::theme::textDim);
    info.setJustificationType (juce::Justification::topLeft);
    info.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (info);

    refill();
    startTimer (500);
}

RecordingPanel::~RecordingPanel() { stopTimer(); }

void RecordingPanel::apply (const RecordSettings& s)
{
    if (engine.isRecording()) { refill(); return; }
    engine.setRecordSettings (s);
    engine.requestPersist();
    refill();
}

void RecordingPanel::chooseFolder()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Kayıt klasörünü seçin"), engine.getRecordSettings().folderOrDefault());
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc)
    {
        const auto dir = fc.getResult();
        if (dir == juce::File()) return;   // cancelled
        // writable? a test file, removed at once
        const auto probe = dir.getNonexistentChildFile ("micvst-yazma-denemesi", ".tmp");
        if (! dir.isDirectory() || ! probe.create())
        {
            juce::NativeMessageBox::showAsync (juce::MessageBoxOptions::makeOptionsOk (juce::MessageBoxIconType::WarningIcon,
                tr ("Kayıt"), tr ("Bu klasöre yazılamıyor (izin yok ya da salt okunur): ") + dir.getFullPathName(), tr ("Tamam")), [] (int) {});
            return;
        }
        probe.deleteFile();
        auto s = engine.getRecordSettings();
        s.folder = dir;
        apply (s);
    });
}

void RecordingPanel::refill()
{
    const auto& s = engine.getRecordSettings();
    const double rate = engine.getProcessingSampleRate();
    shownRate = rate;
    shownRecording = engine.isRecording();

    formatBox.setSelectedId (s.format == RecordSettings::Format::flac ? 2 : s.format == RecordSettings::Format::mp3 ? 3 : 1, juce::dontSendNotification);

    qualityBox.clear (juce::dontSendNotification);
    switch (s.format)
    {
        case RecordSettings::Format::wav:
            qualityBox.addItem ("16 bit PCM", 1);
            qualityBox.addItem ("24 bit PCM (" + tr ("önerilen") + ")", 2);
            qualityBox.addItem ("32 bit PCM", 3);
            qualityBox.addItem ("32 bit float", 4);
            qualityBox.setTooltip (tr ("24 bit: 144 dB dinamik aralık, her programda açılır (önerilen). 16 bit: CD kalitesi, "
                                       "titreşimli (dither) yuvarlama ile. 32 bit float: MicVST'nin iç sesi birebir, hiç kırpılmaz."));
            break;
        case RecordSettings::Format::flac:
            qualityBox.addItem ("16 bit", 11);
            qualityBox.addItem ("24 bit (" + tr ("önerilen") + ")", 12);
            qualityBox.setTooltip (tr ("FLAC kayıpsızdır: açılınca WAV ile bit bit aynıdır."));
            break;
        case RecordSettings::Format::mp3:
            for (int k : RecordSettings::mp3CbrRates())
                qualityBox.addItem (juce::String (k) + " kbps CBR" + (k == 320 ? " (" + tr ("önerilen") + ")" : juce::String()), 100 + k);
            qualityBox.addSeparator();
            qualityBox.addItem ("VBR V0 (~245 kbps, " + tr ("en iyi") + ")", 50);
            qualityBox.addItem ("VBR V2 (~190 kbps)", 52);
            qualityBox.addItem ("VBR V4 (~165 kbps)", 54);
            qualityBox.setTooltip (tr ("LAME 4.0. CBR: sabit bit hızı, her cihazda sorunsuz. VBR: değişken bit hızı, aynı "
                                       "kalitede daha küçük dosya."));
            break;
    }
    qualityBox.setSelectedId (qualityId (s), juce::dontSendNotification);

    const bool flac = s.format == RecordSettings::Format::flac;
    levelLabel.setVisible (flac);
    levelBox.setVisible (flac);
    levelBox.setSelectedId (s.flacLevel + 1, juce::dontSendNotification);

    rateBox.clear (juce::dontSendNotification);
    rateItems = RecordSettings::allowedRates (s.format, rate);
    const int effective = s.effectiveRate (rate);
    for (int i = 0; i < rateItems.size(); ++i)
    {
        auto text = khz (rateItems[i]);
        if (rateItems[i] == (int) std::lround (rate)) text << " (" << tr ("mikrofonla aynı") << ")";
        else text << " (" << tr ("dönüştürülür") << ")";
        rateBox.addItem (text, i + 1);
        if (rateItems[i] == effective) rateBox.setSelectedId (i + 1, juce::dontSendNotification);
    }
    channelBox.setSelectedId (s.channels == 1 ? 1 : 2, juce::dontSendNotification);

    folderPath.setText (s.folderOrDefault().getFullPathName(), juce::dontSendNotification);
    folderPath.setTooltip (s.folderOrDefault().getFullPathName());

    // size per minute and what the file will be
    juce::String size;
    const int ch = s.channels;
    switch (s.format)
    {
        case RecordSettings::Format::wav:
            size = juce::String ((double) effective * ch * s.wavBits / 8.0 * 60.0 / 1.0e6, 1) + " MB/" + tr ("dk");
            break;
        case RecordSettings::Format::flac:
            size = "~" + juce::String ((double) effective * ch * s.flacBits / 8.0 * 60.0 / 1.0e6 * 0.55, 1) + " MB/" + tr ("dk");
            break;
        case RecordSettings::Format::mp3:
            size = (s.mp3Mode == RecordSettings::Mp3Mode::cbr ? juce::String() : juce::String ("~"))
                   + juce::String ((s.mp3Mode == RecordSettings::Mp3Mode::cbr ? s.mp3Kbps : s.mp3Vbr == 0 ? 245 : s.mp3Vbr == 2 ? 190 : 165)
                                   * (ch == 1 ? 0.5 : 1.0) * 60.0 / 8.0 / 1000.0, 1) + " MB/" + tr ("dk");
            break;
    }
    auto used = s;
    used.sampleRate = effective;
    juce::String text = tr ("Kaydedilen: kabloya giden işlenmiş ses (karşı tarafın duyduğu). Dosya: ") + used.describe()
                        + "  ·  " + size + "\n";
    if (s.sampleRate != effective)
        text << tr ("Seçilen hız şu an kullanılamıyor (mikrofon ") << khz ((int) std::lround (rate)) << tr (" çalışıyor); kayıt ")
             << khz (effective) << tr (" olur.") << "\n";
    text << tr ("Dosya adı tarih ve saattir (MicVST_2026-10-21_21-58-03.wav); var olan bir kaydın üzerine asla yazılmaz.");
    if (shownRecording) text = tr ("Kayıt sürerken ayarlar kilitli; değişiklikler sonraki kayıtta geçerli olur.") + "\n" + text;
    info.setText (text, juce::dontSendNotification);

    for (auto* c : std::initializer_list<juce::Component*> { &formatBox, &qualityBox, &levelBox, &rateBox, &channelBox, &folderBtn, &defaultBtn })
        c->setEnabled (! shownRecording);
    resized();
}

void RecordingPanel::timerCallback()
{
    // the microphone's rate decides the rate list; a recording locks the controls
    if (std::abs (engine.getProcessingSampleRate() - shownRate) > 0.5 || engine.isRecording() != shownRecording)
        refill();
}

int RecordingPanel::preferredHeight (int width) const
{
    const int rows = levelBox.isVisible() ? 6 : 5;
    juce::AttributedString a;
    a.append (info.getText(), info.getFont());
    juce::TextLayout layout;
    layout.createLayout (a, (float) juce::jmax (100, width - 10));
    return rows * 36 + 34 + 40 + (int) std::ceil (layout.getHeight()) + 10;
}

void RecordingPanel::resized()
{
    const int w = getWidth();
    float widest = 0.0f;
    for (auto* l : { &formatLabel, &qualityLabel, &levelLabel, &rateLabel, &channelLabel, &folderLabel })
        widest = std::max (widest, juce::GlyphArrangement::getStringWidth (l->getFont(), l->getText()));
    const int lw = juce::jlimit (110, 170, juce::roundToInt (widest) + 16);
    const int bw = juce::jmin (330, w - lw);
    int y = 0;
    auto row = [&] (juce::Label& l, juce::ComboBox& b)
    {
        if (! b.isVisible()) return;
        l.setBounds (0, y, lw, 30);
        b.setBounds (lw, y, bw, 30);
        y += 36;
    };
    row (formatLabel, formatBox);
    row (qualityLabel, qualityBox);
    row (levelLabel, levelBox);
    row (rateLabel, rateBox);
    row (channelLabel, channelBox);

    folderLabel.setBounds (0, y, lw, 30);
    folderPath.setBounds (lw, y, w - lw, 30);
    y += 34;
    auto textW = [] (juce::TextButton& b) { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (15.0f)), b.getButtonText())) + 26; };
    // the three buttons beside the label column; if their texts do not fit there, from the left edge
    const int need = textW (folderBtn) + textW (openBtn) + textW (defaultBtn) + 12;
    int x = need <= w - lw ? lw : 0;
    const double squeeze = need > w - x ? (double) (w - x - 12) / (need - 12) : 1.0;
    for (auto* b : { &folderBtn, &openBtn, &defaultBtn })
    {
        const int bwid = (int) (textW (*b) * squeeze);
        b->setBounds (x, y, bwid, 30);
        x += bwid + 6;
    }
    y += 40;
    info.setBounds (0, y, w, juce::jmax (40, getHeight() - y));
}
