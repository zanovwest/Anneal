#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace anneal
{
    // Shared drawing for every knob, value field, toggle and tooltip.
    // Slider properties read here: "primary" (bool), "bipolar" (bool).
    class AnnealLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        AnnealLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

        juce::Label* createSliderTextBox (juce::Slider&) override;
        juce::Font getLabelFont (juce::Label&) override;
        void drawLabel (juce::Graphics&, juce::Label&) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;

        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                               juce::Rectangle<int> parentArea) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

        juce::Font getTextButtonFont (juce::TextButton&, int) override;
        juce::Font getPopupMenuFont() override;
    };
}
