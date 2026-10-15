#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"
#include "Presets.h"
#include "dsp/ReferenceAnalyser.h"

#include <juce_audio_formats/juce_audio_formats.h>

//==============================================================================
// 4.0: decodes a dropped reference file and analyses it, on its own thread (never the audio
// thread). Decoding uses JUCE's own readers on every platform, no OS codecs.
class AnnealAudioProcessor::ReferenceLoader final : public juce::Thread
{
public:
    explicit ReferenceLoader (AnnealAudioProcessor& p) : juce::Thread ("Anneal reference"), owner (p) {}
    ~ReferenceLoader() override { stopThread (10000); }

    void load (const juce::File& f)
    {
        stopThread (10000);   // a newer drop replaces one still being analysed
        file = f;
        owner.analysing = true;
        startThread();
    }

    static void registerFormats (juce::AudioFormatManager& formats)
    {
        formats.registerFormat (new juce::WavAudioFormat(), true);
        formats.registerFormat (new juce::AiffAudioFormat(), false);
        formats.registerFormat (new juce::FlacAudioFormat(), false);
        formats.registerFormat (new juce::OggVorbisAudioFormat(), false);
        formats.registerFormat (new juce::MP3AudioFormat(), false);
    }

private:
    void run() override
    {
        owner.analysisThread = juce::Thread::getCurrentThreadId();
        analyse();
        owner.analysing = false;
    }

    void analyse()
    {
        juce::AudioFormatManager formats;
        registerFormats (formats);
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr || reader->sampleRate <= 0.0 || reader->numChannels < 1)
        {
            owner.referenceFailed (file, "can't read this file");
            return;
        }

        // Up to 20 minutes, in chunks, so a long file never sits in memory at once.
        const int channels = juce::jlimit (1, 8, (int) reader->numChannels);
        const auto total = std::min<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 1200.0));
        anneal::ReferenceAnalyser analyser (reader->sampleRate);
        juce::AudioBuffer<float> chunk (channels, 65536);
        for (juce::int64 pos = 0; pos < total; pos += chunk.getNumSamples())
        {
            if (threadShouldExit())
                return;
            const int n = (int) std::min<juce::int64> (chunk.getNumSamples(), total - pos);
            if (! reader->read (chunk.getArrayOfWritePointers(), channels, pos, n))
                break;
            analyser.push (chunk.getArrayOfReadPointers(), channels, n);
        }
        const auto curve = analyser.finish();
        if (threadShouldExit())
            return;
        if (! curve.valid)
        {
            owner.referenceFailed (file, "it is silent");
            return;
        }
        owner.setReference (curve, file.getFileName(), true);
        owner.autoMatchPending = true;
    }

    AnnealAudioProcessor& owner;
    juce::File file;
};

//==============================================================================
// 4.0 state children: the reference (1024 little-endian floats, base64) and the paint (two lists
// of 64 numbers). Both optional; parameters stay plain APVTS parameters.
namespace
{
    const juce::Identifier referenceTag ("Reference"), paintTag ("Paint");

    juce::ValueTree referenceToTree (const anneal::ReferenceCurve& curve, const juce::String& name)
    {
        juce::MemoryOutputStream out;
        for (float v : curve.db)
            out.writeFloat (v);   // little-endian on every platform
        juce::ValueTree t (referenceTag);
        t.setProperty ("name", name, nullptr);
        t.setProperty ("points", anneal::ReferenceCurve::numPoints, nullptr);
        t.setProperty ("loHz", 20, nullptr);
        t.setProperty ("hiHz", 20000, nullptr);
        t.setProperty ("db", out.getMemoryBlock().toBase64Encoding(), nullptr);
        return t;
    }

