#include "ui/BuiltInEffectEditor.h"
#include "audio/BuiltInEffects.h"
#include "ui/Theme.h"
#include "Text.h"

struct BuiltInEffectEditor::Row
{
    juce::Label name;
    std::unique_ptr<juce::Component> control;
    std::unique_ptr<juce::SliderParameterAttachment> sliderAttach;
    std::unique_ptr<juce::ComboBoxParameterAttachment> comboAttach;
    std::unique_ptr<juce::ButtonParameterAttachment> buttonAttach;
};

BuiltInEffectEditor::BuiltInEffectEditor (BuiltInEffect& e)
    : juce::AudioProcessorEditor (e), effect (e)
{
    description.setText (micvst::tr (e.getInfo().description), juce::dontSendNotification);
    description.setColour (juce::Label::textColourId, micvst::theme::textDim);
    description.setFont (juce::Font (juce::FontOptions (13.5f)));
    description.setJustificationType (juce::Justification::topLeft);
    description.setMinimumHorizontalScale (1.0f);
    addAndMakeVisible (description);
    {
        // height of the wrapped description
        juce::AttributedString s;
        s.append (description.getText(), juce::Font (juce::FontOptions (13.5f)));
        juce::TextLayout layout;
        layout.createLayout (s, (float) (width - 24 - 10));
        descriptionH = (int) std::ceil (layout.getHeight()) + 8;
    }

    display = e.createDisplay();
    if (display != nullptr)
        addAndMakeVisible (*display);

    for (auto* p : e.getEffectParameters())
    {
        auto* row = rows.add (new Row());
        row->name.setText (micvst::tr (p->getName (64)), juce::dontSendNotification);   // stored in Turkish (key)
        row->name.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (row->name);

        if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (p))
        {
            auto combo = std::make_unique<juce::ComboBox>();
            for (int k = 0; k < choice->choices.size(); ++k) combo->addItem (micvst::tr (choice->choices[k]), k + 1);
            row->comboAttach = std::make_unique<juce::ComboBoxParameterAttachment> (*choice, *combo);
            row->control = std::move (combo);
        }
        else if (auto* toggle = dynamic_cast<juce::AudioParameterBool*> (p))
        {
            auto button = std::make_unique<juce::ToggleButton> (micvst::tr ("Açık"));
            row->buttonAttach = std::make_unique<juce::ButtonParameterAttachment> (*toggle, *button);
            row->control = std::move (button);
        }
        else
        {
            auto slider = std::make_unique<juce::Slider> (juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 86, rowH - 8);
            row->sliderAttach = std::make_unique<juce::SliderParameterAttachment> (*p, *slider);
            slider->setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            row->control = std::move (slider);
        }
        addAndMakeVisible (*row->control);
    }

    defaultsBtn.setButtonText (micvst::tr ("Varsayılanlara dön"));
    defaultsBtn.setTooltip (micvst::tr ("Bu efektin bütün ayarlarını ilk değerlerine döndürür"));
    defaultsBtn.onClick = [this] { effect.resetToDefaults(); };
    defaultsBtn.setVisible (! rows.isEmpty());
    addChildComponent (defaultsBtn);

    int h = 12 + descriptionH + 6;
    if (display != nullptr) h += display->getHeight() + 10;
    if (rows.isEmpty()) h += 4;
    h += rows.size() * rowH;
    if (! rows.isEmpty()) h += 10 + 28;
    h += 12;
    setSize (width, h);
}

BuiltInEffectEditor::~BuiltInEffectEditor()
{
    rows.clear();   // attachments before the controls they watch (each Row owns both)
    // the host's job in JUCE: tell the processor its editor is gone. Without it the effect kept a
    // dangling "active editor", and opening it again gave an empty window (user report).
    effect.editorBeingDeleted (this);
}

void BuiltInEffectEditor::paint (juce::Graphics& g)
{
    g.fillAll (micvst::theme::background);
}

void BuiltInEffectEditor::resized()
{
    auto r = getLocalBounds().reduced (12);
    description.setBounds (r.removeFromTop (descriptionH));
    r.removeFromTop (6);
    if (display != nullptr)
    {
        display->setBounds (r.removeFromTop (display->getHeight()));
        r.removeFromTop (10);
    }
    for (auto* row : rows)
    {
        auto line = r.removeFromTop (rowH);
        row->name.setBounds (line.removeFromLeft (labelW));
        row->control->setBounds (line.reduced (0, 3));
    }
    if (! rows.isEmpty())
    {
        r.removeFromTop (10);
        defaultsBtn.setBounds (r.removeFromTop (28).removeFromRight (170));
    }
}
