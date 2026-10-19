#pragma once

#include <vector>

namespace anneal
{
    // Real-input FFT with the in-place layout and scaling of juce::dsp::FFT's real-only transforms:
    //  - forward: N real samples in data[0..N-1] -> bins 0..N/2 as interleaved (re, im) pairs in
    //    data[0..N+1], unscaled. Only those N + 2 values are written.
    //  - inverse: bins 0..N/2 in data[0..N+1] -> N real samples in data[0..N-1], scaled by 1/N.
    //    The imaginary parts of DC and Nyquist are ignored, as in JUCE.
    // JUCE's portable fallback (the FFT Windows and Linux builds get) transforms N complex points
    // for N real ones. This packs the signal into N/2 complex points (the usual even/odd split) and
    // runs a split-format radix-4 Stockham FFT whose inner loops the compiler vectorises.
    // prepare() allocates; forward() and inverse() are real-time safe. One instance per thread.
    class RealFft
    {
    public:
        void prepare (int size);   // a power of two, at least 8
        int getSize() const noexcept { return n; }

        void forward (float* data) noexcept;
        void inverse (float* data) noexcept;

    private:
        struct Stage { int length = 0, stride = 0; size_t twiddle = 0; };

        // Forward complex FFT of m points, split format. Starts in (re, im); returns true when the
        // result ended up in (re2, im2) rather than (re, im).
        bool transform (float* re, float* im, float* re2, float* im2) noexcept;

        int n = 0, m = 0;
        std::vector<Stage> stages;
        std::vector<float> twiddles;       // per radix-4 stage: w^p, w^2p, w^3p (re, im), length/4 each
        std::vector<float> splitCos, splitSin;   // cos / sin (2 pi k / N), k < N/2
        std::vector<float> ar, ai, br, bi;
    };
}
