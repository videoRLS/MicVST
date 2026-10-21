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
        l.enabled.setButtonText ({});
        l.enabled.setTooltip (tr ("Etkin"));
        l.enabled.setTitle (tr ("Etkin"));
        l.enabled.onClick = [this, a, &l]
        {
            auto b = keys.get (a);
            b.enabled = l.enabled.getToggleState();
            apply (a, b);
        };
        l.name.setText (actionName (a), juce::dontSendNotification);
        l.key.setTooltip (tr ("Tıklayın, sonra yeni tuşa ya da fare tuşuna basın (Ctrl / Alt / Shift ile de); "
                              "Esc ya da yeniden tıklamak vazgeçer"));
        l.key.onClick = [this, i]
        {
            if (capturing == i) { endCapture(); refresh(); return; }   // clicked again: cancel
            if (capturing < 0)
            {
                keys.suspend (true);
                juce::Component::SafePointer<HotkeyDialog> self (this);
                keys.startMouseCapture ([self] (int mouse, int mods)
                {
                    if (self != nullptr) self->mouseCaptured (mouse, mods);
                });
            }
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
    hint.setText (tr ("Kısayol bir klavye tuşu (Ctrl / Alt / Shift ile de), orta fare tuşu, yan fare tuşları "
                      "(4 / 5) ya da Ctrl / Alt / Shift ile fare tekerleği olabilir. Kısayollar MicVST arka "
                      "plandayken de çalışır; etkin bir kısayolun tuşu MicVST açıkken başka programlara gitmez - "
                      "gerekirse başka bir tuş seçin ya da kapatın."),
                  juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, micvst::theme::textDim);
    hint.setFont (juce::Font (juce::FontOptions (13.0f)));
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);
    setWantsKeyboardFocus (true);
    refresh();
    setSize (448, preferredHeight (448));
}

HotkeyDialog::~HotkeyDialog() { endCapture(); }

void HotkeyDialog::endCapture()
{
    if (capturing >= 0)
    {
        keys.stopMouseCapture();
        keys.suspend (false);
    }
    capturing = -1;
}

void HotkeyDialog::apply (GlobalHotkeys::Action a, const GlobalHotkeys::Binding& b)
{
    // the other action already has this key or button: not taken, the old shortcut stays
    const auto other = (GlobalHotkeys::Action) (1 - (int) a);
    if (GlobalHotkeys::sameShortcut (b, keys.get (other)) && b.enabled && keys.get (other).enabled)
    {
        lines[a].problem.setText (tr ("Bu kısayol diğer işlemde zaten kullanılıyor. Başka bir tuş seçin."),
                                  juce::dontSendNotification);
        refresh();
        return;
    }
    const bool ok = setBinding (a, b);
    juce::String problem;
    if (! ok)
        problem = b.isMouse() ? tr ("Fare kısayolu etkinleştirilemedi. Yeniden deneyin ya da bir klavye tuşu seçin.")
                              : tr ("Bu tuş kullanılamıyor: başka bir program almış olabilir. Başka bir tuş seçin.");
    lines[a].problem.setText (problem, juce::dontSendNotification);
    refresh();
}

void HotkeyDialog::mouseCaptured (int mouse, int mods)
{
    if (capturing < 0) return;   // posted just before the capture ended
    const auto a = (GlobalHotkeys::Action) capturing;
    endCapture();
    GlobalHotkeys::Binding b;
    b.enabled = true;
    b.mouse = mouse;
    b.modifiers = GlobalHotkeys::shortcutModifiers (mods);
    apply (a, b);
}

void HotkeyDialog::refresh()
{
    for (int i = 0; i < GlobalHotkeys::numActions; ++i)
    {
        const auto& b = keys.get ((GlobalHotkeys::Action) i);
        auto& l = lines[i];
        l.enabled.setToggleState (b.enabled, juce::dontSendNotification);
        l.key.setButtonText (capturing == i ? tr ("Tuşa / fare tuşuna basın...") : GlobalHotkeys::describe (b));
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
    b.modifiers = GlobalHotkeys::shortcutModifiers (k.getModifiers().getRawFlags());
    apply (a, b);
    return true;
}

int HotkeyDialog::preferredHeight (int width) const
{
    juce::AttributedString as;
    as.append (hint.getText(), hint.getFont(), micvst::theme::textDim);
    as.setWordWrap (juce::AttributedString::byWord);
    juce::TextLayout tl;
    tl.createLayout (as, (float) juce::jmax (100, width - 8));
    return GlobalHotkeys::numActions * 58 + (int) std::ceil (tl.getHeight()) + 12;
}

void HotkeyDialog::paint (juce::Graphics& g)
{
    g.fillAll (micvst::theme::background);
}

void HotkeyDialog::resized()
{
    auto r = getLocalBounds();
    // the action names keep their width when possible; the key / mouse button gets the rest (140..220)
    const juce::Font f (juce::FontOptions (15.0f));
    int nameW = 0, resetW = 80;
    for (auto& l : lines)
    {
        nameW = juce::jmax (nameW, juce::roundToInt (juce::GlyphArrangement::getStringWidth (l.name.getFont(), l.name.getText())) + 12);
        resetW = juce::jmax (resetW, juce::roundToInt (juce::GlyphArrangement::getStringWidth (f, l.reset.getButtonText())) + 20);
    }
    const int keyW = juce::jlimit (140, 220, getWidth() - 30 - resetW - 12 - nameW);
    for (auto& l : lines)
    {
        auto row = r.removeFromTop (28);
        l.enabled.setBounds (row.removeFromLeft (30));
        l.reset.setBounds (row.removeFromRight (resetW));
        row.removeFromRight (6);
        l.key.setBounds (row.removeFromRight (keyW));
        row.removeFromRight (6);
        l.name.setBounds (row);
        l.problem.setBounds (r.removeFromTop (22));
        r.removeFromTop (8);
    }
    hint.setBounds (r);
}
