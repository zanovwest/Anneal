#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// The Anneal mark: the plug-in's own ring knob, its arc running through the temper colours of
// heated steel (blue, purple, bronze, straw) to a white thumb, on a dark rounded square.
// Vector-drawn, so every icon size is rendered directly; small sizes get a simpler, bolder version.
namespace anneal
{
    inline void drawAppIcon (juce::Graphics& g, juce::Rectangle<float> area)
    {
        const float s = std::min (area.getWidth(), area.getHeight());
        const auto box = area.withSizeKeepingCentre (s, s);
        const bool tiny = s < 28.0f, small = s < 56.0f;

        // Background tile.
        const float radius = s * 0.22f;
        g.setColour (juce::Colour (0xff131519));
        g.fillRoundedRectangle (box, radius);
        if (! tiny)
        {
            g.setColour (juce::Colour (0xff2a2e35));
            g.drawRoundedRectangle (box.reduced (s * 0.006f + 0.5f), radius, std::max (1.0f, s * 0.008f));
        }

        // Ring geometry (same 270-degree sweep as the knobs).
        const auto c = box.getCentre().translated (0.0f, s * 0.035f);
        const float stroke = s * (tiny ? 0.15f : small ? 0.13f : 0.11f);
        const float r = s * (tiny ? 0.30f : 0.305f);
        const float a0 = juce::MathConstants<float>::pi * 1.25f, a1 = juce::MathConstants<float>::pi * 2.75f;
        const float amount = 0.78f;
        const float aEnd = a0 + amount * (a1 - a0);

        auto arcPath = [&c, r] (float from, float to)
        {
            juce::Path p;
            p.addCentredArc (c.x, c.y, r, r, 0.0f, from, to, true);
            return p;
        };

        g.setColour (juce::Colour (0xff272b32));
        g.strokePath (arcPath (a0, a1), juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Temper ramp along the arc, drawn as short overlapping segments.
        const juce::Colour stops[] = { juce::Colour (0xff6fa7ff), juce::Colour (0xffa57be0),
                                       juce::Colour (0xffff8b3d), juce::Colour (0xfff1c36b) };
        auto ramp = [&stops] (float t)
        {
            t = juce::jlimit (0.0f, 1.0f, t) * 3.0f;
            const int i = std::min (2, (int) t);
            return stops[i].interpolatedWith (stops[i + 1], t - (float) i);
        };
        const int segments = tiny ? 24 : 96;
        for (int k = 0; k < segments; ++k)
        {
            const float t0 = (float) k / (float) segments, t1 = (float) (k + 1) / (float) segments;
            const float from = a0 + t0 * (aEnd - a0), to = a0 + t1 * (aEnd - a0);
            g.setColour (ramp (0.5f * (t0 + t1)));
            const auto cap = (k == 0) ? juce::PathStrokeType::rounded : juce::PathStrokeType::butt;
            g.strokePath (arcPath (from, std::min (aEnd, to + 0.04f)), juce::PathStrokeType (stroke, juce::PathStrokeType::curved, cap));
        }

        const juce::Point<float> tip (c.x + r * std::sin (aEnd), c.y - r * std::cos (aEnd));

        // Thumb.
        const float td = stroke * (tiny ? 1.05f : 1.12f);
        if (! tiny)
        {
            g.setColour (juce::Colour (0xff131519));
            g.fillEllipse (juce::Rectangle<float> (td + stroke * 0.36f, td + stroke * 0.36f).withCentre (tip));
        }
        g.setColour (juce::Colour (0xffeceef1));
        g.fillEllipse (juce::Rectangle<float> (td, td).withCentre (tip));
    }
}
