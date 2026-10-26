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
        float tiltPivotOct = 0.0f;           // 5.3.3: where Tilt turns, octaves from 1 kHz (0 = as before)
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
        float scKeyLoHz    = keyFullLoHz;    // 5.3.4: the part of the key that counts (the ends = the whole key)
        float scKeyHiHz    = keyFullHiHz;
        int refMode        = refOff;         // 4.0 reference: off / learn the key / match
        float refAmount    = 0.5f;
        float punch        = 0.0f;           // 5.0: -1..1 (0 = off)
        float width        = 1.0f;           // 5.0: 0..2 (1 = as is)
        float monoHz       = 0.0f;           // 5.0: mono below this, Hz (under 20 = off)
        float maxCutDb     = noLimitDb;      // 5.3.2: how far the auto EQ may cut / lift (noLimitDb = no limit)
        float maxBoostDb   = noLimitDb;
    };

    // STFT host for the perceptual model and the harmonic synth.
    // Output is the wet signal, delayed by exactly getLatencySamples().
    class SpectralEngine
    {
    public:
        static constexpr int maxChannels = 2;

        // Latency modes. Precise is the original engine (N = 4096 at <= 50 kHz); Normal halves N
        // and Low quarters it, trading bass resolution for latency. Since 5.2.0 every mode updates
        // the EQ 300 times a second (hop = sampleRate / 300; it was N/8 or N/4 = 94/s, Low 188/s).
        enum Mode { low = 0, normal = 1, precise = 2 };
        static constexpr int numModes = 3;
        static int fftSizeFor (double sampleRate, int mode = precise) noexcept;
        static constexpr double updatesPerSecond = 300.0;
        static int hopFor (double sampleRate, int fftSize) noexcept;

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

        // 5.2.1 Spread: a frame's work runs in numStages stages spread through the hop after it is taken,
        // instead of all at once in the block where its hop ends, so no small host block carries a whole
        // frame. Costs one hop of latency; the output is otherwise the same, sample for sample. Rests
        // while analysis-only (zero latency). Change it with processing suspended, then reset().
        static constexpr int numStages = 4;
        void setSpread (bool on) noexcept { spread = on; }
        bool isSpreading() const noexcept { return spread && ! analysisOnly; }
        int getStagesInLastCall() const noexcept { return stagesInCall; }   // tests: frame stages run by the last process()

        int getLatencySamples() const noexcept { return fftSize + (isSpreading() ? hop : 0); }
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

        // 5.1 Zero latency: analysis only. The model, the display and the curve run as usual, but
        // nothing is resynthesised and process() leaves the audio as it is; ZeroLatencyFilter applies
        // the curve. Kept across prepare() and reset().
        void setAnalysisOnly (bool on) noexcept { analysisOnly = on; }
        bool isAnalysisOnly() const noexcept { return analysisOnly; }
        // 5.2.2 Width paint: the painted share of Width at each bin (0..1), or nullptr when the
        // width lane isn't painted (Width as set everywhere).
        const float* getWidthShare() const noexcept { return widthPainted ? widthShare.data() : nullptr; }
        int samplesToNextFrame() const noexcept { return hop - pos; }
        std::uint32_t getFrameCount() const noexcept { return frameCounter; }
        int getNumBins() const noexcept { return numBins; }
        // This frame's curve as linear gain per bin: Mid and Side in Mid/Side, the same curve twice in Stereo.
        const float* getCurveMid() const noexcept { return curveMid != nullptr ? curveMid : model.getBinGain(); }
        const float* getCurveSide() const noexcept { return curveSide != nullptr ? curveSide : model.getBinGain(); }

    private:
        void processFrame() noexcept;   // the whole frame now (Spread off)
        // One frame in stages: 0 the input spectra, 1 the model (and Mid), 2 the curve, the harmonics,
        // Side and the display, 3 the resynthesis into the overlap-add. Then commitOutput() at the hop.
        void runStage (int s) noexcept;
        int stageAt (int s) const noexcept { return (2 * s + 1) * hop / (2 * numStages); }   // Spread: position in the hop
        void takeFrame() noexcept;      // Spread: copy the frame to work on, then advance the input
        void advanceInput() noexcept;
        void commitOutput() noexcept;   // the overlap-add's next hop to the output fifos
        void smoothParameters() noexcept;
        void analyseMidSide (int component) noexcept;
        void blendMidSide (float blend) noexcept;
        void applyListen (float msBlend) noexcept;
        void applyWidth (bool harmonics) noexcept;   // 5.0 Width and Mono bass

        double sampleRate = 48000.0;
        int fftSize = 4096, hop = 512, numBins = 2049, channels = 2, pos = 0;
        float framesPerSecond = 300.0f, lastHeatGuard = 1.0f;
        std::vector<float> olaGain;   // 5.2: per position, 1 / (sum of the overlapping window^2)

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
        void updateWidthShare() noexcept;
        void updateImage (int step) noexcept;   // 5.2.3
        // 5.2.3 The display's stereo image, in three light steps on consecutive frames of every eight
        // (37.5 a second): the input's Mid / Side power per bin through what is applied to them, then
        // the Mid band levels, then the Side band levels and Side - Mid, published with every frame.
        std::vector<float> imageMid, imageSide, imageMidDb, imageSideDb, imageRelDb;
        int imageTick = 0;
        bool imageValid = false;
        PaintCurves paintCopy;                     // 5.2.2: its width curve, kept across prepare() and reset()
        std::vector<float> binCam, widthShare;      // per bin: its Cam, and the painted share of Width
        bool widthPainted = false;
        TripleBuffer<ReferenceCurve>* learnOut = nullptr;
        ReferenceCurve learnScratch;
        bool learnFresh = true, learnHeld = false;
        int learnTick = 0;
        unsigned int refEpoch = 0;

        bool analysisOnly = false;
        const float* curveMid = nullptr;
        const float* curveSide = nullptr;

        // The frame being worked on: what its stages hand on to each other.
        struct FrameWork
        {
            float msBlend = 0.0f, heatGuard = 1.0f;
            bool listenEq = false, keyHasSignal = false, harmonicsWritten = false;
            int imageStep = -1;        // 5.2.3: this frame's step of the display's stereo image (-1: none)
            const float* keyPtr = nullptr;
            ModelSettings settings;
        };
        FrameWork fw;
        // Its input: the fifos themselves, or with Spread the copies taken at the hop boundary.
        const float* src[maxChannels] {};
        const float* srcKey = nullptr;
        std::vector<float> frameIn[maxChannels], frameKey;
        bool spread = false;
        int nextStage = numStages;   // Spread: the next stage of the frame taken (numStages: none pending)
        int stagesInCall = 0;
    };
}
