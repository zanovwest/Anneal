#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"
#include "Presets.h"

AnnealAudioProcessor::AnnealAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AnnealState", anneal::createParameterLayout())
{
    namespace ids = anneal::ids;
    pReveal   = apvts.getRawParameterValue (ids::reveal);
    pSmooth   = apvts.getRawParameterValue (ids::smooth);
    pTilt     = apvts.getRawParameterValue (ids::tilt);
    pResponse = apvts.getRawParameterValue (ids::response);
    pRangeLo  = apvts.getRawParameterValue (ids::rangeLo);
    pRangeHi  = apvts.getRawParameterValue (ids::rangeHi);
    pHeat     = apvts.getRawParameterValue (ids::heat);
    pColor    = apvts.getRawParameterValue (ids::color);
    pFocus    = apvts.getRawParameterValue (ids::focus);
    pMix      = apvts.getRawParameterValue (ids::mix);
    pOutput   = apvts.getRawParameterValue (ids::output);
    pMatch    = apvts.getRawParameterValue (ids::match);
    pDelta    = apvts.getRawParameterValue (ids::delta);
    pBypass   = apvts.getRawParameterValue (ids::bypass);
    pLatency  = apvts.getRawParameterValue (ids::latency);

    // A usable default before the host calls prepareToPlay (e.g. state loading, editor opening).
    for (int m = 0; m < anneal::SpectralEngine::numModes; ++m)
    {
        engines[(size_t) m] = std::make_unique<anneal::SpectralEngine>();
        engines[(size_t) m]->setDisplayTarget (&display);
        engines[(size_t) m]->prepare (48000.0, 2, m);
    }
    mode = requestedLatencyMode();
    setLatencySamples (getEngine().getLatencySamples());
    startTimerHz (10);
}

AnnealAudioProcessor::~AnnealAudioProcessor()
{
    stopTimer();
}

int AnnealAudioProcessor::requestedLatencyMode() const noexcept
{
    return juce::jlimit (0, anneal::SpectralEngine::numModes - 1, juce::roundToInt (pLatency->load()));
}

void AnnealAudioProcessor::timerCallback()
{
    applyLatencyModeIfChanged();
}

void AnnealAudioProcessor::applyLatencyModeIfChanged()
{
    const int wanted = requestedLatencyMode();
    if (wanted == mode)
        return;

    // suspendProcessing takes the callback lock the host processes under, so no block is running
    // while the engine and the dry path change length. Everything was allocated in prepareToPlay.
    suspendProcessing (true);
    mode = wanted;
    getEngine().reset();
    ringLength = getEngine().getLatencySamples();
    dryRing.clear();
    dryPos = 0;
    suspendProcessing (false);
    setLatencySamples (ringLength);   // the host re-aligns (VST3: restartComponent kLatencyChanged)
}

bool AnnealAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return layouts.getMainInputChannelSet() == out;
}

void AnnealAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedChannels = juce::jlimit (1, 2, getTotalNumInputChannels());
    for (int m = 0; m < anneal::SpectralEngine::numModes; ++m)
        engines[(size_t) m]->prepare (sampleRate, preparedChannels, m);
    mode = requestedLatencyMode();
    setLatencySamples (getEngine().getLatencySamples());

    // The dry ring is sized for the longest mode, so switching modes never allocates.
    ringLength = getEngine().getLatencySamples();
    dryRing.setSize (2, engines[anneal::SpectralEngine::precise]->getLatencySamples());
    dryRing.clear();
    dryPos = 0;

    chunkCapacity = juce::jmax (samplesPerBlock, 512);
    dryBlock.setSize (2, chunkCapacity);
    dryBlock.clear();

    match.prepare (sampleRate);

    for (auto* s : { &mixSmoothed, &outputSmoothed, &deltaSmoothed, &bypassSmoothed })
        s->reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (pMix->load() * 0.01f);
    outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    deltaSmoothed.setCurrentAndTargetValue (pDelta->load() > 0.5f ? 1.0f : 0.0f);
    bypassSmoothed.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 1.0f : 0.0f);
    matchSmoothed.reset (sampleRate, 0.5);
    matchSmoothed.setCurrentAndTargetValue (1.0f);

    pushParametersToEngine();
}

