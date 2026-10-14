#pragma once

#include <vector>

namespace anneal
{
    // Saturation flavours (0.2.6): which harmonics Heat writes and where. Clean is the original
    // law, unchanged; the others reshape it like the hardware they are named after.
    enum Flavour { flavourClean = 0, flavourTape = 1, flavourTube = 2, flavourTransformer = 3, flavourExciter = 4 };

    struct HeatSettings
    {
        float heat  = 0.0f;   // 0..1 drive
        float color = 0.35f;  // 0 = even harmonics only, 1 = odd only, 0.5 = both at full weight
        int flavour = flavourClean;
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
        static constexpr int numFlavours = 5;

        void prepare (int fftSize, double sampleRate = 48000.0);

        // spectra: per channel, interleaved re/im for fftSize/2+1 bins (zero-phase frames).
        // linkedPower: amplitude^2 per bin (mean over channels). need: per-bin weight 0..2
        // applied at the destination bin. harm: per-channel outputs (overwritten).
        // harmPower: amplitude^2 of the written harmonics per bin (mean over channels).
        void process (const float* const* spectra, int numChannels, const float* linkedPower,
                      const float* need, const HeatSettings& settings,
                      float* const* harm, float* harmPower);

        int getLastPeakCount() const noexcept { return lastPeakCount; }

        // The flavour law, shared with the harmonic bars in the editor and the tests. Harmonic n
        // (2..6) of a partial of amplitude a at f0 Hz is written with
        //   parity(Color) * weight(n) * gate(f0) * sign(n) * tanh(u)^(n-1) / n,   u = a * drive * driveScale(f0) / -18 dBFS,
        // and scaled by destination(f) at the bin it lands in (and by the model's need there).
        static float flavourWeight (int flavour, int n) noexcept;              // Clean: 1 for every n
        static float flavourDriveScale (int flavour, double f0) noexcept;      // Transformer: lows saturate more
        static float flavourGate (int flavour, double f0) noexcept;            // Exciter: only partials above ~2 kHz
        static float flavourDestination (int flavour, double hz) noexcept;     // Tape: gentle roll-off above 5 kHz
        // Level of harmonic n relative to a -18 dBFS partial at f0, where the model asks for full
        // enrichment (linear amplitude). This is exactly what process() writes for such a partial.
        static float harmonicLevel (int n, float heat, float color, int flavour, double f0) noexcept;

    private:
        int fftSize = 0, numBins = 0, lastPeakCount = 0;
        float ampScale = 0.0f;
        double binHz = 48000.0 / 4096.0;
        std::vector<int> peaks, bounds;
        std::vector<float> tapeDestination;   // flavourDestination (Tape) per bin
    };
}
