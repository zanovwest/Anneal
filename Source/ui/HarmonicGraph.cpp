#include "HarmonicGraph.h"
#include "Params.h"
#include "Theme.h"

#include <cmath>

namespace anneal
{
using namespace theme;

namespace
{
    constexpr float floorDb = -60.0f;
}

HarmonicGraph::HarmonicGraph (juce::AudioProcessorValueTreeState& state)
{
    heat = state.getRawParameterValue (ids::heat);
    color = state.getRawParameterValue (ids::color);
    setTooltip ("Harmonics Heat writes for one partial at -18 dBFS where the spectrum is thin (model). "
                "Color moves the balance between even (round) and odd (edgy) harmonics.");
}

float HarmonicGraph::harmonicDb (int n, float heat01, float color01)
{
    // Same law as HarmonicSynth::process: amplitude = parity weight * tanh(u)^(n-1) / n,
    // u = drive at the reference level.
    if (heat01 <= 1.0e-4f || n < 2)
        return n < 2 ? 0.0f : -200.0f;
    const float drive = std::pow (10.0f, (heat01 * 40.0f - 34.0f) / 20.0f);
    const float t = std::tanh (drive);
    const float evenW = std::min (1.0f, 2.0f * (1.0f - color01));
    const float oddW  = std::min (1.0f, 2.0f * color01);
    const float w = (n % 2) == 0 ? evenW : oddW;
    const float amp = w * std::pow (t, (float) (n - 1)) / (float) n;
    return amp > 1.0e-10f ? 20.0f * std::log10 (amp) : -200.0f;
}

void HarmonicGraph::refresh()
{
    const float h = heat->load() * 0.01f, c = color->load() * 0.01f;
    if (std::abs (h - shownHeat) > 1.0e-4f || std::abs (c - shownColor) > 1.0e-4f)
    {
        shownHeat = h;
        shownColor = c;
        repaint();
    }
}

void HarmonicGraph::paint (juce::Graphics& g)
{
    // Compact: six bars (fundamental, then 2nd-6th) over a baseline, numbers under them. The
    // bar height is the level relative to the partial, 0 to -60 dB.
    auto r = getLocalBounds().toFloat();
    const auto labels = r.removeFromBottom (13.0f);
    const auto plot = r.withTrimmedTop (2.0f);
    auto yFor = [&] (float db) { return plot.getBottom() - juce::jlimit (0.0f, 1.0f, (db - floorDb) / -floorDb) * plot.getHeight(); };

    g.setColour (colour::grid);
    g.drawHorizontalLine (juce::roundToInt (yFor (-30.0f)), plot.getX(), plot.getRight());
    g.setColour (colour::divider);
    g.drawHorizontalLine (juce::roundToInt (plot.getBottom()), plot.getX(), plot.getRight());

    const int count = 6;
    const float slot = plot.getWidth() / count, bw = std::min (12.0f, slot * 0.46f);
    for (int n = 1; n <= count; ++n)
    {
        const float db = harmonicDb (n, shownHeat, shownColor);
        const float cx = plot.getX() + (n - 0.5f) * slot;
        if (db > floorDb)
        {
            const auto bar = juce::Rectangle<float> (cx - bw * 0.5f, yFor (db), bw, plot.getBottom() - yFor (db));
            if (n == 1)
                g.setColour (colour::muted.withAlpha (0.45f));
            else
                g.setColour (n % 2 == 0 ? colour::bronze : colour::bronze.withAlpha (0.55f));
            g.fillRoundedRectangle (bar, 1.5f);
        }

        g.setFont (font (10.5f));
        g.setColour (n == 1 ? colour::faint : colour::muted);
        g.drawText (n == 1 ? juce::String ("f") : juce::String (n), labels.withX (cx - slot * 0.5f).withWidth (slot),
                    juce::Justification::centred, false);
    }
}
}
