#pragma once

#include "dsp/DisplayFrame.h"
#include "dsp/TripleBuffer.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

namespace anneal
{
    // The main display. Horizontal axis is the ERB-rate (Cam) scale, the same grid the model
    // uses. Three aligned lanes: spectrum (in/out), live auto-EQ gain, harmonic energy written.
    // Range handles are bound to the rangeLo / rangeHi parameters.
    class PerceptionScreen : public juce::Component,
                             public juce::SettableTooltipClient,
                             private juce::Timer
    {
    public:
        PerceptionScreen (TripleBuffer<DisplayFrame>& source, juce::AudioProcessorValueTreeState& state);
        ~PerceptionScreen() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        // Snapshot/test helpers: take the newest frame as-is, and place the hover read-out.
        void refreshNow();
        void setReadoutHz (float hz);

    private:
        enum class Handle { none, lo, hi };

        void timerCallback() override;
        float xForCam (double cam) const noexcept;
        double camForX (float x) const noexcept;
        float valueAtCam (const float* bands, double cam) const noexcept;
        Handle handleAt (float x, float y) const noexcept;

        void paintSpectrum (juce::Graphics&);
        void paintEq (juce::Graphics&);
        void paintHeat (juce::Graphics&);
        void paintRange (juce::Graphics&);
        void paintReadout (juce::Graphics&);
        void paintLegend (juce::Graphics&);

        TripleBuffer<DisplayFrame>& source;

        static constexpr int nb = DisplayFrame::numBands;
        std::array<float, nb> inDb {}, outDb {}, gainDb {}, heatDb {};
        float z0 = 0.0f, dz = 0.0f;
        bool haveFrame = false;
        int idleTicks = 0;

        juce::Rectangle<float> plot, specLane, eqLane, heatLane;

        std::unique_ptr<juce::ParameterAttachment> loAttachment, hiAttachment;
        juce::RangedAudioParameter* loParam = nullptr;
        juce::RangedAudioParameter* hiParam = nullptr;
        float rangeLo = 30.0f, rangeHi = 18000.0f;
        Handle dragging = Handle::none, hovered = Handle::none;
        float hoverX = -1.0f;
    };
}
