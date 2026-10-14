#include "PerceptualModel.h"
#include "ErbScale.h"

#include <algorithm>
#include <cmath>

namespace anneal
{
namespace
{
    constexpr double slopeUpDbPerCam = 9.0;    // masker below target (upward spread of masking)
    constexpr double slopeDnDbPerCam = 16.0;   // masker above target
    constexpr float maxCutDb   = 9.0f;
    constexpr float maxBoostDb = 8.0f;
    constexpr int idleAge = 1 << 20;   // transient age meaning "no attack in progress"

    // Summing amplitude-normalised Hann bins gives 3x the signal power (see ENGINE.md).
    constexpr float bandPowerScale = 1.0f / 3.0f;

    inline float clamp01 (float x) noexcept { return std::clamp (x, 0.0f, 1.0f); }
    inline float smoothstep (float x) noexcept { x = clamp01 (x); return x * x * (3.0f - 2.0f * x); }
    inline float powToDb (double p) noexcept { return 10.0f * (float) std::log10 (std::max (p, 1.0e-20)); }
}

void PerceptualModel::prepare (double newSampleRate, int newFftSize)
{
    sampleRate = newSampleRate;
    fftSize = newFftSize;
    numBins = fftSize / 2 + 1;
    binHz = sampleRate / fftSize;

    const double fTop = std::min (20000.0, 0.49 * sampleRate);
    z0 = erb::hzToCam (20.0);
    dz = (erb::hzToCam (fTop) - z0) / (numBands - 1);

    boxHalf    = std::max (1, (int) std::lround (0.5 / dz));    // +-0.5 Cam = one ERB
    exclusion  = 2 * boxHalf + 1;                               // context starts one ERB away
    smoothHalf = std::max (1, (int) std::lround (0.25 / dz));

    aUp  = std::pow (10.0, -slopeUpDbPerCam * dz / 10.0);
    aDn  = std::pow (10.0, -slopeDnDbPerCam * dz / 10.0);
    aUpM = std::pow (aUp, exclusion);
    aDnM = std::pow (aDn, exclusion);

    bandHz.resize (numBands);
    for (int b = 0; b < numBands; ++b)
        bandHz[(size_t) b] = erb::camToHz (z0 + b * dz);

    // Band -> bin partition by exact frequency overlap.
    partStart.assign (numBands + 1, 0);
    partBin.clear();
    partWeight.clear();
    partBin.reserve ((size_t) (numBands + numBins) * 2);
    partWeight.reserve ((size_t) (numBands + numBins) * 2);

    for (int b = 0; b < numBands; ++b)
    {
        const double fl = std::max (0.0, erb::camToHz (z0 + (b - 0.5) * dz));
        const double fh = erb::camToHz (z0 + (b + 0.5) * dz);
        const int kFirst = std::max (0, (int) std::floor (fl / binHz + 0.5));
        const int kLast  = std::min (numBins - 1, (int) std::floor (fh / binHz + 0.5));

        for (int k = kFirst; k <= kLast; ++k)
        {
            const double bl = (k - 0.5) * binHz, bh = (k + 0.5) * binHz;
            const double overlap = std::min (fh, bh) - std::max (fl, bl);
            if (overlap > 0.0)
            {
                partBin.push_back (k);
                partWeight.push_back ((float) (overlap / binHz));
            }
        }
        partStart[(size_t) b + 1] = (int) partBin.size();
    }

    // Bin -> band interpolation, plus a gate that keeps DC/sub-20 Hz and >fTop bins neutral.
    binBand.resize ((size_t) numBins);
    binFrac.resize ((size_t) numBins);
    binEdge.resize ((size_t) numBins);
    for (int k = 0; k < numBins; ++k)
    {
        const double f = k * binHz;
        double u = (erb::hzToCam (f) - z0) / dz;
        u = std::clamp (u, 0.0, (double) (numBands - 1));
        const int i = std::min ((int) u, numBands - 2);
        binBand[(size_t) k] = i;
        binFrac[(size_t) k] = (float) (u - i);
        binEdge[(size_t) k] = (float) std::clamp (std::min (f / 20.0, 1.0 - (f - fTop) / 1000.0), 0.0, 1.0);
    }

    auto alloc = [] (std::vector<float>& v, size_t n) { v.assign (n, 0.0f); };
    for (auto* v : { &earW, &noiseSlice, &noiseBox, &flatNormDb, &slices, &smoothed, &place, &context,
                     &fwd, &bwd, &prom, &target, &targetSmoothed, &gain, &promSlow, &gainOut, &needOut, &tmp,
                     &raw, &fastPlace, &slowPlace, &transient })
        alloc (*v, (size_t) numBands);
    for (auto* v : { &binGainDb, &binGain, &binNeed })
        alloc (*v, (size_t) numBins);
    prefix.assign ((size_t) numBands + 1, 0.0);
    transientAge.assign ((size_t) numBands, idleAge);

    for (int b = 0; b < numBands; ++b)
    {
        const double hz = bandHz[(size_t) b];
        earW[(size_t) b] = (float) std::pow (10.0, erb::outerEarDb (hz) / 10.0);
        // Threshold power of a one-ERB noise band, spread over this slice, ear-weighted.
        const double thr = 0.5 * std::pow (10.0, (erb::thresholdInQuietDb (hz) - erb::fullScaleSineSpl) / 10.0);
        noiseSlice[(size_t) b] = (float) (thr * dz) * earW[(size_t) b];
    }
    boxSum (noiseSlice.data(), noiseBox.data(), boxHalf, false);

    // Normalisation: a flat input must give 0 dB prominence everywhere (also fixes the edges).
    std::fill (tmp.begin(), tmp.end(), 1.0f);
    boxSum (tmp.data(), place.data(), boxHalf, false);
    contextOf (place.data(), context.data());
    for (int b = 0; b < numBands; ++b)
        flatNormDb[(size_t) b] = powToDb ((double) place[(size_t) b] / context[(size_t) b]);

    reset();
}

void PerceptualModel::reset()
{
    primed = false;
    liftPeakDb = cutPeakDb = transientPeak = 0.0f;
    for (auto* v : { &smoothed, &gain, &promSlow, &prom, &gainOut, &needOut, &slowPlace, &transient })
        std::fill (v->begin(), v->end(), 0.0f);
    std::fill (binGainDb.begin(), binGainDb.end(), 0.0f);
    std::fill (binGain.begin(), binGain.end(), 1.0f);
    std::fill (binNeed.begin(), binNeed.end(), 0.0f);
    std::fill (transientAge.begin(), transientAge.end(), idleAge);
}

int PerceptualModel::bandForHz (double hz) const noexcept
{
    const int b = (int) std::lround ((erb::hzToCam (hz) - z0) / dz);
    return std::clamp (b, 0, numBands - 1);
}

void PerceptualModel::binPowerToSlices (const float* binPower, float* out) const noexcept
{
    for (int b = 0; b < numBands; ++b)
    {
        float sum = 0.0f;
        for (int e = partStart[(size_t) b]; e < partStart[(size_t) b + 1]; ++e)
            sum += partWeight[(size_t) e] * binPower[partBin[(size_t) e]];
        out[b] = sum * bandPowerScale;
    }
}

void PerceptualModel::boxSum (const float* in, float* out, int half, bool mean) noexcept
{
    prefix[0] = 0.0;
    for (int b = 0; b < numBands; ++b)
        prefix[(size_t) b + 1] = prefix[(size_t) b] + in[b];

    for (int b = 0; b < numBands; ++b)
    {
        const int lo = std::max (0, b - half), hi = std::min (numBands, b + half + 1);
        const double s = prefix[(size_t) hi] - prefix[(size_t) lo];
        out[b] = (float) (mean ? s / (hi - lo) : s);
    }
}

void PerceptualModel::contextOf (const float* placeLevel, float* ctx) noexcept
{
    // Exponential spreading in Cam is a first-order recursion along the band axis.
    double acc = 0.0;
    for (int b = 0; b < numBands; ++b)
        fwd[(size_t) b] = (float) (acc = placeLevel[b] + aUp * acc);
    acc = 0.0;
    for (int b = numBands - 1; b >= 0; --b)
        bwd[(size_t) b] = (float) (acc = placeLevel[b] + aDn * acc);

    // Only maskers at least one ERB away count as "context".
    for (int b = 0; b < numBands; ++b)
    {
        double c = 0.0;
        if (b - exclusion >= 0)       c += aUpM * fwd[(size_t) (b - exclusion)];
        if (b + exclusion < numBands) c += aDnM * bwd[(size_t) (b + exclusion)];
        ctx[b] = (float) c;
    }
}

void PerceptualModel::analyse (const float* binPower, const ModelSettings& s, float fps)
{
    binPowerToSlices (binPower, slices.data());

    const float aP = 1.0f - std::exp (-1.0f / (0.020f * fps));   // 20 ms temporal integration
    for (int b = 0; b < numBands; ++b)
    {
        const float x = slices[(size_t) b] * earW[(size_t) b] + noiseSlice[(size_t) b];
        raw[(size_t) b] = x;
        smoothed[(size_t) b] = primed ? smoothed[(size_t) b] + aP * (x - smoothed[(size_t) b]) : x;
    }

    boxSum (smoothed.data(), place.data(), boxHalf, false);
    contextOf (place.data(), context.data());

    // Transient guard detector: the one-ERB level of this frame (no temporal integration)
    // against its own ~150 ms average. A rise of 4 dB starts to count, 12 dB is a full attack.
    // Every frame that still holds the attack is written over it, so for one frame length (21 ms
    // in Low, 85 ms in Precise) the value is held and the reference level does not climb (in a
    // long frame the attack's level builds up over several frames); then it decays (~50 ms).
    // Measured always (so turning Guard up mid-song needs no warm-up), applied only when Guard > 0.
    boxSum (raw.data(), fastPlace.data(), boxHalf, false);
    {
        const float aLevel = 1.0f - std::exp (-1.0f / (0.150f * fps));
        const float decay = std::exp (-1.0f / (0.050f * fps));
        const int holdFrames = std::max (1, (int) std::lround (fftSize / sampleRate * fps));
        for (int b = 0; b < numBands; ++b)
        {
            const float f = fastPlace[(size_t) b];
            float& slow = slowPlace[(size_t) b];
            float& tr = transient[(size_t) b];
            int& age = transientAge[(size_t) b];   // frames since the current attack began
            const bool inAttack = age < holdFrames;
            float onset = 0.0f;
            if (primed)
            {
                const float ratio = f / slow;   // slow > 0: it includes the internal-noise floor
                if (ratio > 2.5118864f)          // > 4 dB
                    onset = clamp01 ((10.0f * std::log10 (ratio) - 4.0f) / 8.0f);
                if (! inAttack || f < slow)
                    slow += aLevel * (f - slow);
            }
            else
            {
                slow = f;
            }

            if (onset > tr)
            {
                tr = onset;
                if (! inAttack)
                    age = 0;   // a new attack (a rise during the hold belongs to the current one)
            }
            else if (! inAttack)
            {
                tr = std::max (onset, tr * decay);
            }
            if (age < holdFrames)
                ++age;
        }
    }
    const bool guarded = s.guard > 0.0f;

    for (int b = 0; b < numBands; ++b)
    {
        const float d = std::clamp (powToDb ((double) place[(size_t) b] / ((double) context[(size_t) b] + 1.0e-30))
                                        - flatNormDb[(size_t) b], -40.0f, 40.0f);
        prom[(size_t) b] = d;

        const float audDb = powToDb ((double) place[(size_t) b] / noiseBox[(size_t) b]);
        const float audible = clamp01 ((audDb - 6.0f) / 12.0f);

        const float cut    = s.smooth * std::min (maxCutDb, 0.5f * std::max (0.0f, d - 2.0f)) * audible;
        const float shadow = std::max (0.0f, -d - 2.0f);
        const float fade   = clamp01 ((30.0f + d) / 12.0f);            // empty gaps are not lifted
        const float boost  = s.reveal * std::min (maxBoostDb, 0.6f * shadow) * fade * audible;

        target[(size_t) b] = boost - cut;
    }

    boxSum (target.data(), targetSmoothed.data(), smoothHalf, true);

    const float tauSlow = std::max (0.005f, s.responseMs * 0.001f);
    const float aSlow = 1.0f - std::exp (-1.0f / (tauSlow * fps));
    const float aFast = 1.0f - std::exp (-3.0f / (tauSlow * fps));

    if (! primed)
    {
        std::copy (targetSmoothed.begin(), targetSmoothed.end(), gain.begin());
        std::copy (prom.begin(), prom.end(), promSlow.begin());
        primed = true;
    }
    else
    {
        for (int b = 0; b < numBands; ++b)
        {
            const float t = targetSmoothed[(size_t) b];
            float& g = gain[(size_t) b];
            float rate = t < g ? aFast : aSlow;
            if (guarded)   // during an attack the curve doesn't chase it (no ducking afterwards)
                rate *= 1.0f - s.guard * transient[(size_t) b];
            g += rate * (t - g);
            promSlow[(size_t) b] += aSlow * (prom[(size_t) b] - promSlow[(size_t) b]);
        }
    }

    const double zLo = erb::hzToCam (std::max (20.0f, s.rangeLoHz));
    const double zHi = std::max (zLo + 0.5, erb::hzToCam (s.rangeHiHz));

    liftPeakDb = cutPeakDb = transientPeak = 0.0f;
    for (int b = 0; b < numBands; ++b)
    {
        const double z = z0 + b * dz;
        const float rg = smoothstep ((float) ((z - (zLo - 0.25)) / 0.5))
                       * smoothstep ((float) (((zHi + 0.25) - z) / 0.5));
        const float tilt = std::clamp (s.tiltDbPerOct * (float) std::log2 (bandHz[(size_t) b] / 1000.0), -9.0f, 9.0f);
        // Guard: attacks pass with the auto-EQ relaxed toward 0 dB (Tilt is static and stays).
        const float relax = guarded ? 1.0f - s.guard * transient[(size_t) b] : 1.0f;
        const float autoDb = guarded ? gain[(size_t) b] * relax : gain[(size_t) b];
        gainOut[(size_t) b] = rg * (autoDb + tilt);
        liftPeakDb = std::max (liftPeakDb, rg * autoDb);
        cutPeakDb  = std::min (cutPeakDb, rg * autoDb);
        transientPeak = std::max (transientPeak, rg * transient[(size_t) b]);

        const float ps = promSlow[(size_t) b];
        const float shadowed = clamp01 ((-ps - 1.0f) / 9.0f);
        const float dominant = clamp01 ((ps - 1.0f) / 6.0f);
        const float need = clamp01 (0.35f + 0.65f * shadowed - 0.5f * dominant);
        const float zn = (float) b / (numBands - 1);
        const float focusW = std::clamp (1.0f + s.focus * (2.0f * zn - 1.0f), 0.0f, 2.0f);
        needOut[(size_t) b] = guarded ? rg * need * focusW * relax : rg * need * focusW;   // no harmonics smeared over attacks
    }

    for (int k = 0; k < numBins; ++k)
    {
        const int i = binBand[(size_t) k];
        const float f = binFrac[(size_t) k], e = binEdge[(size_t) k];
        binGainDb[(size_t) k] = e * (gainOut[(size_t) i] + f * (gainOut[(size_t) i + 1] - gainOut[(size_t) i]));
        binNeed[(size_t) k]   = e * (needOut[(size_t) i] + f * (needOut[(size_t) i + 1] - needOut[(size_t) i]));
    }

    // Light smoothing across bins keeps the per-frame filter short (less circular time-aliasing).
    for (int k = 0; k < numBins; ++k)
    {
        auto at = [this] (int i) { return binGainDb[(size_t) std::clamp (i, 0, numBins - 1)]; };
        const float sm = (at (k - 2) + 2.0f * at (k - 1) + 3.0f * at (k) + 2.0f * at (k + 1) + at (k + 2)) / 9.0f;
        binGain[(size_t) k] = std::pow (10.0f, sm / 20.0f);
    }
}

void PerceptualModel::bandLevelsDb (const float* binPower, float* outDb)
{
    binPowerToSlices (binPower, tmp.data());
    boxSum (tmp.data(), outDb, boxHalf, false);
    for (int b = 0; b < numBands; ++b)
        outDb[b] = powToDb (outDb[b]);
}
}
