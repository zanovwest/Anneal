#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace anneal
{
    // Header control for the Latency parameter: "LATENCY  21.3 ms  v". Click for Low / Normal /
    // Precise with their actual latency at the current sample rate.
    class LatencyMenu : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        LatencyMenu (juce::AudioProcessorValueTreeState& state, std::function<double (int)> msForMode,
                     std::function<double()> appliedMs);

        void refresh();   // from the editor timer
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
        void mouseDown (const juce::MouseEvent&) override;

        static constexpr int captionWidth = 62;

    private:
        std::function<double (int)> msForMode;
        std::function<double()> appliedMs;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        juce::RangedAudioParameter* param = nullptr;
        int choice = 0;
        double shownMs = -1.0;
        bool hover = false;
    };
}
