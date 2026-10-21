#include "SpectralEngine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace anneal
{
int SpectralEngine::fftSizeFor (double sr, int mode) noexcept
{
    const int base = sr <= 50000.0 ? 4096 : sr <= 100000.0 ? 8192 : 16384;
    return mode == low ? base / 4 : mode == normal ? base / 2 : base;
}

int SpectralEngine::hopFor (double sr, int n) noexcept
{
    // 300 updates a second at any sample rate (160 samples at 48 kHz, 147 at 44.1 kHz), never
    // coarser than a quarter frame.
    return std::clamp ((int) std::lround (sr / updatesPerSecond), 16, n / 4);
}

void SpectralEngine::prepare (double newSampleRate, int numChannels, int mode, bool keepCurve)
{
    curveMid = curveSide = nullptr;   // 5.1: the model's buffers may move; the next frame sets them
    sampleRate = newSampleRate;
    channels = std::clamp (numChannels, 1, maxChannels);
    fftSize = fftSizeFor (sampleRate, mode);
    // 5.2.0: 300 updates a second in every mode (was N/8 in Precise, N/4 in Normal and Low = 94/s,
    // Low 188/s at 48 kHz). The model's time constants are in seconds, so it sounds the same,
    // only smoother in time.
    hop = hopFor (sampleRate, fftSize);
    numBins = fftSize / 2 + 1;
    framesPerSecond = (float) (sampleRate / hop);

    fft.prepare (fftSize);

    // Periodic Hann for analysis and synthesis. With a hop that divides N (N/8, N/4) the overlap-add
    // of w^2 is a constant; 300/s gives hops that don't (4096 / 160 = 25.6), so the sum ripples
    // with the position inside a hop. It repeats every hop, so each position gets its own exact
    // gain: 1 / sum over the frames covering it of w^2. (For N/8 and N/4 that is the old constant.)
    window.resize ((size_t) fftSize);
    for (int n = 0; n < fftSize; ++n)
        window[(size_t) n] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * n / fftSize));
    std::vector<double> env ((size_t) hop, 0.0);
    for (int n = 0; n < fftSize; ++n)
        env[(size_t) (n % hop)] += (double) window[(size_t) n] * window[(size_t) n];
    olaGain.resize ((size_t) fftSize);
    for (int n = 0; n < fftSize; ++n)
        olaGain[(size_t) n] = (float) (1.0 / env[(size_t) (n % hop)]);

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
        msSpec[c].assign ((size_t) numBins * 2, 0.0f);
        msHarm[c].assign ((size_t) numBins * 2, 0.0f);
        msPower[c].assign ((size_t) numBins, 0.0f);
        msHarmPower[c].assign ((size_t) numBins, 0.0f);
        msModel[c].prepare (sampleRate, fftSize, keepCurve);
        msSynth[c].prepare (fftSize, sampleRate);
        rawSpec[c].assign ((size_t) numBins * 2, 0.0f);
    }
    power.assign ((size_t) numBins, 0.0f);
    keyFifo.assign ((size_t) fftSize, 0.0f);
    keySpec.assign ((size_t) fftSize * 2, 0.0f);
    keyPower.assign ((size_t) numBins, 0.0f);
    outPower.assign ((size_t) numBins, 0.0f);
    harmPower.assign ((size_t) numBins, 0.0f);

    envRelease = (float) std::exp (-1.0 / (0.060 * sampleRate));
    gateCoeff  = (float) (1.0 - std::exp (-1.0 / (0.001 * sampleRate)));

    model.prepare (sampleRate, fftSize, keepCurve);
    synth.prepare (fftSize, sampleRate);
    reset (keepCurve);
}

void SpectralEngine::reset (bool keepCurve)
{
    curveMid = curveSide = nullptr;   // 5.1: the model's buffers may move; the next frame sets them
    for (int c = 0; c < maxChannels; ++c)
        for (auto* v : { &inFifo[c], &outAcc[c], &outFifo[c], &harmAcc[c], &harmFifo[c], &alignRing[c] })
            std::fill (v->begin(), v->end(), 0.0f);
    std::fill (keyFifo.begin(), keyFifo.end(), 0.0f);
    pos = 0;
    ringPos = 0;
    envIn = envHarm = 0.0f;
    gateGain = 1.0f;
    paramsPrimed = false;
    model.reset (keepCurve);
    for (auto& m : msModel) m.reset (keepCurve);
    msActive = false;
    lastPeakCount = 0;
}

