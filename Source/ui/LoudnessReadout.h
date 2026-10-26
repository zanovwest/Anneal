#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace anneal
{
    // 5.3.1 Loudness readout, in the header beside MATCH: EBU R128 loudness in LUFS, input -> output,
    // short-term (S, the last 3 s) over integrated (I, since the last click). Numbers only; what they
    // mean is in the tooltip. A click starts the integrated measurement over.
    class LoudnessReadout : public juce::Component, public juce::TooltipClient
    {
    public:
        LoudnessReadout();

        // LUFS; anything at or under -70 (LoudnessMeter::none) shows as a dash. Repaints only on a change.
        void set (float shortIn, float shortOut, float integratedIn, float integratedOut);
        std::function<void()> onReset;

        juce::String getShownText() const;   // "S <in> > <out> | I <in> > <out>", for the tests
        static juce::String format (float lufs);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        juce::String getTooltip() override;

    private:
        juce::String text[4];   // short in, short out, integrated in, integrated out
    };
}
