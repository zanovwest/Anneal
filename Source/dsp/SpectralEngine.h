#pragma once

#include "DisplayFrame.h"
#include "HarmonicSynth.h"
#include "PerceptualModel.h"
#include "TripleBuffer.h"

#include <juce_dsp/juce_dsp.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace anneal
{
    struct EngineParams
    {
        float reveal       = 0.0f;
        float smooth       = 0.0f;
        float tiltDbPerOct = 0.0f;
        float responseMs   = 120.0f;
        float rangeLoHz    = 20.0f;
        float rangeHiHz    = 20000.0f;
        float heat         = 0.0f;
        float color        = 0.35f;
        float focus        = 0.0f;
    };

    // STFT host for the perceptual model and the harmonic synth.
    // Output is the wet signal, delayed by exactly getLatencySamples().
    class SpectralEngine
    {
    public:
        static constexpr int maxChannels = 2;

        // Latency modes. Precise is the original engine (N = 4096 at <= 50 kHz, hop N/8);
        // Normal halves N and Low quarters it (hop N/4), trading bass resolution for latency.
        enum Mode { low = 0, normal = 1, precise = 2 };
        static constexpr int numModes = 3;
        static int fftSizeFor (double sampleRate, int mode = precise) noexcept;

        void prepare (double sampleRate, int numChannels, int mode = precise);
        void reset();

        // Called once per host block from the audio thread; values are smoothed per frame.
        void setParameters (const EngineParams& p) noexcept { target = p; }

        // In-place. Real-time safe.
        void process (float* const* channels, int numChannels, int numSamples) noexcept;

        int getLatencySamples() const noexcept { return fftSize; }
        int getFftSize() const noexcept        { return fftSize; }
        int getHopSize() const noexcept        { return hop; }
        int getNumChannels() const noexcept    { return channels; }
        double getSampleRate() const noexcept  { return sampleRate; }
        int getLastPeakCount() const noexcept  { return synth.getLastPeakCount(); }
        float getLastHeatGuard() const noexcept { return lastHeatGuard; }   // 1 = harmonics written in full

        const PerceptualModel& getModel() const noexcept { return model; }
        TripleBuffer<DisplayFrame>& getDisplay() noexcept { return *displayOut; }
        // Several engines (one per latency mode) can publish into one shared display buffer.
        void setDisplayTarget (TripleBuffer<DisplayFrame>* shared) noexcept { displayOut = shared != nullptr ? shared : &display; }

    private:
        void processFrame() noexcept;
        void smoothParameters() noexcept;

        double sampleRate = 48000.0;
        int fftSize = 4096, hop = 512, numBins = 2049, channels = 2, pos = 0;
        float framesPerSecond = 93.75f, olaNorm = 1.0f / 3.0f, lastHeatGuard = 1.0f;

        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> window;
        std::vector<float> inFifo[maxChannels], outAcc[maxChannels], outFifo[maxChannels],
                           spec[maxChannels], harm[maxChannels];
        // Harmonic stream: own spectrum, overlap-add accumulator and output fifo, plus the
        // latency-aligned input used by the envelope gate.
        std::vector<float> harmSpec[maxChannels], harmAcc[maxChannels], harmFifo[maxChannels], alignRing[maxChannels];
        std::vector<float> power, outPower, harmPower;
        int ringPos = 0;
        float envIn = 0.0f, envHarm = 0.0f, gateGain = 1.0f, envRelease = 0.0f, gateCoeff = 0.0f;

        PerceptualModel model;
        HarmonicSynth synth;

        EngineParams target, current;
        bool paramsPrimed = false;

        TripleBuffer<DisplayFrame> display;
        TripleBuffer<DisplayFrame>* displayOut = &display;
        std::uint32_t frameCounter = 0;
    };
}
