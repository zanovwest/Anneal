#include "Fader.h"
#include "Theme.h"

namespace anneal
{
using namespace theme;

namespace
{
    constexpr int rowH = 18, trackH = 22, endH = 13;
}

Fader::Fader (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& t,
              const juce::String& l, const juce::String& r, juce::Colour role, bool bipolar, const juce::String& help)
    : title (t), leftEnd (l), rightEnd (r)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setColour (juce::Slider::trackColourId, role);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setMouseDragSensitivity (260);
    slider.setVelocityModeParameters (0.3, 1, 0.0, true, juce::ModifierKeys::ctrlModifier);
    slider.setTitle (title);
    slider.setTooltip (help + "   (drag, Ctrl-drag fine, double-click reset, click the value to type)");
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);
    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));

    // Typeable value, parsed by the parameter itself (units such as "odd", "Air 40" work).
    value.setFont (font (size::value));
    value.setJustificationType (juce::Justification::centredRight);
    value.setColour (juce::Label::textColourId, colour::text);
    value.setColour (juce::Label::textWhenEditingColourId, colour::text);
    value.setColour (juce::Label::backgroundWhenEditingColourId, colour::body);
    value.setColour (juce::Label::outlineWhenEditingColourId, colour::muted);
    value.setEditable (true, true, false);
    value.setTooltip (slider.getTooltip());
    value.onTextChange = [this]
    {
        slider.setValue (slider.getValueFromText (value.getText()), juce::sendNotificationSync);
        updateValueText();
    };
    addAndMakeVisible (value);

    slider.onValueChange = [this] { updateValueText(); };
    updateValueText();
}

void Fader::updateValueText()
{
    value.setText (slider.getTextFromValue (slider.getValue()), juce::dontSendNotification);
}

void Fader::resized()
{
    auto r = getLocalBounds();
    auto top = r.removeFromTop (rowH);
    value.setBounds (top.removeFromRight (juce::jmin (120, top.getWidth() / 2)));
    slider.setBounds (r.removeFromTop (trackH).expanded (2, 0));
}

void Fader::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    g.setColour (colour::muted);
    g.setFont (caps (11.0f));
    g.drawText (title.toUpperCase(), r.removeFromTop (rowH), juce::Justification::centredLeft, false);

    r.removeFromTop (trackH);
    const auto ends = r.removeFromTop (endH);
    g.setFont (font (11.0f));
    g.setColour (colour::faint);
    g.drawText (leftEnd, ends, juce::Justification::centredLeft, false);
    g.drawText (rightEnd, ends, juce::Justification::centredRight, false);
}
}
