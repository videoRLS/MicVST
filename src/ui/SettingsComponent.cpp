#include "ui/SettingsComponent.h"
#include "ui/Theme.h"
#include "state/AutostartRegistry.h"
#include "Text.h"

using micvst::tr;

namespace
{
    constexpr int navW = 172, pad = 18, headH = 26, sectionGap = 22;
    const char* const sectionNames[] = { TRK ("Ses"), TRK ("Kayıt"), TRK ("Kısayollar"), TRK ("Bildirim sesleri"), TRK ("Görünüm ve başlangıç"), TRK ("Eklentiler"), TRK ("Hakkında") };
    constexpr int numSections = 7;
}

juce::String SettingsComponent::windowTitle() { return tr ("MicVST - Ayarlar"); }

void SettingsComponent::Page::paint (juce::Graphics& g)
{
    g.fillAll (micvst::theme::background);
    g.setColour (micvst::theme::outline);
    for (auto& l : lines) g.fillRect (l);
}

SettingsComponent::SettingsComponent (AudioEngine& e, GlobalHotkeys* keys, Callbacks callbacks)
    : engine (e), cb (std::move (callbacks)), soundPanel (e, false), recordingPanel (e), cuePanel (e)
{
    for (int i = 0; i < numSections; ++i)
    {
        auto* b = navButtons.add (new juce::TextButton (tr (juce::String::fromUTF8 (sectionNames[i]))));
        b->setColour (juce::TextButton::buttonColourId, juce::Colours::transparentBlack);
        b->setColour (juce::TextButton::buttonOnColourId, micvst::theme::accentDim);
        b->setColour (juce::TextButton::textColourOnId, micvst::theme::textBright);
        b->setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
        b->setRadioGroupId (77);
        b->setClickingTogglesState (true);
        b->onClick = [this, i] { scrollTo (i); };
        addAndMakeVisible (b);
        auto* h = headings.add (new juce::Label ({}, tr (juce::String::fromUTF8 (sectionNames[i]))));
        h->setFont (juce::Font (juce::FontOptions (16.0f, juce::Font::bold)));
        h->setColour (juce::Label::textColourId, micvst::theme::textBright);
        page.addAndMakeVisible (h);
    }
    navButtons[0]->setToggleState (true, juce::dontSendNotification);

    page.addAndMakeVisible (soundPanel);
    page.addAndMakeVisible (recordingPanel);
    page.addAndMakeVisible (cuePanel);

    if (keys != nullptr && GlobalHotkeys::supported() && cb.setHotkey)
    {
        hotkeys = std::make_unique<HotkeyDialog> (*keys, cb.setHotkey);
        page.addAndMakeVisible (*hotkeys);
    }
    else
    {
        noHotkeys.setText (tr ("Kısayollar MicVST arka plandayken de çalışır. Etkin bir kısayolun tuşu, MicVST açıkken "
                               "başka programlara gitmez - gerekirse başka bir tuş seçin ya da kapatın."), juce::dontSendNotification);
        noHotkeys.setColour (juce::Label::textColourId, micvst::theme::textDim);
        page.addAndMakeVisible (noHotkeys);
    }

    languageLabel.setText (tr ("Dil"), juce::dontSendNotification);
    themeLabel.setText (tr ("Tema"), juce::dontSendNotification);
    for (auto* l : { &languageLabel, &themeLabel })
    {
        l->setColour (juce::Label::textColourId, micvst::theme::textDim);
        page.addAndMakeVisible (l);
    }
    {
        int id = 1;
        for (auto& l : micvst::languages())
        {
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
        if (juce::isPositiveAndBelow (idx, (int) list.size()) && cb.chooseLanguage)
            cb.chooseLanguage (list[(size_t) idx].id);
    };
    {
        int id = 1;
        for (auto& p : micvst::theme::palettes())
        {
            themeBox.addItem (tr (juce::String::fromUTF8 (p.name)), id);
            if (micvst::theme::currentId == p.id) themeBox.setSelectedId (id, juce::dontSendNotification);
            ++id;
        }
    }
    themeBox.setTooltip (tr ("Pencerenin renk teması. Varsayılan: Gece."));
    themeBox.onChange = [this]
    {
        const int idx = themeBox.getSelectedId() - 1;
        const auto& list = micvst::theme::palettes();
        if (juce::isPositiveAndBelow (idx, (int) list.size()) && cb.chooseTheme)
            cb.chooseTheme (list[(size_t) idx].id);
    };
    page.addAndMakeVisible (languageBox);
    page.addAndMakeVisible (themeBox);

    autostartToggle.setButtonText (tr ("Windows açılışında başlat"));
    autostartToggle.setTooltip (tr ("Windows açılınca MicVST'yi pencere açmadan, simge olarak başlatır"));
    autostartToggle.setToggleState (AutostartRegistry::isEnabled(), juce::dontSendNotification);
    autostartToggle.onClick = [this] { AutostartRegistry::setEnabled (autostartToggle.getToggleState()); };
    page.addAndMakeVisible (autostartToggle);

    foldersBtn.setButtonText (tr ("VST3 klasörleri ve tarama..."));
    foldersBtn.onClick = [this] { if (cb.showFolderMenu) cb.showFolderMenu (&foldersBtn); };
    page.addAndMakeVisible (foldersBtn);
    foldersHint.setText (tr ("Başka konumlardaki VST3 eklentilerini ekleyin, tüm eklentileri yeniden tarayın ya da programı sıfırlayın."),
                         juce::dontSendNotification);
    foldersHint.setColour (juce::Label::textColourId, micvst::theme::textDim);
    foldersHint.setJustificationType (juce::Justification::topLeft);
    for (auto* l : { &foldersHint, &noHotkeys, &aboutLine })
    {
        l->setFont (juce::Font (juce::FontOptions (13.0f)));
        l->setMinimumHorizontalScale (1.0f);
    }
    page.addAndMakeVisible (foldersHint);

    aboutLine.setText ("MicVST v" + juce::String (MICVST_VERSION_STRING) + "  ·  "
                       + tr ("MicVST tamamen çevrimdışı çalışır (internete bağlanmaz)"), juce::dontSendNotification);
    aboutLine.setColour (juce::Label::textColourId, micvst::theme::textDim);
    howToBtn.setButtonText (tr ("Kullanım"));
    howToBtn.onClick = [this] { if (cb.showHowTo) cb.showHowTo(); };
    for (auto* c : { (juce::Component*) &aboutLine, (juce::Component*) &howToBtn })
        page.addAndMakeVisible (c);

    viewport.setViewedComponent (&page, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);
    setSize (640, 600);
    startTimer (250);
}

SettingsComponent::~SettingsComponent() { stopTimer(); }

void SettingsComponent::timerCallback()
{
    updateNav();
    autostartToggle.setToggleState (AutostartRegistry::isEnabled(), juce::dontSendNotification);
    if (soundPanel.getHeight() != soundPanel.preferredHeight()   // the buffer row came or went (device change)
        || recordingPanel.getHeight() != recordingPanel.preferredHeight (recordingPanel.getWidth())   // FLAC row, notes
        || cuePanel.getHeight() != cuePanel.preferredHeight (cuePanel.getWidth()))                  // its notes
        resized();
}

void SettingsComponent::paint (juce::Graphics& g)
{
    g.fillAll (micvst::theme::background);
    g.setColour (micvst::theme::rowA);
    g.fillRect (0, 0, navW, getHeight());
    g.setColour (micvst::theme::outline);
    g.fillRect (navW, 0, 1, getHeight());
}

void SettingsComponent::scrollTo (int section)
{
    if (juce::isPositiveAndBelow (section, (int) sectionY.size()))
        viewport.setViewPosition (0, juce::jmin (sectionY[(size_t) section], page.getHeight() - viewport.getHeight()));
    updateNav();
}

int SettingsComponent::sectionAt (int y) const
{
    int s = 0;
    for (int i = 0; i < (int) sectionY.size(); ++i)
        if (y + 4 >= sectionY[(size_t) i]) s = i;
    return s;
}

SettingsComponent::ViewState SettingsComponent::getViewState() const
{
    ViewState v;
    const int y = viewport.getViewPositionY();
    v.section = sectionAt (y);
    v.offset = sectionY.empty() ? y : y - sectionY[(size_t) v.section];
    return v;
}

void SettingsComponent::setViewState (ViewState v)
{
    if (! juce::isPositiveAndBelow (v.section, (int) sectionY.size())) return;
    viewport.setViewPosition (0, juce::jlimit (0, juce::jmax (0, page.getHeight() - viewport.getHeight()),
                                               sectionY[(size_t) v.section] + juce::jmax (0, v.offset)));
    updateNav();
}

void SettingsComponent::updateNav()
{
    const int s = sectionAt (viewport.getViewPositionY());
    for (int i = 0; i < navButtons.size(); ++i)
        if (navButtons[i]->getToggleState() != (i == s))
            navButtons[i]->setToggleState (i == s, juce::dontSendNotification);
}

void SettingsComponent::resized()
{
    // a new layout (window resized, a row came or went) keeps the section in view
    const bool laidOut = ! sectionY.empty();
    const auto keep = laidOut ? getViewState() : ViewState{};
    auto nav = juce::Rectangle<int> (0, 0, navW, getHeight()).reduced (10, 14);
    for (auto* b : navButtons)
    {
        b->setBounds (nav.removeFromTop (34));
        nav.removeFromTop (4);
    }

    viewport.setBounds (getLocalBounds().withTrimmedLeft (navW + 1));
    const int w = viewport.getWidth() - viewport.getScrollBarThickness() - 2 * pad;
    int y = pad;
    sectionY.clear();
    page.lines.clear();
    auto heading = [&] (int i)
    {
        if (i > 0)
        {
            page.lines.push_back ({ pad, y, w, 1 });
            y += sectionGap;
        }
        sectionY.push_back (juce::jmax (0, y - pad));
        headings[i]->setBounds (pad - 4, y, w, headH);
        y += headH + 8;
    };
    auto line = [&] (juce::Component& c, int h)
    {
        c.setBounds (pad, y, w, h);
        y += h + 8;
    };

    heading (0);
    soundPanel.setSize (w, soundPanel.preferredHeight());
    line (soundPanel, soundPanel.preferredHeight());

    heading (1);
    {
        recordingPanel.setSize (w, 10);
        const int h = recordingPanel.preferredHeight (w);
        line (recordingPanel, h);
    }

    heading (2);
    if (hotkeys != nullptr) line (*hotkeys, hotkeys->preferredHeight (w));
    else                    line (noHotkeys, 48);

    heading (3);
    {
        cuePanel.setSize (w, 10);
        line (cuePanel, cuePanel.preferredHeight (w));
    }

    heading (4);
    {
        const int lw = juce::jmax (90, juce::roundToInt (juce::jmax (juce::GlyphArrangement::getStringWidth (languageLabel.getFont(), languageLabel.getText()),
                                                                     juce::GlyphArrangement::getStringWidth (themeLabel.getFont(), themeLabel.getText()))) + 16);
        languageLabel.setBounds (pad, y, lw, 30);
        languageBox.setBounds (pad + lw, y, juce::jmin (260, w - lw), 30);
        y += 36;
        themeLabel.setBounds (pad, y, lw, 30);
        themeBox.setBounds (pad + lw, y, juce::jmin (260, w - lw), 30);
        y += 38;
        line (autostartToggle, 26);
    }

    heading (5);
    {
        const int bw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (15.0f)), foldersBtn.getButtonText())) + 30;
        foldersBtn.setBounds (pad, y, juce::jmin (w, bw), 32);
        y += 38;
        line (foldersHint, 48);
    }

    heading (6);
    aboutLine.setJustificationType (juce::Justification::topLeft);
    line (aboutLine, 36);
    {
        const int bw = juce::roundToInt (juce::GlyphArrangement::getStringWidth (juce::Font (juce::FontOptions (15.0f)), howToBtn.getButtonText())) + 30;
        howToBtn.setBounds (pad, y, bw, 30);
        y += 40;
    }

    // the last section can still scroll to the top
    page.setSize (viewport.getWidth() - viewport.getScrollBarThickness(), juce::jmax (y + pad, sectionY.back() + viewport.getHeight()));
    if (laidOut) setViewState (keep);
}
