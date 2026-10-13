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

    juce::Path arc (juce::Point<float> c, float r, float from, float to)
    {
        juce::Path p;
        p.addCentredArc (c.x, c.y, r, r, 0.0f, from, to, true);
        return p;
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
    setColour (juce::PopupMenu::backgroundColourId, colour::body);
    setColour (juce::PopupMenu::textColourId, colour::text);
    setColour (juce::PopupMenu::headerTextColourId, colour::muted);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colour::divider);
    setColour (juce::PopupMenu::highlightedTextColourId, colour::text);
}

juce::Font AnnealLookAndFeel::getPopupMenuFont()
{
    return font (14.0f);
}

//==============================================================================
// Knobs: a ring. Track, value arc in the role colour (from the start, or from the top for bipolar
// controls) with a soft halo, a white thumb at the value, and for hero/primary knobs a thin inner
// arc showing how hard the control is working right now (slider property "activity", -1 = none).
void AnnealLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                          float a0, float a1, juce::Slider& s)
{
    const bool hero = s.getProperties()["hero"];
    const bool compact = s.getProperties()["compact"];
    const bool bipolar = s.getProperties()["bipolar"];
    const double activity = s.getProperties().getWithDefault ("activity", -1.0);
    const auto role = s.findColour (juce::Slider::rotarySliderFillColourId);
    const float alpha = s.isEnabled() ? 1.0f : 0.4f;

    auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const float d = std::min (area.getWidth(), area.getHeight());
    area = area.withSizeKeepingCentre (d, d);
    const auto c = area.getCentre();
    const float arcW = compact ? 4.0f : hero ? 8.0f : 7.0f;
    const float r = d * 0.5f - arcW * 0.5f - (compact ? 1.0f : 3.0f);
    const float angle = a0 + pos * (a1 - a0);
    const float mid = 0.5f * (a0 + a1);
    const juce::PathStrokeType stroke (arcW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    g.setColour (colour::track.withMultipliedAlpha (alpha));
    g.strokePath (arc (c, r, a0, a1), stroke);

    const float from = bipolar ? mid : a0;
    if (std::abs (angle - from) > 0.01f)
    {
        const auto valueArc = arc (c, r, std::min (from, angle), std::max (from, angle));
        if (! compact)
        {
            g.setColour (role.withAlpha (0.13f * alpha));
            g.strokePath (valueArc, juce::PathStrokeType (arcW + 9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (role.withMultipliedAlpha (alpha));
        g.strokePath (valueArc, stroke);
    }

    if (bipolar)
    {
        g.setColour (colour::muted.withMultipliedAlpha (alpha));
        g.drawLine (juce::Line<float> (polar (c, r + arcW * 0.5f + 2.0f, mid), polar (c, r + arcW * 0.5f + 5.0f, mid)), 1.5f);
    }

    if (! compact && activity >= 0.0)
    {
        const float ri = r - arcW * 0.5f - 6.0f;
        g.setColour (colour::track.withAlpha (0.6f * alpha));
        g.strokePath (arc (c, ri, a0, a1), juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (activity > 0.005)
        {
            g.setColour (role.withAlpha (0.75f * alpha));
            g.strokePath (arc (c, ri, a0, a0 + (float) activity * (a1 - a0)),
                          juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }

    // Thumb.
    const auto tp = polar (c, r, angle);
    const float td = compact ? 7.0f : arcW + 4.0f;
    g.setColour (colour::screen.withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (td + 3.0f, td + 3.0f).withCentre (tp));
    g.setColour (colour::text.withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (td, td).withCentre (tp));

    if (s.hasKeyboardFocus (false))
    {
        g.setColour (colour::text.withAlpha (0.45f));
        g.drawEllipse (area.reduced (0.5f), 1.0f);
    }
}

juce::Slider::SliderLayout AnnealLookAndFeel::getSliderLayout (juce::Slider& s)
{
    if (s.getSliderStyle() != juce::Slider::RotaryVerticalDrag)
        return LookAndFeel_V4::getSliderLayout (s);

    juce::Slider::SliderLayout layout;
    const auto b = s.getLocalBounds();
    if (s.getProperties()["compact"])
    {
        // Small ring on the left, the value under the title on the right.
        const int d = b.getHeight() - 4;
        layout.sliderBounds = { 0, 2, d, d };
        layout.textBoxBounds = { d + 8, b.getHeight() / 2 - 2, b.getWidth() - d - 8, b.getHeight() / 2 };
    }
    else
    {
        layout.sliderBounds = b;
        const int w = juce::roundToInt (b.getWidth() * 0.62f), h = 30;
        layout.textBoxBounds = juce::Rectangle<int> (w, h).withCentre (b.getCentre());
    }
    return layout;
}

juce::Label* AnnealLookAndFeel::createSliderTextBox (juce::Slider& s)
{
    auto* l = LookAndFeel_V4::createSliderTextBox (s);
    const auto sizeProp = (float) (double) s.getProperties().getWithDefault ("valueSize", (double) size::value);
    const auto styleProp = s.getProperties().getWithDefault ("valueStyle", "Regular").toString();
    l->setFont (font (sizeProp, styleProp));
    l->setJustificationType (s.getProperties()["compact"] ? juce::Justification::centredLeft : juce::Justification::centred);
    l->setColour (juce::Label::textColourId, colour::text);
    l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
    l->setColour (juce::Label::textWhenEditingColourId, colour::text);
    l->setColour (juce::Label::backgroundWhenEditingColourId, colour::body);
    l->setColour (juce::Label::outlineWhenEditingColourId, colour::muted);
    l->setMinimumHorizontalScale (0.8f);
    l->setBorderSize (juce::BorderSize<int> (0));
    return l;
}

juce::Font AnnealLookAndFeel::getLabelFont (juce::Label& l)
{
    return l.getFont();
}

void AnnealLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& l)
{
    LookAndFeel_V4::drawLabel (g, l);
}

//==============================================================================
// Faders (Color, Focus): thin track, the value filled in the role colour from the left (or from
// the centre tick when bipolar), and a round white thumb.
void AnnealLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                          float, float, juce::Slider::SliderStyle style, juce::Slider& s)
{
    if (style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, pos, 0.0f, 0.0f, style, s);
        return;
    }

    const bool bipolar = s.getProperties()["bipolar"];
    const auto role = s.findColour (juce::Slider::trackColourId);
    const float alpha = s.isEnabled() ? 1.0f : 0.4f;
    const float cy = (float) y + (float) height * 0.5f;
    const float x0 = (float) x + 6.0f, x1 = (float) (x + width) - 6.0f;
    const float mid = 0.5f * (x0 + x1);
    const float px = juce::jlimit (x0, x1, pos);

    g.setColour (colour::track.withMultipliedAlpha (alpha));
    g.fillRoundedRectangle (juce::Rectangle<float> (x0, cy - 2.0f, x1 - x0, 4.0f), 2.0f);

    const float from = bipolar ? mid : x0;
    if (std::abs (px - from) > 0.5f)
    {
        g.setColour (role.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (juce::Rectangle<float> (std::min (from, px), cy - 2.0f, std::abs (px - from), 4.0f), 2.0f);
    }
    if (bipolar)
    {
        g.setColour (colour::muted.withMultipliedAlpha (alpha));
        g.fillRect (juce::Rectangle<float> (1.5f, 10.0f).withCentre ({ mid, cy }));
    }

    g.setColour (colour::screen.withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre ({ px, cy }));
    g.setColour (colour::text.withMultipliedAlpha (alpha));
    g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ px, cy }));

    if (s.hasKeyboardFocus (false))
    {
        g.setColour (colour::text.withAlpha (0.5f));
        g.drawEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre ({ px, cy }), 1.0f);
    }
}

//==============================================================================
// Text buttons (Match, Delta, Bypass): pills. Off = outline, on = tinted fill in the role colour.
void AnnealLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&,
                                              bool highlighted, bool down)
{
    const auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float rad = r.getHeight() * 0.5f;
    const auto role = b.findColour (juce::TextButton::buttonOnColourId);
    const bool on = b.getToggleState();

    g.setColour (on ? role.withAlpha (down ? 0.30f : 0.18f) : (down || highlighted ? colour::body : colour::panel));
    g.fillRoundedRectangle (r, rad);
    g.setColour (on ? role.withAlpha (0.9f) : (highlighted ? colour::muted : colour::divider));
    g.drawRoundedRectangle (r, rad, 1.0f);

    if (b.hasKeyboardFocus (false))
    {
        g.setColour (colour::text.withAlpha (0.5f));
        g.drawRoundedRectangle (r.expanded (2.0f), rad + 2.0f, 1.0f);
    }
}

juce::Font AnnealLookAndFeel::getTextButtonFont (juce::TextButton&, int)
{
    return caps (11.5f);
}

void AnnealLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool highlighted, bool)
{
    const bool on = b.getToggleState();
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (on ? b.findColour (juce::TextButton::buttonOnColourId).brighter (0.3f) : (highlighted ? colour::text : colour::muted));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred, false);
}

//==============================================================================
juce::Rectangle<int> AnnealLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                          juce::Rectangle<int> parentArea)
{
    juce::GlyphArrangement ga;
    ga.addLineOfText (font (13.0f), tipText, 0.0f, 0.0f);
    const int w = (int) std::ceil (ga.getBoundingBox (0, -1, false).getWidth()) + 20;
    const int h = 28;
    return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                 screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                 w, h).constrainedWithin (parentArea);
}

void AnnealLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
{
    const auto r = juce::Rectangle<float> ((float) width, (float) height);
    g.setColour (colour::body);
    g.fillRoundedRectangle (r, 5.0f);
    g.setColour (colour::divider);
    g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
    g.setColour (colour::text);
    g.setFont (font (13.0f));
    g.drawText (text, r.reduced (10.0f, 0.0f), juce::Justification::centredLeft, false);
}
}
