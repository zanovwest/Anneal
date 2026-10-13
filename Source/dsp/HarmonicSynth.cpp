#include "HarmonicSynth.h"

#include <algorithm>
#include <cassert>
#include <cmath>

namespace anneal
{
namespace
{
    constexpr int maxChannels = 2;
    constexpr int regionHalfWidth = 4;              // Hann main lobe is +-2 bins; keep near side lobes
    constexpr float referenceAmplitude = 0.125893f; // -18 dBFS
    // Harmonic signs of a soft clipper (n = 2..6), so heavy heat flattens peaks rather than spiking them.
    constexpr float harmonicSign[HarmonicSynth::maxHarmonic + 1] = { 0.0f, 0.0f, 1.0f, -1.0f, -1.0f, 1.0f, 1.0f };
}

void HarmonicSynth::prepare (int newFftSize)
{
    fftSize = newFftSize;
    numBins = fftSize / 2 + 1;
    ampScale = 4.0f / (float) fftSize;   // zero-phase Hann: |X| = a * N / 4
    peaks.assign ((size_t) numBins, 0);
    bounds.assign ((size_t) numBins, 0);
    lastPeakCount = 0;
}

void HarmonicSynth::process (const float* const* spectra, int numChannels, const float* p,
                             const float* need, const HeatSettings& settings,
                             float* const* harm, float* harmPower)
{
    assert (numChannels >= 1 && numChannels <= maxChannels);
    numChannels = std::clamp (numChannels, 1, maxChannels);

    for (int c = 0; c < numChannels; ++c)
        std::fill (harm[c], harm[c] + 2 * numBins, 0.0f);
    std::fill (harmPower, harmPower + numBins, 0.0f);
    lastPeakCount = 0;

    if (settings.heat <= 1.0e-4f)
        return;

    const float drive = std::pow (10.0f, (settings.heat * 40.0f - 34.0f) / 20.0f);
    const float color = std::clamp (settings.color, 0.0f, 1.0f);
    const float evenW = std::min (1.0f, 2.0f * (1.0f - color));
    const float oddW  = std::min (1.0f, 2.0f * color);

    // 1. Peaks: local maxima over +-2 bins, within 70 dB of the frame maximum.
    float framePeak = 0.0f;
    for (int k = 0; k < numBins; ++k)
        framePeak = std::max (framePeak, p[k]);
    const float floorPower = std::max (1.0e-10f, framePeak * 1.0e-7f);

    int np = 0;
    for (int k = 2; k < numBins - 2; ++k)
    {
        const float v = p[k];
        if (v > floorPower && v > p[k - 1] && v >= p[k + 1] && v > p[k - 2] && v >= p[k + 2])
            peaks[(size_t) np++] = k;
    }
    lastPeakCount = np;

    // 2. Regions of influence split at the minimum between neighbouring peaks.
    for (int i = 0; i + 1 < np; ++i)
    {
        int m = peaks[(size_t) i] + 1;
        for (int k = m + 1; k < peaks[(size_t) i + 1]; ++k)
            if (p[k] < p[m])
                m = k;
        bounds[(size_t) i] = m;
    }

    // 3. Copy each region, shifted and rotated, once per harmonic.
    for (int i = 0; i < np; ++i)
    {
        const int k0 = peaks[(size_t) i];
        int lo = k0 - regionHalfWidth, hi = k0 + regionHalfWidth;
        if (i > 0)      lo = std::max (lo, bounds[(size_t) i - 1]);
        if (i + 1 < np) hi = std::min (hi, bounds[(size_t) i] - 1);
        lo = std::max (lo, 1);
        hi = std::min (hi, numBins - 2);

        const float t = std::tanh (std::sqrt (p[k0]) * drive / referenceAmplitude);

        // Fractional peak position from a parabola through the log power.
        const float l = std::log (p[k0 - 1] + 1.0e-30f);
        const float c = std::log (p[k0] + 1.0e-30f);
        const float r = std::log (p[k0 + 1] + 1.0e-30f);
        const float den = l - 2.0f * c + r;
        const float delta = den < 0.0f ? std::clamp (0.5f * (l - r) / den, -0.5f, 0.5f) : 0.0f;
        const double kf = k0 + (double) delta;

        float ur[maxChannels], ui[maxChannels], rr[maxChannels], ri[maxChannels];
        for (int ch = 0; ch < numChannels; ++ch)
        {
            const float re = spectra[ch][2 * k0], im = spectra[ch][2 * k0 + 1];
            const float mag = std::sqrt (re * re + im * im);
            ur[ch] = mag > 1.0e-20f ? re / mag : 1.0f;
            ui[ch] = mag > 1.0e-20f ? im / mag : 0.0f;
            rr[ch] = ur[ch];
            ri[ch] = ui[ch];
        }

        float tp = t;   // tanh(u)^(n-1)
        for (int n = 2; n <= maxHarmonic; ++n)
        {
            const int shift = (int) std::lround ((n - 1) * kf);
            if (lo + shift >= numBins - 1)
                break;

            const float amp = ((n % 2) == 0 ? evenW : oddW) * harmonicSign[n] * tp / (float) n;

            if (std::abs (amp) > 1.0e-7f)
            {
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float* X = spectra[ch];
                    float* H = harm[ch];
                    for (int k = lo; k <= hi; ++k)
                    {
                        const int dst = k + shift;
                        if (dst >= numBins - 1)
                            break;
                        const float g = amp * need[dst];
                        if (g == 0.0f)
                            continue;
                        const float xr = X[2 * k], xi = X[2 * k + 1];
                        H[2 * dst]     += g * (xr * rr[ch] - xi * ri[ch]);
                        H[2 * dst + 1] += g * (xr * ri[ch] + xi * rr[ch]);
                    }
                }
            }

            tp *= t;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float nr = rr[ch] * ur[ch] - ri[ch] * ui[ch];
                ri[ch] = rr[ch] * ui[ch] + ri[ch] * ur[ch];
                rr[ch] = nr;
            }
        }
    }

    const float powScale = ampScale * ampScale / (float) numChannels;
    for (int ch = 0; ch < numChannels; ++ch)
        for (int k = 0; k < numBins; ++k)
            harmPower[k] += (harm[ch][2 * k] * harm[ch][2 * k] + harm[ch][2 * k + 1] * harm[ch][2 * k + 1]) * powScale;
}
}
