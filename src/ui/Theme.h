#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// MicVST's colour themes. The default is "Gece" (night): very dark surfaces, soft (not pure white)
// text and one muted accent, easy on the eyes in a dark room - exactly the colours of 1.2.2. The
// others (Tema, at the bottom of the window): Gece Yarısı (pure black), Kül (grey, Discord-like),
// Açık (light), Okyanus, Ametist, Orman, Gün Batımı, Yüksek Kontrast.
//
// Every colour of the interface comes from the variables below; applyTheme() sets them and the
// window is rebuilt (MainComponent reads them when it is created). The level meters keep their
// meaning in every theme (green level, yellow peak, red over full scale).
namespace micvst::theme
{
    struct Palette
    {
        const char* id;      // stored in config.xml - never change
        const char* name;    // Turkish (UTF-8)
        bool dark;           // native title bar dark / light
        juce::uint32 background, surface, rowA, rowB, menu, outline, text, textBright, textDim,
                     accent, accentDim, warning, error, meterBack, meterLevel, meterPeak, meterOver;
    };

    inline const std::vector<Palette>& palettes()
    {
        static const std::vector<Palette> list {
            //  id               name                  dark   background  surface     rowA        rowB        menu        outline     text        textBright  textDim     accent      accentDim   warning     error       meterBack   meterLevel  meterPeak   meterOver
            { "gece",            "Gece (varsayılan)",  true,  0xff0d0f12, 0xff171a1f, 0xff14171b, 0xff181b20, 0xff14171b, 0xff2a2f37, 0xffc5cad2, 0xffe1e4e9, 0xff7b8390, 0xff2fa597, 0xff1c4f4a, 0xffd8893a, 0xffd9534a, 0xff07080a, 0xff3db463, 0xffd6bf4b, 0xffe0473f },
            { "gece-yarisi",     "Gece Yarısı (siyah)", true, 0xff000000, 0xff0c0c0d, 0xff050505, 0xff0a0a0b, 0xff0c0c0d, 0xff232428, 0xffc8ccd2, 0xffe6e8ec, 0xff7c828c, 0xff3fb5a5, 0xff173d39, 0xffd8893a, 0xffd9534a, 0xff000000, 0xff3db463, 0xffd6bf4b, 0xffe0473f },
            { "kul",             "Kül",                true,  0xff313338, 0xff383a40, 0xff2b2d31, 0xff313338, 0xff2b2d31, 0xff1e1f22, 0xffdbdee1, 0xfff2f3f5, 0xff949ba4, 0xff5865f2, 0xff3c45a5, 0xfff0b232, 0xfff23f43, 0xff1e1f22, 0xff23a55a, 0xfff0b232, 0xfff23f43 },
            { "acik",            "Açık",               false, 0xfff2f3f5, 0xffffffff, 0xffffffff, 0xfff5f6f8, 0xffffffff, 0xffcfd3d8, 0xff2e3338, 0xff060607, 0xff676d75, 0xff1f8a7e, 0xffc4e6e0, 0xffb8650f, 0xffc9302c, 0xffdde0e4, 0xff2e9e55, 0xffc29510, 0xffd23b33 },
            { "okyanus",         "Okyanus",            true,  0xff0b1320, 0xff121c2c, 0xff0f1826, 0xff132033, 0xff101a29, 0xff223249, 0xffc4d2e4, 0xffe3ecf7, 0xff7489a3, 0xff3d8bfd, 0xff1b3a66, 0xffe0a03a, 0xffe0534a, 0xff060b13, 0xff3db463, 0xffd6bf4b, 0xffe0473f },
            { "ametist",         "Ametist",            true,  0xff15111d, 0xff1d1828, 0xff19141f, 0xff1e1826, 0xff1a1523, 0xff2f2740, 0xffd0c8de, 0xffece6f5, 0xff8a7fa0, 0xff9b6cf0, 0xff3d2a66, 0xffe0a03a, 0xffe0534a, 0xff0b0910, 0xff3db463, 0xffd6bf4b, 0xffe0473f },
            { "orman",           "Orman",              true,  0xff0e1411, 0xff151d18, 0xff111814, 0xff16201a, 0xff131a16, 0xff26332b, 0xffc6d4cb, 0xffe3ede6, 0xff7b8f82, 0xff4caf6e, 0xff1f4a2f, 0xffd8a03a, 0xffd9534a, 0xff070b09, 0xff3db463, 0xffd6bf4b, 0xffe0473f },
            { "gun-batimi",      "Gün Batımı",         true,  0xff1a1214, 0xff22181b, 0xff1d1417, 0xff231a1d, 0xff1f1619, 0xff3a2a2f, 0xffe0cfd2, 0xfff5e9eb, 0xff9b8287, 0xffef7d57, 0xff5a2e22, 0xffe0a43a, 0xffe0534a, 0xff0e090a, 0xff3db463, 0xffd6bf4b, 0xffe0473f },
            { "yuksek-kontrast", "Yüksek Kontrast",    true,  0xff000000, 0xff0d0d0d, 0xff000000, 0xff121212, 0xff0d0d0d, 0xffbfbfbf, 0xffffffff, 0xffffffff, 0xffd0d0d0, 0xffffd400, 0xff5c4d00, 0xffff9f1a, 0xffff4d4d, 0xff000000, 0xff00e05a, 0xffffd400, 0xffff3030 },
        };
        return list;
    }

    inline const Palette& paletteFor (const juce::String& id)
    {
        for (auto& p : palettes())
            if (id == p.id) return p;
        return palettes().front();   // unknown or empty: Gece
    }

