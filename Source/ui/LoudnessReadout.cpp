#include "LoudnessReadout.h"
#include "Theme.h"

namespace anneal
{
using namespace theme;

namespace
{
    float textWidthOf (const juce::Font& f, const juce::String& s)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, false).getWidth();
    }
}

LoudnessReadout::LoudnessReadout()
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    for (auto& t : text)
        t = format (-200.0f);
}

juce::String LoudnessReadout::format (float lufs)
{
    if (! (lufs > -70.0f))
        return juce::String::fromUTF8 ("\xe2\x80\x94");   // an em dash: nothing to measure
    const float v = std::round (lufs * 10.0f) / 10.0f;
    return (v < 0.0f ? juce::String::fromUTF8 ("\xe2\x88\x92") : v > 0.0f ? juce::String ("+") : juce::String())
           + juce::String (std::abs (v), 1);
}

void LoudnessReadout::set (float shortIn, float shortOut, float integratedIn, float integratedOut)
{
    const float values[4] = { shortIn, shortOut, integratedIn, integratedOut };
    bool changed = false;
    for (int i = 0; i < 4; ++i)
    {
        const auto t = format (values[i]);
        changed = changed || t != text[i];
        text[i] = t;
    }
    if (changed)
        repaint();
}

juce::String LoudnessReadout::getShownText() const
{
    return "S " + text[0] + " > " + text[1] + " | I " + text[2] + " > " + text[3];
}

void LoudnessReadout::paint (juce::Graphics& g)
{
    // Two rows: S over I. Label, input (muted), an arrow, output (bright), each number in a cell as
    // wide as the widest one ("-88.8"), so the columns line up; LUFS once, after them.
    auto r = getLocalBounds().toFloat();
    const auto value = font (12.0f);
    const float cell = std::ceil (tabularWidths (value, juce::String::fromUTF8 ("\xe2\x88\x92") + "88.8")) + 1.0f;
    const float labelW = 12.0f, arrowW = 16.0f;
    const float used = labelW + 2.0f * cell + arrowW + 6.0f + std::ceil (textWidthOf (caps (10.0f), "LUFS"));
    r.removeFromLeft (std::max (0.0f, std::floor ((r.getWidth() - used) / 2.0f)));   // centred in its slot
    const float rowH = r.getHeight() / 2.0f;
    for (int row = 0; row < 2; ++row)
    {
        auto line = r.withHeight (rowH).withY (r.getY() + row * rowH);
        g.setColour (colour::faint);
        g.setFont (caps (10.0f));
        g.drawText (row == 0 ? "S" : "I", line.removeFromLeft (labelW), juce::Justification::centredLeft, false);

        g.setFont (value);
        g.setColour (colour::muted);
        drawTabular (g, text[row * 2], line.removeFromLeft (cell), juce::Justification::centredRight);
        g.setColour (colour::faint);
        g.drawText (juce::String::fromUTF8 ("\xe2\x86\x92"), line.removeFromLeft (arrowW), juce::Justification::centred, false);
        g.setColour (colour::text);
        drawTabular (g, text[row * 2 + 1], line.removeFromLeft (cell), juce::Justification::centredRight);
    }
    g.setColour (colour::faint);
    g.setFont (caps (10.0f));
    g.drawText ("LUFS", r.withTrimmedLeft (labelW + 2.0f * cell + arrowW + 6.0f), juce::Justification::centredLeft, false);
}

void LoudnessReadout::mouseDown (const juce::MouseEvent&)
{
    if (onReset != nullptr)
        onReset();
}

juce::String LoudnessReadout::getTooltip()
{
    return juce::String::fromUTF8 ("Loudness (EBU R128), input \xe2\x86\x92 output. S: short-term, the last 3 s. "
                                   "I: integrated, everything played since you last clicked here (quiet passages "
                                   "and silence don't pull it down). Click to start I over.");
}
}