    // An invalid curve (no reference) for anything missing, malformed or of another size.
    anneal::ReferenceCurve referenceFromTree (const juce::ValueTree& t, juce::String& name)
    {
        anneal::ReferenceCurve curve;
        name.clear();
        juce::MemoryBlock data;
        if (! t.isValid() || (int) t.getProperty ("points", 0) != anneal::ReferenceCurve::numPoints
            || ! data.fromBase64Encoding (t.getProperty ("db").toString())
            || data.getSize() != sizeof (float) * (size_t) anneal::ReferenceCurve::numPoints)
            return curve;
        juce::MemoryInputStream in (data, false);
        for (auto& v : curve.db)
        {
            v = in.readFloat();
            if (! std::isfinite (v))
                return {};
        }
        curve.valid = true;
        name = t.getProperty ("name").toString();
        return curve;
    }

    juce::ValueTree paintToTree (const anneal::PaintCurves& p)
    {
        auto text = [] (const float* v)
        {
            juce::StringArray s;
            for (int i = 0; i < anneal::PaintCurves::numPoints; ++i)
                s.add (v[i] == 1.0f ? juce::String ("1") : juce::String (v[i], 4));
            return s.joinIntoString (" ");
        };
        juce::ValueTree t (paintTag);
        t.setProperty ("points", anneal::PaintCurves::numPoints, nullptr);
        t.setProperty ("eq", text (p.eq), nullptr);
        t.setProperty ("heat", text (p.heat), nullptr);
        return t;
    }

    // No paint (all 100 %) for anything missing; a malformed list leaves that curve at 100 %.
    anneal::PaintCurves paintFromTree (const juce::ValueTree& t)
    {
        anneal::PaintCurves p;
        if (! t.isValid() || (int) t.getProperty ("points", 0) != anneal::PaintCurves::numPoints)
            return p;
        auto read = [] (const juce::String& text, float* dst)
        {
            const auto tokens = juce::StringArray::fromTokens (text, " ", "");
            if (tokens.size() != anneal::PaintCurves::numPoints)
                return;
            float v[anneal::PaintCurves::numPoints];
            for (int i = 0; i < anneal::PaintCurves::numPoints; ++i)
            {
                v[i] = tokens[i].getFloatValue();
                if (! std::isfinite (v[i]))
                    return;
            }
            for (int i = 0; i < anneal::PaintCurves::numPoints; ++i)
                dst[i] = juce::jlimit (0.0f, 1.0f, v[i]);
        };
        read (t.getProperty ("eq").toString(), p.eq);
        read (t.getProperty ("heat").toString(), p.heat);
        return p;
    }
}

//==============================================================================
AnnealAudioProcessor::AnnealAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                          // 3.0: optional sidechain key (FL Studio: route a track to Anneal's sidechain).
                          .withInput ("Sidechain", juce::AudioChannelSet::stereo(), false)),
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
    pGuard    = apvts.getRawParameterValue (ids::guard);
    pStereoMode = apvts.getRawParameterValue (ids::stereoMode);
    pEqTarget   = apvts.getRawParameterValue (ids::eqTarget);
    pHeatTarget = apvts.getRawParameterValue (ids::heatTarget);
    pListen     = apvts.getRawParameterValue (ids::listen);
    pFlavour    = apvts.getRawParameterValue (ids::flavour);
    pFreeze     = apvts.getRawParameterValue (ids::freeze);
    pScMode     = apvts.getRawParameterValue (ids::scMode);
    pScAmount   = apvts.getRawParameterValue (ids::scAmount);
    pCeiling    = apvts.getRawParameterValue (ids::ceiling);
    pRefMode    = apvts.getRawParameterValue (ids::refMode);
    pRefAmount  = apvts.getRawParameterValue (ids::refAmount);
    for (auto* prm : getParameters())
        prm->addListener (this);

    // A usable default before the host calls prepareToPlay (e.g. state loading, editor opening).
    for (int m = 0; m < anneal::SpectralEngine::numModes; ++m)
    {
        engines[(size_t) m] = std::make_unique<anneal::SpectralEngine>();
        engines[(size_t) m]->setDisplayTarget (&display);
        engines[(size_t) m]->setLearnTarget (&refLearned);
        engines[(size_t) m]->prepare (48000.0, 2, m);
    }
    loader = std::make_unique<ReferenceLoader> (*this);
    limiter.prepare (48000.0, 2);
    mode = requestedLatencyMode();
    setLatencySamples (totalLatency());
    startTimerHz (10);
}