void AnnealAudioProcessor::reset()
{
    // Called by hosts when processing stops (JUCE's VST3 wrapper: setProcessing (false)), without
    // a new prepareToPlay. Clears every buffer that holds audio, so playback that starts again
    // doesn't replay the last ~85 ms from before. No allocation. The loudness-match gain is a slow
    // measurement, not audio, so it is kept (otherwise every restart would re-converge for ~3 s).
    for (auto& e : engines)
        e->reset();
    dryRing.clear();
    dryBlock.clear();
    dryPos = 0;
    for (auto* s : { &mixSmoothed, &outputSmoothed, &deltaSmoothed, &bypassSmoothed })
        s->setCurrentAndTargetValue (s->getTargetValue());
    outputPeak[0] = outputPeak[1] = 0.0f;
}

double AnnealAudioProcessor::getTailLengthSeconds() const
{
    const auto& e = *engines[(size_t) mode];
    return e.getFftSize() / e.getSampleRate();
}

double AnnealAudioProcessor::getLatencyMs() const noexcept
{
    const auto& e = *engines[(size_t) mode];
    return 1000.0 * e.getLatencySamples() / e.getSampleRate();
}

double AnnealAudioProcessor::getLatencyMsForMode (int m) const noexcept
{
    const auto& e = *engines[(size_t) juce::jlimit (0, anneal::SpectralEngine::numModes - 1, m)];
    return 1000.0 * e.getLatencySamples() / e.getSampleRate();
}

void AnnealAudioProcessor::pushParametersToEngine() noexcept
{
    anneal::EngineParams p;
    p.reveal       = pReveal->load() * 0.01f;
    p.smooth       = pSmooth->load() * 0.01f;
    p.tiltDbPerOct = pTilt->load();
    p.responseMs   = pResponse->load();
    p.rangeLoHz    = pRangeLo->load();
    p.rangeHiHz    = juce::jmax (pRangeHi->load(), p.rangeLoHz * 1.5f);
    p.heat         = pHeat->load() * 0.01f;
    p.color        = pColor->load() * 0.01f;
    p.focus        = pFocus->load() * 0.01f;
    getEngine().setParameters (p);
}

void AnnealAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getTotalNumInputChannels();
    for (int c = numIn; c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);
    if (numSamples == 0 || ringLength == 0)   // nothing to do / not prepared yet: pass through
        return;

    const int nch = juce::jmin (numIn, preparedChannels, buffer.getNumChannels());

    pushParametersToEngine();
    mixSmoothed.setTargetValue (pMix->load() * 0.01f);
    outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    deltaSmoothed.setTargetValue (pDelta->load() > 0.5f ? 1.0f : 0.0f);
    bypassSmoothed.setTargetValue (pBypass->load() > 0.5f ? 1.0f : 0.0f);
    const bool matchOn = pMatch->load() > 0.5f;

    float peak[2] = { 0.0f, 0.0f };

    for (int start = 0; start < numSamples; start += chunkCapacity)
    {
        const int len = juce::jmin (chunkCapacity, numSamples - start);

        // 1. Latency-matched dry copy.
        int posAfter = dryPos;
        for (int c = 0; c < nch; ++c)
        {
            const float* x = buffer.getReadPointer (c, start);
            float* d = dryBlock.getWritePointer (c);
            float* ring = dryRing.getWritePointer (c);
            int p = dryPos;
            for (int i = 0; i < len; ++i)
            {
                d[i] = ring[p];
                ring[p] = x[i];
                if (++p == ringLength)
                    p = 0;
            }
            posAfter = p;
        }
        dryPos = posAfter;

        // 2. Spectral engine, in place -> wet.
        float* wet[2] = { buffer.getWritePointer (0, start), nch > 1 ? buffer.getWritePointer (1, start) : nullptr };
        getEngine().process (wet, nch, len);

        // 3. Loudness match (always measured, applied only when enabled).
        const float* dryPtrs[2] = { dryBlock.getReadPointer (0), dryBlock.getReadPointer (nch > 1 ? 1 : 0) };
        const float* wetPtrs[2] = { wet[0], nch > 1 ? wet[1] : wet[0] };
        const float target = match.process (dryPtrs, wetPtrs, nch, len);
        matchSmoothed.setTargetValue (matchOn ? target : 1.0f);

        // 4. Mix / delta / output / bypass.
        for (int i = 0; i < len; ++i)
        {
            const float mg = matchSmoothed.getNextValue();
            const float mx = mixSmoothed.getNextValue();
            const float og = outputSmoothed.getNextValue();
            const float dl = deltaSmoothed.getNextValue();
            const float bp = bypassSmoothed.getNextValue();

            for (int c = 0; c < nch; ++c)
            {
                const float dry = dryBlock.getSample (c, i);
                const float diff = mx * (wet[c][i] * mg - dry);
                const float normal = dry + diff;
                const float y = (normal + dl * (diff - normal)) * og;
                const float out = y + bp * (dry - y);
                wet[c][i] = out;
                peak[c] = juce::jmax (peak[c], std::abs (out));
            }
        }
    }

    matchGainDb.store (juce::Decibels::gainToDecibels (matchSmoothed.getCurrentValue()), std::memory_order_relaxed);
    for (int c = 0; c < nch; ++c)
        if (peak[c] > outputPeak[c].load (std::memory_order_relaxed))
            outputPeak[c].store (peak[c], std::memory_order_relaxed);
}

