#include "Knob.h"
#include "Theme.h"

namespace anneal
{
using namespace theme;

namespace
{
    constexpr int ringGap = 6, titleH = 18, lineH = 18;
}

int Knob::ringDiameter (Size s)    { return s == Size::hero ? 132 : s == Size::primary ? 112 : 42; }
int Knob::preferredWidth (Size s)  { return s == Size::hero ? 168 : s == Size::primary ? 140 : 136; }
int Knob::preferredHeight (Size s) { return s == Size::secondary ? 46 : ringDiameter (s) + ringGap + titleH + lineH; }

Knob::Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& t,
            juce::Colour r, Size sz, bool bipolar, const juce::String& cap, const juce::String& help)
    : title (t), caption (cap), role (r), size (sz)
{
    const bool ring = sz != Size::secondary;
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setColour (juce::Slider::rotarySliderFillColourId, role);
    slider.getProperties().set ("ring", ring);
    slider.getProperties().set ("hero", sz == Size::hero);
    slider.getProperties().set ("compact", ! ring);
    slider.getProperties().set ("bipolar", bipolar);
    slider.getProperties().set ("activity", -1.0);
    slider.getProperties().set ("valueSize", sz == Size::hero ? 25.0 : sz == Size::primary ? 21.0 : 14.0);
    slider.getProperties().set ("valueStyle", ring ? "Semibold" : "Regular");
    // The value field's position comes from AnnealLookAndFeel::getSliderLayout (inside the ring,
    // or beside the small one); the style here only has to keep it visible and editable.
    slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 24);
    slider.setMouseDragSensitivity (ring ? 260 : 200);
    // Ctrl-drag switches to a slow, incremental mode for fine adjustment.
    slider.setVelocityModeParameters (0.3, 1, 0.0, true, juce::ModifierKeys::ctrlModifier);
    slider.setTitle (title);
    slider.setTooltip (help + "   (drag, Ctrl-drag fine, double-click reset, click the value to type)");
    addAndMakeVisible (slider);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, slider);

    if (auto* p = state.getParameter (paramId))
        slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
}

void Knob::resized()
{
    if (size == Size::secondary)
    {
        slider.setBounds (getLocalBounds());
        return;
    }
    const int d = ringDiameter (size);
    slider.setBounds ((getWidth() - d) / 2, 0, d, d);
}

void Knob::setActivity (float amount01, const juce::String& text)
{
    amount01 = juce::jlimit (0.0f, 1.0f, amount01);
    if (text == activityText && std::abs (amount01 - activity) < 0.004f)
        return;
    activity = amount01;
    activityText = text;
    // The inner arc, also on a compact ring (5.2.7: it was dropped there, so Punch's never showed);
    // only the big rings have the line of text.
    slider.getProperties().set ("activity", text.isEmpty() ? -1.0 : (double) amount01);
    slider.repaint();
    if (size != Size::secondary)
        repaint (0, ringDiameter (size) + ringGap + titleH, getWidth(), lineH);
}

void Knob::paint (juce::Graphics& g)
{
    if (size == Size::secondary)
    {
        // Title above the value, both beside the small ring.
        const int d = ringDiameter (size);
        g.setColour (colour::muted);
        g.setFont (caps (11.0f));
        g.drawText (title.toUpperCase(), juce::Rectangle<int> (d + 10, 3, getWidth() - d - 10, 16), juce::Justification::centredLeft, false);
        return;
    }

    auto r = getLocalBounds().withTrimmedTop (ringDiameter (size) + ringGap);
    g.setColour (colour::text);
    g.setFont (caps (size == Size::hero ? 13.0f : 12.5f));
    g.drawText (title.toUpperCase(), r.removeFromTop (titleH), juce::Justification::centred, false);

    const bool live = activityText.isNotEmpty();
    g.setFont (font (size::caption));
    g.setColour (live && activity > 0.0f ? role : colour::muted);
    g.drawFittedText (live ? activityText : caption, r.removeFromTop (lineH), juce::Justification::centred, 1, 1.0f);
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
