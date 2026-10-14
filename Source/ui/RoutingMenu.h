#pragma once

#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace anneal
{
// A host-bound pill menu, shared by the mode and the two routing choices.
class RoutingMenu final : public juce::Component, public juce::SettableTooltipClient
{
public:
    RoutingMenu (juce::AudioProcessorValueTreeState& state, const char* id, juce::String prefix,
                 juce::StringArray choices, const juce::String& tooltip)
        : caption (std::move (prefix)), labels (std::move (choices))
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (id), [this] (float v)
        {
            choice = juce::jlimit (0, labels.size() - 1, juce::roundToInt (v));
            repaint();
        }, state.undoManager);
        attachment->sendInitialUpdate();
        setTooltip (tooltip);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 1.5f);
        const float alpha = isEnabled() ? 1.0f : 0.35f;
        g.setColour (colour::body.withMultipliedAlpha (alpha));
        g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
        g.setColour (colour::divider.withMultipliedAlpha (alpha));
        g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
        g.setColour (colour::text.withMultipliedAlpha (alpha));
        g.setFont (font (12.0f, "Semibold"));
        g.drawText (caption + labels[choice], r.withTrimmedLeft (10).withTrimmedRight (20),
                    juce::Justification::centredLeft, false);
        const float x = r.getRight() - 11, y = r.getCentreY();
        juce::Path arrow;
        arrow.addTriangle (x - 3, y - 1.5f, x + 3, y - 1.5f, x, y + 2.5f);
        g.fillPath (arrow);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        if (! isEnabled()) return;
        juce::PopupMenu menu;
        for (int i = 0; i < labels.size(); ++i) menu.addItem (i + 1, labels[i], true, i == choice);
        juce::Component::SafePointer<RoutingMenu> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withStandardItemHeight (24),
                           [safe] (int selected)
        {
            if (safe != nullptr && selected > 0)
                safe->attachment->setValueAsCompleteGesture ((float) (selected - 1));
        });
    }

private:
    juce::String caption;
    juce::StringArray labels;
    int choice = 0;
    std::unique_ptr<juce::ParameterAttachment> attachment;
};
}
