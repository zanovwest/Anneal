#include "SpectralEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace anneal
{
int SpectralEngine::fftSizeFor (double sr) noexcept
{
    if (sr <= 50000.0)  return 4096;
    if (sr <= 100000.0) return 8192;
    return 16384;
}

void SpectralEngine::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    channels = std::clamp (numChannels, 1, maxChannels);
    fftSize = fftSizeFor (sampleRate);
    hop = fftSize / 8;
    numBins = fftSize / 2 + 1;
    framesPerSecond = (float) (sampleRate / hop);

    int order = 0;
    while ((1 << order) < fftSize)
        ++order;
    fft = std::make_unique<juce::dsp::FFT> (order);

    // Periodic Hann for analysis and synthesis. Overlap-add of w^2 at hop N/8 sums to 3.
    window.resize ((size_t) fftSize);
    double sumSq = 0.0;
    for (int n = 0; n < fftSize; ++n)
    {
        window[(size_t) n] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / fftSize));
        sumSq += (double) window[(size_t) n] * window[(size_t) n];
    }
    olaNorm = (float) (hop / sumSq);

    for (int c = 0; c < maxChannels; ++c)
    {
        inFifo[c].assign ((size_t) fftSize, 0.0f);
        outAcc[c].assign ((size_t) fftSize, 0.0f);
        outFifo[c].assign ((size_t) hop, 0.0f);
        spec[c].assign ((size_t) fftSize * 2, 0.0f);
        harm[c].assign ((size_t) numBins * 2, 0.0f);
        harmSpec[c].assign ((size_t) fftSize * 2, 0.0f);
        harmAcc[c].assign ((size_t) fftSize, 0.0f);
        harmFifo[c].assign ((size_t) hop, 0.0f);
        alignRing[c].assign ((size_t) fftSize, 0.0f);
    }
    power.assign ((size_t) numBins, 0.0f);
    outPower.assign ((size_t) numBins, 0.0f);
    harmPower.assign ((size_t) numBins, 0.0f);

    envRelease = (float) std::exp (-1.0 / (0.060 * sampleRate));
    gateCoeff  = (float) (1.0 - std::exp (-1.0 / (0.001 * sampleRate)));

    model.prepare (sampleRate, fftSize);
    synth.prepare (fftSize);
    reset();
}

void SpectralEngine::reset()
{
    for (int c = 0; c < maxChannels; ++c)
        for (auto* v : { &inFifo[c], &outAcc[c], &outFifo[c], &harmAcc[c], &harmFifo[c], &alignRing[c] })
            std::fill (v->begin(), v->end(), 0.0f);
    pos = 0;
    ringPos = 0;
    envIn = envHarm = 0.0f;
    gateGain = 1.0f;
    paramsPrimed = false;
    model.reset();
}

void SpectralEngine::process (float* const* io, int numChannels, int numSamples) noexcept
{
    numChannels = std::min (numChannels, channels);
    int done = 0;

    while (done < numSamples)
    {
        const int todo = std::min (numSamples - done, hop - pos);

        for (int c = 0; c < channels; ++c)
        {
            float* fifoIn = inFifo[c].data() + (fftSize - hop + pos);
            if (c < numChannels)
                std::memcpy (fifoIn, io[c] + done, sizeof (float) * (size_t) todo);
            else
                std::memset (fifoIn, 0, sizeof (float) * (size_t) todo);
        }

        // Output: main path + gated harmonic stream. The gate keeps the harmonics' envelope
        // at or below the envelope of the input they belong to (latency-aligned), which
        // removes STFT pre-echo ahead of onsets and is transparent in steady state.
        for (int i = 0; i < todo; ++i)
        {
            float aIn = 0.0f, aHarm = 0.0f;
            for (int c = 0; c < numChannels; ++c)
            {
                float& delayed = alignRing[c][(size_t) ringPos];
                aIn = std::max (aIn, std::abs (delayed));
                delayed = io[c][done + i];
                aHarm = std::max (aHarm, std::abs (harmFifo[c][(size_t) (pos + i)]));
            }
            ringPos = ringPos + 1 == fftSize ? 0 : ringPos + 1;

            envIn   = std::max (aIn, envIn * envRelease);
            envHarm = std::max (aHarm, envHarm * envRelease);
            const float gateTarget = envHarm > 1.0e-9f ? std::min (1.0f, envIn / envHarm) : 1.0f;
            gateGain += gateCoeff * (gateTarget - gateGain);

            for (int c = 0; c < numChannels; ++c)
                io[c][done + i] = outFifo[c][(size_t) (pos + i)] + gateGain * harmFifo[c][(size_t) (pos + i)];
        }

        pos += todo;
        done += todo;

        if (pos == hop)
        {
            processFrame();
            pos = 0;
        }
    }
}

