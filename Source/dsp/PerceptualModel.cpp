#include "PerceptualModel.h"
#include "ErbScale.h"
#include "FastMath.h"

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

void PerceptualModel::prepare (double newSampleRate, int newFftSize, bool keepCurve)
{
    // keepCurve: a frozen curve comes through preparing again (4.0.2), as through reset(), when the
    // bands stay where they are (every rate from 40.8 kHz up). It is kept in bands, not bins.
    const double oldZ0 = z0, oldDz = dz;
    std::vector<float> keptGain, keptProm;
    if (keepCurve && curvePrimed)
    {
        keptGain = gain;
        keptProm = promSlow;
    }

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
    bandOct.resize (numBands);
    for (int b = 0; b < numBands; ++b)
    {
        bandHz[(size_t) b] = erb::camToHz (z0 + b * dz);
        bandOct[(size_t) b] = (float) std::log2 (bandHz[(size_t) b] / 1000.0);   // Tilt's octaves from 1 kHz
    }

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
                     &raw, &fastPlace, &slowPlace, &transient, &punchW, &autoOut })
        alloc (*v, (size_t) numBands);
    for (auto* v : { &keySlices, &keySmoothed, &keyPlace, &audDb, &keyRelDb, &keyAudDb })
        alloc (*v, (size_t) numBands);
    for (auto* v : { &refBand, &longPower, &longDb, &mainNorm, &matchRaw, &matchDb, &learnDb, &paintEq, &paintHeat })
        alloc (*v, (size_t) numBands);
    learnSum.assign ((size_t) numBands, 0.0);
    learnFrames = 0;
    matchHalf = std::max (1, (int) std::lround (1.5 / dz));   // the match curve is smoothed +-1.5 Cam
    applyReference();
    applyPaint();
    for (auto* v : { &binGainDb, &binGain, &binNeed, &binAutoDb, &binAutoGain })
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
    if (keptGain.size() == (size_t) numBands && z0 == oldZ0 && dz == oldDz)
    {
        gain = keptGain;
        promSlow = keptProm;
        curvePrimed = true;   // the next frame turns it into this rate's per-bin gains
    }
}

void PerceptualModel::reset (bool keepCurve)
{
    primed = false;
    liftPeakDb = cutPeakDb = transientPeak = matchPeakDb = 0.0f;
    for (auto* v : { &smoothed, &prom, &slowPlace, &transient, &punchW, &matchDb })
        std::fill (v->begin(), v->end(), 0.0f);
    keyPrimed = false;
    longFrames = 0;   // the main's long-term spectrum is measured again (the reference itself stays)
    matchActive = false;
    std::fill (transientAge.begin(), transientAge.end(), idleAge);
    if (keepCurve && curvePrimed)
        return;   // gain, promSlow and the per-bin curve they produced stay as they are

    curvePrimed = false;
    for (auto* v : { &gain, &promSlow, &gainOut, &needOut, &autoOut })
        std::fill (v->begin(), v->end(), 0.0f);
    std::fill (binGainDb.begin(), binGainDb.end(), 0.0f);
    std::fill (binGain.begin(), binGain.end(), 1.0f);
    std::fill (binAutoDb.begin(), binAutoDb.end(), 0.0f);
    std::fill (binAutoGain.begin(), binAutoGain.end(), 1.0f);
    std::fill (binNeed.begin(), binNeed.end(), 0.0f);
}

