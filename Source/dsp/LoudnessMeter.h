#pragma once

#include "LoudnessMatch.h"   // Biquad

#include <cstdint>
#include <vector>

namespace anneal
{
    // 5.3.1 loudness readout: EBU R128 / ITU-R BS.1770-4 loudness of one signal, in LUFS.
    //  - BS.1770's K-weighting (its exact coefficients at 48 kHz), channels summed with weight 1.
    //  - Short-term: the mean square of the last 3 s, updated every 100 ms.
    //  - Integrated: 400 ms blocks every 100 ms (75 % overlap), gated at -70 LUFS and then 10 LU under
    //    the mean of what passed. Each block goes into a histogram (0.05 LU bins, -70 to +10 LUFS,
    //    holding its count and mean square), so a measurement of any length never allocates.
    // process() is real-time safe; the constructor allocates the histogram.
    class LoudnessMeter
    {
    public:
        static constexpr float none = -200.0f;   // nothing measured (silence, or not 400 ms yet)

        LoudnessMeter();

        void prepare (double sampleRate);   // also forgets the last 3 s; keeps the integrated measurement
        void reset() noexcept;              // forgets the last 3 s (audio); keeps the integrated measurement
        void resetIntegrated() noexcept;    // starts the integrated measurement over

        void process (const float* const* channels, int numChannels, int numSamples) noexcept;

        float getShortTerm() const noexcept { return shortTerm; }
        float getIntegrated() const noexcept { return integrated; }

    private:
        void endStep() noexcept;
        void updateIntegrated() noexcept;

        static constexpr int maxChannels = 2, shortSteps = 30, blockSteps = 4;
        static constexpr double histLo = -70.0, histHi = 10.0, histStep = 0.05;
        static constexpr int numBins = (int) ((histHi - histLo) / histStep + 0.5);

        struct KFilter
        {
            Biquad shelf, hp;
            double process (double x) noexcept { return hp.process (shelf.process (x)); }
            void reset() noexcept { shelf.reset(); hp.reset(); }
        };
        KFilter k[maxChannels];

        int stepLength = 4800, stepPos = 0;   // samples per 100 ms step, and how far into this one
        double stepSum = 0.0;                 // sum over this step of the channels' K-weighted squares
        double steps[shortSteps] {};          // the last 30 steps' sums (a ring)
        int stepIndex = 0, stepsFilled = 0;
        int freshSteps = 0;                   // whole steps since resetIntegrated(): blocks start after it

        std::vector<double> binEnergy, binCount;   // per bin: the blocks' summed mean squares, and how many
        double gatedEnergy = 0.0, gatedCount = 0.0;   // every block over -70 LUFS
        float shortTerm = none, integrated = none;
    };
}