void SpectralEngine::setReference (const ReferenceCurve& curve) noexcept
{
    applyReference (curve);
    refEpoch = curve.epoch;
    learnFresh = true;
    learnHeld = curve.holdLearning;
}

void SpectralEngine::applyReference (const ReferenceCurve& curve) noexcept
{
    model.setReference (curve);
    for (auto& m : msModel)
        m.setReference (curve);
}

void SpectralEngine::setPaint (const PaintCurves& paint) noexcept
{
    model.setPaint (paint);
    for (auto& m : msModel)
        m.setPaint (paint);
}

void SpectralEngine::adoptCurveFrom (const SpectralEngine& other) noexcept
{
    model.copyCurveFrom (other.model);
    for (int c = 0; c < 2; ++c)
        msModel[c].copyCurveFrom (other.msModel[c]);
}

void SpectralEngine::process (float* const* io, int numChannels, int numSamples,
                              const float* const* key, int keyChannels) noexcept
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
        {
            // Sidechain key, mono, in step with the main input (silence when there is none).
            float* k = keyFifo.data() + (fftSize - hop + pos);
            if (key != nullptr && keyChannels > 0)
            {
                const float scale = 1.0f / (float) keyChannels;
                for (int i = 0; i < todo; ++i)
                {
                    float sum = 0.0f;
                    for (int c = 0; c < keyChannels; ++c)
                        sum += key[c][done + i];
                    k[i] = sum * scale;
                }
            }
            else
            {
                std::memset (k, 0, sizeof (float) * (size_t) todo);
            }
        }

        // Output: main path + gated harmonic stream. The gate keeps the harmonics' envelope
        // at or below the envelope of the input they belong to (latency-aligned), which
        // removes STFT pre-echo ahead of onsets and is transparent in steady state.
        // Analysis only (5.1 zero latency): the audio stays as it is.
        for (int i = 0; i < (analysisOnly ? 0 : todo); ++i)
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
    lin (current.guard, target.guard);
    lin (current.midSide, target.midSide);
    lin (current.eqMid, target.eqMid);
    lin (current.eqSide, target.eqSide);
    lin (current.heatMid, target.heatMid);
    lin (current.heatSide, target.heatSide);
    lin (current.scAmount, target.scAmount);
    lin (current.refAmount, target.refAmount);
    lin (current.punch, target.punch);
    lin (current.width, target.width);
    lin (current.monoHz, target.monoHz);
    current.freeze = target.freeze;
    current.scMode = target.scMode;
    current.refMode = target.refMode;
    lg  (current.rangeLoHz, target.rangeLoHz);
    lg  (current.rangeHiHz, target.rangeHiHz);
    current.responseMs = target.responseMs;
    current.listen = target.listen;   // a switch per frame; the overlap-add fades it over one frame
    current.flavour = target.flavour; // likewise: the harmonic stream's overlap-add crossfades it
}