void PerceptualModel::copyCurveFrom (const PerceptualModel& other) noexcept
{
    if (other.gain.size() != gain.size() || other.dz != dz || ! other.curvePrimed)
        return;
    std::copy (other.gain.begin(), other.gain.end(), gain.begin());
    std::copy (other.promSlow.begin(), other.promSlow.end(), promSlow.begin());
    curvePrimed = true;
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

void PerceptualModel::analyse (const float* binPower, const ModelSettings& s, float fps, const float* keyPower)
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
        const float punchDecay = std::exp (-1.0f / (0.015f * fps));
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
            // 5.0 Punch shapes the attack itself: as strong as the detector while it holds the
            // attack, then gone within ~15 ms (Guard's own 50 ms decay would reach into the body).
            float& pw = punchW[(size_t) b];
            pw = age < holdFrames ? tr : pw * punchDecay;
            if (age < holdFrames)
                ++age;
        }
    }
    const bool guarded = s.guard > 0.0f;

    // Sidechain key: the same ear-weighted, 20 ms-integrated one-ERB level as the main signal,
    // so key and main are compared like for like in every band. The internal-noise floor is in
    // both, so a silent key is inaudible (keyAudible = 0) and changes nothing.
    const bool keyOn = keyPower != nullptr && s.scMode != 0 && s.scAmount > 0.0f;
    if (keyOn)
    {
        binPowerToSlices (keyPower, keySlices.data());
        for (int b = 0; b < numBands; ++b)
        {
            const float x = keySlices[(size_t) b] * earW[(size_t) b] + noiseSlice[(size_t) b];
            keySmoothed[(size_t) b] = keyPrimed ? keySmoothed[(size_t) b] + aP * (x - keySmoothed[(size_t) b]) : x;
        }
        keyPrimed = true;
        boxSum (keySmoothed.data(), keyPlace.data(), boxHalf, false);
    }
    else
    {
        keyPrimed = false;
    }

    // 4.0 Reference match. Off, Learn key and Amount 0 leave the target exactly as before.
    matchActive = s.refMode == refMatch && refValid && s.refAmount > 0.0f;
    if (matchActive)
        updateMatch (s.refAmount, fps);
    else
        longFrames = 0;   // measured afresh when matching starts again
    const float* extMatch = matchActive ? nullptr : s.matchDb;

    // 5.1.1: every band's level ratios first, then their dB values in one batched (vectorised) pass:
    // prominence (place / context), audibility (place / threshold) and, with a key, key / main.
    for (int b = 0; b < numBands; ++b)
    {
        prom[(size_t) b] = place[(size_t) b] / (context[(size_t) b] + 1.0e-30f);
        audDb[(size_t) b] = place[(size_t) b] / noiseBox[(size_t) b];
    }
    toDb (prom.data(), prom.data(), numBands);
    toDb (audDb.data(), audDb.data(), numBands);
    if (keyOn)
    {
        for (int b = 0; b < numBands; ++b)
        {
            keyRelDb[(size_t) b] = keyPlace[(size_t) b] / place[(size_t) b];
            keyAudDb[(size_t) b] = keyPlace[(size_t) b] / noiseBox[(size_t) b];
        }
        toDb (keyRelDb.data(), keyRelDb.data(), numBands);
        toDb (keyAudDb.data(), keyAudDb.data(), numBands);
    }

    sidePeakDb = 0.0f;
    for (int b = 0; b < numBands; ++b)
    {
        const float d = std::clamp (prom[(size_t) b] - flatNormDb[(size_t) b], -40.0f, 40.0f);
        prom[(size_t) b] = d;

        const float audible = clamp01 ((audDb[(size_t) b] - 6.0f) / 12.0f);

        const float cut    = s.smooth * std::min (maxCutDb, 0.5f * std::max (0.0f, d - 2.0f)) * audible;
        const float shadow = std::max (0.0f, -d - 2.0f);
        const float fade   = clamp01 ((30.0f + d) / 12.0f);            // empty gaps are not lifted
        const float boost  = s.reveal * std::min (maxBoostDb, 0.6f * shadow) * fade * audible;

        // Make room: duck the main where the key is about as loud or louder in the same ERB (up to
        // -9 dB at full amount). Unmask: lift the main where the key masks it, i.e. the key is
        // clearly louder there but the main is still present (not an empty gap).
        float side = 0.0f;
        if (keyOn)
        {
            const float rel = keyRelDb[(size_t) b];
            const float keyAudible = clamp01 ((keyAudDb[(size_t) b] - 6.0f) / 12.0f);
            if (s.scMode == 1)
                side = -s.scAmount * std::min (maxCutDb, 0.75f * std::max (0.0f, rel + 6.0f)) * keyAudible;
            else
                side = s.scAmount * std::min (6.0f, 0.5f * std::max (0.0f, rel - 3.0f))
                     * clamp01 ((30.0f - rel) / 12.0f) * audible * keyAudible;
        }

        // Reference match: like Reveal, nothing is moved where the main is inaudible.
        float t = boost - cut + side;
        if (matchActive)
            t += matchDb[(size_t) b] * audible;
        else if (extMatch != nullptr)
            t += s.matchScale * extMatch[b] * audible;
        target[(size_t) b] = t;
        if (std::abs (side) > std::abs (sidePeakDb))
            sidePeakDb = side;
    }

    boxSum (target.data(), targetSmoothed.data(), smoothHalf, true);

    const float tauSlow = std::max (0.005f, s.responseMs * 0.001f);
    const float aSlow = 1.0f - std::exp (-1.0f / (tauSlow * fps));
    const float aFast = 1.0f - std::exp (-3.0f / (tauSlow * fps));

    const bool curveWasPrimed = curvePrimed;
    curvePrimed = primed = true;
    if (! curveWasPrimed)
    {
        std::copy (targetSmoothed.begin(), targetSmoothed.end(), gain.begin());
        std::copy (prom.begin(), prom.end(), promSlow.begin());
    }
    else if (! s.freeze)   // Freeze: the curve (and its prominence memory) hold where they are
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

    liftPeakDb = cutPeakDb = transientPeak = matchPeakDb = punchPeakDb = 0.0f;
    for (int b = 0; b < numBands; ++b)
    {
        const double z = z0 + b * dz;
        const float rg = smoothstep ((float) ((z - (zLo - 0.25)) / 0.5))
                       * smoothstep ((float) (((zHi + 0.25) - z) / 0.5));
        const float tilt = std::clamp (s.tiltDbPerOct * bandOct[(size_t) b], -9.0f, 9.0f);
        // Guard: attacks pass with the auto-EQ relaxed toward 0 dB (Tilt is static and stays).
        const float relax = guarded ? 1.0f - s.guard * transient[(size_t) b] : 1.0f;
        const float autoDb = guarded ? gain[(size_t) b] * relax : gain[(size_t) b];
        // 5.0 Punch: attacks louder or softer in the bands they hit (up to 8 dB), like the rest of the
        // EQ inside Range and Paint. Punch 0 adds exactly 0, so the curve is unchanged.
        const float punchDb = s.punch * 8.0f * punchW[(size_t) b];
        float eqOut = rg * (autoDb + tilt + punchDb), eqAuto = rg * autoDb;
        if (std::abs (rg * punchDb) > std::abs (punchPeakDb))
            punchPeakDb = rg * punchDb;
        if (paintEqOn)   // Paint: the painted share of the whole change, on top of Range
        {
            eqOut *= paintEq[(size_t) b];
            eqAuto *= paintEq[(size_t) b];
        }
        gainOut[(size_t) b] = eqOut;
        autoOut[(size_t) b] = eqAuto;
        liftPeakDb = std::max (liftPeakDb, eqAuto);
        cutPeakDb  = std::min (cutPeakDb, eqAuto);
        transientPeak = std::max (transientPeak, rg * transient[(size_t) b]);
        if (matchActive)
        {
            const float m = rg * matchDb[(size_t) b] * (paintEqOn ? paintEq[(size_t) b] : 1.0f);
            if (std::abs (m) > std::abs (matchPeakDb))
                matchPeakDb = m;
        }

        const float ps = promSlow[(size_t) b];
        const float shadowed = clamp01 ((-ps - 1.0f) / 9.0f);
        const float dominant = clamp01 ((ps - 1.0f) / 6.0f);
        const float need = clamp01 (0.35f + 0.65f * shadowed - 0.5f * dominant);
        const float zn = (float) b / (numBands - 1);
        const float focusW = std::clamp (1.0f + s.focus * (2.0f * zn - 1.0f), 0.0f, 2.0f);
        float heatNeed = guarded ? rg * need * focusW * relax : rg * need * focusW;   // no harmonics smeared over attacks
        if (paintHeatOn)
            heatNeed *= paintHeat[(size_t) b];
        needOut[(size_t) b] = heatNeed;
    }

    for (int k = 0; k < numBins; ++k)
    {
        const int i = binBand[(size_t) k];
        const float f = binFrac[(size_t) k], e = binEdge[(size_t) k];
        binNeed[(size_t) k] = e * (needOut[(size_t) i] + f * (needOut[(size_t) i + 1] - needOut[(size_t) i]));
    }
    bandsToBinGain (gainOut.data(), binGainDb.data(), binGain.data());
}

