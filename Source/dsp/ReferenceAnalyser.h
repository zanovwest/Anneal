#pragma once

#include "PerceptualModel.h"
#include "RealFft.h"
#include "Reference.h"

#include <juce_dsp/juce_dsp.h>

#include <memory>
#include <vector>

namespace anneal
{
    // Offline analysis of a reference recording (any sample rate, any channel count) dropped on the
    // display: the power mean of every frame, as one-ERB levels (the measure the model uses for the
    // key and the main signal), normalised. Allocates: never on the audio thread.
    class ReferenceAnalyser
    {
    public:
        explicit ReferenceAnalyser (double sampleRate);

        // Any block size; channels are averaged in power.
        void push (const float* const* channels, int numChannels, int numSamples);
        double getSecondsAnalysed() const noexcept { return (double) samplesIn / sampleRate; }

        // The curve, invalid when the recording is silent (nothing above -100 dBFS in any band).
        ReferenceCurve finish();

    private:
        void processFrame();

        static constexpr int maxChannels = 8;
        double sampleRate;
        int fftSize = 8192, hop = 4096, fill = 0, channelsSeen = 1;
        long long samplesIn = 0, frames = 0;
        RealFft fft;
        std::vector<float> window, scratch;
        std::vector<float> fifo[maxChannels];
        std::vector<double> powerSum;
    };
}
