#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"

AnnealAudioProcessor::AnnealAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AnnealState", anneal::createParameterLayout()),
      engine (std::make_unique<anneal::SpectralEngine>())
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

    // A usable default before the host calls prepareToPlay (e.g. state loading, editor opening).
    engine->prepare (48000.0, 2);
    setLatencySamples (engine->getLatencySamples());
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
    engine->prepare (sampleRate, preparedChannels);
    setLatencySamples (engine->getLatencySamples());

    ringLength = engine->getLatencySamples();
    dryRing.setSize (2, ringLength);
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

double AnnealAudioProcessor::getTailLengthSeconds() const
{
    return engine->getFftSize() / engine->getSampleRate();
}

double AnnealAudioProcessor::getLatencyMs() const noexcept
{
    return 1000.0 * engine->getLatencySamples() / engine->getSampleRate();
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
    engine->setParameters (p);
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
        engine->process (wet, nch, len);

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

void AnnealAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("stateVersion", anneal::ids::stateVersion, nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void AnnealAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // Unknown or malformed data is ignored; missing parameters keep their current values.
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* AnnealAudioProcessor::createEditor()
{
    return new AnnealEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AnnealAudioProcessor();
}
