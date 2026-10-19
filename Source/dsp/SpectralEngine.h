#pragma once

#include "DisplayFrame.h"
#include "HarmonicSynth.h"
#include "PerceptualModel.h"
#include "RealFft.h"
#include "TripleBuffer.h"

#include <juce_dsp/juce_dsp.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace anneal
{
    // Listen (solo) modes. Off is the normal output; the others replace the engine's output with
    // one part of what it does: what Reveal lifts, what Smooth cuts, or only the harmonics.
    enum Listen { listenOff = 0, listenReveal = 1, listenSmooth = 2, listenHarmonics = 3 };

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
        float guard        = 0.0f;   // transient guard 0..1 (0 = off)
        float midSide      = 0.0f;   // 0 = linked stereo, 1 = independent Mid/Side
        float eqMid        = 1.0f, eqSide = 1.0f;
        float heatMid      = 1.0f, heatSide = 1.0f;
        int listen         = listenOff;   // Listen enum; switches per frame (overlap-add crossfades it)
        int flavour        = flavourClean;   // Flavour enum (HarmonicSynth.h); switches per frame like listen
        bool freeze        = false;          // hold the auto-EQ curve
        int scMode         = 0;              // sidechain: 0 off, 1 make room, 2 unmask
        float scAmount     = 0.5f;
        int refMode        = refOff;         // 4.0 reference: off / learn the key / match
        float refAmount    = 0.5f;
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

        // keepCurve: a frozen auto-EQ curve survives (the host preparing again, 4.0.2).
        void prepare (double sampleRate, int numChannels, int mode = precise, bool keepCurve = false);
        // keepCurve: a frozen auto-EQ curve survives (the host stopping and starting again).
        void reset (bool keepCurve = false);
        // Takes over another engine's auto-EQ curve (same sample rate), e.g. a frozen curve when
        // the latency mode changes.
        void adoptCurveFrom (const SpectralEngine& other) noexcept;

        // Called once per host block from the audio thread; values are smoothed per frame.
        void setParameters (const EngineParams& p) noexcept { target = p; }

        // In-place. Real-time safe. key: the sidechain input (any channel count, summed to mono),
        // sample-aligned with channels, or nullptr when there is none.
        void process (float* const* channels, int numChannels, int numSamples,
                      const float* const* key = nullptr, int keyChannels = 0) noexcept;

        int getLatencySamples() const noexcept { return fftSize; }
        int getFftSize() const noexcept        { return fftSize; }
        int getHopSize() const noexcept        { return hop; }
        int getNumChannels() const noexcept    { return channels; }
        double getSampleRate() const noexcept  { return sampleRate; }
        int getLastPeakCount() const noexcept  { return lastPeakCount; }
        float getLastHeatGuard() const noexcept { return lastHeatGuard; }   // 1 = harmonics written in full

        const PerceptualModel& getModel() const noexcept { return model; }
        TripleBuffer<DisplayFrame>& getDisplay() noexcept { return *displayOut; }
        // Several engines (one per latency mode) can publish into one shared display buffer.
        void setDisplayTarget (TripleBuffer<DisplayFrame>* shared) noexcept { displayOut = shared != nullptr ? shared : &display; }

        // 4.0 Reference match and Paint (audio thread, or while processing is suspended). Both are
        // kept across prepare() and reset(). A reference from outside (a file, a project) drops
        // the frames of a learn in progress; with curve.holdLearning, Learn key learns again only
        // once it is chosen again.
        void setReference (const ReferenceCurve& curve) noexcept;
        void setPaint (const PaintCurves& paint) noexcept;
        // Where a reference learned from the key is published (a few times a second while learning).
        // Learning starts over with the first key frame after Reference changes to Learn key.
        void setLearnTarget (TripleBuffer<ReferenceCurve>* out) noexcept { learnOut = out; }
        float getLearnedSeconds() const noexcept { return (float) model.getLearnedFrames() / framesPerSecond; }

    private:
        void processFrame() noexcept;
        void smoothParameters() noexcept;
        void processMidSide (const ModelSettings&, float blend, const float* keyPower) noexcept;
        void applyListen (float msBlend) noexcept;

        double sampleRate = 48000.0;
        int fftSize = 4096, hop = 512, numBins = 2049, channels = 2, pos = 0;
        float framesPerSecond = 93.75f, olaNorm = 1.0f / 3.0f, lastHeatGuard = 1.0f;

        RealFft fft;   // 4.0.2: Anneal's own (JUCE's layout and scaling, ~4x faster than its fallback)
        std::vector<float> window;
        std::vector<float> inFifo[maxChannels], outAcc[maxChannels], outFifo[maxChannels],
                           spec[maxChannels], harm[maxChannels];
        // Harmonic stream: own spectrum, overlap-add accumulator and output fifo, plus the
        // latency-aligned input used by the envelope gate.
        std::vector<float> harmSpec[maxChannels], harmAcc[maxChannels], harmFifo[maxChannels], alignRing[maxChannels];
        std::vector<float> power, outPower, harmPower;
        std::vector<float> rawSpec[maxChannels];   // this frame's spectrum before the EQ (listen modes)
        std::vector<float> keyFifo, keySpec, keyPower;   // sidechain key (mono), analysed only when used
        int ringPos = 0;
        float envIn = 0.0f, envHarm = 0.0f, gateGain = 1.0f, envRelease = 0.0f, gateCoeff = 0.0f;

        PerceptualModel model;
        HarmonicSynth synth;
        // Allocated up front; M/S uses the existing L/R FFTs and decodes before overlap-add.
        PerceptualModel msModel[2];
        HarmonicSynth msSynth[2];
        std::vector<float> msSpec[2], msHarm[2], msPower[2], msHarmPower[2];
        float msGuard[2] { 1.0f, 1.0f };
        int lastPeakCount = 0;
        bool msActive = false;

        EngineParams target, current;
        bool paramsPrimed = false;

        TripleBuffer<DisplayFrame> display;
        TripleBuffer<DisplayFrame>* displayOut = &display;
        std::uint32_t frameCounter = 0;

        void applyReference (const ReferenceCurve& curve) noexcept;
        TripleBuffer<ReferenceCurve>* learnOut = nullptr;
        ReferenceCurve learnScratch;
        bool learnFresh = true, learnHeld = false;
        int learnTick = 0;
        unsigned int refEpoch = 0;
    };
}
