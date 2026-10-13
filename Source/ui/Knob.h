#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace anneal
{
    // Title + rotary + typeable value field, bound to one host parameter.
    class Knob : public juce::Component
    {
    public:
        enum class Size { primary, secondary };

        Knob (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
              juce::Colour role, Size size, bool bipolar, const juce::String& caption, const juce::String& help);

        void paint (juce::Graphics&) override;
        void resized() override;

        static int preferredHeight (Size size);
        static int preferredWidth (Size size);

        juce::Slider slider;

    private:
        juce::String title, caption;
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
