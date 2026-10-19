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
        void design (const float* binGain, int analysisBins, float sideScale, float monoHz, float* out) noexcept;

    private:
        int filter (int which, int set, const float* x, float* out, int n) noexcept;   // returns 0 if muted

        double sampleRate = 48000.0;
        int channels = 2, taps = 1024, designSize = 4096, maxSegment = 256, fadeLength = 64;
        RealFft fft;
        std::vector<float> work, logMag;
        std::vector<float> coef[2][2];           // [0 Mid, 1 Side][set]
        int active[2][2] {};                     // taps in use (the rest are below -120 dB)
        bool muted[2][2] {};                     // Side at Width 0: no Side at all
        int current = 0, fadePos = -1;           // fadePos >= 0 while crossfading to the other set
        std::vector<float> hist[2];              // Mid / Side input: taps - 1 past samples, then a segment
        std::vector<float> outMid, outSide, scratch;
        std::uint32_t lastFrame = 0;
    };
}
