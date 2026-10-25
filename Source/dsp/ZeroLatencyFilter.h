#pragma once

#include "RealFft.h"

#include <cstdint>
#include <vector>

namespace anneal
{
    class SpectralEngine;

    // 5.1 Zero latency. The auto-EQ curve that a SpectralEngine measures (in analysis-only mode) is
    // applied to the live signal as minimum-phase FIR filters, one for Mid and one for Side: with
    // no look-ahead there is no latency. Every analysis frame (~5 ms) designs new filters, and the
    // output crossfades to them over ~1.3 ms. Width and Mono bass are part of the Side filter.
    // The curve follows the sound a frame later than in the latency modes; the price of zero latency.
    class ZeroLatencyFilter
    {
    public:
        // analysisFftSize: the analysis engine's FFT size (the filters are as long as its frames).
        void prepare (double sampleRate, int numChannels, int analysisFftSize, int analysisHop);
        void reset() noexcept;   // back to pass-through filters

        // In place, real-time safe. The analysis engine is fed the input here (it leaves it as it is).
        void process (float* const* io, int numChannels, int numSamples, SpectralEngine& analysis,
                      const float* const* key, int keyChannels, float width, float monoHz) noexcept;

        int getTaps() const noexcept { return taps; }

        // The minimum-phase taps for a curve (exposed for tests): binGain = linear gain per analysis
        // bin (analysisBins = N/2 + 1), times sideScale, Mono bass below monoHz (0 = off).
        // 5.2.2 widthShare (per analysis bin, or nullptr): sideScale acts by that share, 1 + s (scale - 1).
        void design (const float* binGain, int analysisBins, float sideScale, float monoHz, float* out,
                     const float* widthShare = nullptr) noexcept;
        // 5.2.5: the design's log, exp, sin and cos go through fastmath, in batches; true = the standard
        // library one value at a time, as up to 5.2.4 (for the tests and the profile).
        void setExactMath (bool on) noexcept { exactMath = on; }
        // 5.2.6: the cepstrum and the phase come from half-size transforms (a DCT and a DST through one
        // N/2-point real FFT each) and the magnitude is the curve itself, not exp (log); true = 5.2.5's
        // full-size transforms (for the tests and the profile). Exact math always uses the full ones.
        void setFullTransforms (bool on) noexcept { fullTransforms = on; }
        // Samples during which the old and new filters both ran (the crossfades), for the tests.
        std::uint64_t getCrossfadedSamples() const noexcept { return crossfaded; }

    private:
        int filter (int which, int set, const float* x, float* out, int n) noexcept;   // returns 0 if muted
        // The design's steps 2 to 4: logMag (natural log) -> the minimum-phase spectrum in w[0..designSize + 1].
        void spectrumFull (float* w) noexcept;   // two designSize-point transforms (up to 5.2.5)
        void spectrumHalf (float* w) noexcept;   // 5.2.6: two designSize / 2-point ones, magnitude from gain

        double sampleRate = 48000.0;
        int channels = 2, taps = 1024, designSize = 4096, maxSegment = 256, fadeLength = 64;
        RealFft fft, fftHalf;                    // designSize and designSize / 2 points
        std::vector<float> work, logMag;
        std::vector<float> phase, sinPart, cosPart, fadeOut;   // 5.2.5: batched design, the fade's table
        std::vector<float> gain, halfSin, halfCos;             // 5.2.6: the curve; sin / cos (pi k / (designSize / 2))
        bool exactMath = false, fullTransforms = false;
        std::uint64_t crossfaded = 0;
        std::vector<float> coef[2][2];           // [0 Mid, 1 Side][set]
        int active[2][2] {};                     // taps in use (the rest are below -120 dB)
        bool muted[2][2] {};                     // Side at Width 0 (unpainted): no Side at all
        int current = 0, fadePos = -1;           // fadePos >= 0 while crossfading to the other set
        std::vector<float> hist[2];              // Mid / Side input: taps - 1 past samples, then a segment
        std::vector<float> outMid, outSide, scratch;
        std::uint32_t lastFrame = 0;
    };
}
