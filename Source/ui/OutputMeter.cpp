#include "OutputMeter.h"
#include "Theme.h"

namespace anneal
{
using namespace theme;

OutputMeter::OutputMeter()
{
    setTooltip ("Output sample peak, dBFS. Red latch = a sample went over 0 dBFS (click to clear).");
}

void OutputMeter::push (const float* peaks, int numChannels, float dt)
{
    channels = juce::jlimit (1, 2, numChannels);
    bool changed = false;
    for (int c = 0; c < channels; ++c)
    {
        const float db = juce::Decibels::gainToDecibels (peaks[c], -100.0f);
        const float next = juce::jmax (db, level[c] - 24.0f * dt);
        const float prevHold = hold[c];
        holdAge[c] += dt;
        if (db >= hold[c])          { hold[c] = db; holdAge[c] = 0.0f; }
        else if (holdAge[c] > 1.5f) { hold[c] = next; }
        if (peaks[c] > 1.0f)
            clipped[c] = true;
        changed = changed || std::abs (next - level[c]) > 0.01f || hold[c] != prevHold;
        level[c] = next;
    }
    if (changed)
        repaint();
}

float OutputMeter::yForDb (float db, juce::Rectangle<float> bar) const noexcept
{
    const float v = juce::jlimit (bottomDb, topDb, db);
    return bar.getBottom() - (v - bottomDb) / (topDb - bottomDb) * bar.getHeight();
}

void OutputMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto labelArea = r.removeFromLeft (26.0f);
    auto bars = r.withTrimmedTop (10.0f).withTrimmedBottom (8.0f);
    const auto latchArea = r.withHeight (6.0f);

    // Scale.
    g.setFont (font (size::caption));
    for (float db : { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f })
    {
        const float y = yForDb (db, bars);
        g.setColour (db == 0.0f ? colour::muted : colour::faint);
        g.drawText (db == 0.0f ? juce::String ("0") : juce::String::fromUTF8 ("\xe2\x88\x92") + juce::String ((int) -db),
                    juce::Rectangle<float> (labelArea.getX(), y - 7.0f, 22.0f, 14.0f), juce::Justification::centredRight, false);
        g.setColour (colour::grid);
        g.drawHorizontalLine (juce::roundToInt (y), bars.getX(), bars.getRight());
    }

    const float gap = 3.0f;
    const float w = (bars.getWidth() - gap * (channels - 1)) / channels;
    const float y0 = yForDb (0.0f, bars);

    for (int c = 0; c < channels; ++c)
    {
        const auto bar = juce::Rectangle<float> (bars.getX() + c * (w + gap), bars.getY(), w, bars.getHeight());
        g.setColour (colour::body);
        g.fillRect (bar);

        const float y = yForDb (level[c], bar);
        if (level[c] > bottomDb)
        {
            g.setColour (colour::muted);
            g.fillRect (bar.withTop (juce::jmax (y, y0)));
            if (y < y0)
            {
                g.setColour (colour::warning);
                g.fillRect (bar.withTop (y).withBottom (y0));
            }
        }
        if (hold[c] > bottomDb)
        {
            g.setColour (hold[c] > 0.0f ? colour::warning : colour::text);
            g.fillRect (bar.withTop (yForDb (hold[c], bar)).withHeight (1.5f));
        }

        g.setColour (clipped[c] ? colour::warning : colour::body);
        g.fillRect (juce::Rectangle<float> (bar.getX(), latchArea.getY(), w, 6.0f));
    }
}

void OutputMeter::mouseDown (const juce::MouseEvent&)
{
    clipped[0] = clipped[1] = false;
    repaint();
}
}
