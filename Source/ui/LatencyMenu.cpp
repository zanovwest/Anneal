#include "LatencyMenu.h"
#include "Params.h"
#include "Theme.h"

namespace anneal
{
using namespace theme;

namespace
{
    const char* const modeNames[] = { "Low", "Normal", "Precise" };
    const char* const modeNotes[] = { "tracking and live playing", "", "finest bass detail" };
}

LatencyMenu::LatencyMenu (juce::AudioProcessorValueTreeState& state, std::function<double (int)> ms,
                          std::function<double()> applied)
    : msForMode (std::move (ms)), appliedMs (std::move (applied))
{
    param = state.getParameter (ids::latency);
    attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v)
    {
        choice = juce::roundToInt (v);
        repaint();
    }, state.undoManager);
    attachment->sendInitialUpdate();
    setTooltip ("Latency: Low for tracking and playing live, Precise for the finest bass detail "
                "(the host re-aligns tracks when it changes)");
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void LatencyMenu::refresh()
{
    const double ms = appliedMs ? appliedMs() : 0.0;
    if (std::abs (ms - shownMs) > 0.01)
    {
        shownMs = ms;
        repaint();
    }
}

void LatencyMenu::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const auto caption = r.removeFromLeft ((float) captionWidth);
    g.setColour (colour::faint);
    g.setFont (caps (10.5f));
    g.drawText ("LATENCY", caption, juce::Justification::centredLeft, false);

    const auto pill = r.reduced (0.5f, 1.5f);
    const float rad = pill.getHeight() * 0.5f;
    g.setColour (hover ? colour::divider : colour::body);
    g.fillRoundedRectangle (pill, rad);
    g.setColour (hover ? colour::muted.withAlpha (0.6f) : colour::divider);
    g.drawRoundedRectangle (pill, rad, 1.0f);

    const double ms = shownMs >= 0.0 ? shownMs : (msForMode ? msForMode (choice) : 0.0);
    g.setColour (colour::text);
    g.setFont (font (13.0f, "Semibold"));
    g.drawText (juce::String (ms, 1) + " ms", pill.withTrimmedLeft (12.0f).withTrimmedRight (22.0f),
                juce::Justification::centredLeft, false);

    const float cx = pill.getRight() - 13.0f, cy = pill.getCentreY();
    juce::Path tri;
    tri.addTriangle (cx - 3.5f, cy - 1.5f, cx + 3.5f, cy - 1.5f, cx, cy + 2.5f);
    g.setColour (hover ? colour::text : colour::muted);
    g.fillPath (tri);
}

void LatencyMenu::mouseDown (const juce::MouseEvent&)
{
    juce::PopupMenu menu;
    menu.addSectionHeader ("LATENCY");
    for (int m = 0; m < 3; ++m)
    {
        auto text = juce::String (modeNames[m]) + "   " + juce::String (msForMode ? msForMode (m) : 0.0, 1) + " ms";
        if (juce::String (modeNotes[m]).isNotEmpty())
            text << "   (" << modeNotes[m] << ")";
        menu.addItem (m + 1, text, true, m == choice);
    }

    juce::Component::SafePointer<LatencyMenu> safe (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withStandardItemHeight (24),
                        [safe] (int result)
                        {
                            if (safe != nullptr && result > 0)
                                safe->attachment->setValueAsCompleteGesture ((float) (result - 1));
                        });
}
}
