#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>
#include "Text.h"

// MicVST's colour themes. The default is "Gece" (night): very dark surfaces, soft (not pure white)
// text and one muted accent, easy on the eyes in a dark room - exactly the colours of 1.2.2. The
// others (Tema, at the bottom of the window): Gece Yarısı (pure black), Kül (grey, Discord-like),
// Açık (light), Okyanus, Ametist, Orman, Gün Batımı, Yüksek Kontrast.
//
// Every colour of the interface comes from the variables below - window, lists, buttons, menus,
// tooltips, the level meters, the "Devre dışı" switch, the effect windows and meters; applyTheme()
// sets them and the window is rebuilt (MainComponent reads them when it is created). Each theme
// tints the meters in its own colours; red always means over full scale.
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
            { "gece",            TRK ("Gece (varsayılan)"),  true,  0xff0d0f12, 0xff171a1f, 0xff14171b, 0xff181b20, 0xff14171b, 0xff2a2f37, 0xffc5cad2, 0xffe1e4e9, 0xff7b8390, 0xff2fa597, 0xff1c4f4a, 0xffd8893a, 0xffd9534a, 0xff07080a, 0xff3db463, 0xffd6bf4b, 0xffe0473f },
            { "gece-yarisi",     TRK ("Gece Yarısı (siyah)"), true, 0xff000000, 0xff0c0c0d, 0xff040404, 0xff0b0b0c, 0xff0c0c0d, 0xff26272b, 0xffc8ccd2, 0xffe6e8ec, 0xff7c828c, 0xff39c5b3, 0xff123a36, 0xffe0913f, 0xffe5534b, 0xff000000, 0xff2fc7a8, 0xffe3cf55, 0xffff4a3d },
            { "kul",             TRK ("Kül"),                true,  0xff313338, 0xff3a3c42, 0xff2b2d31, 0xff313338, 0xff232428, 0xff1e1f22, 0xffdbdee1, 0xfff2f3f5, 0xff949ba4, 0xff5865f2, 0xff3f479e, 0xfff0b232, 0xfff23f43, 0xff1e1f22, 0xff23a55a, 0xfff0b232, 0xfff23f43 },
            { "acik",            TRK ("Aydınlık"),           false, 0xfff2f3f5, 0xffffffff, 0xffffffff, 0xffeef0f3, 0xffffffff, 0xffc9ced5, 0xff2e3338, 0xff060607, 0xff60666f, 0xff0f7c70, 0xffbfe4dd, 0xffc2620a, 0xffc9302c, 0xffd8dce1, 0xff21995a, 0xffd1a10f, 0xffd8352d },
            { "okyanus",         TRK ("Okyanus"),            true,  0xff0a1422, 0xff112036, 0xff0d1a2b, 0xff12233a, 0xff0e1b2d, 0xff21385a, 0xffc2d4ea, 0xffe4eef9, 0xff7090b3, 0xff3d9bff, 0xff17406f, 0xff29c4d6, 0xffff5f6b, 0xff050c16, 0xff2fb8f0, 0xff8fe3ff, 0xffff4d5e },
            { "ametist",         TRK ("Ametist"),            true,  0xff16101f, 0xff211832, 0xff1a1326, 0xff21182f, 0xff1c1429, 0xff36294d, 0xffd6cbe8, 0xfff0e8fb, 0xff9283ad, 0xffa874ff, 0xff432c74, 0xffff7eb6, 0xffff5470, 0xff0b0711, 0xffb57bff, 0xffffa6e0, 0xffff4d6d },
            { "orman",           TRK ("Orman"),              true,  0xff0c1510, 0xff142219, 0xff101b14, 0xff16261c, 0xff111d16, 0xff27402f, 0xffc4d8c9, 0xffe2f0e6, 0xff7a947f, 0xff52b874, 0xff1d4a2c, 0xffd8b23a, 0xffe0574a, 0xff060c08, 0xff5cc46e, 0xffd9e05a, 0xffe8503f },
            { "gun-batimi",      TRK ("Gün Batımı"),         true,  0xff1b1113, 0xff27181c, 0xff201417, 0xff281a1e, 0xff221518, 0xff45292f, 0xffe6d0d3, 0xfff8eaec, 0xffa1848a, 0xffff8357, 0xff5e2a1f, 0xffffc04d, 0xffff4f5e, 0xff0f0809, 0xffff9a4a, 0xffffd166, 0xffff3f5a },
            { "yuksek-kontrast", TRK ("Yüksek Kontrast"),    true,  0xff000000, 0xff0d0d0d, 0xff000000, 0xff141414, 0xff0d0d0d, 0xffbfbfbf, 0xffffffff, 0xffffffff, 0xffd0d0d0, 0xffffd400, 0xff5c4d00, 0xff00e5ff, 0xffff4d4d, 0xff000000, 0xff00e05a, 0xffffd400, 0xffff3030 },
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
