#pragma once

#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace anneal
{
// The Listen menu (OUTPUT group), bound to the "listen" parameter: hear one part of what Anneal
// does on its own. A solo lights the pill in that part's role colour (straw = Reveal, blue =
// Smooth, bronze = Heat), and the display says what is being heard, so it is hard to forget.
class ListenMenu final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int numChoices = 4;

    ListenMenu (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (id), [this] (float v)
        {
            choice = juce::jlimit (0, numChoices - 1, juce::roundToInt (v));
            repaint();
        }, state.undoManager);
        attachment->sendInitialUpdate();
        setTooltip ("Listen: hear one part of Anneal on its own - what Reveal lifts, what Smooth cuts, or only the "
                    "harmonics Heat writes. Off = the full result. Overrides Delta while on");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int getChoice() const noexcept { return choice; }

    static juce::String pillText (int c)
    {
        switch (c)
        {
            case 1:  return "Reveal only";
            case 2:  return "Smooth only";
            case 3:  return "Heat only";
            default: return "Listen: off";
        }
    }

    static juce::String menuText (int c)
    {
        switch (c)
        {
            case 1:  return "Reveal only: what it lifts";
            case 2:  return "Smooth only: what it cuts";
            case 3:  return "Heat only: the harmonics";
            default: return "Off: the full result";
        }
    }

    static juce::Colour roleOf (int c)
    {
        using namespace theme;
        return c == 1 ? colour::straw : c == 2 ? colour::blue : c == 3 ? colour::bronze : colour::text;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 1.5f);
        const float rad = r.getHeight() * 0.5f;
        const bool on = choice > 0;
        const auto role = roleOf (choice);
        g.setColour (on ? role.withAlpha (0.18f) : colour::body);
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? role.withAlpha (0.9f) : colour::divider);
        g.drawRoundedRectangle (r, rad, 1.0f);
        g.setColour (on ? role.brighter (0.3f) : colour::text);
        g.setFont (font (12.0f, "Semibold"));
        g.drawText (pillText (choice), r.withTrimmedLeft (10).withTrimmedRight (17), juce::Justification::centredLeft, false);
        const float x = r.getRight() - 11, y = r.getCentreY();
        juce::Path arrow;
        arrow.addTriangle (x - 3, y - 1.5f, x + 3, y - 1.5f, x, y + 2.5f);
        g.fillPath (arrow);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        juce::PopupMenu menu;
        menu.addSectionHeader ("Listen to");
        for (int i = 0; i < numChoices; ++i)
            menu.addItem (i + 1, menuText (i), true, i == choice);
        juce::Component::SafePointer<ListenMenu> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withStandardItemHeight (24),
                           [safe] (int selected)
        {
            if (safe != nullptr && selected > 0)
                safe->attachment->setValueAsCompleteGesture ((float) (selected - 1));
        });
    }

private:
    int choice = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};
}
