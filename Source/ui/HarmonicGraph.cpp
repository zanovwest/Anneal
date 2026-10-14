#include "HarmonicGraph.h"
#include "Params.h"
#include "Theme.h"
#include "dsp/HarmonicSynth.h"

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
    flavour = state.getRawParameterValue (ids::flavour);
    setTooltip ("Harmonics Heat writes for one partial at -18 dBFS where the spectrum is thin (model). "
                "Color moves the balance between even (round) and odd (edgy) harmonics; the flavour shapes them. "
                "Transformer is shown for a 120 Hz partial (lower notes saturate more, higher ones far less), "
                "Exciter for a 4 kHz one (it leaves partials below 1.5 kHz alone), and Tape also rolls off "
                "harmonics above 5 kHz.");
}

double HarmonicGraph::referenceHz (int f)
{
    return f == flavourExciter ? 4000.0 : 120.0;
}

float HarmonicGraph::harmonicDb (int n, float heat01, float color01, int f)
{
    // HarmonicSynth's own law: parity weight * flavour weight * tanh(u)^(n-1) / n, u = drive at
    // the reference level (Clean: exactly the formula from before flavours).
    if (heat01 <= 1.0e-4f || n < 2)
        return n < 2 ? 0.0f : -200.0f;
    const float amp = HarmonicSynth::harmonicLevel (n, heat01, color01, f, referenceHz (f));
    return amp > 1.0e-10f ? 20.0f * std::log10 (amp) : -200.0f;
}

void HarmonicGraph::refresh()
{
    const float h = heat->load() * 0.01f, c = color->load() * 0.01f;
    const int f = juce::jlimit (0, HarmonicSynth::numFlavours - 1, juce::roundToInt (flavour->load()));
    if (std::abs (h - shownHeat) > 1.0e-4f || std::abs (c - shownColor) > 1.0e-4f || f != shownFlavour)
    {
        shownHeat = h;
        shownColor = c;
        shownFlavour = f;
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
        const float db = harmonicDb (n, shownHeat, shownColor, shownFlavour);
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