AnnealAudioProcessor::~AnnealAudioProcessor()
{
    loader.reset();   // stops a running analysis before anything it reports to goes away
    stopTimer();
    for (auto* prm : getParameters())
        prm->removeListener (this);
}

int AnnealAudioProcessor::requestedLatencyMode() const noexcept
{
    return juce::jlimit (0, anneal::SpectralEngine::numModes - 1, juce::roundToInt (pLatency->load()));
}

void AnnealAudioProcessor::timerCallback()
{
    applyLatencyModeIfChanged();
    pollLearnedReference();

    // A dropped file was analysed: matching it is what dropping it asked for (one undo step).
    if (autoMatchPending.exchange (false))
        if (auto* rp = apvts.getParameter (anneal::ids::refMode); rp != nullptr && juce::roundToInt (pRefMode->load()) != anneal::refMatch)
        {
            rp->beginChangeGesture();
            rp->setValueNotifyingHost (rp->convertTo0to1 ((float) anneal::refMatch));
            rp->endChangeGesture();
        }
}

void AnnealAudioProcessor::applyLatencyModeIfChanged()
{
    const int wanted = requestedLatencyMode();
    if (wanted == mode)
        return;

    // suspendProcessing takes the callback lock the host processes under, so no block is running
    // while the engine and the dry path change length. Everything was allocated in prepareToPlay.
    suspendProcessing (true);
    const int previous = mode;
    mode = wanted;
    getEngine().reset();
    if (pFreeze->load() > 0.5f)   // a frozen curve stays what it is in the new mode
        getEngine().adoptCurveFrom (*engines[(size_t) previous]);
    pollLearnedReference();       // a reference learned in the previous mode's engine comes along
    {
        const juce::ScopedLock sl (refLock);
        auto curve = refCurve;
        curve.holdLearning = false;
        getEngine().setReference (curve);
    }
    ringLength = getEngine().getLatencySamples();
    dryRing.clear();
    dryPos = 0;
    limiter.reset();
    bypassRing.clear();
    bypassPos = 0;
    suspendProcessing (false);
    setLatencySamples (totalLatency());   // the host re-aligns (VST3: restartComponent kLatencyChanged)
}

bool AnnealAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    if (layouts.getMainInputChannelSet() != out)
        return false;
    // Sidechain: off, mono or stereo (it is summed to mono for the analysis).
    if (layouts.inputBuses.size() > 1)
    {
        const auto sc = layouts.getChannelSet (true, 1);
        if (! sc.isDisabled() && sc != juce::AudioChannelSet::mono() && sc != juce::AudioChannelSet::stereo())
            return false;
    }
    return true;
}

void AnnealAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedChannels = juce::jlimit (1, 2, getMainBusNumInputChannels());
    for (int m = 0; m < anneal::SpectralEngine::numModes; ++m)
        engines[(size_t) m]->prepare (sampleRate, preparedChannels, m);
    limiter.prepare (sampleRate, preparedChannels);
    bypassRing.setSize (2, limiter.getLatency());
    bypassRing.clear();
    bypassPos = 0;
    bypassRamp.assign ((size_t) juce::jmax (samplesPerBlock, 512), 0.0f);
    mode = requestedLatencyMode();
    setLatencySamples (totalLatency());

    // The dry ring is sized for the longest mode, so switching modes never allocates.
    ringLength = getEngine().getLatencySamples();
    dryRing.setSize (2, engines[anneal::SpectralEngine::precise]->getLatencySamples());
    dryRing.clear();
    dryPos = 0;

    chunkCapacity = juce::jmax (samplesPerBlock, 512);
    dryBlock.setSize (2, chunkCapacity);
    dryBlock.clear();

    match.prepare (sampleRate);

    for (auto* s : { &mixSmoothed, &outputSmoothed, &deltaSmoothed, &bypassSmoothed, &listenSmoothed })
        s->reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (pMix->load() * 0.01f);
    outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    deltaSmoothed.setCurrentAndTargetValue (pDelta->load() > 0.5f ? 1.0f : 0.0f);
    bypassSmoothed.setCurrentAndTargetValue (pBypass->load() > 0.5f ? 1.0f : 0.0f);
    listenSmoothed.setCurrentAndTargetValue (pListen->load() > 0.5f ? 1.0f : 0.0f);
    matchHold = 0;
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
    // So is a frozen auto-EQ curve (4.0: it was re-primed from the first frame after the restart).
    const bool frozen = pFreeze->load() > 0.5f;
    for (auto& e : engines)
        e->reset (frozen);
    limiter.reset();
    bypassRing.clear();
    bypassPos = 0;
    dryRing.clear();
    dryBlock.clear();
    dryPos = 0;
    for (auto* s : { &mixSmoothed, &outputSmoothed, &deltaSmoothed, &bypassSmoothed, &listenSmoothed })
        s->setCurrentAndTargetValue (s->getTargetValue());
    matchHold = 0;
    outputPeak[0] = outputPeak[1] = 0.0f;
}

