#include "ui/CuePanel.h"
#include "ui/Theme.h"
#include "Text.h"

using micvst::tr;

CuePanel::CuePanel (AudioEngine& e) : engine (e)
{
    enabledToggle.setButtonText (tr ("Mikrofon açılıp kapanınca ses çal"));
    enabledToggle.setTooltip (tr ("Sustur düğmesi, kısayol ve simge menüsü: mikrofon açılınca ve kapanınca kısa bir ses. "
                                  "Yalnızca sizin kulaklığınızdan / hoparlörünüzden çalar; kabloya gitmez, karşı taraf duymaz."));
    enabledToggle.onClick = [this] { auto s = engine.getCues().getSettings(); s.enabled = enabledToggle.getToggleState(); apply (s); };
    addAndMakeVisible (enabledToggle);

    onLabel.setText (tr ("Açılınca"), juce::dontSendNotification);
    offLabel.setText (tr ("Kapanınca"), juce::dontSendNotification);
    volumeLabel.setText (tr ("Ses düzeyi"), juce::dontSendNotification);
    for (auto* l : { &onLabel, &offLabel, &volumeLabel })
    {
        l->setColour (juce::Label::textColourId, micvst::theme::textDim);
        addAndMakeVisible (l);
    }

    onBox.onChange = [this]
    {
        const int i = onBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (i, ids.size())) return;
        auto s = engine.getCues().getSettings(); s.onId = ids[i]; apply (s);
        engine.getCues().preview (ids[i]);
    };
    offBox.onChange = [this]
    {
        const int i = offBox.getSelectedId() - 1;
        if (! juce::isPositiveAndBelow (i, ids.size())) return;
        auto s = engine.getCues().getSettings(); s.offId = ids[i]; apply (s);
        engine.getCues().preview (ids[i]);
    };
    for (auto* b : { &onBox, &offBox }) addAndMakeVisible (b);

    for (auto* b : { &onPlay, &offPlay })
    {
        b->setButtonText (juce::String (juce::CharPointer_UTF8 ("\xe2\x96\xb6")));   // a play triangle
        b->setTooltip (tr ("Dinle"));
        addAndMakeVisible (b);
    }
    onPlay.onClick  = [this] { engine.getCues().preview (engine.getCues().getSettings().onId); };
    offPlay.onClick = [this] { engine.getCues().preview (engine.getCues().getSettings().offId); };

    volume.setSliderStyle (juce::Slider::LinearHorizontal);
    volume.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 24);
    volume.setRange (0.0, 100.0, 1.0);
    volume.setTextValueSuffix (" %");
    volume.onDragEnd = [this] { engine.getCues().preview (engine.getCues().getSettings().offId); };
    volume.onValueChange = [this]
    {
        auto s = engine.getCues().getSettings();
        if (s.volume == (int) volume.getValue()) return;
        s.volume = (int) volume.getValue(); apply (s);
    };
    addAndMakeVisible (volume);

    defaultBtn.setButtonText (tr ("Varsayılana dön"));
    defaultBtn.setTooltip (tr ("Ahşap açılma ve Ahşap kapanma sesleri, ses düzeyi %40"));
    defaultBtn.onClick = [this] { apply (CueSettings{}); };
    folderBtn.setButtonText (tr ("Ses klasörünü aç"));
    folderBtn.onClick = [this]
    {
        const auto f = engine.getCues().library().getUserFolder();
        f.createDirectory();
        f.startAsProcess();
    };
    for (auto* b : { &defaultBtn, &folderBtn }) addAndMakeVisible (b);

    hint.setFont (juce::Font (juce::FontOptions (13.0f)));
    hint.setColour (juce::Label::textColourId, micvst::theme::textDim);
    hint.setJustificationType (juce::Justification::topLeft);
    hint.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (hint);

    refill();
    startTimer (2000);
}

CuePanel::~CuePanel() { stopTimer(); }

void CuePanel::apply (const CueSettings& s)
{
    engine.getCues().setSettings (s);
    engine.requestPersist();
    refill();
}

