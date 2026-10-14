#pragma once

#include "Theme.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace anneal
{
// The Flavour menu (HEAT group title row), bound to the "flavour" parameter: which harmonics
// Heat writes and where. Clean (Anneal's own law, the only one before 0.2.6) is a plain raised
// pill; any other flavour tints it bronze, the Heat role colour, so a flavoured Heat is visible.
class FlavourMenu final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int numChoices = 5;

    FlavourMenu (juce::AudioProcessorValueTreeState& state, const char* id)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (id), [this] (float v)
        {
            choice = juce::jlimit (0, numChoices - 1, juce::roundToInt (v));
            repaint();
        }, state.undoManager);
        attachment->sendInitialUpdate();
        setTooltip ("Flavour: the character of Heat's harmonics. Clean = Anneal's own. Tape: soft, mostly even, "
                    "darker on top. Tube: a strong 2nd. Transformer: odd harmonics, mostly from the low end. "
                    "Exciter: air only (partials above about 2 kHz). Color still moves even/odd on top");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int getChoice() const noexcept { return choice; }

    static juce::String pillText (int c)
    {
        switch (c)
        {
            case 1:  return "Tape";
            case 2:  return "Tube";
            case 3:  return "Transformer";
            case 4:  return "Exciter";
            default: return "Flavour: clean";
        }
    }

    static juce::String menuText (int c)
    {
        switch (c)
        {
            case 1:  return "Tape: soft, even, darker top";
            case 2:  return "Tube: a strong 2nd";
            case 3:  return "Transformer: odd, from the lows";
            case 4:  return "Exciter: air only";
            default: return "Clean: Anneal's own";
        }
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 1.5f);
        const float rad = r.getHeight() * 0.5f;
        const bool on = choice > 0;
        g.setColour (on ? colour::bronze.withAlpha (0.18f) : colour::body);
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? colour::bronze.withAlpha (0.9f) : colour::divider);
        g.drawRoundedRectangle (r, rad, 1.0f);
        g.setColour (on ? colour::bronze.brighter (0.3f) : colour::text);
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
        menu.addSectionHeader ("Saturation flavour");
        for (int i = 0; i < numChoices; ++i)
            menu.addItem (i + 1, menuText (i), true, i == choice);
        juce::Component::SafePointer<FlavourMenu> safe (this);
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