void PerceptualModel::bandsToBinGain (const float* bandDb, float* binDb, float* linear) noexcept
{
    for (int k = 0; k < numBins; ++k)
    {
        const int i = binBand[(size_t) k];
        const float f = binFrac[(size_t) k], e = binEdge[(size_t) k];
        binDb[k] = e * (bandDb[i] + f * (bandDb[i + 1] - bandDb[i]));
    }

    // Light smoothing across bins keeps the per-frame filter short (less circular time-aliasing).
    // 5.1.1: the smoothed dB values first (the edges clamped, the inside straight), then one batched
    // conversion to linear gains.
    auto at = [this, binDb] (int i) { return binDb[std::clamp (i, 0, numBins - 1)]; };
    auto smoothAt = [&at] (int k) { return (at (k - 2) + 2.0f * at (k - 1) + 3.0f * at (k) + 2.0f * at (k + 1) + at (k + 2)) / 9.0f; };
    for (int k = 0; k < std::min (2, numBins); ++k)
        linear[k] = smoothAt (k);
    for (int k = 2; k < numBins - 2; ++k)
        linear[k] = (binDb[k - 2] + 2.0f * binDb[k - 1] + 3.0f * binDb[k] + 2.0f * binDb[k + 1] + binDb[k + 2]) / 9.0f;
    for (int k = std::max (2, numBins - 2); k < numBins; ++k)
        linear[k] = smoothAt (k);
    toGain (linear, linear, numBins);
}

