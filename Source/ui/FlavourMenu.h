#pragma once

#include "Theme.h"
#include "dsp/HarmonicSynth.h"   // heatLimitIsOff, heatLimitOffDb
#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace anneal
{
// The Flavour menu (HEAT group title row), bound to the "flavour" parameter: which harmonics
// Heat writes and where. Clean (Anneal's own law, the only one before 0.2.6) is a plain raised
// pill; any other flavour tints it bronze, the Heat role colour, so a flavoured Heat is visible.
// 5.3.7: a second section sets the Heat limit ("heatLimit"); with a limit set the pill adds it,
// "Tape · −12 dB", the limit in bronze.
class FlavourMenu final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int numChoices = 5;
    static constexpr float limitSteps[] = { 6.0f, 9.0f, 12.0f, 18.0f, 24.0f, 30.0f };   // dB under the music
    static constexpr int numLimitSteps = (int) (sizeof (limitSteps) / sizeof (limitSteps[0]));

    FlavourMenu (juce::AudioProcessorValueTreeState& state, const char* id, const char* limitId)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (id), [this] (float v)
        {
            choice = juce::jlimit (0, numChoices - 1, juce::roundToInt (v));
            repaint();
        }, state.undoManager);
        attachment->sendInitialUpdate();
        limitAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (limitId), [this] (float v)
        {
            limit = v;
            repaint();
        }, state.undoManager);
        limitAttachment->sendInitialUpdate();
        setTooltip ("Flavour: the character of Heat's harmonics. Clean = Anneal's own. Tape: soft, mostly even, "
                    "darker on top. Tube: a strong 2nd. Transformer: odd harmonics, mostly from the low end. "
                    "Exciter: air only (partials above about 2 kHz). Color still moves even/odd on top. "
                    "Heat limit (also in this menu): in every band the harmonics stay at least this far under the "
                    "music, so a high Heat colours the sound without covering it. Off = as before");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int getChoice() const noexcept { return choice; }
    float getLimitDb() const noexcept { return limit; }

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

    // "12" or "4.5" (whole dB without decimals), the size of a limit.
    static juce::String number (float db)
    {
        const float r = std::round (std::abs (db) * 10.0f) / 10.0f;
        return std::abs (r - std::round (r)) < 0.05f ? juce::String (juce::roundToInt (r)) : juce::String (r, 1);
    }

    // What the pill shows in a width (also for the tests): the flavour alone while there is no limit, else the
    // flavour and the limit, "Tape · −12 dB" at 12 px, shrinking to 11 px and then to "Tape −12" where that
    // doesn't fit.
    struct Shown { juce::String name, limit; float fontSize = 12.0f; };
    static Shown shown (int c, float limitDb, float width)
    {
        if (heatLimitIsOff (limitDb))
            return { pillText (c), {}, 12.0f };
        const auto name = c == 0 ? juce::String ("Clean") : pillText (c);
        const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
        const juce::String full = juce::String::fromUTF8 (" \xc2\xb7 ") + minus + number (limitDb) + " dB";
        const juce::String brief = " " + minus + number (limitDb);
        for (const auto& [limitText, size] : { std::pair { full, 12.0f }, std::pair { full, 11.0f }, std::pair { brief, 11.0f } })
            if (textWidth (name + limitText, size) <= width)
                return { name, limitText, size };
        return { name, brief, 11.0f };
    }

    static float textWidth (const juce::String& s, float size)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (theme::font (size, "Semibold"), s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }

    juce::Rectangle<float> textArea() const
    {
        return getLocalBounds().toFloat().reduced (0.5f, 1.5f).withTrimmedLeft (10).withTrimmedRight (17);
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 1.5f);
        const float rad = r.getHeight() * 0.5f;
        const bool limited = ! heatLimitIsOff (limit);
        const bool on = choice > 0 || limited;
        g.setColour (on ? colour::bronze.withAlpha (0.18f) : colour::body);
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? colour::bronze.withAlpha (0.9f) : colour::divider);
        g.drawRoundedRectangle (r, rad, 1.0f);

        const auto area = textArea();
        const auto s = shown (choice, limit, area.getWidth());
        g.setFont (font (s.fontSize, "Semibold"));
        g.setColour (choice > 0 ? colour::bronze.brighter (0.3f) : colour::text);
        const float nameW = textWidth (s.name, s.fontSize);
        g.drawText (s.name, area.withWidth (nameW + 2.0f), juce::Justification::centredLeft, false);
        if (s.limit.isNotEmpty())
        {
            g.setColour (colour::bronze.brighter (0.3f));
            g.drawText (s.limit, area.withTrimmedLeft (nameW), juce::Justification::centredLeft, false);
        }

        g.setColour (choice > 0 ? colour::bronze.brighter (0.3f) : colour::text);
        const float x = r.getRight() - 11, y = r.getCentreY();
        juce::Path arrow;
        arrow.addTriangle (x - 3, y - 1.5f, x + 3, y - 1.5f, x, y + 2.5f);
        g.fillPath (arrow);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        // Ids: 1..5 the flavours; 101 no limit, 102.. the limit steps; a limit set elsewhere (automation,
        // typed in the host) is listed ticked in its place, as 199.
        juce::PopupMenu menu;
        menu.addSectionHeader ("Saturation flavour");
        for (int i = 0; i < numChoices; ++i)
            menu.addItem (i + 1, menuText (i), true, i == choice);

        const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
        menu.addSectionHeader ("Heat limit (under the music)");
        const bool off = heatLimitIsOff (limit);
        menu.addItem (101, "Off", true, off);
        bool listed = off;
        for (int i = 0; i < numLimitSteps; ++i)
        {
            if (! listed && -limit < limitSteps[i] - 0.05f)
            {
                menu.addItem (199, minus + number (limit) + " dB", true, true);
                listed = true;
            }
            const bool here = std::abs (-limit - limitSteps[i]) < 0.05f;
            menu.addItem (102 + i, minus + number (limitSteps[i]) + " dB", true, here);
            listed = listed || here;
        }
        if (! listed)
            menu.addItem (199, minus + number (limit) + " dB", true, true);

        juce::Component::SafePointer<FlavourMenu> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withStandardItemHeight (24),
                           [safe] (int selected)
        {
            if (safe != nullptr && selected > 0)
                safe->choose (selected);
        });
    }

    // What a menu item does (also for the tests): 1..5 a flavour, 101 no limit, 102.. the limit steps.
    void choose (int item)
    {
        if (item >= 1 && item <= numChoices)
            attachment->setValueAsCompleteGesture ((float) (item - 1));
        else if (item == 101)
            limitAttachment->setValueAsCompleteGesture (heatLimitOffDb);
        else if (item >= 102 && item < 102 + numLimitSteps)
            limitAttachment->setValueAsCompleteGesture (-limitSteps[item - 102]);
    }

private:
    int choice = 0;
    float limit = heatLimitOffDb;
    std::unique_ptr<juce::ParameterAttachment> attachment, limitAttachment;
};
}
