#include "PresetBar.h"
#include "Theme.h"

namespace anneal
{
using namespace theme;

PresetBar::PresetBar()
{
    setTooltip ("Factory presets (also in your DAW's preset menu). * = changed since loading it");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void PresetBar::refresh()
{
    const auto text = getDisplayText ? getDisplayText() : juce::String();
    if (text != shown)
    {
        shown = text;
        repaint();
    }
}

juce::Rectangle<int> PresetBar::zoneBounds (Zone z) const
{
    auto r = getLocalBounds();
    switch (z)
    {
        case Zone::prev: return r.removeFromLeft (arrowWidth);
        case Zone::next: return r.removeFromRight (arrowWidth);
        case Zone::name: return r.reduced (arrowWidth + gap, 0);
        case Zone::none: break;
    }
    return {};
}

PresetBar::Zone PresetBar::zoneAt (juce::Point<int> p) const
{
    for (auto z : { Zone::prev, Zone::next, Zone::name })
        if (zoneBounds (z).contains (p))
            return z;
    return Zone::none;
}

void PresetBar::paint (juce::Graphics& g)
{
    auto box = [&g, this] (Zone z)
    {
        const auto r = zoneBounds (z).toFloat().reduced (0.5f);
        if (hover == z)
        {
            g.setColour (colour::body);
            g.fillRoundedRectangle (r, 3.0f);
        }
        g.setColour (hover == z ? colour::muted : colour::divider);
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
        return r;
    };

    auto chevron = [&g] (juce::Rectangle<float> r, bool left, juce::Colour c)
    {
        const auto m = r.getCentre();
        const float w = 3.0f, h = 4.5f, dx = left ? w : -w;
        juce::Path p;
        p.startNewSubPath (m.x + dx * 0.5f, m.y - h);
        p.lineTo (m.x - dx * 0.5f, m.y);
        p.lineTo (m.x + dx * 0.5f, m.y + h);
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    };

    chevron (box (Zone::prev), true, hover == Zone::prev ? colour::text : colour::muted);
    chevron (box (Zone::next), false, hover == Zone::next ? colour::text : colour::muted);

    const auto name = box (Zone::name);
    g.setColour (colour::text);
    g.setFont (font (14.0f));
    g.drawFittedText (shown, name.reduced (10.0f, 0.0f).withTrimmedRight (14.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1, 0.85f);

    // Drop-down mark.
    const float cx = name.getRight() - 12.0f, cy = name.getCentreY();
    juce::Path tri;
    tri.addTriangle (cx - 4.0f, cy - 2.0f, cx + 4.0f, cy - 2.0f, cx, cy + 2.5f);
    g.setColour (hover == Zone::name ? colour::text : colour::muted);
    g.fillPath (tri);
}

void PresetBar::mouseMove (const juce::MouseEvent& e)
{
    const auto z = zoneAt (e.getPosition());
    if (z != hover)
    {
        hover = z;
        repaint();
    }
}

void PresetBar::mouseExit (const juce::MouseEvent&)
{
    hover = Zone::none;
    repaint();
}

void PresetBar::mouseDown (const juce::MouseEvent& e)
{
    switch (zoneAt (e.getPosition()))
    {
        case Zone::prev: step (-1); break;
        case Zone::next: step (+1); break;
        case Zone::name: showMenu(); break;
        case Zone::none: break;
    }
}

void PresetBar::step (int delta)
{
    const int n = getCount ? getCount() : 0;
    if (n <= 0 || ! onChoose)
        return;
    const int from = getChosen ? getChosen() : 0;
    onChoose (((from + delta) % n + n) % n);
    refresh();
}

void PresetBar::showMenu()
{
    const int n = getCount ? getCount() : 0;
    if (n <= 0 || ! getName)
        return;

    juce::PopupMenu menu;
    menu.addSectionHeader ("FACTORY PRESETS");
    const int chosen = getChosen ? getChosen() : -1;
    for (int i = 0; i < n; ++i)
        menu.addItem (i + 1, getName (i), true, i == chosen);

    juce::Component::SafePointer<PresetBar> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this)
                                                  .withMinimumWidth (getWidth())
                                                  .withStandardItemHeight (24),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result > 0 && safe->onChoose)
                            {
                                safe->onChoose (result - 1);
                                safe->refresh();
                            }
                        });
}
}
