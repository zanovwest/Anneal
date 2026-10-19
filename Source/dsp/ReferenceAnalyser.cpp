#include "ReferenceAnalyser.h"

#include <cmath>
#include <cstring>

namespace anneal
{
ReferenceAnalyser::ReferenceAnalyser (double sr)
    : sampleRate (sr > 0.0 ? sr : 48000.0)
{
    // Finer than any latency mode (~5.9 Hz bins at 48 kHz): the reference is analysed once.
    fftSize = sampleRate <= 50000.0 ? 8192 : sampleRate <= 100000.0 ? 16384 : 32768;
    hop = fftSize / 2;
    fft.prepare (fftSize);
    window.resize ((size_t) fftSize);
    for (int i = 0; i < fftSize; ++i)
        window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / fftSize));
    scratch.assign ((size_t) fftSize + 2, 0.0f);
    for (auto& f : fifo)
        f.assign ((size_t) fftSize, 0.0f);
    powerSum.assign ((size_t) fftSize / 2 + 1, 0.0);
}

void ReferenceAnalyser::push (const float* const* channels, int numChannels, int numSamples)
{
    numChannels = std::clamp (numChannels, 1, maxChannels);
    channelsSeen = numChannels;
    samplesIn += numSamples;
    for (int done = 0; done < numSamples;)
    {
        const int todo = std::min (numSamples - done, fftSize - fill);
        for (int c = 0; c < numChannels; ++c)
            std::memcpy (fifo[c].data() + fill, channels[c] + done, sizeof (float) * (size_t) todo);
        fill += todo;
        done += todo;
        if (fill == fftSize)
        {
            processFrame();
            for (int c = 0; c < numChannels; ++c)
                std::memmove (fifo[c].data(), fifo[c].data() + hop, sizeof (float) * (size_t) (fftSize - hop));
            fill = fftSize - hop;
        }
    }
}

void ReferenceAnalyser::processFrame()
{
    // Power in the engine's units (a full-scale sine peaks at 1.0 per channel), averaged over channels.
    const float ampScale = 4.0f / (float) fftSize;
    const double powScale = (double) ampScale * ampScale / channelsSeen;
    const int numBins = fftSize / 2 + 1;
    for (int c = 0; c < channelsSeen; ++c)
    {
        float* s = scratch.data();
        for (int i = 0; i < fftSize; ++i)
            s[i] = fifo[c][(size_t) i] * window[(size_t) i];
        fft.forward (s);
        for (int k = 0; k < numBins; ++k)
            powerSum[(size_t) k] += ((double) s[2 * k] * s[2 * k] + (double) s[2 * k + 1] * s[2 * k + 1]) * powScale;
    }
    ++frames;
}

ReferenceCurve ReferenceAnalyser::finish()
{
    // A recording shorter than one frame (or its tail) is analysed zero-padded.
    if (frames == 0 && fill > 0)
    {
        for (int c = 0; c < channelsSeen; ++c)
            std::fill (fifo[c].begin() + fill, fifo[c].end(), 0.0f);
        processFrame();
    }

    ReferenceCurve out;
    if (frames == 0)
        return out;

    const int numBins = fftSize / 2 + 1;
    std::vector<float> meanPower ((size_t) numBins);
    for (int k = 0; k < numBins; ++k)
        meanPower[(size_t) k] = (float) (powerSum[(size_t) k] / (double) frames);

    // The model's own one-ERB measure, on the model's bands at this rate, then onto the fixed grid.
    PerceptualModel model;
    model.prepare (sampleRate, fftSize);
    std::vector<float> bandDb (PerceptualModel::numBands);
    model.bandLevelsDb (meanPower.data(), bandDb.data());
    if (*std::max_element (bandDb.begin(), bandDb.end()) < -100.0f)
        return out;

    const double z0 = model.getZ0(), dz = model.getDz();
    for (int i = 0; i < ReferenceCurve::numPoints; ++i)
    {
        const double u = std::clamp ((refgrid::camAt (i, ReferenceCurve::numPoints) - z0) / dz, 0.0, (double) (PerceptualModel::numBands - 1));
        const int b = std::min ((int) u, PerceptualModel::numBands - 2);
        const float f = (float) (u - b);
        out.db[i] = bandDb[(size_t) b] + f * (bandDb[(size_t) b + 1] - bandDb[(size_t) b]);
    }
    const double fixedDz = (refgrid::zMax() - refgrid::zMin()) / (ReferenceCurve::numPoints - 1);
    normaliseLevels (out.db, nullptr, ReferenceCurve::numPoints, refgrid::zMin(), fixedDz, out.db);
    out.valid = true;
    return out;
}
}
