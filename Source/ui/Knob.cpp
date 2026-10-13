#include "Knob.h"
#include "Theme.h"

namespace anneal
{
using namespace theme;

namespace
{
    constexpr int titleH = 18, valueH = 20, captionH = 16;
    int knobDiameter (Knob::Size s) { return s == Knob::Size::primary ? 92 : 54; }
}

int Knob::preferredWidth (Size s)  { return s == Size::primary ? 120 : 92; }
int Knob::preferredHeight (Size s) { return titleH + knobDiameter (s) + valueH + (s == Size::primary ? captionH : 0); }

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& t,
            juce::Colour role, Size sz, bool bipolar, const juce::String& cap, const juce::String& help)
    : title (t), caption (cap), size (sz)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, preferredWidth (sz), valueH);
    slider.setColour (juce::Slider::rotarySliderFillColourId, role);
    slider.getProperties().set ("primary", sz == Size::primary);
    slider.getProperties().set ("bipolar", bipolar);
    slider.setMouseDragSensitivity (sz == Size::primary ? 260 : 200);
    // Ctrl-drag switches to a slow, incremental mode for fine adjustment.
    slider.setVelocityModeParameters (0.3, 1, 0.0, true, juce::ModifierKeys::ctrlModifier);
    slider.setTitle (title);
    slider.setTooltip (help + "   (drag, Ctrl-drag fine, double-click reset, click value to type)");
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::resized()
{
    const int d = knobDiameter (size);
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, getWidth(), valueH);
    slider.setBounds (0, titleH, getWidth(), d + valueH);
}

void Knob::paint (juce::Graphics& g)
{
    const bool primary = size == Size::primary;
    g.setColour (primary ? colour::text : colour::muted);
    g.setFont (caps (primary ? 14.0f : size::label));
    g.drawText (title.toUpperCase(), getLocalBounds().removeFromTop (titleH), juce::Justification::centred, false);

    if (caption.isNotEmpty())
    {
        g.setColour (colour::muted);
        g.setFont (font (size::caption));
        g.drawText (caption, getLocalBounds().removeFromBottom (captionH), juce::Justification::centred, false);
    }
}

Toggle::Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text,
                juce::Colour role, const juce::String& help)
    : juce::TextButton (text)
{
    setClickingTogglesState (true);
    setColour (juce::TextButton::buttonOnColourId, role);
    setTooltip (help);
    setTitle (text);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (state, paramId, *this);
}
}
