#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace anneal
{
    // What Heat, Color and Flavour do to one partial: the fundamental and the 2nd-6th harmonics
    // the spectral saturator writes for a partial at the -18 dBFS reference level, where the model
    // asks for full enrichment. Computed with HarmonicSynth's own law (model-derived, labelled as
    // such), so it moves exactly with the controls. Frequency-dependent flavours are shown for a
    // partial they act on (referenceHz): Transformer at 120 Hz, Exciter at 4 kHz.
    class HarmonicGraph : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit HarmonicGraph (juce::AudioProcessorValueTreeState& state);

        void refresh();   // from the editor timer: repaints only when Heat, Color or Flavour moved
        void paint (juce::Graphics&) override;

        // Level of harmonic n (2..6) relative to its fundamental, dB (-inf as -200). For tests.
        static float harmonicDb (int n, float heat01, float color01, int flavour = 0);
        static double referenceHz (int flavour);

    private:
        std::atomic<float>* heat = nullptr;
        std::atomic<float>* color = nullptr;
        std::atomic<float>* flavour = nullptr;
        float shownHeat = -1.0f, shownColor = -1.0f;
        int shownFlavour = -1;
    };
}
