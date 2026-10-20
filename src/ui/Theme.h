#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// MicVST's night theme: very dark surfaces, soft (not pure white) text and one muted accent, so the
// window stays easy on the eyes in a dark room. Every colour of the interface comes from here.
// The level meters keep their meaning (green level, yellow peak, red over full scale), only a
// little less glaring.
namespace micvst::theme
{
    inline const juce::Colour background { 0xff0d0f12 };   // window
    inline const juce::Colour surface    { 0xff171a1f };   // combo boxes, buttons, text fields, lists
    inline const juce::Colour rowA       { 0xff14171b };   // chain rows, alternating
    inline const juce::Colour rowB       { 0xff181b20 };
    inline const juce::Colour menu       { 0xff14171b };   // pop-up menus, tooltips
    inline const juce::Colour outline    { 0xff2a2f37 };
    inline const juce::Colour text       { 0xffc5cad2 };   // soft white
    inline const juce::Colour textBright { 0xffe1e4e9 };   // names in the chain, selected items
    inline const juce::Colour textDim    { 0xff7b8390 };   // secondary information
    inline const juce::Colour accent     { 0xff2fa597 };   // muted teal: active controls
    inline const juce::Colour accentDim  { 0xff1c4f4a };   // selection, dragged row
    inline const juce::Colour warning    { 0xffd8893a };   // bypass on, hints
    inline const juce::Colour error      { 0xffd9534a };
    inline const juce::Colour meterBack  { 0xff07080a };
    inline const juce::Colour meterLevel { 0xff3db463 };   // green
    inline const juce::Colour meterPeak  { 0xffd6bf4b };   // yellow
    inline const juce::Colour meterOver  { 0xffe0473f };   // red

    class NightLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        NightLookAndFeel()
            : juce::LookAndFeel_V4 (juce::LookAndFeel_V4::ColourScheme {
                  background,   // windowBackground
                  surface,      // widgetBackground
                  menu,         // menuBackground
                  outline,      // outline
                  text,         // defaultText
                  surface,      // defaultFill
                  textBright,   // highlightedText
                  accentDim,    // highlightedFill
                  text })       // menuText
        {
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
            setColour (juce::TextButton::textColourOnId, background);
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
            setColour (juce::TooltipWindow::backgroundColourId, menu);
            setColour (juce::TooltipWindow::textColourId, text);
            setColour (juce::TooltipWindow::outlineColourId, outline);
            setColour (juce::Slider::backgroundColourId, outline);
            setColour (juce::Slider::trackColourId, accentDim);
            setColour (juce::Slider::thumbColourId, accent);
            setColour (juce::Slider::textBoxTextColourId, text);
            setColour (juce::Slider::textBoxBackgroundColourId, surface);
            setColour (juce::Slider::textBoxOutlineColourId, outline);
            setColour (juce::ScrollBar::thumbColourId, outline.brighter (0.25f));
            setColour (juce::ProgressBar::backgroundColourId, surface);
            setColour (juce::ProgressBar::foregroundColourId, accent);
            setColour (juce::AlertWindow::backgroundColourId, menu);
            setColour (juce::AlertWindow::textColourId, text);
            setColour (juce::AlertWindow::outlineColourId, outline);
        }
    };
}
