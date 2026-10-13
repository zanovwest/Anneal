#pragma once

#include "dsp/LoudnessMatch.h"
#include "dsp/SpectralEngine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

class AnnealAudioProcessor final : public juce::AudioProcessor
{
public:
    AnnealAudioProcessor();
    ~AnnealAudioProcessor() override = default;

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

    anneal::SpectralEngine& getEngine() noexcept { return *engine; }
    float getMatchGainDb() const noexcept { return matchGainDb.load (std::memory_order_relaxed); }
    float takeOutputPeak (int channel) noexcept;   // linear peak since the last call
    double getLatencyMs() const noexcept;

private:
    void pushParametersToEngine() noexcept;

    std::unique_ptr<anneal::SpectralEngine> engine;
    anneal::LoudnessMatch match;

    juce::AudioBuffer<float> dryRing, dryBlock;
    int dryPos = 0, ringLength = 0, chunkCapacity = 0, preparedChannels = 2;

    juce::SmoothedValue<float> mixSmoothed, outputSmoothed, deltaSmoothed, bypassSmoothed;
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnnealAudioProcessor)
};
