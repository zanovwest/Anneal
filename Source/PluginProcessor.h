#pragma once

#include "Presets.h"
#include "dsp/LoudnessMatch.h"
#include "dsp/LoudnessMeter.h"
#include "dsp/Reference.h"
#include "dsp/SpectralEngine.h"
#include "dsp/TransientShaper.h"
#include "dsp/TruePeakLimiter.h"
#include "dsp/ZeroLatencyFilter.h"

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

    // 5.0 user presets (message thread). Files in getUserPresetFolder(): the user's application-data
    // folder unless a test sets another. Loading one is one undo step; it shows by name, with "*"
    // once edited.
    void setUserPresetFolder (const juce::File& folder, bool deletedToTrash = true) { userFolder = folder; userTrash = deletedToTrash; }
    juce::File getUserPresetFolder() const { return userFolder; }
    juce::StringArray getUserPresetNames() const { return anneal::presets::userNames (userFolder); }
    bool saveUserPreset (const juce::String& name);   // false: no usable name, or the file couldn't be written
    bool loadUserPreset (const juce::String& name);   // false: missing or not an Anneal preset
    bool deleteUserPreset (const juce::String& name);
    juce::String getUserPresetName() const { return userChosen ? userName : juce::String(); }
    // 5.2.4: preset files dropped on the preset bar, copied into the presets folder. Returns the names
    // they have there; why any file wasn't taken goes to `errors`.
    juce::StringArray importUserPresets (const juce::StringArray& paths, juce::StringArray& errors);

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState apvts;

    // The engine of the latency mode in use (one per mode, all prepared up front).
    anneal::SpectralEngine& getEngine() noexcept { return *engines[(size_t) mode]; }
    const anneal::ZeroLatencyFilter& getZeroFilter() const noexcept { return zeroFilter; }   // for the tests
    // 5.2.7 Punch at zero latency: the strongest gain it applied since the last call (dB, signed).
    float takeZeroPunchDb() noexcept { return zeroPunchDb.exchange (0.0f); }
    anneal::TripleBuffer<anneal::DisplayFrame>& getDisplay() noexcept { return display; }
    anneal::PeakHold displayPeakHold;   // 5.3.5: the display's peak hold (message thread), outlives the editor
    float getMatchGainDb() const noexcept { return matchGainDb.load (std::memory_order_relaxed); }
    float takeOutputPeak (int channel) noexcept;   // linear peak since the last call

    // 5.3.1 Loudness readout (EBU R128, LUFS): the input (latency-aligned, as Mix and Match use it) and
    // the output (what leaves Anneal), short-term and integrated. LoudnessMeter::none = nothing measured.
    // Measured all the time, editor open or not; the integrated values run until resetLoudness().
    struct Loudness { float shortIn, shortOut, integratedIn, integratedOut; };
    Loudness getLoudness() const noexcept;
    void resetLoudness() noexcept { loudnessResetPending.store (true); }   // any thread; done at the next block
    double getLatencyMs() const noexcept;
    double getLatencyMsForMode (int latencyMode) const noexcept;
    int getLatencyMode() const noexcept { return mode; }
    bool isZeroLatency() const noexcept { return zero; }   // 5.1
    bool isSpread() const noexcept { return spread; }      // 5.2.1: Spread load applied (it rests at zero latency)
    double getSpreadMs() const noexcept;                    // what Spread load adds in the current mode

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

    // 4.0 Reference match. The reference lives here and in the saved state (not in parameters);
    // the audio thread gets each new one through a wait-free buffer. Any thread but the audio one.
    struct ReferenceInfo
    {
        anneal::ReferenceCurve curve;   // invalid = no reference
        juce::String name;              // the file's name, or "sidechain key"
    };
    ReferenceInfo getReference();
    std::uint32_t getReferenceSerial() const noexcept { return refSerial.load(); }   // changes with each new reference
    // Collects what Learn key has learned since the last call (the processor's timer does this
    // too; the editor calls it before checking the serial, so it also works without that timer).
    void pollLearnedReference();
    // holdLearning: Learn key stops until it is chosen again (a dropped file replaces the key).
    void setReference (const anneal::ReferenceCurve& curve, const juce::String& name, bool holdLearning = false);
    void clearReference() { setReference ({}, {}); }

    // A file dropped on the display (wav, aif/aiff, flac, mp3, ogg) is decoded and analysed on a
    // background thread; when it is ready it becomes the reference and Reference switches to Match.
    static bool canLoadReferenceFile (const juce::File& file);
    void loadReferenceFile (const juce::File& file);
    bool isAnalysingReference() const noexcept { return analysing.load(); }
    juce::String getReferenceError();   // why the last dropped file was not used, or empty
    void* getLastAnalysisThread() const noexcept { return analysisThread.load(); }   // tests

    // 4.0 Paint (message thread). setPaint between beginPaintStroke() and endPaintStroke() is one
    // undo step; a paint change outside a stroke isn't recorded. mergeWithPrevious: the stroke joins
    // the stroke just before it (a double-click's reset and its first click are one step).
    anneal::PaintCurves getPaint();
    void setPaint (const anneal::PaintCurves& paint);
    void beginPaintStroke();
    void endPaintStroke (bool mergeWithPrevious = false);
    std::uint32_t getPaintSerial() const noexcept { return paintSerial.load(); }

