#include "AnnealLookAndFeel.h"
#include "Theme.h"

namespace anneal
{
using namespace theme;

namespace
{
    juce::Point<float> polar (juce::Point<float> c, float r, float angle)
    {
        return { c.x + r * std::sin (angle), c.y - r * std::cos (angle) };
    }
}

AnnealLookAndFeel::AnnealLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, colour::panel);
    setColour (juce::Slider::textBoxTextColourId, colour::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, colour::faint);
    setColour (juce::TextEditor::highlightColourId, colour::faint);
    setColour (juce::TextEditor::highlightedTextColourId, colour::text);
    setColour (juce::CaretComponent::caretColourId, colour::text);
    setColour (juce::TooltipWindow::textColourId, colour::text);
    setColour (juce::TextButton::textColourOffId, colour::muted);
    setColour (juce::TextButton::textColourOnId, colour::text);
}

void AnnealLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                          float a0, float a1, juce::Slider& s)
{
    const bool primary = s.getProperties()["primary"];
    const bool bipolar = s.getProperties()["bipolar"];
    const auto role = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float alpha = s.isEnabled() ? 1.0f : 0.4f;

    const auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float d = std::min (area.getWidth(), area.getHeight());
    const auto c = area.getCentre();
    const float rOuter = d * 0.5f - 1.0f;
    const float arcW = primary ? 4.5f : 3.0f;
    const float rArc = rOuter - arcW * 0.5f - (primary ? 7.0f : 3.0f);
    const float angle = a0 + pos * (a1 - a0);
    const float mid = 0.5f * (a0 + a1);

    // Scale: 11 ticks on primary knobs, end ticks only on secondary ones.
    g.setColour (colour::faint.withMultipliedAlpha (alpha));
    const int ticks = primary ? 11 : 2;
    for (int i = 0; i < ticks; ++i)
    {
        const float t = a0 + (float) i / (float) (ticks - 1) * (a1 - a0);
        const bool major = i == 0 || i == ticks - 1 || (primary && i == ticks / 2);
        const float rIn = rArc + arcW * 0.5f + 2.0f;
        g.drawLine (juce::Line<float> (polar (c, rIn, t), polar (c, rIn + (major ? 5.0f : 3.0f), t)), 1.0f);
    }

    const juce::PathStrokeType stroke (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::butt);

    juce::Path track;
    track.addCentredArc (c.x, c.y, rArc, rArc, 0.0f, a0, a1, true);
    g.setColour (colour::track.withMultipliedAlpha (alpha));
    g.strokePath (track, stroke);

    const float from = bipolar ? mid : a0;
    if (std::abs (angle - from) > 0.002f)
    {
        juce::Path value;
        value.addCentredArc (c.x, c.y, rArc, rArc, 0.0f, std::min (from, angle), std::max (from, angle), true);
        g.setColour (role.withMultipliedAlpha (alpha));
        g.strokePath (value, stroke);
    }

    if (bipolar)
    {
        g.setColour (colour::muted.withMultipliedAlpha (alpha));
        g.drawLine (juce::Line<float> (polar (c, rArc - arcW * 0.5f - 1.0f, mid), polar (c, rArc + arcW * 0.5f + 1.0f, mid)), 1.5f);
    }

    const float rBody = rArc - arcW * 0.5f - (primary ? 6.0f : 4.0f);
    g.setColour (colour::body.withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (rBody * 2.0f, rBody * 2.0f).withCentre (c));
    g.setColour (colour::divider.withMultipliedAlpha (alpha));
    g.drawEllipse (juce::Rectangle<float> (rBody * 2.0f, rBody * 2.0f).withCentre (c), 1.0f);

    g.setColour (colour::text.withMultipliedAlpha (alpha));
    g.drawLine (juce::Line<float> (polar (c, rBody * 0.35f, angle), polar (c, rBody * 0.92f, angle)), primary ? 2.5f : 2.0f);

    if (s.hasKeyboardFocus (false))
    {
        g.setColour (colour::text.withAlpha (0.55f));
        g.drawEllipse (juce::Rectangle<float> (rOuter * 2.0f, rOuter * 2.0f).withCentre (c), 1.0f);
    }
}

juce::Label* AnnealLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    l->setFont (font (size::value));
    l->setJustificationType (juce::Justification::centred);
    l->setColour (juce::Label::textColourId, colour::text);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textWhenEditingColourId, colour::text);
    l->setColour (juce::Label::backgroundWhenEditingColourId, colour::body);
    l->setColour (juce::Label::outlineWhenEditingColourId, colour::muted);
    l->setMinimumHorizontalScale (1.0f);
    l->getProperties().set ("tabular", true);
    return l;
}

juce::Font AnnealLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void AnnealLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
{
    if (! (bool) l.getProperties()["tabular"] || l.isBeingEdited())
    {
        LookAndFeel_V4::drawLabel (g, l);
        return;
    }

    g.fillAll (l.findColour (juce::Label::backgroundColourId));
    g.setColour (l.findColour (juce::Label::textColourId).withMultipliedAlpha (l.isEnabled() ? 1.0f : 0.5f));
    g.setFont (getLabelFont (l));
    drawTabular (g, l.getText(), l.getLocalBounds().toFloat(), juce::Justification::centred);
}

void AnnealLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                              bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const auto role = b.findColour (juce::TextButton::buttonOnColourId);
    const bool on = b.getToggleState();

    if (on)
    {
        g.setColour (role.withAlpha (down ? 0.32f : 0.20f));
        g.fillRoundedRectangle (r, 3.0f);
    }
    else if (down)
    {
        g.setColour (colour::body);
        g.fillRoundedRectangle (r, 3.0f);
    }

    g.setColour (on ? role : (highlighted ? colour::muted : colour::divider));
    g.drawRoundedRectangle (r, 3.0f, 1.0f);

    if (b.hasKeyboardFocus (false))
    {
        g.setColour (colour::text.withAlpha (0.55f));
        g.drawRoundedRectangle (r.expanded (2.0f), 4.0f, 1.0f);
    }
}

juce::Font AnnealLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return caps (12.5f);
}

void AnnealLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool on = b.getToggleState();
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (on ? b.findColour (juce::TextButton::buttonOnColourId).brighter (0.25f) : colour::muted);
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

juce::Rectangle<int> AnnealLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                          juce::Rectangle<int> parentArea)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (font (13.0f), tipText, 0.0f, 0.0f);
    const int w = (int) std::ceil (ga.getBoundingBox (0, -1, false).getWidth()) + 18;
    const int h = 26;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h).constrainedWithin (parentArea);
}

void AnnealLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (colour::body);
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (colour::divider);
    g.drawRoundedRectangle (r.reduced (0.5f), 3.0f, 1.0f);
    g.setColour (colour::text);
    g.setFont (font (13.0f));
    g.drawText (text, r.reduced (9.0f, 0.0f), juce::Justification::centredLeft, false);
}
}
