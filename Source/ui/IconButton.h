#pragma once

#include "Theme.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace anneal
{
    // Header pill with a drawn glyph (3.0: Undo / Redo). Dimmed while disabled.
    class IconButton final : public juce::Button
    {
    public:
        enum class Glyph { undo, redo };

        IconButton (Glyph g, const juce::String& tip) : juce::Button (g == Glyph::undo ? "Undo" : "Redo"), glyph (g)
        {
            setTooltip (tip);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool over, bool down) override
        {
            using namespace theme;
            const float alpha = isEnabled() ? 1.0f : 0.35f;
            const auto r = getLocalBounds().toFloat().reduced (0.5f, 1.5f);
            const float rad = r.getHeight() * 0.5f;
            g.setColour ((down ? colour::divider : over && isEnabled() ? colour::body.brighter (0.06f) : colour::body).withMultipliedAlpha (alpha));
            g.fillRoundedRectangle (r, rad);
            g.setColour ((over && isEnabled() ? colour::muted.withAlpha (0.6f) : colour::divider).withMultipliedAlpha (alpha));
            g.drawRoundedRectangle (r, rad, 1.0f);

            // A curved arrow: a 200-degree arc with a head at its end, mirrored for Redo.
            const auto c = r.getCentre().translated (0.0f, 1.0f);
            const float rr = 5.0f;
            juce::Path arc;
            arc.addCentredArc (c.x, c.y, rr, rr, 0.0f, -2.2f, 1.2f, true);
            const float ax = c.x + rr * std::sin (-2.2f), ay = c.y - rr * std::cos (-2.2f);
            juce::Path head;
            head.addTriangle (ax - 3.5f, ay - 1.0f, ax + 2.5f, ay - 3.5f, ax + 1.0f, ay + 2.5f);
            if (glyph == Glyph::redo)
            {
                const auto flip = juce::AffineTransform::scale (-1.0f, 1.0f, c.x, c.y);
                arc.applyTransform (flip);
                head.applyTransform (flip);
            }
            g.setColour ((over && isEnabled() ? colour::text : colour::muted).withMultipliedAlpha (alpha));
            g.strokePath (arc, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.fillPath (head);
        }

    private:
        Glyph glyph;
    };
}
