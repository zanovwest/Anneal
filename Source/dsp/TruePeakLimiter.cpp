#include "TruePeakLimiter.h"

#include <algorithm>
#include <cmath>

namespace anneal
{
void TruePeakLimiter::prepare (double sampleRate, int numChannels)
{
    channels = std::clamp (numChannels, 1, maxChannels);
    attack = std::max (8, (int) std::lround (0.00125 * sampleRate));   // 1.25 ms
    latency = attack + detectDelay;
    releaseCoef = (float) (1.0 - std::exp (-1.0 / (0.080 * sampleRate)));

    // Windowed-sinc interpolators (16 taps) for the points 1/4, 1/2 and 3/4 of the way into the
    // interval that ends at x[t - detectDelay]. Each phase is normalised to unity gain at DC.
    constexpr double pi = 3.14159265358979323846;
    for (int p = 0; p < phases; ++p)
    {
        const double frac = (p + 1) * 0.25;
        double sum = 0.0;
        for (int k = 0; k < taps; ++k)
        {
            const double x = (double) (k - detectDelay) - frac;   // tap position relative to the point
            const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (pi * x) / (pi * x);
            const double halfWidth = taps / 2 + 0.5;
            const double w = std::abs (x) < halfWidth ? 0.5 * (1.0 + std::cos (pi * x / halfWidth)) : 0.0;
            coeff[p][k] = (float) (sinc * w);
            sum += coeff[p][k];
        }
        for (int k = 0; k < taps; ++k)
            coeff[p][k] = (float) (coeff[p][k] / sum);
    }

    for (int c = 0; c < maxChannels; ++c)
    {
        history[c].assign ((size_t) taps, 0.0f);
        delay[c].assign ((size_t) latency + 1, 0.0f);
    }
    required.assign ((size_t) attack + 1, 1.0f);
    smoothed.assign ((size_t) attack + 1, 1.0f);
    reset();
}

void TruePeakLimiter::reset()
{
    for (int c = 0; c < maxChannels; ++c)
    {
        std::fill (history[c].begin(), history[c].end(), 0.0f);
        std::fill (delay[c].begin(), delay[c].end(), 0.0f);
    }
    std::fill (required.begin(), required.end(), 1.0f);
    std::fill (smoothed.begin(), smoothed.end(), 1.0f);
    boxSum = (double) smoothed.size();
    delayPos = ringPos = 0;
    held = deepest = 1.0f;
}

void TruePeakLimiter::setCeiling (bool on, float ceilingDbtp) noexcept
{
    if (on != enabled && on)
    {
        // Start from "no reduction" so switching on never applies a stale gain.
        std::fill (required.begin(), required.end(), 1.0f);
        std::fill (smoothed.begin(), smoothed.end(), 1.0f);
        boxSum = (double) smoothed.size();
        held = 1.0f;
    }
    enabled = on;
    ceilingLin = std::pow (10.0f, ceilingDbtp / 20.0f);
}

float TruePeakLimiter::takeGainReductionDb() noexcept
{
    const float db = 20.0f * std::log10 (std::max (deepest, 1.0e-6f));
    deepest = 1.0f;
    return db;
}

void TruePeakLimiter::process (float* const* io, int numChannels, int numSamples) noexcept
{
    numChannels = std::min (numChannels, channels);
    const int len = latency + 1, win = attack + 1;

    for (int i = 0; i < numSamples; ++i)
    {
        // Audio: write the new sample, read the one from `latency` samples ago.
        const int readPos = delayPos + 1 == len ? 0 : delayPos + 1;
        float out[maxChannels] {};
        float peak = 0.0f;
        for (int c = 0; c < numChannels; ++c)
        {
            const float x = io[c][i];
            delay[c][(size_t) delayPos] = x;
            out[c] = delay[c][(size_t) readPos];

            // The detector's history follows the audio also while the ceiling is off, so switching
            // it on never measures samples from the last time it was on (a spurious dip).
            float* h = history[c].data();
            std::copy (h + 1, h + taps, h);
            h[taps - 1] = x;
            if (enabled)
            {
                // True peak of the interval ending at x[t - detectDelay]: that sample and three points before it.
                float m = std::abs (h[detectDelay + 1]);
                for (int p = 0; p < phases; ++p)
                {
                    float y = 0.0f;
                    for (int k = 0; k < taps; ++k)
                        y += coeff[p][k] * h[k];
                    m = std::max (m, std::abs (y));
                }
                peak = std::max (peak, m);
            }
        }
        delayPos = readPos;

        float gain = 1.0f;
        if (enabled)
        {
            // Required gain -> sliding minimum over the attack window -> release -> box average.
            required[(size_t) ringPos] = peak > ceilingLin ? ceilingLin / peak : 1.0f;
            float minimum = 1.0f;
            for (int k = 0; k < win; ++k)
                minimum = std::min (minimum, required[(size_t) k]);
            held = minimum < held ? minimum : held + releaseCoef * (minimum - held);

            boxSum += (double) held - smoothed[(size_t) ringPos];
            smoothed[(size_t) ringPos] = held;
            gain = (float) std::min (1.0, boxSum / (double) win);
            ringPos = ringPos + 1 == win ? 0 : ringPos + 1;
            deepest = std::min (deepest, gain);
        }

        for (int c = 0; c < numChannels; ++c)
            io[c][i] = out[c] * gain;
    }
}
}
