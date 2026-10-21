#include "ui/HotkeyDialog.h"
#include "ui/Theme.h"
#include "Text.h"

using micvst::tr;

juce::String HotkeyDialog::actionName (GlobalHotkeys::Action a)
{
    return a == GlobalHotkeys::mute ? tr ("Sustur / sesi aç") : tr ("Ham ses (önce / sonra)");
}

HotkeyDialog::HotkeyDialog (GlobalHotkeys& k, Setter s) : keys (k), setBinding (std::move (s))
{
    for (int i = 0; i < GlobalHotkeys::numActions; ++i)
    {
        const auto a = (GlobalHotkeys::Action) i;
        auto& l = lines[i];
        l.enabled.setButtonText (tr ("Etkin"));
        l.enabled.onClick = [this, a, &l]
        {
            auto b = keys.get (a);
            b.enabled = l.enabled.getToggleState();
            apply (a, b);
        };
        l.name.setText (actionName (a), juce::dontSendNotification);
        l.key.setTooltip (tr ("Tıklayın, sonra yeni tuşa (Ctrl / Alt / Shift ile de) basın; Esc vazgeçer"));
        l.key.onClick = [this, i]
        {
            if (capturing < 0) keys.suspend (true);
            capturing = i;
            refresh();
            grabKeyboardFocus();
        };
        l.reset.setButtonText (tr ("Varsayılan"));
        l.reset.onClick = [this, a] { endCapture(); apply (a, GlobalHotkeys::defaultFor (a)); };
        l.problem.setColour (juce::Label::textColourId, micvst::theme::error);
        l.problem.setFont (juce::Font (juce::FontOptions (13.0f)));
        for (juce::Component* c : { (juce::Component*) &l.enabled, (juce::Component*) &l.name, (juce::Component*) &l.key,
                                    (juce::Component*) &l.reset, (juce::Component*) &l.problem })
            addAndMakeVisible (c);
    }
    hint.setText (tr ("Kısayollar MicVST arka plandayken de çalışır. Etkin bir kısayolun tuşu, MicVST açıkken "
                      "başka programlara gitmez - gerekirse başka bir tuş seçin ya da kapatın."),
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, micvst::theme::textDim);
    hint.setFont (juce::Font (juce::FontOptions (13.0f)));
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);
    setWantsKeyboardFocus (true);
    refresh();
    setSize (520, 30 + GlobalHotkeys::numActions * 58 + 50);
}

HotkeyDialog::~HotkeyDialog() { endCapture(); }

void HotkeyDialog::endCapture()
{
    if (capturing >= 0) keys.suspend (false);
    capturing = -1;
}

void HotkeyDialog::apply (GlobalHotkeys::Action a, const GlobalHotkeys::Binding& b)
{
    const bool ok = setBinding (a, b);
    lines[a].problem.setText (ok ? juce::String() : tr ("Bu tuş kullanılamıyor: başka bir program almış olabilir. Başka bir tuş seçin."),
                              juce::dontSendNotification);
    refresh();
}

void HotkeyDialog::refresh()
{
    for (int i = 0; i < GlobalHotkeys::numActions; ++i)
    {
        const auto& b = keys.get ((GlobalHotkeys::Action) i);
        auto& l = lines[i];
        l.enabled.setToggleState (b.enabled, juce::dontSendNotification);
        l.key.setButtonText (capturing == i ? tr ("Bir tuşa basın...") : GlobalHotkeys::describe (b));
        l.key.setToggleState (capturing == i, juce::dontSendNotification);
        l.key.setColour (juce::TextButton::buttonOnColourId, micvst::theme::accentDim);
    }
}

bool HotkeyDialog::keyPressed (const juce::KeyPress& k)
{
    if (capturing < 0) return false;
    const auto a = (GlobalHotkeys::Action) capturing;
    endCapture();
    if (k == juce::KeyPress::escapeKey) { refresh(); return true; }
    const int code = k.getKeyCode();
    // modifier keys alone are no shortcut
    if (code == 0 || code == juce::KeyPress::escapeKey) { refresh(); return true; }
    GlobalHotkeys::Binding b;
    b.enabled = true;
    b.keyCode = code;
    b.modifiers = k.getModifiers().getRawFlags()
                  & (juce::ModifierKeys::shiftModifier | juce::ModifierKeys::ctrlModifier | juce::ModifierKeys::altModifier);
    apply (a, b);
    return true;
}

void HotkeyDialog::paint (juce::Graphics& g)
{
    g.fillAll (micvst::theme::background);
}

void HotkeyDialog::resized()
{
    auto r = getLocalBounds().reduced (12);
    for (auto& l : lines)
    {
        auto row = r.removeFromTop (28);
        l.enabled.setBounds (row.removeFromLeft (90));
        l.reset.setBounds (row.removeFromRight (100));
        row.removeFromRight (6);
        l.key.setBounds (row.removeFromRight (140));
        row.removeFromRight (6);
        l.name.setBounds (row);
        l.problem.setBounds (r.removeFromTop (22));
        r.removeFromTop (8);
    }
    hint.setBounds (r);
}