void SpectralEngine::smoothParameters() noexcept
{
    if (! paramsPrimed)
    {
        current = target;
        paramsPrimed = true;
        return;
    }

    const float a = 1.0f - std::exp (-1.0f / (0.040f * framesPerSecond));
    auto lin = [a] (float& v, float t) { v += a * (t - v); };
    auto lg  = [a] (float& v, float t) { v = std::exp (std::log (v) + a * (std::log (t) - std::log (v))); };

    lin (current.reveal, target.reveal);
    lin (current.smooth, target.smooth);
    lin (current.tiltDbPerOct, target.tiltDbPerOct);
    lin (current.heat, target.heat);
    lin (current.color, target.color);
    lin (current.focus, target.focus);
    lg  (current.rangeLoHz, target.rangeLoHz);
    lg  (current.rangeHiHz, target.rangeHiHz);
    current.responseMs = target.responseMs;
}

void SpectralEngine::processFrame() noexcept
{
    smoothParameters();

    const int half = fftSize / 2, mask = fftSize - 1;

    // Zero-phase analysis: the window centre lands on index 0, so a stationary partial's
    // bins carry the partial's phase at the frame centre (needed for harmonic phase-locking).
    // In the same pass, measure windowed energy before/after the frame centre (index n order).
    double earlyEnergy = 0.0, lateEnergy = 0.0;
    for (int c = 0; c < channels; ++c)
    {
        float* s = spec[c].data();
        const float* in = inFifo[c].data();
        for (int i = 0; i < fftSize; ++i)
        {
            const int n = (i + half) & mask;
            const float v = in[n] * window[(size_t) n];
            s[i] = v;
            (n < half ? earlyEnergy : lateEnergy) += (double) v * v;
        }
        std::fill (s + fftSize, s + 2 * fftSize, 0.0f);
        fft->performRealOnlyForwardTransform (s, true);
    }

    // Transient guard: harmonics are synthesised across the whole frame, so a frame whose
    // energy arrives mostly after its centre would smear them ahead of the onset (pre-echo,
    // which hearing does not mask). Such frames write proportionally less; frames centred
    // after the onset write in full, where post-masking covers the spread.
    const double guardRatio = (earlyEnergy + 1.0e-12) / (lateEnergy + 1.0e-12);
    const float heatGuard = (float) std::clamp (guardRatio * 1.25, 0.0, 1.0);
    lastHeatGuard = heatGuard;

    const float ampScale = 4.0f / (float) fftSize;
    const float powScale = ampScale * ampScale / (float) channels;
    for (int k = 0; k < numBins; ++k)
    {
        float sum = 0.0f;
        for (int c = 0; c < channels; ++c)
            sum += spec[c][(size_t) 2 * k] * spec[c][(size_t) 2 * k] + spec[c][(size_t) 2 * k + 1] * spec[c][(size_t) 2 * k + 1];
        power[(size_t) k] = sum * powScale;
    }

    ModelSettings ms;
    ms.reveal = current.reveal;
    ms.smooth = current.smooth;
    ms.tiltDbPerOct = current.tiltDbPerOct;
    ms.responseMs = current.responseMs;
    ms.rangeLoHz = current.rangeLoHz;
    ms.rangeHiHz = current.rangeHiHz;
    ms.focus = current.focus;
    model.analyse (power.data(), ms, framesPerSecond);

    const float* specPtrs[maxChannels] = { spec[0].data(), spec[1].data() };
    float* harmPtrs[maxChannels] = { harm[0].data(), harm[1].data() };
    HeatSettings hs;
    hs.heat = current.heat;
    hs.color = current.color;
    synth.process (specPtrs, channels, power.data(), model.getBinNeed(), hs, harmPtrs, harmPower.data());

    // Main path gets the auto-EQ; harmonics go to their own stream (gated in process()).
    const bool harmonicsWritten = current.heat > 1.0e-4f && heatGuard > 0.0f && synth.getLastPeakCount() > 0;
    const float* g = model.getBinGain();
    std::fill (outPower.begin(), outPower.end(), 0.0f);
    for (int c = 0; c < channels; ++c)
    {
        float* s = spec[c].data();
        float* hs2 = harmSpec[c].data();
        const float* h = harm[c].data();
        for (int k = 0; k < numBins; ++k)
        {
            s[2 * k]     *= g[k];
            s[2 * k + 1] *= g[k];
            const float hr = heatGuard * h[2 * k], hi = heatGuard * h[2 * k + 1];
            hs2[2 * k]     = hr;
            hs2[2 * k + 1] = hi;
            const float yr = s[2 * k] + hr, yi = s[2 * k + 1] + hi;
            outPower[(size_t) k] += (yr * yr + yi * yi) * powScale;
        }
    }
    if (heatGuard < 1.0f)
        for (auto& hp : harmPower)
            hp *= heatGuard * heatGuard;

    {
        DisplayFrame& f = display.beginWrite();
        f.z0 = model.getZ0();
        f.dz = model.getDz();
        f.rangeLoHz = current.rangeLoHz;
        f.rangeHiHz = current.rangeHiHz;
        f.counter = ++frameCounter;
        model.bandLevelsDb (power.data(), f.inDb);
        model.bandLevelsDb (outPower.data(), f.outDb);
        model.bandLevelsDb (harmPower.data(), f.heatDb);
        std::copy (model.getBandGainDb(), model.getBandGainDb() + DisplayFrame::numBands, f.gainDb);
        display.endWrite();
    }

    auto synthesise = [this, half, mask] (float* s, float* acc, float* fifo)
    {
        fft->performRealOnlyInverseTransform (s);   // JUCE scales the inverse by 1/N
        for (int i = 0; i < fftSize; ++i)
        {
            const int n = (i + half) & mask;
            acc[n] += s[i] * window[(size_t) n] * olaNorm;
        }
        std::memcpy (fifo, acc, sizeof (float) * (size_t) hop);
        std::memmove (acc, acc + hop, sizeof (float) * (size_t) (fftSize - hop));
        std::memset (acc + (fftSize - hop), 0, sizeof (float) * (size_t) hop);
    };

    for (int c = 0; c < channels; ++c)
    {
        synthesise (spec[c].data(), outAcc[c].data(), outFifo[c].data());

        if (harmonicsWritten)
        {
            std::fill (harmSpec[c].begin() + 2 * numBins, harmSpec[c].end(), 0.0f);
            synthesise (harmSpec[c].data(), harmAcc[c].data(), harmFifo[c].data());
        }
        else
        {
            // Nothing new to add: just advance the overlap-add tail of earlier frames.
            float* acc = harmAcc[c].data();
            std::memcpy (harmFifo[c].data(), acc, sizeof (float) * (size_t) hop);
            std::memmove (acc, acc + hop, sizeof (float) * (size_t) (fftSize - hop));
            std::memset (acc + (fftSize - hop), 0, sizeof (float) * (size_t) hop);
        }

        float* in = inFifo[c].data();
        std::memmove (in, in + hop, sizeof (float) * (size_t) (fftSize - hop));
    }
}
}
