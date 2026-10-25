#include "TransientShaper.h"

#include <algorithm>
#include <cmath>

namespace anneal
{
void TransientShaper::prepare (double sr) noexcept
{
    sampleRate = sr;
    auto coef = [sr] (double seconds) { return (float) (1.0 - std::exp (-1.0 / (seconds * sr))); };
    fastAttack = coef (0.001);
    fastRelease = coef (0.030);
    slowCoef = coef (0.040);
    letGo = (float) std::exp (-1.0 / (0.012 * sr));
    gainCoef = coef (0.0003);
    primeLength = std::max (1, (int) std::lround (0.002 * sr));
    shownLo = shownHi = -1.0f;
    reset();
}

void TransientShaper::reset() noexcept
{
    for (int c = 0; c < 2; ++c)
        hpS[c] = lpS[c] = 0.0f;
    for (auto& d : det)
        d = {};
    primeLeft = primeLength;
    peakDb = 0.0f;
}

void TransientShaper::setRange (float loHz, float hiHz) noexcept
{
    if (loHz == shownLo && hiHz == shownHi)
        return;
    shownLo = loHz;
    shownHi = hiHz;
    auto onePole = [this] (double hz)
    {
        const double t = std::tan (3.141592653589793 * std::clamp (hz, 1.0, 0.47 * sampleRate) / sampleRate);
        return (float) (t / (1.0 + t));
    };
    hpG = onePole (loHz);
    lpG = onePole (std::max (hiHz, loHz));
}

float TransientShaper::gainFor (Detector& d, float power, float amount) noexcept
{
    d.fast += (power > d.fast ? fastAttack : fastRelease) * (power - d.fast);
    if (primeLeft > 0)
        d.slow = d.fast;
    else
        d.slow += slowCoef * (d.fast - d.slow);

    // How much of an attack this is: 0 below a 4 dB rise over the slow level, 1 from 12 dB. Held
    // while it lasts, then let go. Only worked out while it can act.
    float onset = 0.0f;
    if (amount != 0.0f && d.fast > 2.5118864f * d.slow)
        onset = d.fast >= 15.848932f * d.slow ? 1.0f : (10.0f * std::log10 (d.fast / d.slow) - 4.0f) / 8.0f;
    d.weight = std::max (onset, d.weight * letGo);
    if (d.weight < 1.0e-4f)
        d.weight = 0.0f;

    const float target = amount * maxDb * d.weight;
    d.gainDb += gainCoef * (target - d.gainDb);
    if (target == 0.0f && std::abs (d.gainDb) < 1.0e-5f)
        d.gainDb = 0.0f;
    if (d.gainDb == 0.0f)
        return 1.0f;
    if (std::abs (d.gainDb) > std::abs (peakDb))
        peakDb = d.gainDb;
    return std::exp (d.gainDb * 0.11512925f);   // 10^(dB / 20)
}

void TransientShaper::process (float* const* io, int numChannels, int numSamples, const Settings& s) noexcept
{
    numChannels = std::clamp (numChannels, 1, 2);
    setRange (s.rangeLoHz, s.rangeHiHz);
    const bool ms = s.midSide && numChannels == 2;
    if (ms != lastMidSide)
    {
        lastMidSide = ms;
        primeLeft = primeLength;   // the components change: their levels start from what they are now
    }
    const float amount0 = s.punch * (ms ? s.midAmount : 1.0f);
    const float amount1 = ms ? s.punch * s.sideAmount : 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // The part inside Range: high-pass at Range Low, then low-pass at Range High (one-pole TPT).
        float band[2] = { 0.0f, 0.0f };
        for (int c = 0; c < numChannels; ++c)
        {
            const float x = io[c][i];
            float v = (x - hpS[c]) * hpG;
            const float below = v + hpS[c];
            hpS[c] = below + v;
            const float h = x - below;
            v = (h - lpS[c]) * lpG;
            const float b = v + lpS[c];
            lpS[c] = b + v;
            band[c] = b;
        }

        if (numChannels == 1)
        {
            const float g = gainFor (det[0], band[0] * band[0], amount0) - 1.0f;
            if (g != 0.0f)
                io[0][i] += g * band[0];
        }
        else if (! ms)
        {
            // Stereo: one detector on both channels, the same gain on both (the image stays).
            const float g = gainFor (det[0], band[0] * band[0] + band[1] * band[1], amount0) - 1.0f;
            if (g != 0.0f)
            {
                io[0][i] += g * band[0];
                io[1][i] += g * band[1];
            }
        }
        else
        {
            const float bm = 0.5f * (band[0] + band[1]), bs = 0.5f * (band[0] - band[1]);
            const float gm = gainFor (det[0], bm * bm, amount0) - 1.0f;
            const float gs = gainFor (det[1], bs * bs, amount1) - 1.0f;
            if (gm != 0.0f || gs != 0.0f)
            {
                io[0][i] += gm * bm + gs * bs;
                io[1][i] += gm * bm - gs * bs;
            }
        }
        if (primeLeft > 0)
            --primeLeft;
    }
}

float TransientShaper::takePeakDb() noexcept
{
    const float p = peakDb;
    peakDb = 0.0f;
    return p;
}
}
