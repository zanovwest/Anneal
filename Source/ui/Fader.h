#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace anneal
{
    // Horizontal fader for a two-ended character control (Even <-> Odd, Body <-> Air):
    //   TITLE ............................. value (click to type)
    //   [================|=============]
    //   LEFT END                     RIGHT END
    // Bound to one host parameter; drag, Ctrl-drag fine, double-click reset.
    // 5.3.6 compact: one row, TITLE  [=======|=======]  value, for a setting that sits under the rings it
    // qualifies (Detail); the value names the end it leans to ("Broad 40" / "Fine 40"), the tooltip both.
    class Fader : public juce::Component
    {
    public:
        Fader (juce::AudioProcessorValueTreeState& state, const juce::String& paramId, const juce::String& title,
               const juce::String& leftEnd, const juce::String& rightEnd, juce::Colour role, bool bipolar,
               const juce::String& help, bool compact = false);

        void paint (juce::Graphics&) override;
        void resized() override;

        static constexpr int compactTitleW = 58, compactValueW = 66;

        juce::Slider slider;

    private:
        void updateValueText();

        juce::String title, leftEnd, rightEnd;
        bool compact = false;
        juce::Label value;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
}
