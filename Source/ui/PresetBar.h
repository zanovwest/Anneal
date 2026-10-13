#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace anneal
{
    // Header preset selector: [<] [ name  v ] [>]. Clicking the name opens the factory list;
    // the arrows step through it (wrapping). Draws itself; the owner supplies the data.
    class PresetBar : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        PresetBar();

        std::function<int()> getCount;                     // number of presets
        std::function<juce::String (int)> getName;         // name of preset i
        std::function<int()> getChosen;                    // index the list ticks / the arrows step from
        std::function<juce::String()> getDisplayText;      // what the box shows, e.g. "Vocal air *"
        std::function<void (int)> onChoose;

        void refresh();   // call from a timer: repaints only when the shown text changes

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;

        static constexpr int arrowWidth = 30;

    private:
        enum class Zone { none, prev, name, next };
        Zone zoneAt (juce::Point<int>) const;
        juce::Rectangle<int> zoneBounds (Zone) const;
        void step (int delta);
        void showMenu();

        juce::String shown;
        Zone hover = Zone::none;
    };
}
