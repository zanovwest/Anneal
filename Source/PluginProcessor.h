#pragma once

#include "dsp/LoudnessMatch.h"
#include "dsp/SpectralEngine.h"
#include "dsp/TruePeakLimiter.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <vector>

class AnnealAudioProcessor final : public juce::AudioProcessor, private juce::Timer,
                                   private juce::AudioProcessorParameter::Listener
{
public:
    AnnealAudioProcessor();
    ~AnnealAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void reset() override;   // host stopped processing: forget all buffered audio
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Anneal"; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override;

    // Factory presets (Presets.h), exposed to the host as programs. Message thread.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentPreset.load(); }
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    // What the preset bar shows: the preset the settings equal, "<last chosen> *" once edited,
    // or "Custom" for settings that never came from a preset (e.g. projects from before 0.1.1).
    juce::String getPresetDisplayName();

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState apvts;

    // The engine of the latency mode in use (one per mode, all prepared up front).
    anneal::SpectralEngine& getEngine() noexcept { return *engines[(size_t) mode]; }
    anneal::TripleBuffer<anneal::DisplayFrame>& getDisplay() noexcept { return display; }
    float getMatchGainDb() const noexcept { return matchGainDb.load (std::memory_order_relaxed); }
    float takeOutputPeak (int channel) noexcept;   // linear peak since the last call
    double getLatencyMs() const noexcept;
    double getLatencyMsForMode (int latencyMode) const noexcept;
    int getLatencyMode() const noexcept { return mode; }

    // Applies the Latency parameter now if it differs from the mode in use (message thread; the
    // processor's own timer does this within 100 ms of a change). Pauses processing for the
    // switch, then reports the new latency to the host.
    void applyLatencyModeIfChanged();

    // 3.0: total latency = engine frame + the ceiling's look-ahead (constant, also when it's off).
    int getCeilingLatency() const noexcept { return limiter.getLatency(); }
    float takeCeilingReductionDb() noexcept { return limiterReduction.exchange (0.0f); }
    bool isSidechainConnected() const noexcept { return sidechainLive.load (std::memory_order_relaxed); }

    // 3.0 Undo / Redo of parameter edits (message thread). Records completed gestures from the
    // editor and the host's own controls; automation playback has no gestures and isn't recorded.
    // A preset change is one step.
    bool canUndo() const noexcept { return ! undoStack.empty(); }
    bool canRedo() const noexcept { return ! redoStack.empty(); }
    void undo();
    void redo();

private:
    void pushParametersToEngine() noexcept;
    void timerCallback() override;
    int requestedLatencyMode() const noexcept;
    int totalLatency() noexcept { return getEngine().getLatencySamples() + limiter.getLatency(); }

    // Undo bookkeeping.
    using Snapshot = std::map<int, float>;   // parameter index -> normalised value
    struct UndoStep { Snapshot before, after; };
    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int index, bool starting) override;
    void beginUndoGroup();
    void endUndoGroup();
    void commitPending();
    void applySnapshot (const Snapshot& values);
    std::vector<UndoStep> undoStack, redoStack;
    UndoStep pending;
    Snapshot gestureStart;
    int openGestures = 0, groupDepth = 0;
    bool applyingUndo = false;

    anneal::TruePeakLimiter limiter;
    juce::AudioBuffer<float> bypassRing;   // dry delayed by the limiter's look-ahead, for Bypass
    std::vector<float> bypassRamp;         // per-sample bypass amount for the current chunk
    int bypassPos = 0;
    std::atomic<float> limiterReduction { 0.0f };
    std::atomic<bool> sidechainLive { false };

    std::array<std::unique_ptr<anneal::SpectralEngine>, anneal::SpectralEngine::numModes> engines;
    anneal::TripleBuffer<anneal::DisplayFrame> display;   // shared by the engines
    int mode = anneal::SpectralEngine::low;              // changed only while processing is suspended
    anneal::LoudnessMatch match;

    juce::AudioBuffer<float> dryRing, dryBlock;
    int dryPos = 0, ringLength = 0, chunkCapacity = 0, preparedChannels = 2;

    juce::SmoothedValue<float> mixSmoothed, outputSmoothed, deltaSmoothed, bypassSmoothed, listenSmoothed;
    // While a listen mode is on (and for two frame lengths after, until the engine's output is
    // normal again) the wet signal is a solo, so the loudness match keeps its last gain.
    int matchHold = 0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> matchSmoothed;

    // Last preset chosen (saved in the state, so a host restoring its program number after the
    // state finds it unchanged and does not re-apply the preset over the saved knobs).
    std::atomic<int> currentPreset { 0 };
    std::atomic<bool> presetChosen { false };

    std::atomic<float> matchGainDb { 0.0f };
    std::atomic<float> outputPeak[2] { 0.0f, 0.0f };

    std::atomic<float>* pReveal = nullptr;
    std::atomic<float>* pSmooth = nullptr;
    std::atomic<float>* pTilt = nullptr;
    std::atomic<float>* pResponse = nullptr;
    std::atomic<float>* pRangeLo = nullptr;
    std::atomic<float>* pRangeHi = nullptr;
    std::atomic<float>* pHeat = nullptr;
    std::atomic<float>* pColor = nullptr;
    std::atomic<float>* pFocus = nullptr;
    std::atomic<float>* pMix = nullptr;
    std::atomic<float>* pOutput = nullptr;
    std::atomic<float>* pMatch = nullptr;
    std::atomic<float>* pDelta = nullptr;
    std::atomic<float>* pBypass = nullptr;
    std::atomic<float>* pLatency = nullptr;
    std::atomic<float>* pGuard = nullptr;
    std::atomic<float>* pStereoMode = nullptr;
    std::atomic<float>* pEqTarget = nullptr;
    std::atomic<float>* pHeatTarget = nullptr;
    std::atomic<float>* pListen = nullptr;
    std::atomic<float>* pFlavour = nullptr;
    std::atomic<float>* pFreeze = nullptr;
    std::atomic<float>* pScMode = nullptr;
    std::atomic<float>* pScAmount = nullptr;
    std::atomic<float>* pCeiling = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnnealAudioProcessor)
};
