#pragma once

#include "Theme.h"
#include "Params.h"   // ListenCheck
#include <juce_audio_processors/juce_audio_processors.h>

namespace anneal
{
// The Listen menu (OUTPUT group), bound to the "listen" parameter: hear one part of what Anneal
// does on its own. A solo lights the pill in that part's role colour (straw = Reveal, blue =
// Smooth, bronze = Heat), and the display says what is being heard, so it is hard to forget.
// 5.3.9: a second section sets the Listen check ("listenCheck"): the output in Stereo (as is), summed
// to Mono, or its Sides only. A check alone lights the pill neutral white (a way of listening, not a
// process); with a solo the pill adds it, "Heat only · mono".
class ListenMenu final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int numChoices = 4;
    static constexpr int numChecks = 3;

    ListenMenu (juce::AudioProcessorValueTreeState& state, const char* id, const char* checkId)
    {
        attachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (id), [this] (float v)
        {
            choice = juce::jlimit (0, numChoices - 1, juce::roundToInt (v));
            repaint();
        }, state.undoManager);
        attachment->sendInitialUpdate();
        checkAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (checkId), [this] (float v)
        {
            check = juce::jlimit (0, numChecks - 1, juce::roundToInt (v));
            repaint();
        }, state.undoManager);
        checkAttachment->sendInitialUpdate();
        setTooltip ("Listen: hear one part of Anneal on its own - what Reveal lifts, what Smooth cuts, or only the "
                    "harmonics Heat writes. Off = the full result. Overrides Delta while on. "
                    "Check (also in this menu): hear the output summed to Mono, to check the mix holds up on a phone "
                    "or a club system, or its Sides only, what mono loses (both speakers, in phase). Stereo = as is. "
                    "The output's loudness reads what you hear; Bypass plays the input as it is");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int getChoice() const noexcept { return choice; }
    int getCheck() const noexcept { return check; }

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

    static juce::String checkMenuText (int k)
    {
        switch (k)
        {
            case checkMono:  return "Mono: left and right summed";
            case checkSides: return "Sides only: what mono loses";
            default:         return "Stereo: as is";
        }
    }

    static juce::Colour roleOf (int c)
    {
        using namespace theme;
        return c == 1 ? colour::straw : c == 2 ? colour::blue : c == 3 ? colour::bronze : colour::text;
    }

    // What the pill shows in a width (also for the tests): the solo alone while the check is Stereo; a check
    // alone as "Mono" / "Sides only"; both as "Heat only · mono" at 12 px, then 11 px, then "Heat only + M".
    struct Shown { juce::String name, check; float fontSize = 12.0f; };
    static Shown shown (int c, int k, float width)
    {
        if (k == checkStereo)
            return { pillText (c), {}, 12.0f };
        if (c == 0)
            return { {}, k == checkMono ? juce::String ("Mono") : juce::String ("Sides only"), 12.0f };
        const juce::String full = juce::String::fromUTF8 (" \xc2\xb7 ") + (k == checkMono ? "mono" : "sides");
        const juce::String brief = k == checkMono ? " + M" : " + S";
        for (const auto& [checkText, size] : { std::pair { full, 12.0f }, std::pair { full, 11.0f }, std::pair { brief, 11.0f } })
            if (textWidth (pillText (c) + checkText, size) <= width)
                return { pillText (c), checkText, size };
        return { pillText (c), brief, 11.0f };
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
        const bool on = choice > 0 || check != checkStereo;
        const auto role = roleOf (choice);
        g.setColour (on ? role.withAlpha (0.18f) : colour::body);
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? role.withAlpha (0.9f) : colour::divider);
        g.drawRoundedRectangle (r, rad, 1.0f);

        const auto area = textArea();
        const auto s = shown (choice, check, area.getWidth());
        g.setFont (font (s.fontSize, "Semibold"));
        g.setColour (on ? role.brighter (0.3f) : colour::text);
        const float nameW = s.name.isEmpty() ? 0.0f : textWidth (s.name, s.fontSize);
        if (s.name.isNotEmpty())
            g.drawText (s.name, area.withWidth (nameW + 2.0f), juce::Justification::centredLeft, false);
        if (s.check.isNotEmpty())
        {
            g.setColour (colour::text);   // the check is neutral, whatever the solo's colour
            g.drawText (s.check, area.withTrimmedLeft (nameW), juce::Justification::centredLeft, false);
        }

        g.setColour (on ? role.brighter (0.3f) : colour::text);
        const float x = r.getRight() - 11, y = r.getCentreY();
        juce::Path arrow;
        arrow.addTriangle (x - 3, y - 1.5f, x + 3, y - 1.5f, x, y + 2.5f);
        g.fillPath (arrow);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        // Ids: 1..4 the solos; 101..103 Stereo / Mono / Sides only.
        juce::PopupMenu menu;
        menu.addSectionHeader ("Listen to");
        for (int i = 0; i < numChoices; ++i)
            menu.addItem (i + 1, menuText (i), true, i == choice);
        menu.addSectionHeader ("Check the output");
        for (int k = 0; k < numChecks; ++k)
            menu.addItem (101 + k, checkMenuText (k), true, k == check);
        juce::Component::SafePointer<ListenMenu> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withStandardItemHeight (24),
                           [safe] (int selected)
        {
            if (safe != nullptr && selected > 0)
                safe->choose (selected);
        });
    }

    // What a menu item does (also for the tests): 1..4 a solo, 101..103 a check.
    void choose (int item)
    {
        if (item >= 1 && item <= numChoices)
            attachment->setValueAsCompleteGesture ((float) (item - 1));
        else if (item >= 101 && item < 101 + numChecks)
            checkAttachment->setValueAsCompleteGesture ((float) (item - 101));
    }

private:
    int choice = 0, check = checkStereo;
    std::unique_ptr<juce::ParameterAttachment> attachment, checkAttachment;
};
}