double AnnealAudioProcessor::getTailLengthSeconds() const
{
    const auto& e = *engines[(size_t) mode];
    return (e.getFftSize() + limiter.getLatency()) / e.getSampleRate();
}

double AnnealAudioProcessor::getLatencyMs() const noexcept
{
    return getLatencyMsForMode (mode);
}

double AnnealAudioProcessor::getLatencyMsForMode (int m) const noexcept
{
    const auto& e = *engines[(size_t) juce::jlimit (0, anneal::SpectralEngine::numModes - 1, m)];
    return 1000.0 * (e.getLatencySamples() + limiter.getLatency()) / e.getSampleRate();
}

void AnnealAudioProcessor::pushParametersToEngine() noexcept
{
    anneal::EngineParams p;
    p.reveal       = pReveal->load() * 0.01f;
    p.smooth       = pSmooth->load() * 0.01f;
    p.tiltDbPerOct = pTilt->load();
    p.responseMs   = pResponse->load();
    p.rangeLoHz    = pRangeLo->load();
    p.rangeHiHz    = anneal::effectiveRangeHi (p.rangeLoHz, pRangeHi->load());
    p.heat         = pHeat->load() * 0.01f;
    p.color        = pColor->load() * 0.01f;
    p.focus        = pFocus->load() * 0.01f;
    p.guard        = pGuard->load() * 0.01f;
    p.midSide      = pStereoMode->load() > 0.5f ? 1.0f : 0.0f;
    const int eq = juce::roundToInt (pEqTarget->load()), ht = juce::roundToInt (pHeatTarget->load());
    p.eqMid = eq == 2 ? 0.0f : 1.0f;
    p.eqSide = eq == 1 ? 0.0f : 1.0f;
    p.heatMid = ht == 2 ? 0.0f : 1.0f;
    p.heatSide = ht == 1 ? 0.0f : 1.0f;
    p.listen = juce::jlimit (0, 3, juce::roundToInt (pListen->load()));
    p.flavour = juce::jlimit (0, anneal::HarmonicSynth::numFlavours - 1, juce::roundToInt (pFlavour->load()));
    p.freeze = pFreeze->load() > 0.5f;
    p.scMode = juce::jlimit (0, 2, juce::roundToInt (pScMode->load()));
    p.scAmount = pScAmount->load() * 0.01f;
    p.refMode = juce::jlimit (0, 2, juce::roundToInt (pRefMode->load()));
    p.refAmount = pRefAmount->load() * 0.01f;
    getEngine().setParameters (p);
}

void AnnealAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn = getMainBusNumInputChannels();
    for (int c = numIn; c < getMainBusNumOutputChannels(); ++c)
        buffer.clear (c, 0, numSamples);
    if (numSamples == 0 || ringLength == 0)   // nothing to do / not prepared yet: pass through
        return;

    const int nch = juce::jmin (numIn, preparedChannels, buffer.getNumChannels());

    // 4.0: a new reference or paint from the message thread (wait-free; nothing is allocated).
    if (const auto* r = refToAudio.read())
        for (auto& e : engines)
            e->setReference (*r);
    if (const auto* pc = paintToAudio.read())
        for (auto& e : engines)
            e->setPaint (*pc);

    // Sidechain key (3.0): the second input bus, when the host has it enabled.
    const float* key[2] = { nullptr, nullptr };
    int keyChannels = 0;
    if (auto* scBus = getBus (true, 1); scBus != nullptr && scBus->isEnabled())
    {
        auto sc = getBusBuffer (buffer, true, 1);
        keyChannels = juce::jmin (2, sc.getNumChannels());
        for (int c = 0; c < keyChannels; ++c)
            key[c] = sc.getReadPointer (c);
    }
    sidechainLive.store (keyChannels > 0, std::memory_order_relaxed);

    const int ceilingChoice = juce::jlimit (0, 3, juce::roundToInt (pCeiling->load()));
    limiter.setCeiling (ceilingChoice > 0, anneal::ceilingDbtp[ceilingChoice]);

    pushParametersToEngine();
    mixSmoothed.setTargetValue (pMix->load() * 0.01f);
    outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (pOutput->load()));
    deltaSmoothed.setTargetValue (pDelta->load() > 0.5f ? 1.0f : 0.0f);
    bypassSmoothed.setTargetValue (pBypass->load() > 0.5f ? 1.0f : 0.0f);
    const bool matchOn = pMatch->load() > 0.5f;
    const bool listenOn = pListen->load() > 0.5f;
    listenSmoothed.setTargetValue (listenOn ? 1.0f : 0.0f);
    if (listenOn)
        matchHold = 2 * getEngine().getFftSize();

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
        const float* keyChunk[2] = { key[0] != nullptr ? key[0] + start : nullptr, key[1] != nullptr ? key[1] + start : nullptr };
        getEngine().process (wet, nch, len, keyChannels > 0 ? keyChunk : nullptr, keyChannels);

        // 3. Loudness match (always measured, applied only when enabled). A listen solo is not
        //    the wet signal, so the measurement holds while one is heard.
        float target = match.getGain();
        if (matchHold > 0)
        {
            matchHold = juce::jmax (0, matchHold - len);
        }
        else
        {
            const float* dryPtrs[2] = { dryBlock.getReadPointer (0), dryBlock.getReadPointer (nch > 1 ? 1 : 0) };
            const float* wetPtrs[2] = { wet[0], nch > 1 ? wet[1] : wet[0] };
            target = match.process (dryPtrs, wetPtrs, nch, len);
        }
        matchSmoothed.setTargetValue (matchOn ? target : 1.0f);

        // 4. Mix / delta / listen / output / bypass.
        for (int i = 0; i < len; ++i)
        {
            const float mg = matchSmoothed.getNextValue();
            const float mx = mixSmoothed.getNextValue();
            const float og = outputSmoothed.getNextValue();
            const float dl = deltaSmoothed.getNextValue();
            const float ls = listenSmoothed.getNextValue();
            const float bp = bypassSmoothed.getNextValue();

            for (int c = 0; c < nch; ++c)
            {
                const float dry = dryBlock.getSample (c, i);
                const float diff = mx * (wet[c][i] * mg - dry);
                const float normal = dry + diff;
                const float y = (normal + dl * (diff - normal)) * og;
                // Listen: the engine's solo alone, scaled like the wet signal it is part of.
                const float heard = y + ls * (mx * mg * wet[c][i] * og - y);
                wet[c][i] = heard;
            }
            bypassRamp[(size_t) i] = bp;
        }

        // 5. True-peak ceiling (3.0), then Bypass. The limiter delays by its look-ahead even when
        //    off, and Bypass crossfades to the dry signal delayed by the same amount, so the whole
        //    plug-in always has one constant latency and Bypass stays exactly the dry signal.
        limiter.process (wet, nch, len);
        const int L = bypassRing.getNumSamples();
        int bpAfter = bypassPos;
        for (int c = 0; c < nch; ++c)
        {
            float* ring = bypassRing.getWritePointer (c);
            const float* dry = dryBlock.getReadPointer (c);
            int p = bypassPos;
            for (int i = 0; i < len; ++i)
            {
                const float dryL = ring[p];
                ring[p] = dry[i];
                if (++p == L)
                    p = 0;
                const float out = wet[c][i] + bypassRamp[(size_t) i] * (dryL - wet[c][i]);
                wet[c][i] = out;
                peak[c] = juce::jmax (peak[c], std::abs (out));
            }
            bpAfter = p;
        }
        bypassPos = bpAfter;
    }

    {
        const float gr = limiter.takeGainReductionDb();
        if (gr < limiterReduction.load (std::memory_order_relaxed))
            limiterReduction.store (gr, std::memory_order_relaxed);
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
    beginUndoGroup();   // a preset is one undo step
    anneal::presets::apply (apvts, index);
    endUndoGroup();
}