void PerceptualModel::toDb (const float* power, float* db, int n) const noexcept
{
    if (! exactMath)
        fastmath::powerToDb (power, db, n);
    else
        for (int i = 0; i < n; ++i)
            db[i] = powToDb (power[i]);
}

void PerceptualModel::toGain (const float* db, float* linear, int n) const noexcept
{
    if (! exactMath)
        fastmath::dbToGain (db, linear, n);
    else
        for (int i = 0; i < n; ++i)
            linear[i] = std::pow (10.0f, db[i] / 20.0f);
}

void PerceptualModel::computeAutoGain() noexcept
{
    // Same steps as the full curve, so with Tilt at 0 both gains are bit-identical.
    bandsToBinGain (autoOut.data(), binAutoDb.data(), binAutoGain.data());
}

void PerceptualModel::bandLevelsDb (const float* binPower, float* outDb)
{
    binPowerToSlices (binPower, tmp.data());
    boxSum (tmp.data(), outDb, boxHalf, false);
    toDb (outDb, outDb, numBands);
}

//==============================================================================
// 4.0 Reference match

void PerceptualModel::setReference (const ReferenceCurve& curve) noexcept
{
    refFixed = curve;
    applyReference();
}

void PerceptualModel::applyReference() noexcept
{
    refValid = refFixed.valid && ! refBand.empty();
    if (! refValid)
        return;
    for (int b = 0; b < numBands; ++b)
        refBand[(size_t) b] = refgrid::sampleAt (refFixed.db, ReferenceCurve::numPoints, z0 + b * dz);
    // Normalised again on these bands: below 40.8 kHz the top of the grid is cut off.
    normaliseLevels (refBand.data(), nullptr, numBands, z0, dz, refBand.data());
}

