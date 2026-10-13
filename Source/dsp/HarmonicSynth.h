#pragma once

#include <vector>

namespace anneal
{
    struct HeatSettings
    {
        float heat  = 0.0f;   // 0..1 drive
        float color = 0.35f;  // 0 = even harmonics only, 1 = odd only, 0.5 = both at full weight
    };

    // Spectral saturation: every spectral peak grows its own harmonic series (2nd..6th)
    // directly in the FFT domain. Peak regions are shifted by whole bins and phase-rotated
    // by (n-1) * peak phase, which phase-locks each harmonic to its fundamental.
    // Because partials are enriched independently there are no intermodulation products,
    // and harmonics beyond the top bin are simply never written (no aliasing).
    class HarmonicSynth
    {
    public:
        static constexpr int maxHarmonic = 6;

        void prepare (int fftSize);

        // spectra: per channel, interleaved re/im for fftSize/2+1 bins (zero-phase frames).
        // linkedPower: amplitude^2 per bin (mean over channels). need: per-bin weight 0..2
        // applied at the destination bin. harm: per-channel outputs (overwritten).
        // harmPower: amplitude^2 of the written harmonics per bin (mean over channels).
        void process (const float* const* spectra, int numChannels, const float* linkedPower,
                      const float* need, const HeatSettings& settings,
                      float* const* harm, float* harmPower);

        int getLastPeakCount() const noexcept { return lastPeakCount; }

    private:
        int fftSize = 0, numBins = 0, lastPeakCount = 0;
        float ampScale = 0.0f;
        std::vector<int> peaks, bounds;
    };
}
