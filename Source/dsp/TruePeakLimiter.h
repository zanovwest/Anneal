#pragma once

#include <vector>

namespace anneal
{
    // Look-ahead true-peak limiter for the output ceiling (3.0).
    //  - Detector: each sample and three points between samples (4x windowed-sinc interpolation),
    //    so inter-sample ("true") peaks are caught, linked across channels.
    //  - Gain: the required gain min(1, ceiling / peak), a sliding minimum over the attack window,
    //    a ~80 ms release, then a box average over the same window, so the gain is fully down by
    //    the time the peak plays and the ramp is smooth.
    //  - The audio is always delayed by getLatency() (constant, also when off), so switching the
    //    ceiling never changes the latency. Off = exactly that delay.
    // All memory is allocated in prepare(); process() is real-time safe.
    class TruePeakLimiter
    {
    public:
        static constexpr int maxChannels = 2;

        void prepare (double sampleRate, int numChannels);
        void reset();
        int getLatency() const noexcept { return latency; }

        void setCeiling (bool enabled, float ceilingDbtp) noexcept;
        void process (float* const* channels, int numChannels, int numSamples) noexcept;   // in place

        // Deepest gain reduction since the last call (dB, <= 0). For the meter.
        float takeGainReductionDb() noexcept;

    private:
        static constexpr int taps = 16, detectDelay = taps / 2 - 1, phases = 3;   // needs 7 future samples
        float coeff[phases][taps] {};

        int attack = 60, latency = 63, channels = 2;
        bool enabled = false;
        float ceilingLin = 1.0f, releaseCoef = 0.0f, held = 1.0f, deepest = 1.0f;

        std::vector<float> history[maxChannels];   // last `taps` input samples
        std::vector<float> delay[maxChannels];     // audio delay line, latency + 1
        int delayPos = 0;
        std::vector<float> required, smoothed;     // rings of attack + 1
        int ringPos = 0;
        double boxSum = 0.0;
    };
}
