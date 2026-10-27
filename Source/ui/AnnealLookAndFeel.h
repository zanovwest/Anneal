#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace anneal
{
    // Shared drawing for every knob, fader, value field, button, menu and tooltip.
    // Slider properties read here: "ring", "hero", "compact", "bipolar" (bool), "activity"
    // (0..1, -1 = none), "valueSize" (px) and "valueStyle" (font style) for the value field.
    class AnnealLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        AnnealLookAndFeel();

        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
        juce::Slider::SliderLayout getSliderLayout (juce::Slider&) override;
        void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float minSliderPos, float maxSliderPos, juce::Slider::SliderStyle, juce::Slider&) override;

        juce::Label* createSliderTextBox (juce::Slider&) override;
        juce::Font getLabelFont (juce::Label&) override;
        void drawLabel (juce::Graphics&, juce::Label&) override;

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
        juce::Font getTextButtonFont (juce::TextButton&, int) override;

        // 5.3.5: a tooltip wider than this wraps onto more lines (it used to be one line, cut off at the
        // window's edge).
        static constexpr float tooltipWrapWidth = 520.0f;
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                               juce::Rectangle<int> parentArea) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

        juce::Font getPopupMenuFont() override;
    };
}
