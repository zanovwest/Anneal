#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace anneal
{
    // One host parameter as a knob.
    //  hero / primary: a ring with the value inside it (click the value to type), the title under
    //    the ring, and a live line under the title saying how hard the control is working right
    //    now; the same amount is drawn as a thin inner arc. Without audio, a static caption.
    //  secondary: a compact row - small ring on the left, title and value beside it; a control
    //    that reports activity (Punch) shows it as a thin arc inside the small ring (no text).
    class Knob : public juce::Component
    {
    public:
        enum class Size { hero, primary, secondary };

        Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
              juce::Colour role, Size size, bool bipolar, const juce::String& caption, const juce::String& help);

        void paint (juce::Graphics&) override;
        void resized() override;

        // amount01 drives the inner arc; empty text = no audio or nothing to show (the caption, no arc).
        void setActivity (float amount01, const juce::String& text);

        static int preferredHeight (Size size);
        static int preferredWidth (Size size);
        static int ringDiameter (Size size);

        juce::Slider slider;

    private:
        juce::String title, caption, activityText;
        float activity = 0.0f;
        juce::Colour role;
        Size size;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    // Latching text toggle bound to a bool parameter.
    class Toggle : public juce::TextButton
    {
    public:
        Toggle (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& text,
                juce::Colour role, const juce::String& help);

    private:
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };
}
