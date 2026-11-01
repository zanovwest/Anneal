#pragma once

#include "Theme.h"
#include "Params.h"   // FreezeMode
#include <juce_audio_processors/juce_audio_processors.h>

namespace anneal
{
// 5.3.11 The FREEZE pill (AUTO EQ title row), bound to "freeze" and "freezeMode". A click opens a small menu:
// Off, Hold now (what Freeze always did: the curve as it is) or Hold the average of the last 10 s. Lit in straw
// while it holds, and "FREEZE · AVG" while that is the average. A choice sets both parameters as one undo step.
class FreezeMenu final : public juce::TextButton
{
public:
    FreezeMenu (juce::AudioProcessorValueTreeState& state, const char* freezeId, const char* modeId)
        : juce::TextButton ("FREEZE")
    {
        freezeAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (freezeId), [this] (float v)
        {
            on = v > 0.5f;
            refresh();
        }, state.undoManager);
        modeAttachment = std::make_unique<juce::ParameterAttachment> (*state.getParameter (modeId), [this] (float v)
        {
            mode = juce::jlimit (0, 1, juce::roundToInt (v));
            refresh();
        }, state.undoManager);
        freezeAttachment->sendInitialUpdate();
        modeAttachment->sendInitialUpdate();
        setColour (juce::TextButton::buttonOnColourId, theme::colour::straw);
        setTooltip ("Freeze: hold the auto-EQ curve; Heat keeps following the music. Hold now keeps the curve as it is; "
                    "Hold the average keeps its average over the last 10 s of music (silence left out), a static EQ "
                    "for a whole passage. Off: the curve follows the music again");
        setTitle ("Freeze");
        onClick = [this] { showMenu(); };
    }

    bool isOn() const noexcept { return on; }
    int getMode() const noexcept { return mode; }

    static juce::String pillText (bool frozen, int m)
    {
        return frozen && m == freezeAverage ? juce::String::fromUTF8 ("FREEZE \xc2\xb7 AVG") : juce::String ("FREEZE");
    }

    static juce::String menuText (int item)
    {
        switch (item)
        {
            case 2:  return "Hold now: the curve as it is";
            case 3:  return "Hold the average of the last 10 s";
            default: return "Off: follow the music";
        }
    }

    // The width a pill text takes in the pill's font (the look and feel's, 26 px high), for the tests.
    static float textWidth (const juce::String& s)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (theme::caps (11.5f), s, 0.0f, 0.0f);
        return ga.getBoundingBox (0, -1, true).getWidth();
    }

    void showMenu()
    {
        // Ids: 1 Off, 2 Hold now, 3 Hold the average.
        juce::PopupMenu menu;
        menu.addSectionHeader ("Freeze the auto EQ");
        for (int item = 1; item <= 3; ++item)
            menu.addItem (item, menuText (item), true, item == chosenItem());
        juce::Component::SafePointer<FreezeMenu> safe (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withStandardItemHeight (24),
                            [safe] (int selected)
        {
            if (safe != nullptr && selected > 0)
                safe->choose (selected);
        });
    }

    int chosenItem() const noexcept { return ! on ? 1 : mode == freezeAverage ? 3 : 2; }

    // What a menu item does (also for the tests): 1 Off, 2 Hold now, 3 Hold the average.
    void choose (int item)
    {
        if (item == 1)
        {
            freezeAttachment->setValueAsCompleteGesture (0.0f);
        }
        else if (item == 2 || item == 3)
        {
            // The mode first, so Freeze comes on holding what was chosen; both in one gesture, one undo step.
            modeAttachment->beginGesture();
            freezeAttachment->beginGesture();
            modeAttachment->setValueAsPartOfGesture ((float) (item == 3 ? freezeAverage : freezeNow));
            freezeAttachment->setValueAsPartOfGesture (1.0f);
            modeAttachment->endGesture();
            freezeAttachment->endGesture();
        }
    }

private:
    void refresh()
    {
        setToggleState (on, juce::dontSendNotification);
        setButtonText (pillText (on, mode));
    }

    bool on = false;
    int mode = freezeNow;
    std::unique_ptr<juce::ParameterAttachment> freezeAttachment, modeAttachment;
};
}