void PerceptualModel::updateMatch (float amount, float fps) noexcept
{
    // The main's physical one-ERB power (no ear weighting, no internal noise: the reference has
    // neither), averaged over ~3 s; a plain running mean until that many frames have been heard.
    boxSum (slices.data(), tmp.data(), boxHalf, false);
    ++longFrames;
    const float aLong = 1.0f - std::exp (-1.0f / (3.0f * fps));
    const float a = std::max (1.0f / (float) longFrames, aLong);
    for (int b = 0; b < numBands; ++b)
        longPower[(size_t) b] += a * (tmp[(size_t) b] - longPower[(size_t) b]);
    toDb (longPower.data(), longDb.data(), numBands);
    normaliseLevels (longDb.data(), longPower.data(), numBands, z0, dz, mainNorm.data());

    // Only where both have something: no lifting into an empty region of the main, and nothing
    // pulled down where the reference is empty (e.g. above an MP3's cut-off).
    for (int b = 0; b < numBands; ++b)
    {
        const float r = refBand[(size_t) b], m = mainNorm[(size_t) b];
        matchRaw[(size_t) b] = (r - m) * clamp01 ((r + 45.0f) / 15.0f) * clamp01 ((m + 45.0f) / 15.0f);
    }
    boxSum (matchRaw.data(), tmp.data(), matchHalf, true);
    for (int b = 0; b < numBands; ++b)
        matchDb[(size_t) b] = amount * std::clamp (tmp[(size_t) b], -6.0f, 6.0f);
}

void PerceptualModel::clearLearned() noexcept
{
    std::fill (learnSum.begin(), learnSum.end(), 0.0);
    learnFrames = 0;
}

void PerceptualModel::learnKey (const float* keyPower) noexcept
{
    binPowerToSlices (keyPower, keySlices.data());
    boxSum (keySlices.data(), tmp.data(), boxHalf, false);
    for (int b = 0; b < numBands; ++b)
        learnSum[(size_t) b] += tmp[(size_t) b];
    ++learnFrames;
}

void PerceptualModel::getLearnedCurve (ReferenceCurve& out) noexcept
{
    out.valid = false;
    if (learnFrames == 0)
        return;
    for (int b = 0; b < numBands; ++b)
        learnDb[(size_t) b] = powToDb (learnSum[(size_t) b] / learnFrames);
    for (int i = 0; i < ReferenceCurve::numPoints; ++i)
    {
        const double u = std::clamp ((refgrid::camAt (i, ReferenceCurve::numPoints) - z0) / dz, 0.0, (double) (numBands - 1));
        const int b = std::min ((int) u, numBands - 2);
        const float f = (float) (u - b);
        out.db[i] = learnDb[(size_t) b] + f * (learnDb[(size_t) b + 1] - learnDb[(size_t) b]);
    }
    const double fixedDz = (refgrid::zMax() - refgrid::zMin()) / (ReferenceCurve::numPoints - 1);
    normaliseLevels (out.db, nullptr, ReferenceCurve::numPoints, refgrid::zMin(), fixedDz, out.db);
    out.valid = true;
}

//==============================================================================
// 4.0 Paint

void PerceptualModel::setPaint (const PaintCurves& paint) noexcept
{
    paintSrc = paint;
    applyPaint();
}

void PerceptualModel::applyPaint() noexcept
{
    // All 100 % switches the weighting off entirely, so the result is bit-identical to no paint.
    paintEqOn = ! PaintCurves::isFull (paintSrc.eq);
    paintHeatOn = ! PaintCurves::isFull (paintSrc.heat);
    if (paintEq.empty())
        return;
    for (int b = 0; b < numBands; ++b)
    {
        const double z = z0 + b * dz;
        paintEq[(size_t) b] = std::clamp (refgrid::sampleAt (paintSrc.eq, PaintCurves::numPoints, z), 0.0f, 1.0f);
        paintHeat[(size_t) b] = std::clamp (refgrid::sampleAt (paintSrc.heat, PaintCurves::numPoints, z), 0.0f, 1.0f);
    }
}
}