float AnnealAudioProcessor::takeOutputPeak (int channel) noexcept
{
    return outputPeak[juce::jlimit (0, 1, channel)].exchange (0.0f, std::memory_order_relaxed);
}

juce::AudioProcessorParameter* AnnealAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (anneal::ids::bypass);
}

int AnnealAudioProcessor::getNumPrograms()
{
    return anneal::presets::count();
}

void AnnealAudioProcessor::setCurrentProgram (int index)
{
    if (! juce::isPositiveAndBelow (index, anneal::presets::count()))
        return;
    currentPreset = index;
    presetChosen = true;
    anneal::presets::apply (apvts, index);
}

const juce::String AnnealAudioProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, anneal::presets::count()) ? juce::String (anneal::presets::get (index).name)
                                                                      : juce::String();
}

juce::String AnnealAudioProcessor::getPresetDisplayName()
{
    const int chosen = currentPreset.load();
    const int matching = anneal::presets::findMatching (apvts, presetChosen ? chosen : -1);
    if (matching >= 0)
        return anneal::presets::get (matching).name;
    if (presetChosen)
        return juce::String (anneal::presets::get (chosen).name) + " *";
    return "Custom";
}

void AnnealAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", anneal::ids::stateVersion, nullptr);
    if (presetChosen)
        state.setProperty ("preset", currentPreset.load(), nullptr);
    else
        state.removeProperty ("preset", nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AnnealAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Unknown or malformed data is ignored; missing parameters keep their current values.
    // "preset" is optional (added in 0.1.1; older versions ignore it, older states lack it).
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
        {
            const auto tree = juce::ValueTree::fromXml (*xml);
            // Check before replaceState: the APVTS fills missing parameters into the same tree.
            const bool hasLatency = tree.getChildWithProperty ("id", anneal::ids::latency).isValid();
            const int preset = tree.getProperty ("preset", -1);
            presetChosen = juce::isPositiveAndBelow (preset, anneal::presets::count());
            currentPreset = presetChosen ? preset : 0;
            apvts.replaceState (tree);

            // Projects saved before 0.2.0 have no Latency: they keep the engine they were made
            // with (Precise, 85 ms at 48 kHz), so they sound exactly as before.
            if (! hasLatency)
                if (auto* lp = apvts.getParameter (anneal::ids::latency))
                    lp->setValueNotifyingHost (lp->convertTo0to1 ((float) anneal::SpectralEngine::precise));

            if (juce::MessageManager::existsAndIsCurrentThread())
                applyLatencyModeIfChanged();
        }
}

juce::AudioProcessorEditor* AnnealAudioProcessor::createEditor()
{
    return new AnnealEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnnealAudioProcessor();
}