//==============================================================================
void AnnealAudioProcessor::parameterGestureChanged (int index, bool starting)
{
    // Only gestures made on the message thread (the editor, the host's own controls).
    if (applyingUndo || ! juce::MessageManager::existsAndIsCurrentThread())
        return;
    auto* prm = getParameters()[index];
    if (prm == nullptr)
        return;
    if (starting)
    {
        ++openGestures;
        if (gestureStart.count (index) == 0)
            gestureStart[index] = prm->getValue();
    }
    else
    {
        openGestures = juce::jmax (0, openGestures - 1);
        if (auto it = gestureStart.find (index); it != gestureStart.end())
        {
            if (std::abs (prm->getValue() - it->second) > 1.0e-6f)
            {
                if (pending.before.count (index) == 0)
                    pending.before[index] = it->second;
                pending.after[index] = prm->getValue();
            }
            gestureStart.erase (it);
        }
        if (openGestures == 0 && groupDepth == 0)
            commitPending();
    }
}

void AnnealAudioProcessor::beginUndoGroup()
{
    ++groupDepth;
}

void AnnealAudioProcessor::endUndoGroup()
{
    groupDepth = juce::jmax (0, groupDepth - 1);
    if (groupDepth == 0 && openGestures == 0)
        commitPending();
}

void AnnealAudioProcessor::commitPending()
{
    if (! pending.after.empty())
        pushUndoStep (std::move (pending));
    pending = {};
}

void AnnealAudioProcessor::pushUndoStep (UndoStep step)
{
    lastStepWasStroke = false;
    undoStack.push_back (std::move (step));
    if (undoStack.size() > 200)
        undoStack.erase (undoStack.begin());
    redoStack.clear();
}

void AnnealAudioProcessor::applySnapshot (const Snapshot& values)
{
    const juce::ScopedValueSetter<bool> guard (applyingUndo, true);
    auto& prms = getParameters();
    for (auto [index, value] : values)
        if (auto* prm = prms[index])
        {
            prm->beginChangeGesture();
            prm->setValueNotifyingHost (value);
            prm->endChangeGesture();
        }
}

void AnnealAudioProcessor::undo()
{
    if (undoStack.empty())
        return;
    lastStepWasStroke = false;
    auto step = std::move (undoStack.back());
    undoStack.pop_back();
    applySnapshot (step.before);
    if (step.paintChange)
        applyPaintNow (step.paintBefore);
    redoStack.push_back (std::move (step));
}

void AnnealAudioProcessor::redo()
{
    if (redoStack.empty())
        return;
    lastStepWasStroke = false;
    auto step = std::move (redoStack.back());
    redoStack.pop_back();
    applySnapshot (step.after);
    if (step.paintChange)
        applyPaintNow (step.paintAfter);
    undoStack.push_back (std::move (step));
}

//==============================================================================
// 4.0 Reference match

AnnealAudioProcessor::ReferenceInfo AnnealAudioProcessor::getReference()
{
    pollLearnedReference();
    const juce::ScopedLock sl (refLock);
    return { refCurve, refName };
}

void AnnealAudioProcessor::setReference (const anneal::ReferenceCurve& curve, const juce::String& name, bool holdLearning)
{
    const juce::ScopedLock sl (refLock);
    refCurve = curve;
    refCurve.epoch = ++refEpoch;
    refCurve.holdLearning = holdLearning;
    refName = curve.valid ? name : juce::String();
    refError.clear();
    refToAudio.beginWrite() = refCurve;
    refToAudio.endWrite();
    ++refSerial;
}

