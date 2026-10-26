#include "LoudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace anneal
{
namespace
{
    // BS.1770: L = -0.691 + 10 log10 (sum over channels of the K-weighted mean square).
    double lufs (double meanSquare) noexcept { return -0.691 + 10.0 * std::log10 (meanSquare); }
    constexpr double absoluteGate = -70.0, relativeGate = -10.0;
}

LoudnessMeter::LoudnessMeter()
    : binEnergy ((size_t) numBins, 0.0), binCount ((size_t) numBins, 0.0)
{
    prepare (48000.0);
}

void LoudnessMeter::prepare (double fs)
{
    // BS.1770's K-weighting at any rate (B. De Man's analogue fit, as libebur128 and pyloudnorm use):
    // the ITU's own coefficients at 48 kHz, +0.70 dB at 1 kHz. LoudnessMatch's RBJ sections with the
    // same numbers are a looser fit (+0.44 dB at 1 kHz): fine for its ratio, not for a reading in LUFS.
    constexpr double pi = 3.14159265358979323846;
    double b[3], a[2];
    {
        const double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
        const double K = std::tan (pi * f0 / fs), vh = std::pow (10.0, gainDb / 20.0), vb = std::pow (vh, 0.4996667741545416);
        const double a0 = 1.0 + K / q + K * K;
        b[0] = (vh + vb * K / q + K * K) / a0;
        b[1] = 2.0 * (K * K - vh) / a0;
        b[2] = (vh - vb * K / q + K * K) / a0;
        a[0] = 2.0 * (K * K - 1.0) / a0;
        a[1] = (1.0 - K / q + K * K) / a0;
    }
    for (auto& f : k)
        f.shelf.setCoefficients (b[0], b[1], b[2], a[0], a[1]);
    {
        const double f0 = 38.13547087602444, q = 0.5003270373238773;
        const double K = std::tan (pi * f0 / fs), a0 = 1.0 + K / q + K * K;
        for (auto& f : k)
            f.hp.setCoefficients (1.0, -2.0, 1.0, 2.0 * (K * K - 1.0) / a0, (1.0 - K / q + K * K) / a0);
    }
    stepLength = std::max (1, (int) std::lround (0.1 * fs));
    reset();
}

void LoudnessMeter::reset() noexcept
{
    for (auto& f : k)
        f.reset();
    stepPos = 0;
    stepSum = 0.0;
    std::fill (std::begin (steps), std::end (steps), 0.0);
    stepIndex = stepsFilled = 0;
    shortTerm = none;
}

void LoudnessMeter::resetIntegrated() noexcept
{
    std::fill (binEnergy.begin(), binEnergy.end(), 0.0);
    std::fill (binCount.begin(), binCount.end(), 0.0);
    gatedEnergy = gatedCount = 0.0;
    integrated = none;
    freshSteps = stepPos == 0 ? 0 : -1;   // the step under way began before the reset: it doesn't count
}

void LoudnessMeter::process (const float* const* x, int numChannels, int numSamples) noexcept
{
    numChannels = std::clamp (numChannels, 1, maxChannels);
    for (int i = 0; i < numSamples; ++i)
    {
        double s = 0.0;
        for (int c = 0; c < numChannels; ++c)
        {
            const double y = k[c].process (x[c][i]);
            s += y * y;
        }
        stepSum += s;
        if (++stepPos == stepLength)
            endStep();
    }
}

void LoudnessMeter::endStep() noexcept
{
    steps[stepIndex] = stepSum;
    stepIndex = (stepIndex + 1) % shortSteps;
    stepsFilled = std::min (stepsFilled + 1, shortSteps);
    freshSteps = std::min (freshSteps + 1, blockSteps);
    stepPos = 0;
    stepSum = 0.0;

    // Short-term: the last 3 s (or as much as there is, in the first 3 s).
    double sum = 0.0;
    for (int j = 0; j < stepsFilled; ++j)
        sum += steps[j];
    const double shortMs = sum / ((double) stepsFilled * stepLength);
    shortTerm = shortMs > 0.0 && lufs (shortMs) > absoluteGate ? (float) lufs (shortMs) : none;

    // Integrated: the 400 ms block that ends here, into the histogram when it passes the absolute gate.
    if (stepsFilled < blockSteps || freshSteps < blockSteps)
        return;
    double block = 0.0;
    for (int j = 1; j <= blockSteps; ++j)
        block += steps[(stepIndex - j + shortSteps) % shortSteps];
    const double z = block / ((double) blockSteps * stepLength);
    if (! (z > 0.0) || ! (lufs (z) > absoluteGate))
        return;
    const int bin = std::clamp ((int) ((lufs (z) - histLo) / histStep), 0, numBins - 1);
    binEnergy[(size_t) bin] += z;
    binCount[(size_t) bin] += 1.0;
    gatedEnergy += z;
    gatedCount += 1.0;
    updateIntegrated();
}

void LoudnessMeter::updateIntegrated() noexcept
{
    // The relative gate: 10 LU under the mean of the blocks over the absolute gate. Blocks in the bin
    // the gate falls into count when the bin's middle is over it (within 0.025 LU of exact).
    const double gate = lufs (gatedEnergy / gatedCount) + relativeGate;
    const double first = (gate - histLo) / histStep - 0.5;
    double energy = 0.0, count = 0.0;
    for (int b = std::max (0, (int) std::floor (first) + 1); b < numBins; ++b)
    {
        energy += binEnergy[(size_t) b];
        count += binCount[(size_t) b];
    }
    integrated = count > 0.0 ? (float) lufs (energy / count) : none;
}
}
