#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace anneal
{
    // Sample-peak output meter: instant attack, 24 dB/s fall, 1.5 s peak hold.
    // The red clip latch lights only when a sample exceeded 0 dBFS; click to clear.
    class OutputMeter : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        OutputMeter();

        void push (const float* linearPeaks, int numChannels, float dtSeconds);
        // 3.0: the ceiling's gain reduction (dB, <= 0) since the last tick; shown under the peak.
        void pushReduction (float reductionDb, float dtSeconds);
        float getShownReductionDb() const noexcept { return reduction; }
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;

    private:
        float yForDb (float db, juce::Rectangle<float> bar) const noexcept;

        static constexpr float topDb = 6.0f, bottomDb = -48.0f;
        float level[2] { -100.0f, -100.0f }, hold[2] { -100.0f, -100.0f }, holdAge[2] { 0.0f, 0.0f };
        bool clipped[2] { false, false };
        float reduction = 0.0f, reductionAge = 0.0f;   // deepest held 1.5 s, then decays 12 dB/s
        int channels = 2;
    };
}
