#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace anneal
{
    // Horizontal fader for a two-ended character control (Even <-> Odd, Body <-> Air):
    //   TITLE ............................. value (click to type)
    //   [================|=============]
    //   LEFT END                     RIGHT END
    // Bound to one host parameter; drag, Ctrl-drag fine, double-click reset.
    class Fader : public juce::Component
    {
    public:
        Fader (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
               const juce::String& leftEnd, const juce::String& rightEnd, juce::Colour role, bool bipolar,
               const juce::String& help);

        void paint (juce::Graphics&) override;
        void resized() override;

        juce::Slider slider;

    private:
        void updateValueText();

        juce::String title, leftEnd, rightEnd;
        juce::Label value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
}
