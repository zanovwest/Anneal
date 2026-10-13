#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace anneal
{
    // What Heat and Color do to one partial: the fundamental and the 2nd-6th harmonics the
    // spectral saturator writes for a partial at the -18 dBFS reference level, where the model
    // asks for full enrichment. Computed from the same formula as HarmonicSynth (model-derived,
    // labelled as such), so it moves exactly with the knobs.
    class HarmonicGraph : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit HarmonicGraph (juce::AudioProcessorValueTreeState& state);

        void refresh();   // from the editor timer: repaints only when Heat or Color moved
        void paint (juce::Graphics&) override;

        // Level of harmonic n (2..6) relative to its fundamental, dB (-inf as -200). For tests.
        static float harmonicDb (int n, float heat01, float color01);

    private:
        std::atomic<float>* heat = nullptr;
        std::atomic<float>* color = nullptr;
        float shownHeat = -1.0f, shownColor = -1.0f;
    };
}
