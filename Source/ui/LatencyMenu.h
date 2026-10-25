#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>

namespace anneal
{
    // Header control for the Latency parameter: "LATENCY  21.3 ms  v". Click for Zero / Low / Normal /
    // Precise with their actual latency at the current sample rate, the Spread load switch (5.2.1) and
    // the Punch at zero latency switch (5.2.7).
    class LatencyMenu : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        LatencyMenu (juce::AudioProcessorValueTreeState& state, std::function<double (int)> msForMode,
                     std::function<double()> appliedMs, std::function<double()> spreadMs = {});

        void refresh();   // from the editor timer
        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
        void mouseDown (const juce::MouseEvent&) override;

        static constexpr int captionWidth = 62;

    private:
        std::function<double (int)> msForMode;
        std::function<double()> appliedMs, spreadMs;
        std::unique_ptr<juce::ParameterAttachment> attachment, zeroAttachment;   // 5.1: Zero latency
        std::unique_ptr<juce::ParameterAttachment> spreadAttachment;             // 5.2.1: Spread load
        std::unique_ptr<juce::ParameterAttachment> zeroPunchAttachment;          // 5.2.7: Punch at zero latency
        juce::RangedAudioParameter* param = nullptr;
        int choice = 0;
        bool zero = false, spread = false, zeroPunch = true;
        double shownMs = -1.0;
        bool hover = false;
    };
}