private:
    void pushParametersToEngine() noexcept;
    void timerCallback() override;
    int requestedLatencyMode() const noexcept;
    bool requestedZeroLatency() const noexcept { return pZero != nullptr && pZero->load() > 0.5f; }
    bool requestedSpread() const noexcept { return pSpread != nullptr && pSpread->load() > 0.5f; }
    void applySpreadToEngines() noexcept;
    // 5.1 Zero latency: nothing at all (no STFT, no look-ahead).
    int totalLatency() noexcept { return zero ? 0 : getEngine().getLatencySamples() + limiter.getLatency(); }

    // Undo bookkeeping. A step holds parameter values, a paint stroke (4.0), or both.
    using Snapshot = std::map<int, float>;   // parameter index -> normalised value
    struct UndoStep
    {
        Snapshot before, after;
        bool paintChange = false;
        anneal::PaintCurves paintBefore, paintAfter;
    };
    void pushUndoStep (UndoStep step);
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
    bool zero = false;                                   // 5.1 Zero latency (mode is then Low, analysis only)
    bool spread = false;                                 // 5.2.1 Spread load (the engines rest it while analysis only)
    anneal::ZeroLatencyFilter zeroFilter;
    float zeroWidth = 1.0f, zeroMonoHz = 0.0f;
    anneal::TransientShaper zeroPunch;                   // 5.2.7 Punch at zero latency (time domain)
    anneal::TransientShaper::Settings zeroPunchSettings;
    std::atomic<float> zeroPunchDb { 0.0f };
    anneal::LoudnessMatch match;
    anneal::LoudnessMeter loudIn, loudOut;   // 5.3.1
    std::atomic<float> loudness[4] { anneal::LoudnessMeter::none, anneal::LoudnessMeter::none,
                                     anneal::LoudnessMeter::none, anneal::LoudnessMeter::none };
    std::atomic<bool> loudnessResetPending { false };

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
    std::atomic<float>* pZero = nullptr;
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
    std::atomic<float>* pRefMode = nullptr;
    std::atomic<float>* pRefAmount = nullptr;
    std::atomic<float>* pPunch = nullptr;      // 5.0
    std::atomic<float>* pWidth = nullptr;
    std::atomic<float>* pMonoBass = nullptr;
    std::atomic<float>* pSpread = nullptr;     // 5.2.1
    std::atomic<float>* pZeroPunch = nullptr;  // 5.2.7
    std::atomic<float>* pMaxCut = nullptr;     // 5.3.2
    std::atomic<float>* pMaxBoost = nullptr;
    std::atomic<float>* pTiltPivot = nullptr;  // 5.3.3
    std::atomic<float>* pScKeyLo = nullptr;    // 5.3.4
    std::atomic<float>* pScKeyHi = nullptr;

    // 5.0 user presets
    juce::File userFolder { anneal::presets::defaultUserFolder() };
    juce::String userName;
    anneal::presets::Values userValues {};
    bool userChosen = false, userTrash = true;

    // 4.0 Reference and Paint. refLock guards the copies below, the writer side of refToAudio and
    // paintToAudio and the reader side of refLearned; the audio thread never takes it.
    void applyPaintNow (const anneal::PaintCurves& paint);   // no undo step
    void referenceFailed (const juce::File& file, const juce::String& why);
    anneal::TripleBuffer<anneal::ReferenceCurve> refToAudio, refLearned;
    anneal::TripleBuffer<anneal::PaintCurves> paintToAudio;
    juce::CriticalSection refLock;
    anneal::ReferenceCurve refCurve;
    juce::String refName, refError;
    anneal::PaintCurves paint, strokeStart;
    bool strokeOpen = false, lastStepWasStroke = false;
    unsigned int refEpoch = 0;
    std::atomic<std::uint32_t> refSerial { 0 }, paintSerial { 0 };
    std::atomic<bool> analysing { false }, autoMatchPending { false };
    std::atomic<void*> analysisThread { nullptr };
    class ReferenceLoader;
    std::unique_ptr<ReferenceLoader> loader;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AnnealAudioProcessor)
};