    // The current colours (Gece until applyTheme() is called).
    inline juce::Colour background { 0xff0d0f12 };   // window
    inline juce::Colour surface    { 0xff171a1f };   // combo boxes, buttons, text fields, lists
    inline juce::Colour rowA       { 0xff14171b };   // chain rows, alternating
    inline juce::Colour rowB       { 0xff181b20 };
    inline juce::Colour menu       { 0xff14171b };   // pop-up menus, tooltips
    inline juce::Colour outline    { 0xff2a2f37 };
    inline juce::Colour text       { 0xffc5cad2 };   // soft white
    inline juce::Colour textBright { 0xffe1e4e9 };   // names in the chain, selected items
    inline juce::Colour textDim    { 0xff7b8390 };   // secondary information
    inline juce::Colour accent     { 0xff2fa597 };   // muted teal: active controls
    inline juce::Colour accentDim  { 0xff1c4f4a };   // selection, dragged row
    inline juce::Colour warning    { 0xffd8893a };   // bypass on, hints
    inline juce::Colour error      { 0xffd9534a };
    inline juce::Colour meterBack  { 0xff07080a };
    inline juce::Colour meterLevel { 0xff3db463 };   // green
    inline juce::Colour meterPeak  { 0xffd6bf4b };   // yellow
    inline juce::Colour meterOver  { 0xffe0473f };   // red
    inline bool dark = true;
    inline juce::String currentId { "gece" };

    // Sets the colour variables (message thread). The LookAndFeel and the windows follow with
    // NightLookAndFeel::refresh() and a rebuild of their content.
    inline void applyTheme (const juce::String& id)
    {
        const auto& p = paletteFor (id);
        background = juce::Colour (p.background); surface = juce::Colour (p.surface);
        rowA = juce::Colour (p.rowA); rowB = juce::Colour (p.rowB); menu = juce::Colour (p.menu);
        outline = juce::Colour (p.outline); text = juce::Colour (p.text); textBright = juce::Colour (p.textBright);
        textDim = juce::Colour (p.textDim); accent = juce::Colour (p.accent); accentDim = juce::Colour (p.accentDim);
        warning = juce::Colour (p.warning); error = juce::Colour (p.error); meterBack = juce::Colour (p.meterBack);
        meterLevel = juce::Colour (p.meterLevel); meterPeak = juce::Colour (p.meterPeak); meterOver = juce::Colour (p.meterOver);
        dark = p.dark;
        currentId = p.id;
    }

    // The LookAndFeel of every window (name kept from 1.2.2; it draws whichever theme is applied).
    class NightLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        NightLookAndFeel() { refresh(); }

        // Takes the current theme colours (after applyTheme).
        void refresh()
        {
            setColourScheme (juce::LookAndFeel_V4::ColourScheme {
                  background,   // windowBackground
                  surface,      // widgetBackground
                  menu,         // menuBackground
                  outline,      // outline
                  text,         // defaultText
                  surface,      // defaultFill
                  textBright,   // highlightedText
                  accentDim,    // highlightedFill
                  text });      // menuText
            setColour (juce::ResizableWindow::backgroundColourId, background);
            setColour (juce::DocumentWindow::textColourId, text);
            setColour (juce::ComboBox::backgroundColourId, surface);
            setColour (juce::ComboBox::outlineColourId, outline);
            setColour (juce::ComboBox::arrowColourId, textDim);
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::ComboBox::focusedOutlineColourId, accent);
            setColour (juce::TextButton::buttonColourId, surface);
            setColour (juce::TextButton::buttonOnColourId, accent);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, dark ? background : juce::Colours::white);
            setColour (juce::ToggleButton::textColourId, text);
            setColour (juce::ToggleButton::tickColourId, accent);
            setColour (juce::ToggleButton::tickDisabledColourId, textDim);
            setColour (juce::Label::textColourId, text);
            setColour (juce::TextEditor::backgroundColourId, surface);
            setColour (juce::TextEditor::textColourId, text);
            setColour (juce::TextEditor::outlineColourId, outline);
            setColour (juce::TextEditor::focusedOutlineColourId, accent);
            setColour (juce::TextEditor::highlightColourId, accentDim);
            setColour (juce::CaretComponent::caretColourId, accent);
            setColour (juce::ListBox::backgroundColourId, surface);
            setColour (juce::ListBox::outlineColourId, outline);
            setColour (juce::PopupMenu::backgroundColourId, menu);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, accentDim);
            setColour (juce::PopupMenu::highlightedTextColourId, textBright);
            setColour (juce::PopupMenu::headerTextColourId, textDim);
            setColour (juce::TooltipWindow::backgroundColourId, menu);
            setColour (juce::TooltipWindow::textColourId, text);
            setColour (juce::TooltipWindow::outlineColourId, outline);
            setColour (juce::Slider::backgroundColourId, outline);
            setColour (juce::Slider::trackColourId, accentDim);
            setColour (juce::Slider::thumbColourId, accent);
            setColour (juce::Slider::textBoxTextColourId, text);
            setColour (juce::Slider::textBoxBackgroundColourId, surface);
            setColour (juce::Slider::textBoxOutlineColourId, outline);
            setColour (juce::ScrollBar::thumbColourId, dark ? outline.brighter (0.25f) : outline.darker (0.2f));
            setColour (juce::ProgressBar::backgroundColourId, surface);
            setColour (juce::ProgressBar::foregroundColourId, accent);
            setColour (juce::AlertWindow::backgroundColourId, menu);
            setColour (juce::AlertWindow::textColourId, text);
            setColour (juce::AlertWindow::outlineColourId, outline);
        }
    };
}