void AnnealAudioProcessor::pollLearnedReference()
{
    // A curve learned before the newest reference was handed over is stale (its epoch is older).
    const juce::ScopedLock sl (refLock);
    if (const auto* r = refLearned.read(); r != nullptr && r->epoch == refEpoch)
    {
        refCurve = *r;
        refName = "sidechain key";
        ++refSerial;
    }
}

bool AnnealAudioProcessor::canLoadReferenceFile (const juce::File& file)
{
    return file.existsAsFile() && file.hasFileExtension ("wav;wave;aif;aiff;aifc;flac;mp3;ogg");
}

void AnnealAudioProcessor::loadReferenceFile (const juce::File& file)
{
    {
        const juce::ScopedLock sl (refLock);
        refError.clear();
    }
    loader->load (file);
}

void AnnealAudioProcessor::referenceFailed (const juce::File& file, const juce::String& why)
{
    const juce::ScopedLock sl (refLock);
    refError = file.getFileName() + ": " + why;
}

juce::String AnnealAudioProcessor::getReferenceError()
{
    const juce::ScopedLock sl (refLock);
    return refError;
}

//==============================================================================
// 4.0 Paint

anneal::PaintCurves AnnealAudioProcessor::getPaint()
{
    const juce::ScopedLock sl (refLock);
    return paint;
}

void AnnealAudioProcessor::applyPaintNow (const anneal::PaintCurves& newPaint)
{
    const juce::ScopedLock sl (refLock);
    paint = newPaint;
    paintToAudio.beginWrite() = newPaint;
    paintToAudio.endWrite();
    ++paintSerial;
}

void AnnealAudioProcessor::setPaint (const anneal::PaintCurves& newPaint)
{
    applyPaintNow (newPaint);
}

void AnnealAudioProcessor::beginPaintStroke()
{
    strokeStart = getPaint();
    strokeOpen = true;
}

