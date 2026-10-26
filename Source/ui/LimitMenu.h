#pragma once

#include "Theme.h"
#include "dsp/PerceptualModel.h"   // noLimitDb, isNoLimit
#include <juce_audio_processors/juce_audio_processors.h>

#include <vector>

namespace anneal
{
// 5.3.2 The Limits menu (AUTO EQ title row), bound to "maxBoost" and "maxCut": how far the auto EQ may
// lift and cut. At rest a plain pill ("Limits: none"); with a limit set it reads "Max +3 / −6 dB" with
// the numbers in their role colours (straw lifts, blue cuts), as the display's dashed lines do.
class LimitMenu final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr float steps[] = { 1.0f, 2.0f, 3.0f, 4.0f, 6.0f, 9.0f, 12.0f };
    static constexpr int numSteps = (int) (sizeof (steps) / sizeof (steps[0]));

    LimitMenu (juce::AudioProcessorValueTreeState& state, const char* boostId, const char* cutId)
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
        boostAttachment = attach (boostId, boost);
        cutAttachment = attach (cutId, cut);
        setTooltip ("Limits: how far the auto EQ may move, e.g. lift no more than 3 dB and cut no more than 6 dB. "
                    "Covers Reveal, Smooth, the sidechain and the reference together (Tilt and Punch are your own "
                    "moves and aren't limited). The display draws the limits as dashed lines. No limit = as before");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    float getMaxBoostDb() const noexcept { return boost; }
    float getMaxCutDb() const noexcept   { return cut; }

    // "3" or "4.5" (whole dB without decimals).
    static juce::String number (float db)
    {
        const float r = std::round (db * 10.0f) / 10.0f;
        return std::abs (r - std::round (r)) < 0.05f ? juce::String (juce::roundToInt (r)) : juce::String (r, 1);
    }

    struct Part { juce::String text; juce::Colour colour; };
    static std::vector<Part> pillParts (float boostDb, float cutDb)
    {
        using namespace theme;
        const bool b = ! isNoLimit (boostDb), c = ! isNoLimit (cutDb);
        if (! b && ! c)
            return { { "Limits: none", colour::text } };
        std::vector<Part> parts { { "Max ", colour::text } };
        if (b)
            parts.push_back ({ "+" + number (boostDb), colour::straw });
        if (b && c)
            parts.push_back ({ " / ", colour::muted });
        if (c)
            parts.push_back ({ juce::String::fromUTF8 ("\xe2\x88\x92") + number (cutDb), colour::blue });
        parts.push_back ({ " dB", colour::text });
        return parts;
    }

    static juce::String pillText (float boostDb, float cutDb)
    {
        juce::String s;
        for (const auto& p : pillParts (boostDb, cutDb))
            s << p.text;
        return s;
    }

    void paint (juce::Graphics& g) override
    {
        using namespace theme;
        const auto r = getLocalBounds().toFloat().reduced (0.5f, 1.5f);
        const float rad = r.getHeight() * 0.5f;
        const bool on = ! isNoLimit (boost) || ! isNoLimit (cut);
        g.setColour (on ? colour::text.withAlpha (0.07f) : colour::body);
        g.fillRoundedRectangle (r, rad);
        g.setColour (on ? colour::muted.withAlpha (0.8f) : colour::divider);
        g.drawRoundedRectangle (r, rad, 1.0f);

        const auto parts = pillParts (boost, cut);
        const auto area = r.withTrimmedLeft (10).withTrimmedRight (17);
        auto f = font (12.0f, "Semibold");
        auto widthOf = [] (const juce::Font& fo, const juce::String& s)
        {
            juce::GlyphArrangement ga;
            ga.addLineOfText (fo, s, 0.0f, 0.0f);
            return ga.getBoundingBox (0, -1, true).getWidth();   // with the spaces: "Max ", " / ", " dB"
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
        // Ids: 1 + i boost (0 = no limit), 101 + i cut; a value set elsewhere (automation, typed in the
        // host) is listed ticked in its place, as 99 / 199.
        juce::PopupMenu menu;
        auto section = [&menu] (const char* title, float current, int base, const juce::String& sign)
        {
            menu.addSectionHeader (title);
            menu.addItem (base, "No limit", true, isNoLimit (current));
            bool listed = isNoLimit (current);
            for (int i = 0; i < numSteps; ++i)
            {
                if (! listed && current < steps[i] - 0.05f)
                {
                    menu.addItem (base + 98, sign + number (current) + " dB", true, true);
                    listed = true;
                }
                const bool here = std::abs (current - steps[i]) < 0.05f;
                menu.addItem (base + 1 + i, sign + number (steps[i]) + " dB", true, here);
                listed = listed || here;
            }
            if (! listed)
                menu.addItem (base + 98, sign + number (current) + " dB", true, true);
        };
        section ("Max boost", boost, 1, "+");
        section ("Max cut", cut, 101, juce::String::fromUTF8 ("\xe2\x88\x92"));

        juce::Component::SafePointer<LimitMenu> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withStandardItemHeight (24),
                            [safe] (int selected)
        {
            if (safe != nullptr && selected > 0)
                safe->choose (selected);
        });
    }

    // What a menu item does (also for the tests): 1 / 101 no limit, 2.. / 102.. the steps.
    void choose (int item)
    {
        auto* a = item > 100 ? cutAttachment.get() : boostAttachment.get();
        const int i = (item > 100 ? item - 100 : item) - 1;
        if (i == 0)
            a->setValueAsCompleteGesture (noLimitDb);
        else if (i >= 1 && i <= numSteps)
            a->setValueAsCompleteGesture (steps[i - 1]);
    }

private:
    float boost = noLimitDb, cut = noLimitDb;
    std::unique_ptr<juce::ParameterAttachment> boostAttachment, cutAttachment;
};
}
