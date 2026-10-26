#pragma once

#include "Theme.h"
#include "Params.h"                 // effectiveRangeHi
#include "dsp/PerceptualModel.h"   // keyFullLoHz / keyFullHiHz, keyLoIsOpen / keyHiIsOpen
#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace anneal
{
// 5.3.4 The Key range menu, on the Sidechain menu's row, bound to "scKeyLo" and "scKeyHi": which part of
// the sidechain key counts. At rest a plain pill ("Key: all"); with a range set it reads "Key 1k–5k"
// ("Key < 150" / "Key > 5k" with one end open), the numbers in violet like the key's line.
class KeyRangeMenu final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float loSteps[] = { 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f };
    static constexpr float hiSteps[] = { 150.0f, 300.0f, 800.0f, 2000.0f, 5000.0f, 10000.0f };
    static constexpr int numSteps = 6;

    // The ready-made ranges at the top of the menu (items 2.., after the whole key): Low, High.
    struct Choice { const char* name; float lo, hi; };
    static constexpr Choice choices[] = { { "Low end (up to 150 Hz)", keyFullLoHz, 150.0f },
                                          { "Vocal presence (1-5 kHz)", 1000.0f, 5000.0f },
                                          { "Highs (above 5 kHz)", 5000.0f, keyFullHiHz } };
    static constexpr int numChoices = 3;

    KeyRangeMenu (juce::AudioProcessorValueTreeState& state, const char* loId, const char* hiId)
    {
        auto attach = [this, &state] (const char* id, float& value)
        {
            auto a = std::make_unique<juce::ParameterAttachment> (*state.getParameter (id), [this, &value] (float v)
            {
                value = v;
                repaint();
            }, state.undoManager);
            a->sendInitialUpdate();
            return a;
        };
        loAttachment = attach (loId, lo);
        hiAttachment = attach (hiId, hi);
        setTooltip ("Key range: which part of the sidechain key counts, e.g. only a vocal's 1-5 kHz presence, so its "
                    "low end ducks or lifts nothing. The display draws the key bright inside the range and faint "
                    "outside it. All = the whole key, as before");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    float getLoHz() const noexcept { return lo; }
    float getHiHz() const noexcept { return hi; }

    // "150" or "2.5k" (pill), "150 Hz" or "2.5 kHz" (menu).
    static juce::String number (float hz)
    {
        if (hz < 999.5f)
            return juce::String (juce::roundToInt (hz));
        const float k = std::round (hz / 100.0f) / 10.0f;
        return (std::abs (k - std::round (k)) < 0.05f ? juce::String (juce::roundToInt (k)) : juce::String (k, 1)) + "k";
    }
    static juce::String hzText (float hz)
    {
        return hz < 999.5f ? number (hz) + " Hz" : number (hz).dropLastCharacters (1) + " kHz";
    }

    struct Part { juce::String text; juce::Colour colour; };
    static std::vector<Part> pillParts (float loHz, float hiHz)
    {
        using namespace theme;
        const float h = effectiveRangeHi (loHz, hiHz);
        const bool l = ! keyLoIsOpen (loHz), u = ! keyHiIsOpen (h);
        if (! l && ! u)
            return { { "Key: all", colour::text } };
        if (l && u)
            return { { "Key ", colour::text }, { number (loHz), colour::violet }, { juce::String::fromUTF8 ("\xe2\x80\x93"), colour::muted },
                     { number (h), colour::violet } };
        return { { l ? "Key > " : "Key < ", colour::text }, { number (l ? loHz : h), colour::violet } };
    }

    static juce::String pillText (float loHz, float hiHz)
    {
        juce::String s;
        for (const auto& p : pillParts (loHz, hiHz))
            s << p.text;
        return s;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 1.5f);
        const float rad = r.getHeight() * 0.5f;
        const bool on = ! keyLoIsOpen (lo) || ! keyHiIsOpen (effectiveRangeHi (lo, hi));
        g.setColour (on ? colour::violet.withAlpha (0.10f) : colour::body);
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? colour::violet.withAlpha (0.6f) : colour::divider);
        g.drawRoundedRectangle (r, rad, 1.0f);

        const auto parts = pillParts (lo, hi);
        const auto area = r.withTrimmedLeft (10).withTrimmedRight (17);
        auto f = font (12.0f, "Semibold");
        auto widthOf = [] (const juce::Font& fo, const juce::String& s)
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (fo, s, 0.0f, 0.0f);
            return ga.getBoundingBox (0, -1, true).getWidth();   // with the spaces: "Key ", "Key < "
        };
        float total = 0.0f;
        for (const auto& p : parts)
            total += widthOf (f, p.text);
        if (total > area.getWidth())
            f = font (11.0f, "Semibold");
        g.setFont (f);
        float x = area.getX();
        for (const auto& p : parts)
        {
            const float w = widthOf (f, p.text);
            g.setColour (p.colour);
            g.drawText (p.text, juce::Rectangle<float> (x, area.getY(), w + 2.0f, area.getHeight()), juce::Justification::centredLeft, false);
            x += w;
        }

        g.setColour (colour::text);
        const float ax = r.getRight() - 11, ay = r.getCentreY();
        juce::Path arrow;
        arrow.addTriangle (ax - 3, ay - 1.5f, ax + 3, ay - 1.5f, ax, ay + 2.5f);
        g.fillPath (arrow);
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        // Ids: 1 the whole key, 2.. the ready-made ranges; 101 / 201 an open end, 102.. / 202.. the steps; a
        // value set elsewhere (automation, typed in the host) is listed ticked in its place, as 199 / 299.
        juce::PopupMenu menu;
        const float h = effectiveRangeHi (lo, hi);
        const bool loOpen = keyLoIsOpen (lo), hiOpen = keyHiIsOpen (h);
        menu.addItem (1, "Whole key (as before)", true, loOpen && hiOpen);
        for (int i = 0; i < numChoices; ++i)
            menu.addItem (2 + i, choices[i].name, true, std::abs (lo - choices[i].lo) < 0.5f && std::abs (h - choices[i].hi) < 0.5f);

        auto section = [&menu] (const char* title, float current, bool open, int base, const char* openName, const float* steps)
        {
            menu.addSectionHeader (title);
            menu.addItem (base, openName, true, open);
            bool listed = open;
            for (int i = 0; i < numSteps; ++i)
            {
                if (! listed && current < steps[i] - 0.5f)
                {
                    menu.addItem (base + 98, hzText (current), true, true);
                    listed = true;
                }
                const bool here = std::abs (current - steps[i]) < 0.5f;
                menu.addItem (base + 1 + i, hzText (steps[i]), true, here);
                listed = listed || here;
            }
            if (! listed)
                menu.addItem (base + 98, hzText (current), true, true);
        };
        section ("From", lo, loOpen, 101, "Lowest (20 Hz)", loSteps);
        section ("Up to", h, hiOpen, 201, "Highest (20 kHz)", hiSteps);

        juce::Component::SafePointer<KeyRangeMenu> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withStandardItemHeight (24),
                            [safe] (int selected)
        {
            if (safe != nullptr && selected > 0)
                safe->choose (selected);
        });
    }

    // What a menu item does (also for the tests). A From above Up to (or the other way round) opens the
    // other end, so the range never crosses.
    void choose (int item)
    {
        auto setLo = [this] (float v) { loAttachment->setValueAsCompleteGesture (v); };
        auto setHi = [this] (float v) { hiAttachment->setValueAsCompleteGesture (v); };
        if (item == 1)
        {
            setLo (keyFullLoHz);
            setHi (keyFullHiHz);
        }
        else if (item >= 2 && item < 2 + numChoices)
        {
            setLo (choices[item - 2].lo);
            setHi (choices[item - 2].hi);
        }
        else if (item >= 101 && item <= 101 + numSteps)
        {
            const float v = item == 101 ? keyFullLoHz : loSteps[item - 102];
            setLo (v);
            if (! keyLoIsOpen (v) && v * 1.5f > hi)
                setHi (keyFullHiHz);
        }
        else if (item >= 201 && item <= 201 + numSteps)
        {
            const float v = item == 201 ? keyFullHiHz : hiSteps[item - 202];
            setHi (v);
            if (! keyHiIsOpen (v) && lo * 1.5f > v)
                setLo (keyFullLoHz);
        }
    }

private:
    float lo = keyFullLoHz, hi = keyFullHiHz;
    std::unique_ptr<juce::ParameterAttachment> loAttachment, hiAttachment;
};
}
