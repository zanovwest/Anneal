#include "LoudnessMatch.h"

#include <algorithm>
#include <cmath>

namespace anneal
{
namespace
{
    constexpr double pi = 3.14159265358979323846;
}

// RBJ cookbook high shelf, normalised by a0.
void Biquad::setHighShelf (double fs, double f0, double gainDb, double q) noexcept
{
    const double A = std::pow (10.0, gainDb / 40.0);
    const double w0 = 2.0 * pi * f0 / fs, cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    const double sa = 2.0 * std::sqrt (A) * alpha;
    const double a0 = (A + 1) - (A - 1) * cw + sa;
    b0 = A * ((A + 1) + (A - 1) * cw + sa) / a0;
    b1 = -2.0 * A * ((A - 1) + (A + 1) * cw) / a0;
    b2 = A * ((A + 1) + (A - 1) * cw - sa) / a0;
    a1 = 2.0 * ((A - 1) - (A + 1) * cw) / a0;
    a2 = ((A + 1) - (A - 1) * cw - sa) / a0;
}

void Biquad::setHighPass (double fs, double f0, double q) noexcept
{
    const double w0 = 2.0 * pi * f0 / fs, cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
    const double a0 = 1.0 + alpha;
    b0 = (1.0 + cw) * 0.5 / a0;
    b1 = -(1.0 + cw) / a0;
    b2 = (1.0 + cw) * 0.5 / a0;
    a1 = -2.0 * cw / a0;
    a2 = (1.0 - alpha) / a0;
}

void LoudnessMatch::prepare (double fs)
{
    // BS.1770 K-weighting expressed as RBJ sections (same parameters pyloudnorm uses).
    for (int c = 0; c < maxChannels; ++c)
        for (auto* k : { &kDry[c], &kWet[c] })
        {
            k->shelf.setHighShelf (fs, 1681.974450955533, 3.999843853973347, 0.7071752369554196);
            k->hp.setHighPass (fs, 38.13547087602444, 0.5003270373238773);
        }
    alpha = 1.0 - std::exp (-1.0 / (3.0 * fs));
    // Only the filters' audio history starts over. The measurement (a slow ratio of K-weighted
    // powers, the same at any rate) and its gain are kept when the host prepares again (4.0.2).
    for (int c = 0; c < maxChannels; ++c)
    {
        kDry[c].reset();
        kWet[c].reset();
    }
}

void LoudnessMatch::reset()
{
    for (int c = 0; c < maxChannels; ++c)
    {
        kDry[c].reset();
        kWet[c].reset();
    }
    msDry = msWet = 0.0;
    gain = 1.0f;
}

float LoudnessMatch::process (const float* const* dry, const float* const* wet, int numChannels, int numSamples) noexcept
{
    numChannels = std::min (numChannels, maxChannels);

    for (int i = 0; i < numSamples; ++i)
    {
        double sd = 0.0, sw = 0.0;
        for (int c = 0; c < numChannels; ++c)
        {
            const double d = kDry[c].process (dry[c][i]);
            const double w = kWet[c].process (wet[c][i]);
            sd += d * d;
            sw += w * w;
        }
        msDry += alpha * (sd - msDry);
        msWet += alpha * (sw - msWet);
    }

    if (msDry > 1.0e-7 && msWet > 1.0e-12)   // hold the last gain in silence (below ~-70 dB)
        gain = (float) std::clamp (std::sqrt (msDry / msWet), 0.2511886, 3.9810717);

    return gain;
}
}