void AnnealAudioProcessor::endPaintStroke (bool mergeWithPrevious)
{
    if (! strokeOpen)
        return;
    strokeOpen = false;
    const auto now = getPaint();

    // A double-click's reset joins the click just before it, when that click was the last step.
    if (mergeWithPrevious && lastStepWasStroke && ! undoStack.empty() && undoStack.back().paintChange
        && undoStack.back().before.empty() && undoStack.back().paintAfter == strokeStart)
    {
        if (undoStack.back().paintBefore == now)
        {
            undoStack.pop_back();
            lastStepWasStroke = false;
        }
        else
            undoStack.back().paintAfter = now;
        return;
    }
    if (now == strokeStart)
        return;
    UndoStep step;
    step.paintChange = true;
    step.paintBefore = strokeStart;
    step.paintAfter = now;
    pushUndoStep (std::move (step));
    lastStepWasStroke = true;
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
    pollLearnedReference();
    {
        const juce::ScopedLock sl (refLock);
        if (refCurve.valid)
            state.appendChild (referenceToTree (refCurve, refName), nullptr);
        if (! paint.isDefault())
            state.appendChild (paintToTree (paint), nullptr);
    }
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
            auto tree = juce::ValueTree::fromXml (*xml);
            // 4.0: the reference and the paint live in the processor, not in the APVTS tree.
            const auto refTree = tree.getChildWithName (referenceTag);
            const auto paintTree = tree.getChildWithName (paintTag);
            tree.removeChild (refTree, nullptr);
            tree.removeChild (paintTree, nullptr);
            // Check before replaceState: the APVTS fills missing parameters into the same tree.
            const bool hasLatency = tree.getChildWithProperty ("id", anneal::ids::latency).isValid();
            const bool hasGuard = tree.getChildWithProperty ("id", anneal::ids::guard).isValid();
            const bool hasStereo = tree.getChildWithProperty ("id", anneal::ids::stereoMode).isValid();
            const bool hasEqTarget = tree.getChildWithProperty ("id", anneal::ids::eqTarget).isValid();
            const bool hasHeatTarget = tree.getChildWithProperty ("id", anneal::ids::heatTarget).isValid();
            const bool hasListen = tree.getChildWithProperty ("id", anneal::ids::listen).isValid();
            const bool hasFlavour = tree.getChildWithProperty ("id", anneal::ids::flavour).isValid();
            const bool hasFreeze = tree.getChildWithProperty ("id", anneal::ids::freeze).isValid();
            const bool hasScMode = tree.getChildWithProperty ("id", anneal::ids::scMode).isValid();
            const bool hasCeiling = tree.getChildWithProperty ("id", anneal::ids::ceiling).isValid();
            const bool hasRefMode = tree.getChildWithProperty ("id", anneal::ids::refMode).isValid();
            const bool hasRefAmount = tree.getChildWithProperty ("id", anneal::ids::refAmount).isValid();
            const int preset = tree.getProperty ("preset", -1);
            presetChosen = juce::isPositiveAndBelow (preset, anneal::presets::count());
            currentPreset = presetChosen ? preset : 0;
            apvts.replaceState (tree);

            // Projects saved before 0.2.0 have no Latency: they keep the engine they were made
            // with (Precise, 85 ms at 48 kHz), so they sound exactly as before.
            if (! hasLatency)
                if (auto* lp = apvts.getParameter (anneal::ids::latency))
                    lp->setValueNotifyingHost (lp->convertTo0to1 ((float) anneal::SpectralEngine::precise));

            // Projects saved before 0.2.2 have no Guard: off, as it was then (not whatever this
            // instance had), so they sound exactly as before.
            if (! hasGuard)
                if (auto* gp = apvts.getParameter (anneal::ids::guard))
                    gp->setValueNotifyingHost (gp->convertTo0to1 (0.0f));

            // Old sessions always reopen as linked Stereo, even in an instance previously set
            // to M/S. The routing choices are independent of factory tone presets.
            for (auto [id, present] : { std::pair { anneal::ids::stereoMode, hasStereo },
                                       std::pair { anneal::ids::eqTarget, hasEqTarget },
                                       std::pair { anneal::ids::heatTarget, hasHeatTarget } })
                if (! present)
                    if (auto* rp = apvts.getParameter (id))
                        rp->setValueNotifyingHost (0.0f);

            // Projects saved before 0.2.5 have no Listen: they open hearing the full result, not
            // whatever solo this instance was on.
            if (! hasListen)
                if (auto* lp = apvts.getParameter (anneal::ids::listen))
                    lp->setValueNotifyingHost (0.0f);

            // Projects saved before 0.2.6 have no Flavour: Clean, the only saturation they had,
            // so they sound exactly as before (not whatever flavour this instance was on).
            if (! hasFlavour)
                if (auto* fp = apvts.getParameter (anneal::ids::flavour))
                    fp->setValueNotifyingHost (0.0f);

            // Projects saved before 3.0: no Freeze, no sidechain, no ceiling, as they were made.
            for (auto [id, present] : { std::pair { anneal::ids::freeze, hasFreeze },
                                       std::pair { anneal::ids::scMode, hasScMode },
                                       std::pair { anneal::ids::ceiling, hasCeiling } })
                if (! present)
                    if (auto* np = apvts.getParameter (id))
                        np->setValueNotifyingHost (0.0f);

            // Projects saved before 4.0: no reference, Reference Off (the auto EQ as they were made),
            // Amount at its default, and no paint. A project without a saved reference or paint
            // also clears whatever this instance had.
            if (! hasRefMode)
                if (auto* rp = apvts.getParameter (anneal::ids::refMode))
                    rp->setValueNotifyingHost (0.0f);
            if (! hasRefAmount)
                if (auto* ap = apvts.getParameter (anneal::ids::refAmount))
                    ap->setValueNotifyingHost (ap->getDefaultValue());
            {
                juce::String name;
                const auto curve = referenceFromTree (refTree, name);
                setReference (curve, name);
            }
            applyPaintNow (paintFromTree (paintTree));

            // A loaded project starts a fresh undo history.
            undoStack.clear();
            redoStack.clear();
            pending = {};

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