void SpectralEngine::processFrame() noexcept
{
    smoothParameters();

    // Crossfade spectra rather than changing the basis of buffered time-domain audio.
    // The overlap-add tails and the dry path always remain L/R, including during automation.
    const float msBlend = channels == 2 ? (current.midSide < 1.0e-4f ? 0.0f
                                         : current.midSide > 0.9999f ? 1.0f : current.midSide) : 0.0f;
    if (msBlend == 0.0f && msActive)
        for (auto& m : msModel) m.reset();
    msActive = msBlend > 0.0f;

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
        fft.forward (s);
    }

    // Listening to Reveal or Smooth needs this frame's spectrum before the EQ.
    const bool listenEq = current.listen == listenReveal || current.listen == listenSmooth;
    if (listenEq)
        for (int c = 0; c < channels; ++c)
            std::copy (spec[c].begin(), spec[c].begin() + 2 * numBins, rawSpec[c].begin());

    if (msActive)
    {
        // M = (L+R)/2, S = (L-R)/2: centred audio keeps its familiar drive calibration.
        for (int k = 0; k < 2 * numBins; ++k)
        {
            msSpec[0][(size_t) k] = 0.5f * (spec[0][(size_t) k] + spec[1][(size_t) k]);
            msSpec[1][(size_t) k] = 0.5f * (spec[0][(size_t) k] - spec[1][(size_t) k]);
        }
        double early[2] {}, late[2] {};
        for (int n = 0; n < fftSize; ++n)
        {
            const float v[2] = { 0.5f * (inFifo[0][(size_t) n] + inFifo[1][(size_t) n]),
                                 0.5f * (inFifo[0][(size_t) n] - inFifo[1][(size_t) n]) };
            for (int c = 0; c < 2; ++c)
            {
                const double w = v[c] * window[(size_t) n];
                (n < half ? early[c] : late[c]) += w * w;
            }
        }
        for (int c = 0; c < 2; ++c)
            msGuard[c] = (float) std::clamp (1.25 * (early[c] + 1.0e-12) / (late[c] + 1.0e-12), 0.0, 1.0);
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

    // Sidechain key: only transformed when a sidechain mode is on or a reference is being learned
    // from it (no cost otherwise).
    const bool learning = current.refMode == refLearn;
    const float* keyPtr = nullptr;
    bool keyHasSignal = false;
    double keyTotal = 0.0;
    if (current.scMode != 0 || learning)
    {
        float* s = keySpec.data();
        for (int i = 0; i < fftSize; ++i)
        {
            const int n = (i + half) & mask;
            s[i] = keyFifo[(size_t) n] * window[(size_t) n];
        }
        fft.forward (s);
        const float keyScale = ampScale * ampScale;
        double total = 0.0;
        for (int k = 0; k < numBins; ++k)
        {
            keyPower[(size_t) k] = (s[2 * k] * s[2 * k] + s[2 * k + 1] * s[2 * k + 1]) * keyScale;
            total += keyPower[(size_t) k];
        }
        keyHasSignal = total > 1.0e-12;
        keyTotal = total;
        if (current.scMode != 0)
            keyPtr = keyPower.data();
    }

    // 4.0 Learn key: the key's long-term spectrum, from every frame in which it plays (above
    // -90 dBFS). A new learn starts with the first such frame after Learn key is chosen, so choosing
    // it with nothing routed keeps the reference there was.
    auto publishLearned = [this]
    {
        model.getLearnedCurve (learnScratch);
        learnScratch.epoch = refEpoch;
        applyReference (learnScratch);   // Match uses it as soon as it is chosen
        if (learnOut != nullptr)
        {
            learnOut->beginWrite() = learnScratch;
            learnOut->endWrite();
        }
    };
    if (! learning)
    {
        if (! learnFresh && ! learnHeld && model.getLearnedFrames() > 0)
            publishLearned();   // the frames since the last publish
        learnFresh = true;
        learnHeld = false;
    }
    else if (! learnHeld && keyTotal > 1.0e-9)
    {
        if (learnFresh)
        {
            model.clearLearned();
            learnFresh = false;
            learnTick = 0;
        }
        model.learnKey (keyPower.data());
        if (learnTick-- <= 0)   // about four times a second
        {
            learnTick = std::max (1, (int) (framesPerSecond / 4.0f));
            publishLearned();
        }
    }
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
    ms.guard = current.guard;
    ms.freeze = current.freeze;
    ms.scMode = current.scMode;
    ms.scAmount = current.scAmount;
    ms.refMode = current.refMode;
    ms.refAmount = current.refAmount;
    ms.punch = current.punch;
    model.analyse (power.data(), ms, framesPerSecond, keyPtr);

    bool harmonicsWritten = false;
    if (analysisOnly)
    {
        // 5.1 zero latency: the curve and the display, nothing resynthesised.
        const float* g = model.getBinGain();
        std::fill (outPower.begin(), outPower.end(), 0.0f);
        std::fill (harmPower.begin(), harmPower.end(), 0.0f);
        for (int c = 0; c < channels; ++c)
            for (int k = 0; k < numBins; ++k)
            {
                const float yr = spec[c][(size_t) 2 * k] * g[k], yi = spec[c][(size_t) 2 * k + 1] * g[k];
                outPower[(size_t) k] += (yr * yr + yi * yi) * powScale;
            }
        curveMid = curveSide = g;
        if (msActive)
        {
            processMidSide (ms, msBlend, keyPtr);   // analyses Mid and Side (no harmonics: Heat is 0 here)
            if (msBlend >= 0.5f)
            {
                curveMid = msModel[0].getBinGain();
                curveSide = msModel[1].getBinGain();
            }
        }
    }
    else
    {
    const float* specPtrs[maxChannels] = { spec[0].data(), spec[1].data() };
    float* harmPtrs[maxChannels] = { harm[0].data(), harm[1].data() };
    HeatSettings hs;
    hs.heat = current.heat;
    hs.color = current.color;
    hs.flavour = current.flavour;
    // 5.1.1: fully in Mid/Side, processMidSide replaces the linked harmonics with the Mid and Side
    // ones, so the linked synth (stateless from frame to frame) has nothing to add and is skipped.
    if (msBlend < 1.0f)
    {
        synth.process (specPtrs, channels, power.data(), model.getBinNeed(), hs, harmPtrs, harmPower.data());
        lastPeakCount = synth.getLastPeakCount();
    }
    else
    {
        for (int c = 0; c < channels; ++c)
            std::fill (harm[c].begin(), harm[c].end(), 0.0f);
        lastPeakCount = 0;
    }

    // Main path gets the auto-EQ; harmonics go to their own stream (gated in process()).
    harmonicsWritten = current.heat > 1.0e-4f && heatGuard > 0.0f && lastPeakCount > 0;
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

    if (msActive)
    {
        processMidSide (ms, msBlend, keyPtr);
        harmonicsWritten = harmonicsWritten || lastPeakCount > 0;
    }
    }

    {
        DisplayFrame& f = displayOut->beginWrite();
        f.z0 = model.getZ0();
        f.dz = model.getDz();
        f.rangeLoHz = current.rangeLoHz;
        f.rangeHiHz = current.rangeHiHz;
        f.counter = ++frameCounter;
        f.liftDb = model.getMaxLiftDb();
        f.cutDb = model.getMaxCutDb();
        f.midSide = msActive;
        f.keyActive = keyHasSignal;
        f.scDb = msActive ? (std::abs (msModel[0].getSidechainPeakDb()) > std::abs (msModel[1].getSidechainPeakDb())
                                 ? msModel[0].getSidechainPeakDb() : msModel[1].getSidechainPeakDb())
                          : model.getSidechainPeakDb();
        f.frozen = current.freeze;
        f.refMode = current.refMode;
        f.refValid = model.hasReference();
        f.refSeconds = getLearnedSeconds();
        f.matchDb = model.getMatchPeakDb();
        f.zeroLatency = analysisOnly;
        f.attacks = current.guard > 0.0f || std::abs (current.punch) > 1.0e-4f;
        f.punchDb = msActive ? (std::abs (msModel[0].getPunchPeakDb()) > std::abs (msModel[1].getPunchPeakDb())
                                    ? msModel[0].getPunchPeakDb() : msModel[1].getPunchPeakDb())
                             : model.getPunchPeakDb();
        if (f.attacks)
            std::copy (model.getBandPunch(), model.getBandPunch() + DisplayFrame::numBands, f.transient);
        if (keyHasSignal)
            model.bandLevelsDb (keyPower.data(), f.keyDb);
        if (msActive)
        {
            f.liftDb = std::max (msModel[0].getMaxLiftDb(), msModel[1].getMaxLiftDb());
            f.cutDb = std::min (msModel[0].getMaxCutDb(), msModel[1].getMaxCutDb());
        }
        {
            double h = 0.0, o = 0.0;
            for (int k = 0; k < numBins; ++k)
            {
                h += harmPower[(size_t) k];
                o += outPower[(size_t) k];
            }
            f.heatRelDb = (h > 1.0e-14 && o > 1.0e-14) ? (float) (10.0 * std::log10 (h / o)) : -200.0f;
        }
        model.bandLevelsDb (power.data(), f.inDb);
        model.bandLevelsDb (outPower.data(), f.outDb);
        model.bandLevelsDb (harmPower.data(), f.heatDb);
        std::copy (model.getBandGainDb(), model.getBandGainDb() + DisplayFrame::numBands, f.gainDb);
        if (msActive)
            for (int b = 0; b < DisplayFrame::numBands; ++b)
            {
                f.gainDb[b] = msModel[0].getBandGainDb()[b];
                f.sideGainDb[b] = msModel[1].getBandGainDb()[b];
            }
        displayOut->endWrite();
    }

    if (analysisOnly)
    {
        for (int c = 0; c < channels; ++c)
            std::memmove (inFifo[c].data(), inFifo[c].data() + hop, sizeof (float) * (size_t) (fftSize - hop));
        std::memmove (keyFifo.data(), keyFifo.data() + hop, sizeof (float) * (size_t) (fftSize - hop));
        return;
    }

    // Listen modes change only what is heard; the display above shows the full processing.
    if (current.listen != listenOff)
    {
        applyListen (msBlend);
        if (listenEq)
            harmonicsWritten = false;   // the harmonic stream's overlap-add tail just fades out
    }

    // 5.0 Width and Mono bass shape the stereo image of what is heard. Width 100 % with Mono bass
    // off skips this entirely, so the sound is exactly as before.
    if (channels == 2 && (std::abs (current.width - 1.0f) > 1.0e-4f || current.monoHz >= 20.0f))
        applyWidth (harmonicsWritten);

    auto synthesise =[this, half, mask] (float* s, float* acc, float* fifo)
    {
        fft.inverse (s);   // scaled by 1/N, like JUCE's; reads bins 0..N/2 only
        for (int i = 0; i < fftSize; ++i)
        {
            const int n = (i + half) & mask;
            acc[n] += s[i] * window[(size_t) n] * olaGain[(size_t) n];   // a sample at n leaves at n % hop
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
    std::memmove (keyFifo.data(), keyFifo.data() + hop, sizeof (float) * (size_t) (fftSize - hop));
}

void SpectralEngine::processMidSide (const ModelSettings& settings, float blend, const float* keyPtr) noexcept
{
    const float scale = 4.0f / (float) fftSize;
    for (int c = 0; c < 2; ++c)
    {
        for (int k = 0; k < numBins; ++k)
        {
            const float re = msSpec[c][(size_t) 2 * k], im = msSpec[c][(size_t) 2 * k + 1];
            msPower[c][(size_t) k] = (re * re + im * im) * scale * scale;
        }
        auto ms = settings;
        const float eqAmount = c == 0 ? current.eqMid : current.eqSide;
        ms.reveal *= eqAmount;
        ms.smooth *= eqAmount;
        ms.tiltDbPerOct *= eqAmount;
        ms.punch *= eqAmount;
        // Reference match: the stereo (linked) match curve, applied to each routed component.
        ms.refMode = refOff;
        if (model.isMatching())
        {
            ms.matchDb = model.getMatchDb();
            ms.matchScale = eqAmount;
        }
        msModel[c].analyse (msPower[c].data(), ms, framesPerSecond, keyPtr);

        const float* in[] = { msSpec[c].data() };
        float* out[] = { msHarm[c].data() };
        HeatSettings hs;
        hs.heat = current.heat * (c == 0 ? current.heatMid : current.heatSide);
        hs.color = current.color;
        hs.flavour = current.flavour;
        msSynth[c].process (in, 1, msPower[c].data(), msModel[c].getBinNeed(), hs, out, msHarmPower[c].data());
        const auto* gain = msModel[c].getBinGain();
        for (int k = 0; k < numBins; ++k)
            for (int ri = 0; ri < 2; ++ri)
            {
                msSpec[c][(size_t) (2 * k + ri)] *= gain[k];
                msHarm[c][(size_t) (2 * k + ri)] *= msGuard[c];
            }
    }
    lastPeakCount = msSynth[0].getLastPeakCount() + msSynth[1].getLastPeakCount();
    for (int k = 0; k < numBins; ++k)
    {
        float hEnergy = 0.0f, outEnergy = 0.0f;
        for (int c = 0; c < 2; ++c)
            for (int ri = 0; ri < 2; ++ri)
            {
                const auto i = (size_t) (2 * k + ri);
                const float sign = c == 0 ? 1.0f : -1.0f;
                const float msWet = msSpec[0][i] + sign * msSpec[1][i];
                const float msHeat = msHarm[0][i] + sign * msHarm[1][i];
                spec[c][i] += blend * (msWet - spec[c][i]);
                harmSpec[c][i] += blend * (msHeat - harmSpec[c][i]);
                hEnergy += harmSpec[c][i] * harmSpec[c][i];
                const float y = spec[c][i] + harmSpec[c][i];
                outEnergy += y * y;
            }
        harmPower[(size_t) k] = hEnergy * scale * scale * 0.5f;
        outPower[(size_t) k] = outEnergy * scale * scale * 0.5f;
    }
}

void SpectralEngine::applyWidth (bool harmonics) noexcept
{
    // Per bin, linear phase (no crossover filters): Side = (L - R) / 2 scaled by the width, Mid kept.
    // Mono bass: fully mono a quarter octave below its frequency, the full width a quarter octave above.
    const float binHz = (float) (sampleRate / fftSize);
    const bool mono = current.monoHz >= 20.0f;
    const float lo = mono ? current.monoHz * 0.84089642f : 0.0f;
    const float hi = mono ? current.monoHz * 1.18920712f : 0.0f;
    const float width = current.width;
    auto apply = [&] (float* l, float* r)
    {
        for (int k = 0; k < numBins; ++k)
        {
            float w = width;
            const float f = (float) k * binHz;
            if (f < hi)
            {
                float t = f <= lo ? 0.0f : 2.0f * std::log2 (f / lo);   // 0..1 across the half octave
                t = std::clamp (t, 0.0f, 1.0f);
                w *= t * t * (3.0f - 2.0f * t);
            }
            for (int ri = 0; ri < 2; ++ri)
            {
                const auto i = (size_t) (2 * k + ri);
                const float m = 0.5f * (l[i] + r[i]), s = 0.5f * (l[i] - r[i]) * w;
                l[i] = m + s;
                r[i] = m - s;
            }
        }
    };
    apply (spec[0].data(), spec[1].data());
    if (harmonics)
        apply (harmSpec[0].data(), harmSpec[1].data());
}

void SpectralEngine::applyListen (float msBlend) noexcept
{
    if (current.listen == listenHarmonics)
    {
        // Only the harmonic stream: the main path writes nothing (its overlap-add tail fades out).
        for (int c = 0; c < channels; ++c)
            std::fill (spec[c].begin(), spec[c].begin() + 2 * numBins, 0.0f);
        return;
    }

    // With A = the auto-EQ gain without Tilt, the normal output is X*A*Tilt. Reveal solo:
    // X*(A-1) where the EQ lifts; Smooth solo: X*(1-A) where it cuts (the part taken away, in
    // phase). With Tilt at 0, normal - Reveal solo = X and normal + Smooth solo = X.
    const bool reveal = current.listen == listenReveal;
    auto part = [reveal] (float a) noexcept { return reveal ? std::max (a - 1.0f, 0.0f) : std::max (1.0f - a, 0.0f); };

    model.computeAutoGain();
    const float* a = model.getBinAutoGain();
    for (int c = 0; c < channels; ++c)
    {
        float* s = spec[c].data();
        const float* x = rawSpec[c].data();
        for (int k = 0; k < numBins; ++k)
        {
            const float w = part (a[k]);
            s[2 * k]     = w * x[2 * k];
            s[2 * k + 1] = w * x[2 * k + 1];
        }
    }

    if (msBlend > 0.0f)
    {
        // Mid/Side: each component's own lift or cut, decoded to L/R and blended like the main path.
        msModel[0].computeAutoGain();
        msModel[1].computeAutoGain();
        const float* am = msModel[0].getBinAutoGain();
        const float* as = msModel[1].getBinAutoGain();
        float* l = spec[0].data();
        float* r = spec[1].data();
        const float* xl = rawSpec[0].data();
        const float* xr = rawSpec[1].data();
        for (int k = 0; k < numBins; ++k)
        {
            const float wm = part (am[k]), ws = part (as[k]);
            for (int ri = 0; ri < 2; ++ri)
            {
                const int i = 2 * k + ri;
                const float m = 0.5f * (xl[i] + xr[i]) * wm;
                const float s = 0.5f * (xl[i] - xr[i]) * ws;
                l[i] += msBlend * (m + s - l[i]);
                r[i] += msBlend * (m - s - r[i]);
            }
        }
    }
}
}