void CuePanel::refill()
{
    auto& cues = engine.getCues();
    const auto& s = cues.getSettings();
    const auto list = cues.library().list();

    juce::String key;
    for (auto& c : list) key << c.id << "\n";
    if (key != shownList)
    {
        shownList = key;
        ids.clear();
        for (auto* box : { &onBox, &offBox })
        {
            box->clear (juce::dontSendNotification);
            bool userHeading = false;
            box->addSectionHeading (tr ("MicVST sesleri"));
            for (int i = 0; i < list.size(); ++i)
            {
                if (! list[i].builtIn && ! userHeading) { box->addSectionHeading (tr ("Eklediğiniz sesler")); userHeading = true; }
                box->addItem (list[i].name, i + 1);
            }
        }
        for (auto& c : list) ids.add (c.id);
    }
    const auto select = [&] (juce::ComboBox& box, const juce::String& id, const juce::String& fallback)
    {
        int i = ids.indexOf (id);
        if (i < 0) i = ids.indexOf (fallback);   // the chosen file is gone: the default plays
        box.setSelectedId (i + 1, juce::dontSendNotification);
    };
    select (onBox, s.onId, CueSettings::defaultOnId());
    select (offBox, s.offId, CueSettings::defaultOffId());
    enabledToggle.setToggleState (s.enabled, juce::dontSendNotification);
    volume.setValue (s.volume, juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> { &onBox, &offBox, &onPlay, &offPlay, &volume })
        c->setEnabled (s.enabled);

    juce::String text = tr ("Kendi seslerinizi eklemek için dosyaları ses klasörüne koyun (WAV, FLAC, MP3, AIFF, OGG; en çok 10 saniye): "
                            "listede kendiliğinden görünürler. Klasör: ") + cues.library().getUserFolder().getFullPathName();
    if (cues.library().skippedFiles() > 0)
        text << "\n" << tr ("Klasörde kullanılamayan dosya: ") << juce::String (cues.library().skippedFiles())
             << tr (" (okunamıyor ya da 10 saniyeden uzun)");
    if (cues.lastProblem().isNotEmpty()) text << "\n" << cues.lastProblem();
    hint.setText (text, juce::dontSendNotification);
}

void CuePanel::timerCallback()
{
    refill();   // new / removed files in the folder, a problem to show (Settings re-lays out on a height change)
}

int CuePanel::preferredHeight (int width) const
{
    juce::AttributedString a;
    a.append (hint.getText(), hint.getFont());
    juce::TextLayout layout;
    layout.createLayout (a, (float) juce::jmax (100, width - 10));
    return 30 + 4 * 36 + 40 + (int) std::ceil (layout.getHeight()) + 10;
}

void CuePanel::resized()
{
    const int w = getWidth();
    float widest = 0.0f;
    for (auto* l : { &onLabel, &offLabel, &volumeLabel })
        widest = std::max (widest, juce::GlyphArrangement::getStringWidth (l->getFont(), l->getText()));
    const int lw = juce::jlimit (90, 170, juce::roundToInt (widest) + 16);
    int y = 0;
    enabledToggle.setBounds (0, y, w, 26);
    y += 32;
    const int playW = 36, boxW = juce::jmin (330, w - lw - playW - 6);
    onLabel.setBounds (0, y, lw, 30);  onBox.setBounds (lw, y, boxW, 30);  onPlay.setBounds (lw + boxW + 6, y, playW, 30);  y += 36;
    offLabel.setBounds (0, y, lw, 30); offBox.setBounds (lw, y, boxW, 30); offPlay.setBounds (lw + boxW + 6, y, playW, 30); y += 36;
    volumeLabel.setBounds (0, y, lw, 30); volume.setBounds (lw, y, boxW + playW + 6, 30); y += 36;
    auto textW = [] (juce::TextButton& b) { return juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (15.0f)), b.getButtonText())) + 26; };
    const int need = textW (defaultBtn) + textW (folderBtn) + 6;
    int x = need <= w - lw ? lw : 0;
    defaultBtn.setBounds (x, y, textW (defaultBtn), 30); x += textW (defaultBtn) + 6;
    folderBtn.setBounds (x, y, juce::jmin (textW (folderBtn), w - x), 30);
    y += 40;
    hint.setBounds (0, y, w, juce::jmax (40, getHeight() - y));
}
