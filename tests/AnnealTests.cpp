// Headless acceptance tests and benchmark for Anneal.
// Exit code = number of failed checks.

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Params.h"
#include "Presets.h"
#include "dsp/FastMath.h"
#include "dsp/RealFft.h"
#include "dsp/ReferenceAnalyser.h"
#include "dsp/SpectralEngine.h"
#include "dsp/TransientShaper.h"
#include "dsp/TruePeakLimiter.h"
#include "dsp/ErbScale.h"
#include "ui/PerceptionScreen.h"
#include "ui/Theme.h"
#include "TestSignals.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <random>

testsig::Channels pinkNoise (double fs, double seconds, float amp, unsigned seed);   // defined with the 3.0 tests

namespace
{
using testsig::Channels;
using testsig::toDb;
namespace ids = anneal::ids;

int failures = 0, checks = 0;

void check (bool ok, const char* name, const juce::String& detail)
{
    ++checks;
    if (! ok)
        ++failures;
    std::printf ("[%s] %-46s %s\n", ok ? "PASS" : "FAIL", name, detail.toRawUTF8());
    std::fflush (stdout);
}

void info (const juce::String& s)
{
    std::printf ("       %s\n", s.toRawUTF8());
    std::fflush (stdout);
}

void section (const char* s)
{
    std::printf ("\n== %s\n", s);
}

juce::String db (double v, int dec = 1) { return juce::String (v, dec) + " dB"; }

std::function<int()> fixedBlocks (int n) { return [n] { return n; }; }

std::function<int()> randomBlocks (int lo, int hi, unsigned seed)
{
    auto rng = std::make_shared<std::mt19937> (seed);
    return [rng, lo, hi] { return std::uniform_int_distribution<int> (lo, hi) (*rng); };
}

anneal::EngineParams neutral()
{
    anneal::EngineParams p;
    p.reveal = 0.0f; p.smooth = 0.0f; p.tiltDbPerOct = 0.0f; p.heat = 0.0f;
    p.rangeLoHz = 20.0f; p.rangeHiHz = 20000.0f; p.focus = 0.0f;
    return p;
}

Channels runEngine (anneal::SpectralEngine& e, const Channels& in, const std::function<int()>& nextBlock)
{
    Channels out = in;
    const int n = (int) in[0].size();
    for (int pos = 0; pos < n;)
    {
        const int b = std::min (nextBlock(), n - pos);
        float* ptrs[2] = { out[0].data() + pos, out.size() > 1 ? out[1].data() + pos : nullptr };
        e.process (ptrs, (int) out.size(), b);
        pos += b;
    }
    return out;
}

struct Rig
{
    std::unique_ptr<AnnealAudioProcessor> proc = std::make_unique<AnnealAudioProcessor>();
    double fs = 48000.0;
    int block = 512;

    explicit Rig (int channels = 2)
    {
        if (channels == 1)
        {
            juce::AudioProcessor::BusesLayout l;
            l.inputBuses.add (juce::AudioChannelSet::mono());
            l.inputBuses.add (juce::AudioChannelSet::disabled());   // 3.0 sidechain bus
            l.outputBuses.add (juce::AudioChannelSet::mono());
            proc->setBusesLayout (l);
        }
    }

    void set (const char* id, float plain)
    {
        auto* p = proc->apvts.getParameter (id);
        p->setValueNotifyingHost (p->convertTo0to1 (plain));
    }

    void setNeutral()
    {
        set (ids::reveal, 0); set (ids::smooth, 0); set (ids::heat, 0); set (ids::tilt, 0);
        set (ids::rangeLo, 20); set (ids::rangeHi, 20000); set (ids::mix, 100); set (ids::output, 0);
        set (ids::match, 0); set (ids::delta, 0); set (ids::bypass, 0);
    }

    void prepare (double sampleRate, int blockSize)
    {
        fs = sampleRate;
        block = blockSize;
        proc->setRateAndBufferSizeDetails (fs, block);
        proc->prepareToPlay (fs, block);
    }

    Channels run (const Channels& in, const std::function<int()>& nextBlock)
    {
        const int nch = (int) in.size(), n = (int) in[0].size();
        Channels out = in;
        juce::AudioBuffer<float> buf (nch, 8192);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < n;)
        {
            const int b = std::min ({ nextBlock(), n - pos, 8192 });
            buf.setSize (nch, b, false, false, true);
            for (int c = 0; c < nch; ++c)
                buf.copyFrom (c, 0, in[(size_t) c].data() + pos, b);
            proc->processBlock (buf, midi);
            for (int c = 0; c < nch; ++c)
                std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + b, out[(size_t) c].data() + pos);
            pos += b;
        }
        return out;
    }
};

double maxAbs (const Channels& x, int from = 0)
{
    double m = 0.0;
    for (auto& ch : x)
        for (size_t i = (size_t) from; i < ch.size(); ++i)
            m = std::max (m, (double) std::abs (ch[i]));
    return m;
}

bool allFinite (const Channels& x)
{
    for (auto& ch : x)
        for (float v : ch)
            if (! std::isfinite (v))
                return false;
    return true;
}

double delayedError (const Channels& out, const Channels& in, int latency)
{
    double m = 0.0;
    for (size_t c = 0; c < out.size(); ++c)
        for (size_t i = (size_t) latency; i < out[c].size(); ++i)
            m = std::max (m, (double) std::abs (out[c][i] - in[c][i - (size_t) latency]));
    return m;
}

//==============================================================================
void testIdentity()
{
    section ("1. STFT identity (neutral settings), every latency mode");
    const char* modeNames[] = { "Low", "Normal", "Precise" };
    for (int mode = 0; mode < anneal::SpectralEngine::numModes; ++mode)
    {
        double worst = 0.0, worstHead = 0.0;
        juce::String sizes;
        for (double fs : { 44100.0, 48000.0, 96000.0 })
        {
            const std::pair<const char*, std::function<int()>> patterns[] = {
                { "1", fixedBlocks (1) }, { "31", fixedBlocks (31) }, { "64", fixedBlocks (64) }, { "480", fixedBlocks (480) },
                { "512", fixedBlocks (512) }, { "4096", fixedBlocks (4096) }, { "rand 1-2000", randomBlocks (1, 2000, 3) } };

            int latency = 0;
            for (auto& [name, gen] : patterns)
            {
                auto e = std::make_unique<anneal::SpectralEngine>();
                e->prepare (fs, 2, mode);
                e->setParameters (neutral());
                latency = e->getLatencySamples();
                const auto in = testsig::whiteNoise (2, (int) (fs * 0.5) + latency, 0.5f, 11);
                const auto out = runEngine (*e, in, gen);
                worst = std::max (worst, delayedError (out, in, latency));
                double head = 0.0;
                for (auto& ch : out)
                    for (int i = 0; i < latency; ++i)
                        head = std::max (head, (double) std::abs (ch[(size_t) i]));
                worstHead = std::max (worstHead, head);
            }
            sizes << juce::String (fs / 1000.0, 1) << "k:" << latency << " ";
        }
        check (worst < 1.0e-4 && worstHead < 1.0e-4,
               (juce::String ("identity, ") + modeNames[mode] + ", 44.1/48/96 kHz x 7 block patterns").toRawUTF8(),
               "N " + sizes + " max err " + juce::String (worst, 9) + "  pre-latency " + juce::String (worstHead, 9));
    }
}

int peakIndex (const Channels& x)
{
    return (int) (std::max_element (x[0].begin(), x[0].end(), [] (float a, float b) { return std::abs (a) < std::abs (b); }) - x[0].begin());
}

void testLatency()
{
    section ("2. Latency");
    auto in = testsig::silence (2, 20000);
    in[0][1000] = in[1][1000] = 1.0f;

    {
        bool ok = true;
        juce::String log;
        for (int mode = 0; mode < anneal::SpectralEngine::numModes; ++mode)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (48000.0, 2, mode);
            e->setParameters (neutral());
            const auto out = runEngine (*e, in, fixedBlocks (256));
            const int peakAt = peakIndex (out);
            ok = ok && peakAt == 1000 + e->getLatencySamples() && std::abs (out[0][(size_t) peakAt] - 1.0f) < 1.0e-4f;
            log << e->getLatencySamples() << " ";
        }
        check (ok, "engine impulse arrives at reported latency, each mode", "N at 48 kHz: " + log);
    }

    // Processor: new instances run Low; switching modes re-reports the latency and stays aligned.
    Rig rig;
    rig.setNeutral();
    rig.prepare (48000.0, 512);
    juce::String log;
    bool ok = rig.proc->getLatencyMode() == anneal::SpectralEngine::low;
    for (int mode : { 0, 1, 2, 0 })
    {
        rig.set (ids::latency, (float) mode);
        rig.proc->applyLatencyModeIfChanged();
        const auto pout = rig.run (in, fixedBlocks (512));
        const int lat = rig.proc->getLatencySamples();
        const int pPeak = peakIndex (pout);
        ok = ok && lat == anneal::SpectralEngine::fftSizeFor (48000.0, mode) + rig.proc->getCeilingLatency() && pPeak == 1000 + lat;
        log << juce::String (1000.0 * lat / 48000.0, 1) << " ms (impulse +" << (pPeak - 1000) << ")  ";
    }
    check (ok, "processor: Low by default, modes switch and stay aligned", log);

    // A project saved before 0.2.0 has no Latency parameter: it loads as Precise.
    {
        Rig a;
        juce::MemoryBlock blob;
        a.proc->getStateInformation (blob);
        auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
        if (auto* lat = xml->getChildByAttribute ("id", ids::latency))
            xml->removeChildElement (lat, true);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        Rig b;
        b.prepare (48000.0, 512);
        b.proc->setStateInformation (old.getData(), (int) old.getSize());
        b.proc->applyLatencyModeIfChanged();
        check (b.proc->getLatencyMode() == anneal::SpectralEngine::precise && b.proc->getLatencySamples() == 4096 + b.proc->getCeilingLatency(),
               "projects from before 0.2.0 load as Precise (85 ms)", "mode " + juce::String (b.proc->getLatencyMode())
                   + ", latency " + juce::String (b.proc->getLatencySamples()));
    }
}

void testNulls()
{
    section ("3. Dry path nulls");
    const auto in = testsig::musicLike (48000.0, 3.0);

    Rig a;
    a.set (ids::mix, 0.0f);   // other settings at defaults (reveal/smooth/heat active)
    a.prepare (48000.0, 512);
    const auto outA = a.run (in, randomBlocks (1, 1500, 5));
    const double errA = delayedError (outA, in, a.proc->getLatencySamples());
    check (errA == 0.0, "Mix 0 % = bit-exact delayed dry", "max err " + juce::String (errA, 12));

    Rig b;
    b.setNeutral();
    b.set (ids::delta, 1.0f);
    b.prepare (48000.0, 512);
    const auto outB = b.run (in, fixedBlocks (512));
    const double peakB = maxAbs (outB, b.proc->getLatencySamples());
    check (toDb (peakB) < -90.0, "Delta with neutral settings is silent", "peak " + db (toDb (peakB)));
}

double rel (const float* x, int n, double fs, double f, double ref)
{
    return toDb (testsig::toneAmplitude (x, n, f, fs) / ref);
}

void testHarmonics()
{
    section ("4. Spectral harmonics (1 kHz, -18 dBFS, Heat 100 %)");
    const double fs = 48000.0;
    for (float color : { 0.0f, 1.0f })
    {
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2);
        auto p = neutral();
        p.heat = 1.0f;
        p.color = color;
        e->setParameters (p);
        const auto in = testsig::sines (fs, (int) (3 * fs), { { 1000.0, 0.125893 } });
        const auto out = runEngine (*e, in, fixedBlocks (512));
        const float* seg = out[0].data() + (int) (1.5 * fs);
        const int n = (int) fs;
        const double ref = testsig::toneAmplitude (seg, n, 1000.0, fs);
        double h[7] {};
        for (int k = 2; k <= 6; ++k)
            h[k] = rel (seg, n, fs, 1000.0 * k, ref);

        info ("color " + juce::String (color, 0) + ": fundamental " + db (toDb (ref)) + "   2nd " + db (h[2]) + "  3rd " + db (h[3])
              + "  4th " + db (h[4]) + "  5th " + db (h[5]) + "  6th " + db (h[6]));
        if (color == 0.0f)
            check (h[2] > -40.0 && h[3] < -60.0 && h[5] < -60.0, "even colour: 2nd present, odd absent", "2nd " + db (h[2]) + ", 3rd " + db (h[3]));
        else
            check (h[3] > -40.0 && h[2] < -60.0 && h[4] < -60.0, "odd colour: 3rd present, even absent", "3rd " + db (h[3]) + ", 2nd " + db (h[2]));
    }

    {
        // Low latency mode: the shorter frame still writes the harmonic series.
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2, anneal::SpectralEngine::low);
        auto p = neutral();
        p.heat = 1.0f;
        p.color = 0.0f;
        e->setParameters (p);
        const auto in = testsig::sines (fs, (int) (3 * fs), { { 1000.0, 0.125893 } });
        const auto out = runEngine (*e, in, fixedBlocks (512));
        const float* seg = out[0].data() + (int) (1.5 * fs);
        const int n = (int) fs;
        const double ref = testsig::toneAmplitude (seg, n, 1000.0, fs);
        const double h2 = rel (seg, n, fs, 2000.0, ref), h3 = rel (seg, n, fs, 3000.0, ref);
        check (h2 > -40.0 && h3 < -60.0, "Low latency: even colour still writes the 2nd", "2nd " + db (h2) + ", 3rd " + db (h3));
    }
}

void testAliasing()
{
    section ("5. No aliasing (9 kHz, -18 dBFS @ 48 kHz)");
    const double fs = 48000.0;
    const auto in = testsig::sines (fs, (int) (3 * fs), { { 9000.0, 0.125893 } });
    const int n = (int) fs;

    for (float color : { 1.0f, 0.0f })
    {
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2);
        auto p = neutral();
        p.heat = 1.0f;
        p.color = color;
        e->setParameters (p);
        const auto out = runEngine (*e, in, fixedBlocks (512));
        const float* seg = out[0].data() + (int) (1.5 * fs);
        const double ref = testsig::toneAmplitude (seg, n, 9000.0, fs);
        if (color == 1.0f)
        {
            const double a21 = rel (seg, n, fs, 21000.0, ref), a3 = rel (seg, n, fs, 3000.0, ref);
            check (a21 < -90.0 && a3 < -90.0, "odd: no fold-back of 27k/45k", "21 kHz " + db (a21) + ", 3 kHz " + db (a3));
        }
        else
        {
            const double a18 = rel (seg, n, fs, 18000.0, ref), a12 = rel (seg, n, fs, 12000.0, ref);
            check (a18 > -40.0 && a12 < -90.0, "even: 18k written, no 36k fold-back", "18 kHz " + db (a18) + ", 12 kHz " + db (a12));
        }
    }

    // Context only: what a plain tanh waveshaper at 48 kHz would do with the same tone.
    std::vector<float> naive (in[0].size());
    for (size_t i = 0; i < naive.size(); ++i)
        naive[i] = std::tanh (4.0f * in[0][i]) / 4.0f;
    const float* seg = naive.data() + (int) (1.5 * fs);
    const double ref = testsig::toneAmplitude (seg, n, 9000.0, fs);
    info ("reference: naive tanh(4x)/4 @48k aliases 21 kHz " + db (rel (seg, n, fs, 21000.0, ref))
          + ", 3 kHz " + db (rel (seg, n, fs, 3000.0, ref)));
}

void testImd()
{
    section ("6. No intermodulation (1000 + 1300 Hz, -24 dBFS each, Heat 100 %, Color 50 %)");
    const double fs = 48000.0;
    const auto in = testsig::sines (fs, (int) (3 * fs), { { 1000.0, 0.0631 }, { 1300.0, 0.0631 } });
    const int n = (int) fs;

    auto e = std::make_unique<anneal::SpectralEngine>();
    e->prepare (fs, 2);
    auto p = neutral();
    p.heat = 1.0f;
    p.color = 0.5f;
    e->setParameters (p);
    const auto out = runEngine (*e, in, fixedBlocks (512));
    const float* seg = out[0].data() + (int) (1.5 * fs);
    const double ref = testsig::toneAmplitude (seg, n, 1000.0, fs);
    const double d300 = rel (seg, n, fs, 300.0, ref), s2300 = rel (seg, n, fs, 2300.0, ref);
    const double h2000 = rel (seg, n, fs, 2000.0, ref), h2600 = rel (seg, n, fs, 2600.0, ref), h3000 = rel (seg, n, fs, 3000.0, ref);
    info ("harmonics: 2000 " + db (h2000) + ", 2600 " + db (h2600) + ", 3000 " + db (h3000));
    check (d300 < -80.0 && s2300 < -80.0 && h2000 > -45.0 && h2600 > -45.0, "difference/sum tones absent, harmonics present",
           "300 Hz " + db (d300) + ", 2300 Hz " + db (s2300));

    std::vector<float> naive (in[0].size());
    for (size_t i = 0; i < naive.size(); ++i)
        naive[i] = std::tanh (6.0f * in[0][i]) / 6.0f + 0.05f * in[0][i] * in[0][i] * 6.0f;
    const float* ns = naive.data() + (int) (1.5 * fs);
    const double nref = testsig::toneAmplitude (ns, n, 1000.0, fs);
    info ("reference: asymmetric tanh waveshaper gives 300 Hz " + db (rel (ns, n, fs, 300.0, nref))
          + ", 2300 Hz " + db (rel (ns, n, fs, 2300.0, nref)));
}

float bandGain (anneal::SpectralEngine& e, double hz)
{
    const auto& m = e.getModel();
    return m.getBandGainDb()[m.bandForHz (hz)];
}

void testModel()
{
    section ("7. Perceptual model: Smooth / Reveal / Range / Tilt");
    const double fs = 48000.0;
    const int n = (int) (4 * fs);

    {
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2);
        e->setParameters (neutral());
        runEngine (*e, testsig::musicLike (fs, 2.0), fixedBlocks (512));
        float worst = 0.0f;
        for (int b = 0; b < anneal::PerceptualModel::numBands; ++b)
            worst = std::max (worst, std::abs (e->getModel().getBandGainDb()[b]));
        check (worst < 1.0e-6f, "Reveal 0 / Smooth 0 / Tilt 0 => flat curve", "max |gain| " + juce::String (worst, 8) + " dB");
    }

    // Pink noise bed + a sustained ring at 3150 Hz.
    Channels ringy = testsig::silence (2, n);
    {
        testsig::Pink pl (1), pr (2);
        for (int i = 0; i < n; ++i)
        {
            const float ring = 0.05f * (float) std::sin (2.0 * testsig::pi * 3150.0 * i / fs);
            ringy[0][(size_t) i] = 0.03f * pl.next() + ring;
            ringy[1][(size_t) i] = 0.03f * pr.next() + ring;
        }
    }
    {
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2);
        auto p = neutral();
        p.smooth = 1.0f;
        e->setParameters (p);
        runEngine (*e, ringy, fixedBlocks (512));
        const float g3150 = bandGain (*e, 3150.0), g500 = bandGain (*e, 500.0);
        check (g3150 <= -3.0f && std::abs (g500) < 1.0f, "Smooth 100 %: ring at 3150 Hz is cut", "3150 Hz " + db (g3150) + ", 500 Hz " + db (g500));

        {
            // Same in the Low latency mode (N = 1024 at 48 kHz): the coarser frame still finds it.
            auto lowE = std::make_unique<anneal::SpectralEngine>();
            lowE->prepare (fs, 2, anneal::SpectralEngine::low);
            lowE->setParameters (p);
            runEngine (*lowE, ringy, fixedBlocks (512));
            const float l3150 = bandGain (*lowE, 3150.0), l500 = bandGain (*lowE, 500.0);
            check (l3150 <= -3.0f && std::abs (l500) < 1.0f, "Low latency: Smooth still cuts the ring", "3150 Hz " + db (l3150) + ", 500 Hz " + db (l500));
        }

        p.rangeLoHz = 5000.0f;
        auto e2 = std::make_unique<anneal::SpectralEngine>();
        e2->prepare (fs, 2);
        e2->setParameters (p);
        runEngine (*e2, ringy, fixedBlocks (512));
        const float r3150 = bandGain (*e2, 3150.0);
        check (std::abs (r3150) < 0.5f, "Range 5k-20k: 3150 Hz left alone", "3150 Hz " + db (r3150));
    }

    // Pink bed + a dense low-mid build-up (150-350 Hz): the region just above it is in its masking shadow.
    Channels muddy = testsig::silence (2, n);
    {
        testsig::Pink pl (3), pr (4);
        std::mt19937 rng (9);
        std::uniform_real_distribution<double> ph (0.0, 2.0 * testsig::pi);
        std::vector<std::pair<double, double>> partials;
        for (double f = 150.0; f <= 350.0; f += 2.5)
            partials.push_back ({ f, ph (rng) });
        for (int i = 0; i < n; ++i)
        {
            double bump = 0.0;
            for (auto [f, phase] : partials)
                bump += std::sin (2.0 * testsig::pi * f * i / fs + phase);
            muddy[0][(size_t) i] = 0.01f * pl.next() + (float) (0.004 * bump);
            muddy[1][(size_t) i] = 0.01f * pr.next() + (float) (0.004 * bump);
        }
    }
    {
        float g[3] {};
        const double probe = 470.0;
        for (int i = 0; i < 2; ++i)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2);
            auto p = neutral();
            p.reveal = i == 0 ? 0.0f : 1.0f;
            e->setParameters (p);
            runEngine (*e, muddy, fixedBlocks (512));
            g[i] = bandGain (*e, probe);
            if (i == 1)
                info ("Reveal 100 %: prominence at 470 Hz " + db (e->getModel().getBandProminence()[e->getModel().bandForHz (probe)])
                      + ", gain 250 Hz " + db (bandGain (*e, 250.0)) + ", 1 kHz " + db (bandGain (*e, 1000.0)));
        }
        check (g[1] >= 1.0f && std::abs (g[0]) < 1.0e-6f, "Reveal 100 %: shadowed band above build-up lifted", "470 Hz " + db (g[1]) + " (Reveal 0: " + db (g[0]) + ")");
    }

    {
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2);
        auto p = neutral();
        p.tiltDbPerOct = 1.0f;
        e->setParameters (p);
        runEngine (*e, testsig::musicLike (fs, 1.0), fixedBlocks (512));
        const float g4k = bandGain (*e, 4000.0), g250 = bandGain (*e, 250.0);
        check (std::abs (g4k - 2.0f) < 0.3f && std::abs (g250 + 2.0f) < 0.3f, "Tilt +1 dB/oct: +2 dB @4k, -2 dB @250", "4 kHz " + db (g4k, 2) + ", 250 Hz " + db (g250, 2));
    }
}

void testBlockInvariance()
{
    section ("8. Block-size invariance (default settings, Match off)");
    const auto in = testsig::musicLike (48000.0, 3.0);
    Channels ref;
    double worst = 0.0;
    for (auto gen : { fixedBlocks (512), fixedBlocks (64), fixedBlocks (1000), randomBlocks (1, 3000, 21) })
    {
        Rig r;
        r.prepare (48000.0, 512);
        const auto out = r.run (in, gen);
        if (ref.empty())
            ref = out;
        else
            for (size_t c = 0; c < out.size(); ++c)
                for (size_t i = 0; i < out[c].size(); ++i)
                    worst = std::max (worst, (double) std::abs (out[c][i] - ref[c][i]));
    }
    check (worst < 1.0e-6, "same output for 512 / 64 / 1000 / random blocks", "max diff " + juce::String (worst, 10));
}

void testStress()
{
    section ("9. Automation stress");
    const double fs = 48000.0;
    const auto in = testsig::musicLike (fs, 30.0, 3);
    Rig r;
    r.prepare (fs, 2048);

    std::mt19937 rng (77);
    std::uniform_real_distribution<float> u (0.0f, 1.0f);
    const char* all[] = { ids::reveal, ids::smooth, ids::tilt, ids::response, ids::rangeLo, ids::rangeHi, ids::heat,
                          ids::color, ids::focus, ids::mix, ids::output, ids::match, ids::delta, ids::bypass, ids::guard,
                          ids::maxCut, ids::maxBoost, ids::tiltPivot, ids::detail };

    const int n = (int) in[0].size();
    Channels out = in;
    juce::AudioBuffer<float> buf (2, 2048);
    juce::MidiBuffer midi;
    int blocks = 0;
    for (int pos = 0; pos < n; ++blocks)
    {
        for (auto* id : all)
            if (u (rng) < 0.3f)
                r.proc->apvts.getParameter (id)->setValueNotifyingHost (u (rng));
        const int b = std::min (std::uniform_int_distribution<int> (1, 2048) (rng), n - pos);
        buf.setSize (2, b, false, false, true);
        for (int c = 0; c < 2; ++c)
            buf.copyFrom (c, 0, in[(size_t) c].data() + pos, b);
        r.proc->processBlock (buf, midi);
        for (int c = 0; c < 2; ++c)
            std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + b, out[(size_t) c].data() + pos);
        pos += b;
    }
    const double peak = maxAbs (out);
    check (allFinite (out) && peak < 64.0, "30 s, random params every block, random sizes",
           juce::String (blocks) + " blocks, output peak " + db (toDb (peak)) + " (input peak " + db (toDb (maxAbs (in))) + ")");

    // Hot square wave, everything up.
    Rig h;
    h.set (ids::heat, 100); h.set (ids::reveal, 100); h.set (ids::smooth, 100); h.set (ids::output, 18);
    h.prepare (fs, 512);
    auto sq = testsig::silence (2, (int) (2 * fs));
    for (size_t i = 0; i < sq[0].size(); ++i)
        sq[0][i] = sq[1][i] = (std::fmod (i * 100.0 / fs, 1.0) < 0.5) ? 1.0f : -1.0f;
    const auto hout = h.run (sq, fixedBlocks (512));
    check (allFinite (hout), "full-scale square, Heat/Reveal/Smooth 100 %, +18 dB", "output peak " + db (toDb (maxAbs (hout))));
}

void testReprepare()
{
    section ("10. Re-prepare at other rates");
    Rig r;
    bool ok = true;
    juce::String log;
    for (double fs : { 44100.0, 96000.0, 48000.0, 192000.0, 88200.0 })
    {
        r.prepare (fs, 480);
        const auto out = r.run (testsig::musicLike (fs, 1.0), fixedBlocks (480));
        const int expected = anneal::SpectralEngine::fftSizeFor (fs, r.proc->getLatencyMode()) + r.proc->getCeilingLatency();
        ok = ok && allFinite (out) && r.proc->getLatencySamples() == expected;
        log << juce::String (fs / 1000.0, 1) << "k:" << r.proc->getLatencySamples() << "  ";
    }
    check (ok, "latency follows FFT size, output finite", log);
}

void testState()
{
    section ("11. State");
    Rig a;
    const std::pair<const char*, float> values[] = { { ids::reveal, 73.0f }, { ids::smooth, 12.5f }, { ids::tilt, -0.8f },
        { ids::response, 300.0f }, { ids::rangeLo, 85.0f }, { ids::rangeHi, 9000.0f }, { ids::heat, 66.0f }, { ids::color, 80.0f },
        { ids::focus, -45.0f }, { ids::mix, 70.0f }, { ids::output, -3.5f }, { ids::match, 1.0f }, { ids::delta, 1.0f },
        { ids::guard, 55.0f }, { ids::stereoMode, 1.0f }, { ids::eqTarget, 2.0f }, { ids::heatTarget, 1.0f },
        { ids::listen, 2.0f }, { ids::flavour, 3.0f } };
    for (auto [id, v] : values)
        a.set (id, v);
    juce::MemoryBlock blob;
    a.proc->getStateInformation (blob);

    Rig b;
    b.proc->setStateInformation (blob.getData(), (int) blob.getSize());
    float worst = 0.0f;
    for (auto [id, v] : values)
    {
        auto* p = b.proc->apvts.getParameter (id);
        worst = std::max (worst, std::abs (p->convertFrom0to1 (p->getValue()) - v));
    }
    const auto savedXml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    const bool hasVersion = savedXml != nullptr && (int) savedXml->getIntAttribute ("stateVersion") == ids::stateVersion;
    check (worst < 0.05f && hasVersion, "round trip restores all parameters", "max diff " + juce::String (worst, 5) + (hasVersion ? ", stateVersion present" : ", NO version"));

    Rig c;
    c.set (ids::heat, 42.0f);
    std::vector<char> junk (300);
    std::mt19937 rng (5);
    for (auto& j : junk)
        j = (char) std::uniform_int_distribution<int> (0, 255) (rng);
    c.proc->setStateInformation (junk.data(), (int) junk.size());
    const char wrongXml[] = "<SomethingElse heat=\"1\"/>";
    juce::MemoryBlock wrong;
    juce::AudioProcessor::copyXmlToBinary (*juce::parseXML (wrongXml), wrong);
    c.proc->setStateInformation (wrong.getData(), (int) wrong.getSize());
    c.proc->setStateInformation (nullptr, 0);
    auto* hp = c.proc->apvts.getParameter (ids::heat);
    check (std::abs (hp->convertFrom0to1 (hp->getValue()) - 42.0f) < 0.05f, "malformed / foreign / empty state ignored", "heat still " + juce::String (hp->convertFrom0to1 (hp->getValue()), 1));
}

void testBypassAndMono()
{
    section ("12. Bypass and mono");
    const auto in = testsig::musicLike (48000.0, 2.0);
    Rig r;
    r.set (ids::heat, 100); r.set (ids::bypass, 1);
    r.prepare (48000.0, 512);
    const auto out = r.run (in, fixedBlocks (512));
    const double err = delayedError (out, in, r.proc->getLatencySamples());
    check (err < 1.0e-6, "bypass = latency-aligned dry", "max err " + juce::String (err, 9));

    // Hosts call reset() when they stop processing (JUCE's VST3 wrapper: setProcessing (false)).
    // Afterwards nothing from before may come out: silence in, silence out.
    {
        Rig z;
        z.prepare (48000.0, 512);
        z.run (in, fixedBlocks (512));
        z.proc->reset();
        const auto after = z.run (testsig::silence (2, 16384), fixedBlocks (512));
        const double leak = maxAbs (after);
        check (leak < 1.0e-6, "reset(): no old audio after the host restarts", "peak after reset " + db (toDb (leak)));
    }

    Rig m (1);
    m.prepare (48000.0, 512);
    Channels mono { in[0] };
    const auto mout = m.run (mono, fixedBlocks (512));
    check (m.proc->getTotalNumInputChannels() == 1 && allFinite (mout) && maxAbs (mout, 4096) > 0.01,
           "mono in/out layout processes", "channels " + juce::String (m.proc->getTotalNumInputChannels()) + ", peak " + db (toDb (maxAbs (mout))));
}

void testPreEcho()
{
    section ("13. Pre-echo of written harmonics (80 Hz hit after silence, Heat 100 %)");
    const double fs = 48000.0;
    const int n = (int) (2 * fs), t0 = (int) fs;
    auto in = testsig::silence (2, n);
    for (int i = t0; i < n; ++i)
    {
        const double t = (i - t0) / fs;
        const float v = (float) (0.5 * std::sin (2.0 * testsig::pi * 80.0 * t) * std::exp (-t * 12.0) + (i - t0 < 48 ? 0.4 : 0.0));
        in[0][(size_t) i] = in[1][(size_t) i] = v;
    }
    Channels outs[2];
    for (int k = 0; k < 2; ++k)
    {
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2);
        auto p = neutral();
        p.heat = k == 0 ? 0.0f : 1.0f;
        e->setParameters (p);
        outs[k] = runEngine (*e, in, fixedBlocks (512));
    }
    const int L = 4096, w = (int) (0.040 * fs);
    double before = 0.0, after = 0.0;
    for (int i = 0; i < w; ++i)
    {
        const double db_ = outs[1][0][(size_t) (t0 + L - w + i)] - outs[0][0][(size_t) (t0 + L - w + i)];
        const double da = outs[1][0][(size_t) (t0 + L + i)] - outs[0][0][(size_t) (t0 + L + i)];
        before += db_ * db_;
        after += da * da;
    }
    const double ratio = 10.0 * std::log10 (std::max (before, 1e-30) / std::max (after, 1e-30));
    check (ratio < -30.0, "harmonic pre-echo >= 30 dB below post-onset", "40 ms before vs 40 ms after onset: " + db (ratio));
}

void testPresets()
{
    section ("14. Factory presets");
    namespace presets = anneal::presets;
    auto plain = [] (Rig& r, const char* id) { auto* p = r.proc->apvts.getParameter (id); return p->convertFrom0to1 (p->getValue()); };

    {
        Rig r;
        juce::StringArray names;
        for (int i = 0; i < r.proc->getNumPrograms(); ++i)
            names.addIfNotAlreadyThere (r.proc->getProgramName (i));
        check (r.proc->getNumPrograms() == presets::count() && presets::count() == 8 && names.size() == 8
                   && r.proc->getPresetDisplayName() == "Default" && r.proc->getCurrentProgram() == 0,
               "8 named presets, a fresh instance shows Default", names.joinIntoString (", "));
    }

    {
        // Every preset lands exactly, keeps Match/Delta/Bypass, and processes music sensibly.
        bool ok = true;
        juce::String log;
        const auto in = testsig::musicLike (48000.0, 3.0);
        for (int i = 0; i < presets::count(); ++i)
        {
            Rig r;
            r.set (ids::match, 1);
            r.proc->setCurrentProgram (i);
            const auto& pr = presets::get (i);
            float worst = 0.0f;
            for (int k = 0; k < presets::numValues; ++k)
                worst = std::max (worst, std::abs (plain (r, presets::paramIds[(size_t) k]) - pr.values[(size_t) k]));
            r.prepare (48000.0, 512);
            const auto out = r.run (in, fixedBlocks (512));
            const double peak = toDb (maxAbs (out, 4096));
            const bool fine = worst < 0.06f && plain (r, ids::match) > 0.5f && r.proc->getPresetDisplayName() == pr.name
                              && allFinite (out) && peak < toDb (maxAbs (in)) + 9.0 && peak > -40.0;
            ok = ok && fine;
            log << pr.name << (fine ? "" : " FAILED") << " " << juce::String (peak, 1) << " dB;  ";
        }
        check (ok, "each preset sets its values and plays", log);
    }

    {
        // Edit after choosing -> "Name *"; save and load: same knobs, same preset number, so a host
        // restoring the program after the state changes nothing.
        Rig a;
        a.proc->setCurrentProgram (4);
        a.set (ids::reveal, 22.0f);
        const auto edited = a.proc->getPresetDisplayName();
        juce::MemoryBlock blob;
        a.proc->getStateInformation (blob);

        Rig b;
        b.proc->setStateInformation (blob.getData(), (int) blob.getSize());
        if (b.proc->getCurrentProgram() != 4)          // what JUCE's VST3 wrapper does on a program change
            b.proc->setCurrentProgram (4);
        check (edited == juce::String (presets::get (4).name) + " *" && b.proc->getCurrentProgram() == 4
                   && std::abs (plain (b, ids::reveal) - 22.0f) < 0.05f && b.proc->getPresetDisplayName() == edited,
               "edited preset: shown with *, survives save/load", edited + " -> reveal " + juce::String (plain (b, ids::reveal), 1) + " %");
    }

    {
        // A project saved before presets existed: no "preset" in the state, knobs untouched.
        Rig a;
        a.set (ids::heat, 77.0f);
        juce::MemoryBlock blob;
        a.proc->getStateInformation (blob);
        auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
        const bool noPresetSaved = xml != nullptr && ! xml->hasAttribute ("preset");
        Rig b;
        b.proc->setCurrentProgram (5);
        b.proc->setStateInformation (blob.getData(), (int) blob.getSize());
        check (noPresetSaved && b.proc->getPresetDisplayName() == "Custom" && b.proc->getCurrentProgram() == 0
                   && std::abs (plain (b, ids::heat) - 77.0f) < 0.05f,
               "older projects load as Custom, knobs kept", "shows " + b.proc->getPresetDisplayName());
    }
}

// Plucks over a quiet pink bed: a tone with a hard attack and an 80 ms decay every 400 ms.
Channels pluckTrain (double fs, double seconds, double toneHz, std::vector<int>& onsets)
{
    const int n = (int) (fs * seconds);
    Channels x = testsig::silence (2, n);
    testsig::Pink pl (5), pr (6);
    for (int i = 0; i < n; ++i)
    {
        x[0][(size_t) i] = 0.01f * pl.next();
        x[1][(size_t) i] = 0.01f * pr.next();
    }
    const int period = (int) (0.4 * fs);
    for (int t0 = (int) (0.5 * fs); t0 + period <= n; t0 += period)
    {
        onsets.push_back (t0);
        for (int i = 0; i < period; ++i)
        {
            const double t = i / fs;
            const float v = (float) (0.3 * std::sin (2.0 * testsig::pi * toneHz * t) * std::exp (-t / 0.08));
            x[0][(size_t) (t0 + i)] += v;
            x[1][(size_t) (t0 + i)] += v;
        }
    }
    return x;
}

// Mean level (dB) of `hz` in out[t0 + latency + from, + len) relative to in[t0 + from, + len), over
// every onset after the first two (the model has settled by then).
double windowGainDb (const Channels& out, const Channels& in, const std::vector<int>& onsets, int latency,
                     int from, int len, double hz, double fs)
{
    double sum = 0.0;
    int count = 0;
    for (size_t k = 2; k < onsets.size(); ++k)
    {
        const int t = onsets[k] + from;
        const double a = testsig::toneAmplitude (out[0].data() + t + latency, len, hz, fs);
        const double b = testsig::toneAmplitude (in[0].data() + t, len, hz, fs);
        sum += toDb (a / b);
        ++count;
    }
    return count > 0 ? sum / count : 0.0;
}

void testGuard()
{
    section ("15. Transient guard");
    const double fs = 48000.0;
    const char* modeNames[] = { "Low", "Normal", "Precise" };
    std::vector<int> onsets;
    const auto plucks = pluckTrain (fs, 4.0, 2000.0, onsets);
    const int attackLen = (int) (0.015 * fs);

    // Attacks: Smooth at 100 % and a fast Response cut into each pluck's attack. Guard 100 % lets
    // the attack through (within 1 dB of the input) and cuts clearly less than Guard off.
    for (int mode : { (int) anneal::SpectralEngine::low, (int) anneal::SpectralEngine::precise })
    {
        double att[2] {}, body[2] {};
        float peakT = 0.0f;
        for (int g = 0; g < 2; ++g)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2, mode);
            auto p = neutral();
            p.smooth = 1.0f;
            p.responseMs = 40.0f;
            p.guard = (float) g;
            e->setParameters (p);
            const auto out = runEngine (*e, plucks, fixedBlocks (256));
            const int L = e->getLatencySamples();
            att[g] = windowGainDb (out, plucks, onsets, L, 0, attackLen, 2000.0, fs);
            body[g] = windowGainDb (out, plucks, onsets, L, (int) (0.08 * fs), (int) (0.08 * fs), 2000.0, fs);
            peakT = std::max (peakT, e->getModel().getMaxTransient());
        }
        info (juce::String (modeNames[mode]) + ": attack (first 15 ms) Guard off " + db (att[0], 2) + ", Guard 100 % " + db (att[1], 2)
              + ";  body (80-160 ms) off " + db (body[0], 2) + ", 100 % " + db (body[1], 2));
        check (std::abs (att[1]) < 1.0 && att[1] - att[0] >= 1.5,
               (juce::String ("Guard 100 %: attacks pass Smooth, ") + modeNames[mode]).toRawUTF8(),
               "attack " + db (att[0], 2) + " -> " + db (att[1], 2));
    }

    // Steady material is not affected: the sustained 3150 Hz ring in pink noise is cut as before.
    {
        Channels ringy = testsig::silence (2, (int) (4 * fs));
        testsig::Pink pl (1), pr (2);
        for (size_t i = 0; i < ringy[0].size(); ++i)
        {
            const float ring = 0.05f * (float) std::sin (2.0 * testsig::pi * 3150.0 * (double) i / fs);
            ringy[0][i] = 0.03f * pl.next() + ring;
            ringy[1][i] = 0.03f * pr.next() + ring;
        }
        juce::String log;
        bool ok = true;
        for (int mode : { (int) anneal::SpectralEngine::low, (int) anneal::SpectralEngine::precise })
        {
            float cut[2] {};
            for (int g = 0; g < 2; ++g)
            {
                auto e = std::make_unique<anneal::SpectralEngine>();
                e->prepare (fs, 2, mode);
                auto p = neutral();
                p.smooth = 1.0f;
                p.guard = (float) g;
                e->setParameters (p);
                runEngine (*e, ringy, fixedBlocks (512));
                cut[g] = bandGain (*e, 3150.0);
            }
            ok = ok && cut[0] <= -3.0f && std::abs (cut[1] - cut[0]) < 0.5f;
            log << modeNames[mode] << " " << db (cut[0], 2) << " / " << db (cut[1], 2) << "  ";
        }
        check (ok, "Guard 100 %: sustained ring still cut the same", "3150 Hz, Guard off / 100 %: " + log);
    }

    // Heat: no harmonics smeared over the attack. The 2nd harmonic of the pluck in its first 15 ms
    // drops clearly with Guard; a steady tone keeps its harmonics.
    {
        double h2[2] {}, steady[2] {};
        for (int g = 0; g < 2; ++g)
        {
            auto p = neutral();
            p.heat = 1.0f;
            p.color = 0.0f;
            p.guard = (float) g;

            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2, anneal::SpectralEngine::low);
            e->setParameters (p);
            const auto out = runEngine (*e, plucks, fixedBlocks (256));
            const int L = e->getLatencySamples();
            double sum = 0.0;
            int count = 0;
            for (size_t k = 2; k < onsets.size(); ++k)
            {
                const float* seg = out[0].data() + onsets[k] + L;
                sum += toDb (testsig::toneAmplitude (seg, attackLen, 4000.0, fs) / testsig::toneAmplitude (seg, attackLen, 2000.0, fs));
                ++count;
            }
            h2[g] = sum / count;

            auto s = std::make_unique<anneal::SpectralEngine>();
            s->prepare (fs, 2, anneal::SpectralEngine::low);
            s->setParameters (p);
            const auto sout = runEngine (*s, testsig::sines (fs, (int) (3 * fs), { { 1000.0, 0.125893 } }), fixedBlocks (512));
            const float* seg = sout[0].data() + (int) (1.5 * fs);
            steady[g] = rel (seg, (int) fs, fs, 2000.0, testsig::toneAmplitude (seg, (int) fs, 1000.0, fs));
        }
        check (h2[1] - h2[0] <= -6.0 && std::abs (steady[1] - steady[0]) < 0.5,
               "Guard 100 %: no harmonics over attacks, steady kept",
               "2nd in the attack " + db (h2[0]) + " -> " + db (h2[1]) + ";  steady 1 kHz 2nd " + db (steady[0]) + " -> " + db (steady[1]));
    }

    // Old projects (no Guard in the state) load with Guard off, whatever the instance had.
    {
        Rig a;
        juce::MemoryBlock blob;
        a.proc->getStateInformation (blob);
        auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
        if (auto* gx = xml->getChildByAttribute ("id", ids::guard))
            xml->removeChildElement (gx, true);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        Rig b;
        b.set (ids::guard, 70.0f);
        b.proc->setStateInformation (old.getData(), (int) old.getSize());
        auto* gp = b.proc->apvts.getParameter (ids::guard);
        const float loaded = gp->convertFrom0to1 (gp->getValue());
        const float parsed = gp->convertFrom0to1 (gp->getValueForText ("35 %"));
        check (loaded == 0.0f && gp->getDefaultValue() == 0.0f && gp->getCurrentValueAsText() == "Off"
                   && gp->getValueForText ("off") == 0.0f && std::abs (parsed - 35.0f) < 0.05f,
               "projects from before 0.2.2 load with Guard off", "Guard " + gp->getCurrentValueAsText()
                   + "; typing 'off' -> " + juce::String (gp->getValueForText ("off"), 1) + ", '35 %' -> " + juce::String (parsed, 1));
    }
}

void testDisplayRange()
{
    section ("16. The display shows the range the engine uses");
    // Automation can put Range Low above Range High; the engine then works on Low .. 1.5 x Low.
    Rig r;
    r.set (ids::rangeLo, 1800.0f);
    r.set (ids::rangeHi, 1000.0f);
    r.prepare (48000.0, 512);
    r.run (testsig::musicLike (48000.0, 1.0), fixedBlocks (512));
    const auto* frame = r.proc->getDisplay().read();
    anneal::PerceptionScreen screen (r.proc->getDisplay(), r.proc->apvts);
    const auto shown = screen.getShownRange();
    const bool ok = frame != nullptr && std::abs (shown.x - frame->rangeLoHz) < 1.0f
                    && std::abs (shown.y - frame->rangeHiHz) < 0.01f * frame->rangeHiHz;
    check (ok, "crossed Range Low/High: display = engine",
           "engine " + juce::String (frame != nullptr ? frame->rangeLoHz : 0.0f, 0) + "-" + juce::String (frame != nullptr ? frame->rangeHiHz : 0.0f, 0)
               + " Hz, display " + juce::String (shown.x, 0) + "-" + juce::String (shown.y, 0) + " Hz");
}

void testRangeHandleDrag()
{
    section ("17. Range handles after crossed automation");
    Rig r;
    r.set (ids::rangeLo, 1800.0f);
    r.set (ids::rangeHi, 1000.0f);
    anneal::PerceptionScreen screen (r.proc->getDisplay(), r.proc->apvts);
    screen.setSize (800, 350);

    auto pointAt = [&screen] (float hz)
    {
        const double lo = anneal::erb::hzToCam (20.0), hi = anneal::erb::hzToCam (20000.0);
        return juce::Point<float> { 18.0f + (float) ((anneal::erb::hzToCam (hz) - lo) / (hi - lo))
                                                   * (screen.getWidth() - 64.0f), 50.0f };
    };
    auto eventAt = [&screen] (juce::Point<float> point, juce::Point<float> down, bool dragged)
    {
        const auto now = juce::Time::getCurrentTime();
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), point,
                                juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &screen, &screen,
                                now, down, now, 1, dragged);
    };
    auto shownAs = [&screen] (float lo, float hi)
    {
        const auto shown = screen.getShownRange();
        return std::abs (shown.x - lo) < 1.0f && std::abs (shown.y - hi) < 1.0f;
    };

    const auto lowDown = pointAt (1800.0f);
    screen.mouseDown (eventAt (lowDown, lowDown, false));
    check (shownAs (1800.0f, 2700.0f) && r.proc->apvts.getRawParameterValue (ids::rangeHi)->load() == 1000.0f,
           "crossed range: clicking leaves values intact", "no drag yet");
    screen.mouseDrag (eventAt (lowDown, lowDown, true));
    check (shownAs (1800.0f, 2700.0f), "crossed range: low handle starts without a jump",
           "shown low " + juce::String (screen.getShownRange().x, 1) + " Hz");
    screen.mouseDrag (eventAt (pointAt (1500.0f), lowDown, true));
    check (shownAs (1500.0f, 2700.0f), "crossed range: dragging low preserves shown high",
           "shown " + juce::String (screen.getShownRange().x, 1) + "-"
               + juce::String (screen.getShownRange().y, 1) + " Hz");
    screen.mouseUp (eventAt (pointAt (1500.0f), lowDown, true));

    r.set (ids::rangeLo, 1800.0f);
    r.set (ids::rangeHi, 1000.0f);
    const auto highDown = pointAt (2700.0f);
    screen.mouseDown (eventAt (highDown, highDown, false));
    screen.mouseDrag (eventAt (pointAt (3200.0f), highDown, true));
    screen.mouseUp (eventAt (pointAt (3200.0f), highDown, true));
    check (shownAs (1800.0f, 3200.0f), "crossed range: high handle follows the pointer", "low stays at 1800 Hz");

    r.set (ids::rangeLo, 1200.0f);
    r.set (ids::rangeHi, 18000.0f);
    const auto normalDown = pointAt (1200.0f);
    screen.mouseDown (eventAt (normalDown, normalDown, false));
    screen.mouseDrag (eventAt (pointAt (1300.0f), normalDown, true));
    screen.mouseUp (eventAt (pointAt (1300.0f), normalDown, true));
    check (shownAs (1300.0f, 18000.0f), "ordered range: low drag leaves high unchanged", "high stays at 18000 Hz");

    // Double-clicking a handle resets it in one host gesture. JUCE delivers down, up, down,
    // double-click, up: the second press has already opened the handle's gesture, so the reset
    // must not open another inside it (a host recording touch automation would see begin, begin,
    // end, end for the same parameter).
    struct GestureLog final : juce::AudioProcessorParameter::Listener
    {
        int depth = 0, deepest = 0, begins = 0, ends = 0;
        void parameterValueChanged (int, float) override {}
        void parameterGestureChanged (int, bool starting) override
        {
            depth += starting ? 1 : -1;
            deepest = std::max (deepest, depth);
            ++(starting ? begins : ends);
        }
    };
    for (const char* id : { ids::rangeLo, ids::rangeHi })
    {
        r.set (ids::rangeLo, 300.0f);
        r.set (ids::rangeHi, 9000.0f);
        auto* param = r.proc->apvts.getParameter (id);
        GestureLog log;
        param->addListener (&log);
        const auto at = pointAt (id == ids::rangeLo ? 300.0f : 9000.0f);
        screen.mouseDown (eventAt (at, at, false));
        screen.mouseUp (eventAt (at, at, false));
        screen.mouseDown (eventAt (at, at, false));
        screen.mouseDoubleClick (eventAt (at, at, false));
        screen.mouseUp (eventAt (at, at, false));
        param->removeListener (&log);
        const float now = param->convertFrom0to1 (param->getValue());
        const float def = param->convertFrom0to1 (param->getDefaultValue());
        check (log.deepest == 1 && log.begins == log.ends && std::abs (now - def) < 0.5f,
               id == ids::rangeLo ? "double-click resets Range Low in one gesture" : "double-click resets Range High in one gesture",
               juce::String (log.begins) + " begins / " + juce::String (log.ends) + " ends, nested " + juce::String (log.deepest)
                   + " deep; now " + anneal::formatHz (now));
    }
}

void testMidSide()
{
    section ("Mid/Side routing, independent analysis and legacy sessions");
    const double fs = 48000.0;
    const int n = (int) (3 * fs);
    auto input = testsig::silence (2, n);
    for (int i = 0; i < n; ++i)
    {
        const float mid = 0.12f * (float) std::sin (2.0 * testsig::pi * 500.0 * i / fs);
        const float side = 0.09f * (float) std::sin (2.0 * testsig::pi * 3000.0 * i / fs);
        input[0][(size_t) i] = mid + side;
        input[1][(size_t) i] = mid - side;
    }
    auto decode = [] (const Channels& lr, int c)
    {
        std::vector<float> result (lr[0].size());
        for (size_t i = 0; i < result.size(); ++i)
            result[i] = 0.5f * (lr[0][i] + (c == 0 ? lr[1][i] : -lr[1][i]));
        return result;
    };

    // The neutral transform must null even with impulses/noise, at every latency and rate.
    for (int mode = 0; mode < anneal::SpectralEngine::numModes; ++mode)
    {
        double worst = 0.0;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            anneal::SpectralEngine e;
            e.prepare (rate, 2, mode);
            auto p = neutral(); p.midSide = 1;
            e.setParameters (p);
            const auto x = testsig::musicLike (rate, 0.8);
            const auto y = runEngine (e, x, randomBlocks (1, 1000, 8));
            worst = std::max (worst, delayedError (y, x, e.getLatencySamples()));
        }
        check (worst < 1.0e-6, "neutral M/S nulls at 44.1/48/96k", "mode " + juce::String (mode) + ", max error " + juce::String (worst, 9));
    }

    // Targeting EQ must leave the other component untouched, including its transient audio.
    for (int target = 1; target <= 2; ++target)
    {
        Rig r; r.setNeutral(); r.set (ids::stereoMode, 1); r.set (ids::eqTarget, (float) target);
        r.set (ids::tilt, 1); r.prepare (fs, 512);
        const auto y = r.run (input, fixedBlocks (512));
        const auto mid = decode (y, 0), side = decode (y, 1);
        const int from = n - (int) fs;
        const double midDb = toDb (testsig::toneAmplitude (mid.data() + from, (int) fs, 500, fs) / 0.12);
        const double sideDb = toDb (testsig::toneAmplitude (side.data() + from, (int) fs, 3000, fs) / 0.09);
        const bool ok = target == 1 ? midDb < -0.9 && midDb > -1.1 && std::abs (sideDb) < 0.05
                                   : std::abs (midDb) < 0.05 && sideDb > 1.4 && sideDb < 1.8;
        check (ok, target == 1 ? "EQ Mid leaves Side untouched" : "EQ Side applies Tilt only to Side",
               "Mid " + db (midDb, 3) + ", Side " + db (sideDb, 3));
    }

    // Independent centre/sides models must agree with two separately analysed mono streams.
    {
        const auto x = testsig::musicLike (fs, 2.0, 11);
        auto mid = decode (x, 0), side = decode (x, 1);
        anneal::SpectralEngine stereo, mono[2];
        stereo.prepare (fs, 2); auto p = neutral(); p.reveal = 0.8f; p.smooth = 0.7f; p.midSide = 1;
        stereo.setParameters (p);
        const auto actual = runEngine (stereo, x, fixedBlocks (256));
        p.midSide = 0;
        Channels expected = x;
        for (int c = 0; c < 2; ++c)
        {
            mono[c].prepare (fs, 1); mono[c].setParameters (p);
            const auto result = runEngine (mono[c], Channels { c == 0 ? mid : side }, fixedBlocks (256));
            for (size_t i = 0; i < result[0].size(); ++i)
                if (c == 0) expected[0][i] = expected[1][i] = result[0][i];
                else { expected[0][i] += result[0][i]; expected[1][i] -= result[0][i]; }
        }
        double err = 0;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < actual[c].size(); ++i) err = std::max (err, (double) std::abs (actual[c][i] - expected[c][i]));
        check (err < 1.0e-6, "M/S EQ equals independent mono models", "max error " + juce::String (err, 9));
    }

    for (int target = 1; target <= 2; ++target)
    {
        Rig r; r.setNeutral(); r.set (ids::stereoMode, 1); r.set (ids::heatTarget, (float) target);
        r.set (ids::heat, 100); r.set (ids::color, 0); r.prepare (fs, 512);
        const auto y = r.run (input, randomBlocks (1, 1024, 5));
        const auto mid = decode (y, 0), side = decode (y, 1);
        const int from = n - (int) fs;
        const double midH = toDb (testsig::toneAmplitude (mid.data() + from, (int) fs, 1000, fs) / 0.12);
        const double sideH = toDb (testsig::toneAmplitude (side.data() + from, (int) fs, 6000, fs) / 0.09);
        check (target == 1 ? midH > -50 && sideH < -90 : sideH > -50 && midH < -90,
               target == 1 ? "Heat Mid adds no Side harmonics" : "Heat Side adds no Mid harmonics",
               "Mid H2 " + db (midH) + ", Side H2 " + db (sideH));
    }

    // M/S on a mono bus is explicitly a linked mono fallback; Side targets cannot mute it.
    {
        Rig a (1), b (1); a.setNeutral(); b.setNeutral();
        a.set (ids::heat, 60); b.set (ids::heat, 60);
        b.set (ids::stereoMode, 1); b.set (ids::eqTarget, 2); b.set (ids::heatTarget, 2);
        a.prepare (fs, 512); b.prepare (fs, 512);
        const Channels mono { input[0] };
        const auto x = a.run (mono, fixedBlocks (512)), y = b.run (mono, fixedBlocks (512));
        double err = 0;
        for (size_t i = 0; i < x[0].size(); ++i) err = std::max (err, (double) std::abs (x[0][i] - y[0][i]));
        check (err == 0, "mono M/S falls back to linked processing", "max error " + juce::String (err, 9));
    }

    {
        Rig r; r.set (ids::stereoMode, 1); r.set (ids::eqTarget, 2); r.set (ids::heatTarget, 1);
        auto old = r.proc->apvts.copyState();
        for (auto id : { ids::stereoMode, ids::eqTarget, ids::heatTarget })
            old.removeChild (old.getChildWithProperty ("id", id), nullptr);
        juce::MemoryBlock state; juce::AudioProcessor::copyXmlToBinary (*old.createXml(), state);
        r.proc->setStateInformation (state.getData(), (int) state.getSize());
        bool defaults = true;
        for (auto id : { ids::stereoMode, ids::eqTarget, ids::heatTarget })
            defaults = defaults && r.proc->apvts.getRawParameterValue (id)->load() == 0;
        check (defaults, "old projects restore linked Stereo and Both", "routing reset from a previously configured M/S instance");
        r.set (ids::stereoMode, 1); r.set (ids::eqTarget, 2); r.set (ids::heatTarget, 1);
        r.proc->setCurrentProgram (4);
        check (r.proc->apvts.getRawParameterValue (ids::stereoMode)->load() == 1
               && r.proc->apvts.getRawParameterValue (ids::eqTarget)->load() == 2
               && r.proc->apvts.getRawParameterValue (ids::heatTarget)->load() == 1,
               "factory presets preserve M/S routing", "Mid/Side, EQ Side, Heat Mid");
    }

    {
        Rig r; r.prepare (fs, 512);
        int block = 0;
        const auto y = r.run (input, [&]
        {
            r.set (ids::stereoMode, (float) ((block / 8) % 2));
            r.set (ids::eqTarget, (float) ((block / 11) % 3));
            r.set (ids::heatTarget, (float) ((block++ / 13) % 3));
            return 257;
        });
        check (allFinite (y) && maxAbs (y) < 1 && r.proc->getLatencySamples() == 1024 + r.proc->getCeilingLatency(),
               "routing automation remains finite, same latency", "peak " + db (toDb (maxAbs (y))));
    }
}

void testListen()
{
    section ("Listen modes: Reveal only / Smooth only / Heat only");
    const double fs = 48000.0;
    const auto music = testsig::musicLike (fs, 3.0, 7);

    // Listen choices: 0 Off, 1 Reveal only, 2 Smooth only, 3 Heat only. One processor per mode, same settings and input (the engine is deterministic).
    auto runModes = [fs] (const std::function<void (Rig&)>& configure, std::initializer_list<int> modes,
                          const Channels& in, int& latency)
    {
        std::vector<Channels> outs;
        for (int m : modes)
        {
            Rig r;
            r.setNeutral();
            configure (r);
            r.set (ids::listen, (float) m);
            r.prepare (fs, 512);
            outs.push_back (r.run (in, fixedBlocks (512)));
            latency = r.proc->getLatencySamples();
        }
        return outs;
    };
    // max | normal - revealSolo + smoothSolo - delayed dry |: zero by construction when Tilt is 0
    // (normal = X*A, Reveal solo = X*max(A-1,0), Smooth solo = X*max(1-A,0)).
    auto eqIdentity = [] (const std::vector<Channels>& o, const Channels& in, int latency)
    {
        double m = 0.0;
        for (size_t c = 0; c < in.size(); ++c)
            for (size_t i = (size_t) latency; i < in[c].size(); ++i)
                m = std::max (m, (double) std::abs (o[0][c][i] - o[1][c][i] + o[2][c][i] - in[c][i - (size_t) latency]));
        return m;
    };
    auto level = [] (const Channels& x, int from) { return toDb (maxAbs (x, from)); };

    // Reveal and Smooth solos split the auto EQ exactly: in Stereo (Low and Precise) and Mid/Side.
    for (auto [lm, ms] : { std::pair { 0, 0 }, std::pair { 2, 0 }, std::pair { 0, 1 } })
    {
        const int latencyMode = lm, midSide = ms;
        int latency = 0;
        const auto o = runModes ([=] (Rig& r)
        {
            r.set (ids::reveal, 100); r.set (ids::smooth, 100);
            r.set (ids::latency, (float) latencyMode); r.set (ids::stereoMode, (float) midSide);
        }, { 0, 1, 2 }, music, latency);
        const double err = eqIdentity (o, music, latency);
        const double rv = level (o[1], latency), sm = level (o[2], latency);
        const juce::String name = juce::String ("normal - Reveal solo + Smooth solo = input, ")
                                + (latencyMode == 0 ? "Low" : "Precise") + (midSide ? " M/S" : "");
        check (err < 1.0e-4 && rv > -50.0 && sm > -50.0, name.toRawUTF8(),
               "max err " + juce::String (err, 9) + "; solo peaks Reveal " + db (rv) + ", Smooth " + db (sm));
    }

    // Heat only = exactly the harmonics: normal - solo = input, and the solo has no fundamental.
    {
        const auto tone = testsig::sines (fs, (int) (3 * fs), { { 1000.0, 0.125893 } });
        int latency = 0;
        const auto o = runModes ([] (Rig& r) { r.set (ids::heat, 100); r.set (ids::color, 0); }, { 0, 3 }, tone, latency);
        double err = 0.0;
        for (size_t c = 0; c < tone.size(); ++c)
            for (size_t i = (size_t) latency; i < tone[c].size(); ++i)
                err = std::max (err, (double) std::abs (o[0][c][i] - o[1][c][i] - tone[c][i - (size_t) latency]));
        const float* seg = o[1][0].data() + (int) (1.5 * fs);
        const double fund = rel (seg, (int) fs, fs, 1000.0, 0.125893), h2 = rel (seg, (int) fs, fs, 2000.0, 0.125893);
        check (err < 1.0e-4 && fund < -80.0 && h2 > -40.0, "Heat only: just the harmonics",
               "normal - solo vs input max err " + juce::String (err, 9) + "; solo 1 kHz " + db (fund) + ", 2 kHz " + db (h2));
    }

    // Solos follow Mix (what that part contributes at this Mix), and Bypass still wins.
    {
        int latency = 0;
        const auto full = runModes ([] (Rig& r) { r.set (ids::smooth, 100); }, { 2 }, music, latency);
        const auto half = runModes ([] (Rig& r) { r.set (ids::smooth, 100); r.set (ids::mix, 50); }, { 2 }, music, latency);
        double err = 0.0;
        for (size_t c = 0; c < music.size(); ++c)
            for (size_t i = 0; i < music[c].size(); ++i)
                err = std::max (err, (double) std::abs (half[0][c][i] - 0.5f * full[0][c][i]));
        const auto byp = runModes ([] (Rig& r) { r.set (ids::heat, 100); r.set (ids::bypass, 1); }, { 3 }, music, latency);
        const double bypErr = delayedError (byp[0], music, latency);
        check (err < 1.0e-6 && bypErr < 1.0e-6, "solo scales with Mix, Bypass overrides it",
               "Mix 50 % vs half of 100 %: " + juce::String (err, 9) + "; bypass vs dry " + juce::String (bypErr, 9));
    }

    // The loudness match does not chase the (quieter) solo: it holds while one is heard.
    {
        Rig r;
        r.set (ids::smooth, 100); r.set (ids::match, 1); r.set (ids::listen, 2);
        r.prepare (fs, 512);
        r.run (testsig::musicLike (fs, 5.0, 3), fixedBlocks (512));
        const float held = r.proc->getMatchGainDb();
        r.set (ids::listen, 0);
        r.run (testsig::musicLike (fs, 8.0, 3), fixedBlocks (512));
        const float after = r.proc->getMatchGainDb();
        Rig ref;
        ref.set (ids::smooth, 100); ref.set (ids::match, 1);
        ref.prepare (fs, 512);
        ref.run (testsig::musicLike (fs, 8.0, 3), fixedBlocks (512));
        check (std::abs (held) < 0.1f && std::abs (after - ref.proc->getMatchGainDb()) < 1.0f,
               "Match holds while soloing, resumes after",
               "while soloing " + db (held, 2) + ", after " + db (after, 2) + " (never soloed " + db (ref.proc->getMatchGainDb(), 2) + ")");
    }

    // Switching listen modes while playing doesn't click: no sample step larger than steady playback has.
    {
        const auto tone = testsig::sines (fs, (int) (4 * fs), { { 1000.0, 0.25 }, { 3150.0, 0.05 } });
        auto maxStep = [] (const Channels& x, int from)
        {
            double m = 0.0;
            for (auto& ch : x)
                for (size_t i = (size_t) from + 1; i < ch.size(); ++i)
                    m = std::max (m, (double) std::abs (ch[i] - ch[i - 1]));
            return m;
        };
        auto setUp = [] (Rig& r) { r.set (ids::heat, 60); r.set (ids::reveal, 60); r.set (ids::smooth, 60); };
        int latency = 0;
        double steady = 0.0;
        for (const auto& o : runModes (setUp, { 0, 1, 2, 3 }, tone, latency))
            steady = std::max (steady, maxStep (o, latency + 4096));

        Rig r;
        r.setNeutral();
        setUp (r);
        r.prepare (fs, 512);
        int block = 0;
        const auto y = r.run (tone, [&r, &block] { r.set (ids::listen, (float) ((block++ / 7) % 4)); return 512; });
        const double switching = maxStep (y, latency + 4096);
        check (allFinite (y) && switching <= 1.1 * steady, "listen modes switch without clicks",
               "largest sample step " + juce::String (switching, 5) + " (steady playback " + juce::String (steady, 5) + ")");
    }

    // The display and the knobs' read-outs keep showing the full processing while soloing.
    {
        auto lastFrame = [fs] (int mode, anneal::DisplayFrame& out)
        {
            Rig r;
            r.set (ids::smooth, 100); r.set (ids::listen, (float) mode);
            r.prepare (fs, 512);
            r.run (testsig::musicLike (fs, 2.0), fixedBlocks (512));
            if (const auto* f = r.proc->getDisplay().read())
                out = *f;
        };
        auto a = std::make_unique<anneal::DisplayFrame>(), b = std::make_unique<anneal::DisplayFrame>();
        lastFrame (0, *a);
        lastFrame (2, *b);
        float worst = std::abs (a->cutDb - b->cutDb) + std::abs (a->heatRelDb - b->heatRelDb);
        for (int i = 0; i < anneal::DisplayFrame::numBands; ++i)
            worst = std::max ({ worst, std::abs (a->outDb[i] - b->outDb[i]), std::abs (a->gainDb[i] - b->gainDb[i]) });
        check (a->counter > 0 && worst < 1.0e-3f, "display shows the full result while soloing",
               "cutting " + db (b->cutDb) + " in both; max difference " + juce::String (worst, 6) + " dB");
    }

    // Old projects (no Listen in the state) open hearing everything; typed values are understood.
    {
        Rig a;
        auto old = a.proc->apvts.copyState();
        old.removeChild (old.getChildWithProperty ("id", ids::listen), nullptr);
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary (*old.createXml(), state);
        Rig b;
        b.set (ids::listen, 3);
        b.proc->setStateInformation (state.getData(), (int) state.getSize());
        auto* lp = b.proc->apvts.getParameter (ids::listen);
        auto typed = [lp] (const char* t) { return juce::roundToInt (lp->convertFrom0to1 (lp->getValueForText (t))); };
        const bool parsed = typed ("reveal") == 1 && typed ("Smooth only") == 2 && typed ("harmonics") == 3
                         && typed ("heat") == 3 && typed ("off") == 0 && typed ("2") == 2;
        check (lp->getValue() == 0.0f && lp->getDefaultValue() == 0.0f && parsed,
               "projects from before 0.2.5 open with Listen off", "Listen " + lp->getCurrentValueAsText()
                   + "; 'reveal' -> " + lp->getText (lp->getValueForText ("reveal"), 32) + ", 'heat' -> " + lp->getText (lp->getValueForText ("heat"), 32));
    }
}

// Levels (dB relative to each fundamental) of the 2nd..6th harmonics the engine writes on steady
// -18 dBFS tones; h[t][n] for tone t. Precise mode unless the params say otherwise.
std::vector<std::array<double, 7>> harmonicsOf (const anneal::EngineParams& p, std::initializer_list<double> tones,
                                                int mode = anneal::SpectralEngine::precise)
{
    const double fs = 48000.0;
    auto e = std::make_unique<anneal::SpectralEngine>();
    e->prepare (fs, 2, mode);
    e->setParameters (p);
    Channels in = testsig::silence (2, (int) (3 * fs));
    for (double f : tones)
    {
        const auto one = testsig::sines (fs, (int) (3 * fs), { { f, 0.125893 } });
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < in[c].size(); ++i)
                in[c][i] += one[c][i];
    }
    const auto out = runEngine (*e, in, fixedBlocks (512));
    const float* seg = out[0].data() + (int) (1.5 * fs);
    std::vector<std::array<double, 7>> result;
    for (double f : tones)
    {
        const double ref = testsig::toneAmplitude (seg, (int) fs, f, fs);
        std::array<double, 7> h {};
        for (int n = 2; n <= 6; ++n)
            h[(size_t) n] = n * f < 0.49 * fs ? rel (seg, (int) fs, fs, n * f, ref) : -300.0;
        result.push_back (h);
    }
    return result;
}

void testFlavours()
{
    section ("Saturation flavours: Clean / Tape / Tube / Transformer / Exciter");
    const double fs = 48000.0;
    auto params = [] (int flavour, float heat)
    {
        auto p = neutral();
        p.heat = heat;
        p.color = 0.35f;   // the default Color: the flavours must read as themselves there
        p.flavour = flavour;
        return p;
    };
    auto line = [] (const std::array<double, 7>& h)
    {
        return "2nd " + db (h[2]) + ", 3rd " + db (h[3]) + ", 4th " + db (h[4]) + ", 5th " + db (h[5]) + ", 6th " + db (h[6]);
    };

    // Tube: a strong 2nd, the rest pulled back.
    {
        const auto clean = harmonicsOf (params (anneal::flavourClean, 1.0f), { 1000.0 })[0];
        const auto tube = harmonicsOf (params (anneal::flavourTube, 1.0f), { 1000.0 })[0];
        info ("1 kHz, Heat 100 %: Clean " + line (clean));
        info ("                   Tube  " + line (tube));
        check (tube[2] >= clean[2] + 2.0 && (tube[2] - tube[3]) >= (clean[2] - clean[3]) + 8.0,
               "Tube: 2nd-heavy", "2nd " + db (clean[2]) + " -> " + db (tube[2]) + "; 2nd over 3rd "
                   + db (clean[2] - clean[3]) + " -> " + db (tube[2] - tube[3]));
    }

    // Tape: mostly even, the upper orders fall away, and harmonics landing high are rolled off
    // (a 4.5 kHz partial's 2nd at 9 kHz) while low ones are not (a 500 Hz partial's 2nd).
    {
        const auto clean = harmonicsOf (params (anneal::flavourClean, 1.0f), { 500.0, 4500.0 });
        const auto tape = harmonicsOf (params (anneal::flavourTape, 1.0f), { 500.0, 4500.0 });
        info ("500 Hz: Clean " + line (clean[0]) + " | Tape " + line (tape[0]));
        const bool even = (tape[0][2] - tape[0][3]) >= (clean[0][2] - clean[0][3]) + 4.0
                          && tape[0][5] <= clean[0][5] - 12.0 && tape[0][6] <= clean[0][6] - 12.0;
        check (even, "Tape: mostly even, soft upper orders",
               "2nd over 3rd " + db (clean[0][2] - clean[0][3]) + " -> " + db (tape[0][2] - tape[0][3])
                   + "; 5th " + db (clean[0][5]) + " -> " + db (tape[0][5]));
        const double low = tape[0][2] - clean[0][2], high = tape[1][2] - clean[1][2];
        check (std::abs (low) < 1.0 && high <= -4.0, "Tape: gentle roll-off of high harmonics",
               "2nd vs Clean at 1 kHz " + db (low) + ", at 9 kHz " + db (high));
    }

    // Transformer: odd harmonics, driven by the low end. A 60 Hz partial gets a strong 3rd (more
    // than Clean, and well above its own 2nd); a 2 kHz partial gets almost nothing.
    {
        const auto clean = harmonicsOf (params (anneal::flavourClean, 0.6f), { 60.0 });
        const auto xf = harmonicsOf (params (anneal::flavourTransformer, 0.6f), { 60.0 });
        const auto cleanHigh = harmonicsOf (params (anneal::flavourClean, 0.6f), { 2000.0 });
        const auto xfHigh = harmonicsOf (params (anneal::flavourTransformer, 0.6f), { 2000.0 });
        info ("60 Hz, Heat 60 %: Clean " + line (clean[0]) + " | Transformer " + line (xf[0]));
        check (xf[0][3] - xf[0][2] >= 10.0 && xf[0][3] >= clean[0][3] + 6.0 && xfHigh[0][3] <= cleanHigh[0][3] - 20.0,
               "Transformer: odd, from the low end",
               "60 Hz: 3rd over 2nd " + db (xf[0][3] - xf[0][2]) + ", 3rd " + db (clean[0][3]) + " -> " + db (xf[0][3])
                   + "; 2 kHz: 3rd " + db (cleanHigh[0][3]) + " -> " + db (xfHigh[0][3]));
    }

    // Exciter: air only. A 300 Hz partial gets nothing; a 4 kHz partial gets more than Clean gives it.
    for (int ms = 0; ms < 2; ++ms)
    {
        auto p = params (anneal::flavourExciter, 0.6f);
        auto c = params (anneal::flavourClean, 0.6f);
        if (ms == 1)
            for (auto* q : { &p, &c })
            {
                q->midSide = 1.0f;
                q->heatSide = 0.0f;   // Heat on Mid only (the tones are centred)
            }
        const auto ex = harmonicsOf (p, { 300.0, 4000.0 });
        const auto cl = harmonicsOf (c, { 300.0, 4000.0 });
        const bool ok = ex[0][2] < -80.0 && ex[0][3] < -80.0 && cl[0][2] > -40.0 && ex[1][2] > -40.0 && ex[1][2] >= cl[1][2] + 3.0;
        check (ok, ms == 0 ? "Exciter: air only" : "Exciter in Mid/Side (Heat: Mid)",
               "300 Hz partial: 600 Hz " + db (cl[0][2]) + " -> " + db (ex[0][2]) + "; 4 kHz partial: 8 kHz "
                   + db (cl[1][2]) + " -> " + db (ex[1][2]));
    }

    // A flavour changes only the harmonics: at Heat 0 every flavour is bit-identical to Clean, and
    // with Heat on, the output minus its Heat-only solo (= the EQ path) is the same for all of them.
    {
        const auto music = testsig::musicLike (fs, 3.0, 9);
        auto render = [&music] (int flavour, float heat, int listen)
        {
            Rig r;
            r.set (ids::reveal, 60); r.set (ids::smooth, 60); r.set (ids::heat, heat);
            r.set (ids::flavour, (float) flavour); r.set (ids::listen, (float) listen);
            r.prepare (48000.0, 512);
            return r.run (music, fixedBlocks (512));
        };
        double cold = 0.0, eqPath = 0.0, solosDiffer = 1.0e9;
        const auto cleanCold = render (0, 0.0f, 0);
        const auto cleanOn = render (0, 60.0f, 0), cleanSolo = render (0, 60.0f, 3);
        for (int f = 1; f < anneal::HarmonicSynth::numFlavours; ++f)
        {
            const auto c = render (f, 0.0f, 0);
            const auto on = render (f, 60.0f, 0), solo = render (f, 60.0f, 3);
            double soloDiff = 0.0;
            for (size_t ch = 0; ch < music.size(); ++ch)
                for (size_t i = 0; i < music[ch].size(); ++i)
                {
                    cold = std::max (cold, (double) std::abs (c[ch][i] - cleanCold[ch][i]));
                    eqPath = std::max (eqPath, (double) std::abs ((on[ch][i] - solo[ch][i]) - (cleanOn[ch][i] - cleanSolo[ch][i])));
                    soloDiff = std::max (soloDiff, (double) std::abs (solo[ch][i] - cleanSolo[ch][i]));
                }
            solosDiffer = std::min (solosDiffer, soloDiff);
        }
        check (cold == 0.0 && eqPath < 1.0e-5 && toDb (solosDiffer) > -60.0, "flavours change only the harmonics",
               "Heat 0 vs Clean " + juce::String (cold, 9) + "; EQ path vs Clean " + juce::String (eqPath, 9)
                   + "; harmonics differ by at least " + db (toDb (solosDiffer)));
    }

    // Switching flavour while playing doesn't click (the overlap-add crossfades it over one frame).
    {
        const auto tone = testsig::sines (fs, (int) (4 * fs), { { 110.0, 0.2 }, { 1000.0, 0.15 }, { 4000.0, 0.05 } });
        auto maxStep = [] (const Channels& x, int from)
        {
            double m = 0.0;
            for (auto& ch : x)
                for (size_t i = (size_t) from + 1; i < ch.size(); ++i)
                    m = std::max (m, (double) std::abs (ch[i] - ch[i - 1]));
            return m;
        };
        double steady = 0.0;
        int latency = 0;
        for (int f = 0; f < anneal::HarmonicSynth::numFlavours; ++f)
        {
            Rig r;
            r.set (ids::heat, 80); r.set (ids::flavour, (float) f);
            r.prepare (fs, 512);
            steady = std::max (steady, maxStep (r.run (tone, fixedBlocks (512)), r.proc->getLatencySamples() + 4096));
            latency = r.proc->getLatencySamples();
        }
        Rig r;
        r.set (ids::heat, 80);
        r.prepare (fs, 512);
        int block = 0;
        const auto y = r.run (tone, [&r, &block] { r.set (ids::flavour, (float) ((block++ / 5) % 5)); return 512; });
        const double switching = maxStep (y, latency + 4096);
        check (allFinite (y) && switching <= 1.1 * steady, "flavours switch without clicks",
               "largest sample step " + juce::String (switching, 5) + " (steady playback " + juce::String (steady, 5) + ")");
    }

    // The harmonic bars use the synth's own law: Clean exactly as before flavours, the others shaped.
    {
        float worst = 0.0f;
        for (float heat : { 0.1f, 0.3f, 0.7f, 1.0f })
            for (float color : { 0.0f, 0.35f, 0.5f, 1.0f })
                for (int n = 2; n <= 6; ++n)
                {
                    // The formula of 0.2.5's HarmonicGraph::harmonicDb.
                    const float t = std::tanh (std::pow (10.0f, (heat * 40.0f - 34.0f) / 20.0f));
                    const float w = (n % 2) == 0 ? std::min (1.0f, 2.0f * (1.0f - color)) : std::min (1.0f, 2.0f * color);
                    const float amp = w * std::pow (t, (float) (n - 1)) / (float) n;
                    const float before = amp > 1.0e-10f ? 20.0f * std::log10 (amp) : -200.0f;
                    worst = std::max (worst, std::abs (anneal::HarmonicGraph::harmonicDb (n, heat, color, 0) - before));
                }
        using G = anneal::HarmonicGraph;
        const bool shaped = G::harmonicDb (2, 0.6f, 0.35f, anneal::flavourTube) > G::harmonicDb (2, 0.6f, 0.35f, 0) + 2.0f
                         && G::harmonicDb (3, 0.6f, 0.35f, anneal::flavourTransformer) > G::harmonicDb (2, 0.6f, 0.35f, anneal::flavourTransformer)
                         && G::harmonicDb (5, 0.6f, 0.35f, anneal::flavourTape) < G::harmonicDb (5, 0.6f, 0.35f, 0) - 12.0f
                         && G::harmonicDb (2, 0.6f, 0.35f, anneal::flavourExciter) > G::harmonicDb (2, 0.6f, 0.35f, 0);
        check (worst == 0.0f && shaped, "harmonic bars follow the flavour",
               "Clean vs 0.2.5 formula: max diff " + juce::String (worst, 6) + " dB; Tube 2nd "
                   + db (G::harmonicDb (2, 0.6f, 0.35f, anneal::flavourTube)) + " vs Clean " + db (G::harmonicDb (2, 0.6f, 0.35f, 0)));
    }

    // Older projects (no Flavour in the state) open Clean; typed values; presets set Clean.
    {
        Rig a;
        auto old = a.proc->apvts.copyState();
        old.removeChild (old.getChildWithProperty ("id", ids::flavour), nullptr);
        juce::MemoryBlock state;
        juce::AudioProcessor::copyXmlToBinary (*old.createXml(), state);
        Rig b;
        b.set (ids::flavour, 2);
        b.proc->setStateInformation (state.getData(), (int) state.getSize());
        auto* fp = b.proc->apvts.getParameter (ids::flavour);
        auto typed = [fp] (const char* t) { return juce::roundToInt (fp->convertFrom0to1 (fp->getValueForText (t))); };
        const bool parsed = typed ("tape") == 1 && typed ("Tube") == 2 && typed ("transformer") == 3 && typed ("iron") == 3
                         && typed ("exciter") == 4 && typed ("air") == 4 && typed ("clean") == 0 && typed ("3") == 3;
        check (fp->getValue() == 0.0f && fp->getDefaultValue() == 0.0f && fp->getCurrentValueAsText() == "Clean" && parsed,
               "projects from before 0.2.6 open Clean", "Flavour " + fp->getCurrentValueAsText() + "; 'iron' -> "
                   + fp->getText (fp->getValueForText ("iron"), 32) + ", 'air' -> " + fp->getText (fp->getValueForText ("air"), 32));

        Rig c;
        c.set (ids::flavour, 2);
        c.proc->setCurrentProgram (2);
        const bool presetClean = c.proc->apvts.getRawParameterValue (ids::flavour)->load() == 0.0f
                                 && c.proc->getPresetDisplayName() == "Mix bus glue";
        c.set (ids::flavour, 1);
        check (presetClean && c.proc->getPresetDisplayName() == "Mix bus glue *",
               "factory presets keep Clean (they sound as before)", "after Tube, preset 2 -> Clean; Tape then shows "
                   + c.proc->getPresetDisplayName());
    }
}

void testEditorLayout()
{
    section ("Editor layout");
    Rig r;
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
    auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
    if (editor == nullptr)
    {
        check (false, "editor opens", "no AnnealEditor");
        return;
    }
    auto& content = editor->getContent();

    juce::String clash;
    const int n = content.getNumChildComponents();
    for (int i = 0; i < n; ++i)
        for (int j = i + 1; j < n; ++j)
        {
            const auto* a = content.getChildComponent (i);
            const auto* b = content.getChildComponent (j);
            if (a->isVisible() && b->isVisible() && a->getBounds().intersects (b->getBounds()))
                clash << a->getBounds().toString() << " / " << b->getBounds().toString() << "  ";
        }
    check (clash.isEmpty(), "no two controls overlap", clash.isEmpty() ? juce::String (n) + " controls" : clash);

    // The EQ and Heat routing menus sit on the group title row (408..423), both at the same height.
    std::vector<juce::Component*> routing;
    anneal::ListenMenu* listen = nullptr;
    anneal::FlavourMenu* flavour = nullptr;
    for (auto* c : content.getChildren())
    {
        if (auto* m = dynamic_cast<anneal::RoutingMenu*> (c); m != nullptr && m->getTooltip().startsWith ("In Mid/Side mode"))
            routing.push_back (c);
        if (auto* l = dynamic_cast<anneal::ListenMenu*> (c))
            listen = l;
        if (auto* f = dynamic_cast<anneal::FlavourMenu*> (c))
            flavour = f;
    }
    juce::String where;
    for (auto* c : routing)
        where << "y " << c->getY() << " (centre " << juce::String (c->getBounds().toFloat().getCentreY(), 1) << ")  ";
    check (routing.size() == 2 && routing[0]->getY() == routing[1]->getY()
               && std::abs (routing[0]->getBounds().toFloat().getCentreY() - 425.5f) <= 1.0f,   // 3.0 title row
           "EQ and Heat routing menus share the title row", where);

    r.set (ids::listen, 2);
    check (listen != nullptr && listen->getChoice() == 2 && anneal::ListenMenu::pillText (2) == "Smooth only",
           "Listen menu follows the parameter", listen != nullptr ? anneal::ListenMenu::pillText (listen->getChoice()) : juce::String ("missing"));

    // The Flavour menu sits on the HEAT title row with the routing menu, inside the HEAT group
    // (after its title, before the Heat routing menu), and follows the parameter.
    r.set (ids::flavour, 3);
    const bool placed = flavour != nullptr && routing.size() == 2 && flavour->getY() == routing[1]->getY()
                        && flavour->getX() > 556 && flavour->getRight() < routing[1]->getX();
    check (placed && flavour->getChoice() == 3 && anneal::FlavourMenu::pillText (3) == "Transformer"
               && anneal::FlavourMenu::pillText (0) == "Flavour: clean",
           "Flavour menu on the HEAT title row, follows the parameter",
           flavour != nullptr ? flavour->getBounds().toString() + ", shows " + anneal::FlavourMenu::pillText (flavour->getChoice())
                              : juce::String ("missing"));

    // 3.0: every control inside the base size, and no two controls overlapping.
    {
        juce::String overlaps;
        const juce::Rectangle<int> base (0, 0, AnnealEditorContent::baseWidth, AnnealEditorContent::baseHeight);
        auto kids = content.getChildren();
        for (int i = 0; i < kids.size(); ++i)
        {
            if (! kids[i]->isVisible())
                continue;
            if (! base.contains (kids[i]->getBounds()))
                overlaps << "outside: " << kids[i]->getBounds().toString() << "  ";
            for (int j = i + 1; j < kids.size(); ++j)
                if (kids[j]->isVisible() && kids[i]->getBounds().intersects (kids[j]->getBounds()))
                    overlaps << kids[i]->getBounds().toString() << " x " << kids[j]->getBounds().toString() << "  ";
        }
        check (overlaps.isEmpty(), "5.0 layout: all controls inside, none overlapping",
               overlaps.isEmpty() ? juce::String (kids.size()) + " controls" : overlaps);
    }

    // 5.0: the SHAPE group (Punch, Width, Mono bass) between the SIDECHAIN / REFERENCE column and
    // OUTPUT (5.3: dividers 1032, 1232).
    {
        int inShape = 0;
        for (auto* c : content.getChildren())
            if (dynamic_cast<anneal::Knob*> (c) != nullptr && c->getX() > 1032 && c->getRight() < 1232)
                ++inShape;
        check (inShape == 3 && AnnealEditorContent::baseWidth == 1464, "SHAPE group: Punch, Width and Mono bass before OUTPUT",
               juce::String (inShape) + " knobs, base width " + juce::String (AnnealEditorContent::baseWidth));
    }

    // 4.0: REFERENCE sits in the column after HEAT (5.3: under SIDECHAIN, dividers 832 and 1032), its
    // menu follows the parameter, and the Paint pill is in the header.
    anneal::ReferenceMenu* refMenu = nullptr;
    juce::Component* scMenu = nullptr;
    anneal::Knob* scAmount = nullptr;
    anneal::Knob* refAmount = nullptr;
    {
        juce::Component* paint = nullptr;
        for (auto* c : content.getChildren())
        {
            if (auto* m = dynamic_cast<anneal::ReferenceMenu*> (c))
                refMenu = m;
            if (auto* m = dynamic_cast<anneal::RoutingMenu*> (c); m != nullptr && m->getTooltip().startsWith ("Sidechain:"))
                scMenu = c;
            if (auto* k = dynamic_cast<anneal::Knob*> (c))
            {
                if (k->slider.getTooltip().startsWith ("Sidechain amount"))
                    scAmount = k;
                if (k->slider.getTooltip().startsWith ("Reference amount"))
                    refAmount = k;
            }
            if (auto* b = dynamic_cast<juce::TextButton*> (c); b != nullptr && b->getButtonText() == "PAINT")
                paint = c;
        }
        r.set (ids::refMode, 2);
        const bool ok = refMenu != nullptr && scMenu != nullptr && paint != nullptr && refMenu->getX() > 832 && refMenu->getRight() < 1032
                        && scMenu->getX() > 832 && scMenu->getRight() < 1032 && refMenu->getY() > scMenu->getBottom() + 60
                        && refMenu->getChoice() == 2 && anneal::ReferenceMenu::pillText (0) == "Reference: off"
                        && paint->getBottom() <= 48 && AnnealEditorContent::baseWidth == 1464;
        check (ok, "SIDECHAIN over REFERENCE in one column, Paint pill placed, menu follows the parameter",
               (refMenu != nullptr ? "menu " + refMenu->getBounds().toString() + " shows " + anneal::ReferenceMenu::pillText (refMenu->getChoice())
                                   : juce::String ("NO MENU"))
                   + (scMenu != nullptr ? ", sidechain " + scMenu->getBounds().toString() : juce::String (", NO SIDECHAIN MENU"))
                   + (paint != nullptr ? ", Paint " + paint->getBounds().toString() : juce::String (", NO PAINT PILL")));
        r.set (ids::refMode, 0);
    }

    // 5.3, a calmer deck: what does nothing in the current setting isn't shown. The Mid/Side routing
    // menus appear in Mid/Side only; an Amount row only while its Sidechain / Reference mode is on.
    // With everything shown, still nothing overlaps and everything stays inside its column.
    {
        auto shown = [] (juce::Component* c) { return c != nullptr && c->isVisible(); };
        content.tick (0.0f);
        const bool quiet = ! shown (routing.size() == 2 ? routing[0] : nullptr) && ! shown (routing.size() == 2 ? routing[1] : nullptr)
                           && ! shown (scAmount) && ! shown (refAmount);
        r.set (ids::stereoMode, 1.0f);
        r.set (ids::scMode, 1.0f);
        r.set (ids::refMode, 2.0f);
        content.tick (0.0f);
        const bool all = routing.size() == 2 && shown (routing[0]) && shown (routing[1]) && shown (scAmount) && shown (refAmount);
        juce::String overlaps;
        auto kids = content.getChildren();
        for (int i = 0; i < kids.size(); ++i)
            for (int j = i + 1; j < kids.size(); ++j)
                if (kids[i]->isVisible() && kids[j]->isVisible() && kids[i]->getBounds().intersects (kids[j]->getBounds()))
                    overlaps << kids[i]->getBounds().toString() << " x " << kids[j]->getBounds().toString() << "  ";
        const bool inColumn = scAmount != nullptr && refAmount != nullptr && refMenu != nullptr
                              && scAmount->getX() > 832 && scAmount->getRight() < 1032 && refAmount->getX() > 832 && refAmount->getRight() < 1032
                              && scAmount->getBottom() < refMenu->getY() - 15 && refAmount->getY() > refMenu->getBottom()
                              && refAmount->getBottom() <= 628;
        r.set (ids::stereoMode, 0.0f);
        r.set (ids::scMode, 0.0f);
        r.set (ids::refMode, 0.0f);
        content.tick (0.0f);
        const bool quietAgain = ! shown (scAmount) && ! shown (refAmount) && ! shown (routing.size() == 2 ? routing[0] : nullptr);
        check (quiet && all && quietAgain && overlaps.isEmpty() && inColumn,
               "5.3 calmer deck: routing in Mid/Side only, Amount rows only while their mode is on, nothing overlapping",
               juce::String ("at rest: ") + (quiet ? "hidden" : "SHOWN") + "; Mid/Side, sidechain and reference on: "
                   + (all ? "shown" : "NOT ALL SHOWN") + (inColumn ? ", in their column" : ", OUT OF THE COLUMN")
                   + (overlaps.isEmpty() ? juce::String (", no overlaps") : ", overlaps " + overlaps)
                   + "; off again: " + (quietAgain ? "hidden" : "SHOWN"));
    }
}

void benchmark()
{
    section ("18. Benchmark (Release build, this machine)");
    // The single slowest block mostly measures Windows pre-empting this test, not Anneal: the
    // percentiles (and running at high priority, as DAW audio threads do) show the real cost.
    juce::Process::setPriority (juce::Process::HighPriority);
    struct Case { double fs; int block; int mode; int midSide = 0; int flavour = 0; int matchPaint = 0; int shape = 0; int zero = 0; int spread = 0; };
    const char* modeNames[] = { "Low", "Normal", "Precise" };

    // 4.0: Reference Match at 100 % (a pink-noise reference) with EQ and Heat paint.
    anneal::ReferenceCurve pinkRef;
    {
        const auto pink = pinkNoise (48000.0, 4.0, 0.05f, 91);
        anneal::ReferenceAnalyser a (48000.0);
        const float* ptrs[2] = { pink[0].data(), pink[1].data() };
        a.push (ptrs, 2, (int) pink[0].size());
        pinkRef = a.finish();
    }
    anneal::PaintCurves somePaint;
    for (int i = 20; i < 40; ++i)
    {
        somePaint.eq[i] = 0.5f;
        somePaint.heat[i + 10] = 0.25f;
    }

    for (auto [fs, block, mode, midSide, flavour, matchPaint, shape, zero, spread] : { Case { 48000.0, 64, 0 }, Case { 48000.0, 128, 0 }, Case { 48000.0, 512, 0 },
                                    Case { 48000.0, 512, 1 }, Case { 48000.0, 64, 2 }, Case { 48000.0, 512, 2 },
                                    Case { 96000.0, 512, 0 }, Case { 48000.0, 64, 0, 1 },
                                    Case { 48000.0, 512, 1, 1 }, Case { 48000.0, 64, 2, 1 },
                                    Case { 48000.0, 64, 0, 0, 1 }, Case { 48000.0, 512, 2, 0, 1 },
                                    Case { 48000.0, 64, 0, 0, 0, 1 }, Case { 48000.0, 512, 1, 0, 0, 1 }, Case { 48000.0, 64, 2, 0, 0, 1 },
                                    Case { 48000.0, 64, 0, 0, 0, 0, 1 }, Case { 48000.0, 64, 2, 0, 0, 0, 1 },
                                    Case { 48000.0, 64, 0, 0, 0, 0, 0, 1 }, Case { 48000.0, 512, 0, 0, 0, 0, 0, 1 },
                                    Case { 48000.0, 64, 0, 1, 0, 0, 0, 1 },
                                    Case { 48000.0, 64, 0, 0, 0, 0, 1, 1 },   // 5.2.7: Punch working at zero latency
                                    // 5.2.1 Spread load, against the same cases without it above (and at 32)
                                    Case { 48000.0, 64, 0, 0, 0, 0, 0, 0, 1 }, Case { 48000.0, 64, 2, 0, 0, 0, 0, 0, 1 },
                                    Case { 48000.0, 64, 2, 1, 0, 0, 0, 0, 1 }, Case { 48000.0, 32, 2, 1 },
                                    Case { 48000.0, 32, 2, 1, 0, 0, 0, 0, 1 } })
    {
        Rig r;   // default settings: Reveal 40, Smooth 40, Heat 30
        r.set (ids::latency, (float) mode);
        r.set (ids::stereoMode, (float) midSide);
        r.set (ids::flavour, (float) flavour);   // Tape: the flavour with per-bin work
        if (matchPaint)
        {
            r.proc->setReference (pinkRef, "pink");
            r.proc->setPaint (somePaint);
            r.set (ids::refMode, 2.0f);
            r.set (ids::refAmount, 100.0f);
        }
        if (zero)    // 5.1: Zero latency
            r.set (ids::zeroLatency, 1.0f);
        if (spread)  // 5.2.1: Spread load
            r.set (ids::spread, 1.0f);
        if (shape)   // 5.0: Punch, Width and Mono bass all working
        {
            r.set (ids::punch, 50.0f);
            r.set (ids::width, 150.0f);
            r.set (ids::monoBass, 120.0f);
        }
        r.prepare (fs, block);
        const auto in = testsig::musicLike (fs, 20.0, 1);
        const int n = (int) in[0].size();
        juce::AudioBuffer<float> buf (2, block);
        juce::MidiBuffer midi;
        std::vector<double> times;
        times.reserve ((size_t) (n / block + 1));
        double total = 0.0;
        for (int pos = 0; pos + block <= n; pos += block)
        {
            for (int c = 0; c < 2; ++c)
                buf.copyFrom (c, 0, in[(size_t) c].data() + pos, block);
            const auto t0 = std::chrono::steady_clock::now();
            r.proc->processBlock (buf, midi);
            const double dt = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
            total += dt;
            times.push_back (dt);
        }
        const auto slowest = std::max_element (times.begin(), times.end());
        const int slowestBlock = (int) (slowest - times.begin());
        std::vector<double> first8 (times.begin(), times.begin() + std::min<size_t> (8, times.size()));
        std::sort (times.begin(), times.end());
        auto pct = [&times] (double q) { return times[(size_t) std::min ((double) times.size() - 1, q * (double) times.size())]; };
        const double audio = n / fs, deadline = block / fs;
        auto ms = [deadline] (double t) { return juce::String (t * 1000.0, 3) + " ms (" + juce::String (juce::roundToInt (100.0 * t / deadline)) + " %)"; };
        info (juce::String (modeNames[mode]) + (midSide ? " M/S " : " Stereo ") + (flavour ? "Tape " : "") + (matchPaint ? "Match+Paint " : "") + (shape ? "Shape " : "") + (zero ? "Zero latency " : "") + (spread ? "Spread " : "") + juce::String (fs / 1000.0, 0) + " kHz / " + juce::String (block) + ": CPU " + juce::String (100.0 * total / audio, 2)
              + " % of one core; deadline " + juce::String (deadline * 1000.0, 3) + " ms; p99 " + ms (pct (0.99))
              + ", p99.9 " + ms (pct (0.999)) + ", max " + ms (times.back()) + " at block " + juce::String (slowestBlock));
        juce::String f8;
        for (double t : first8) f8 << juce::String (t * 1000.0, 3) << " ";
        info ("first blocks (ms): " + f8);
    }
    juce::Process::setPriority (juce::Process::NormalPriority);
}

// 4.0.2: Anneal's own real FFT against JUCE's (same layout and scaling) and an exact DFT.
void testRealFft()
{
    section ("31. Real FFT (4.0.2)");
    std::mt19937 rng (17);
    std::uniform_real_distribution<float> u (-1.0f, 1.0f);
    double worstJuceFwd = 0.0, worstJuceInv = 0.0, worstRound = 0.0, worstExact = 0.0, juceExact = 0.0;
    bool untouched = true;
    for (int order = 3; order <= 15; ++order)
    {
        const int n = 1 << order;
        anneal::RealFft mine;
        mine.prepare (n);
        juce::dsp::FFT theirs (order);

        std::vector<float> x ((size_t) n);
        for (auto& v : x) v = u (rng);
        std::vector<float> a ((size_t) n * 2 + 8, 12345.0f), b ((size_t) n * 2, 0.0f);
        std::copy (x.begin(), x.end(), a.begin());
        std::copy (x.begin(), x.end(), b.begin());
        mine.forward (a.data());
        theirs.performRealOnlyForwardTransform (b.data(), true);
        for (size_t i = (size_t) n + 2; i < a.size(); ++i)
            untouched = untouched && a[i] == 12345.0f;   // only bins 0..N/2 are written
        double peak = 0.0, diff = 0.0;
        for (int i = 0; i < n + 2; ++i)
        {
            peak = std::max (peak, (double) std::abs (b[(size_t) i]));
            diff = std::max (diff, (double) std::abs (a[(size_t) i] - b[(size_t) i]));
        }
        worstJuceFwd = std::max (worstJuceFwd, diff / peak);

        if (n <= 1024)   // exact DFT in double precision
        {
            double errMine = 0.0, errJuce = 0.0, top = 0.0;
            for (int k = 0; k <= n / 2; ++k)
            {
                double re = 0.0, im = 0.0;
                for (int t = 0; t < n; ++t)
                {
                    const double ph = -2.0 * juce::MathConstants<double>::pi * (double) ((long long) k * t % n) / n;
                    re += x[(size_t) t] * std::cos (ph);
                    im += x[(size_t) t] * std::sin (ph);
                }
                top = std::max (top, std::hypot (re, im));
                errMine = std::max (errMine, std::hypot (a[2 * (size_t) k] - re, a[2 * (size_t) k + 1] - im));
                errJuce = std::max (errJuce, std::hypot (b[2 * (size_t) k] - re, b[2 * (size_t) k + 1] - im));
            }
            worstExact = std::max (worstExact, errMine / top);
            juceExact = std::max (juceExact, errJuce / top);
        }

        // Inverse of an arbitrary spectrum, including imaginary parts at DC and Nyquist (ignored).
        std::vector<float> s ((size_t) n + 2);
        for (auto& v : s) v = u (rng);
        std::vector<float> c ((size_t) n * 2, 0.0f), d ((size_t) n * 2, 0.0f);
        std::copy (s.begin(), s.end(), c.begin());
        std::copy (s.begin(), s.end(), d.begin());
        mine.inverse (c.data());
        theirs.performRealOnlyInverseTransform (d.data());
        peak = diff = 0.0;
        for (int i = 0; i < n; ++i)
        {
            peak = std::max (peak, (double) std::abs (d[(size_t) i]));
            diff = std::max (diff, (double) std::abs (c[(size_t) i] - d[(size_t) i]));
        }
        worstJuceInv = std::max (worstJuceInv, diff / peak);

        mine.inverse (a.data());   // the forward result back to the samples
        double rt = 0.0;
        for (int i = 0; i < n; ++i)
            rt = std::max (rt, (double) std::abs (a[(size_t) i] - x[(size_t) i]));
        worstRound = std::max (worstRound, rt);
    }
    check (worstJuceFwd < 1.0e-5, "forward = JUCE's, sizes 8 to 32768",
           "largest difference " + db (toDb (worstJuceFwd)) + " re the peak bin");
    check (worstExact < 2.0e-6 && worstExact <= 2.0 * juceExact + 1.0e-7, "forward = exact DFT (sizes 8 to 1024)",
           "error " + db (toDb (worstExact)) + " (JUCE's " + db (toDb (juceExact)) + ")");
    check (worstJuceInv < 1.0e-5, "inverse = JUCE's (scale 1/N, DC/Nyquist imag ignored)",
           "largest difference " + db (toDb (worstJuceInv)) + " re the peak sample");
    check (worstRound < 1.0e-5, "inverse (forward (x)) = x", "largest error " + db (toDb (worstRound)) + " (full scale 1)");
    check (untouched, "forward writes only bins 0..N/2", untouched ? juce::String ("N+2.. untouched") : juce::String ("WROTE PAST N+1"));
}

// 4.0.2 bug fixes: the host preparing Anneal again. JUCE's VST3 wrapper calls prepareToPlay on every
// setActive (true); hosts do that when the buffer size or the sample rate changes, and many when an
// export starts. Measurements (a frozen curve, the loudness match) must come through it, as they do
// through reset().
void testFixes402()
{
    section ("32. 4.0.2 bug fixes");
    const double fs = 48000.0;

    // Freeze: before 4.0.2 preparing again re-primed the held curve from the first frame afterwards.
    {
        const int n = (int) (2 * fs);
        Channels ringy = testsig::silence (2, n);
        testsig::Pink pl (1), pr (2);
        for (int i = 0; i < n; ++i)
        {
            const float ring = 0.05f * (float) std::sin (2.0 * testsig::pi * 3150.0 * i / fs);
            ringy[0][(size_t) i] = 0.03f * pl.next() + ring;
            ringy[1][(size_t) i] = 0.03f * pr.next() + ring;
        }

        Rig r;
        r.setNeutral();
        r.set (ids::smooth, 100.0f);
        r.prepare (fs, 512);
        r.run (ringy, fixedBlocks (512));
        r.set (ids::freeze, 1.0f);
        r.run (pinkNoise (fs, 0.1, 0.03f, 4), fixedBlocks (512));
        const float held = bandGain (r.proc->getEngine(), 3150.0);
        r.prepare (fs, 256);   // the host activates Anneal again with another buffer size
        r.run (pinkNoise (fs, 1.0, 0.03f, 9), fixedBlocks (256));
        const float afterPrepare = bandGain (r.proc->getEngine(), 3150.0);
        r.prepare (44100.0, 512);   // and at 44.1 kHz (the same bands)
        r.run (pinkNoise (44100.0, 1.0, 0.03f, 9), fixedBlocks (512));
        const float afterRate = bandGain (r.proc->getEngine(), 3150.0);
        check (held <= -3.0f && std::abs (afterPrepare - held) < 0.05f && std::abs (afterRate - held) < 0.05f,
               "Freeze holds when the host prepares again",
               "held " + db (held) + ", after prepareToPlay " + db (afterPrepare) + ", at 44.1 kHz " + db (afterRate));

        // Unfrozen, the curve follows the new audio as before.
        r.set (ids::freeze, 0.0f);
        r.prepare (fs, 512);
        r.run (pinkNoise (fs, 1.0, 0.03f, 9), fixedBlocks (512));
        const float released = bandGain (r.proc->getEngine(), 3150.0);
        check (released > held + 2.0f, "unfrozen, the curve follows the audio again",
               "3150 Hz " + db (released) + " (was held at " + db (held) + ")");
    }

    // Loudness Match: before 4.0.2 preparing again started the match gain over at 0 dB (a 0.5 s
    // swell at the start of an export).
    {
        Rig r;
        r.setNeutral();
        r.set (ids::reveal, 100.0f);
        r.set (ids::heat, 100.0f);
        r.set (ids::output, 0.0f);
        r.set (ids::match, 1.0f);
        r.prepare (fs, 512);
        const auto music = testsig::musicLike (fs, 6.0, 3);
        r.run (music, fixedBlocks (512));
        const float settled = r.proc->getMatchGainDb();
        r.prepare (fs, 1024);
        Channels shortPart = testsig::silence (2, (int) (0.1 * fs));
        for (int c = 0; c < 2; ++c)
            std::copy (music[(size_t) c].begin(), music[(size_t) c].begin() + (long) shortPart[0].size(), shortPart[(size_t) c].begin());
        r.run (shortPart, fixedBlocks (1024));
        const float afterPrepare = r.proc->getMatchGainDb();
        check (std::abs (settled) > 0.5f && std::abs (afterPrepare - settled) < 0.15f,
               "loudness Match keeps its gain when the host prepares again",
               "settled " + db (settled, 2) + ", 0.1 s after prepareToPlay " + db (afterPrepare, 2));
    }
}

// `AnnealTests profile`: where one STFT frame's time goes (Release build, this machine). Not a
// check; run it by hand before and after performance work.
void profile()
{
    section ("Profile: microseconds per STFT frame, 48 kHz stereo, default settings");
    juce::Process::setPriority (juce::Process::HighPriority);
    const double fs = 48000.0;
    const auto music = testsig::musicLike (fs, 20.0, 1);
    auto usPer = [] (int reps, const std::function<void()>& f)
    {
        f();   // warm up
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < reps; ++i)
            f();
        return 1.0e6 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / reps;
    };

    for (int mode : { 0, 1, 2 })
    {
        const int n = anneal::SpectralEngine::fftSizeFor (fs, mode);
        const int bins = n / 2 + 1;
        int order = 0;
        while ((1 << order) < n)
            ++order;

        // One windowed frame of the music and its spectrum.
        std::vector<float> frame ((size_t) n * 2, 0.0f), work ((size_t) n * 2), power ((size_t) bins), outDb (1024);
        for (int i = 0; i < n; ++i)
            frame[(size_t) i] = music[0][(size_t) (fs * 5.0) + (size_t) i] * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / n));
        juce::dsp::FFT jfft (order);
        work = frame;
        jfft.performRealOnlyForwardTransform (work.data(), true);
        const float scale = 4.0f / (float) n;
        for (int k = 0; k < bins; ++k)
            power[(size_t) k] = (work[2 * (size_t) k] * work[2 * (size_t) k] + work[2 * (size_t) k + 1] * work[2 * (size_t) k + 1]) * scale * scale;
        const std::vector<float> spectrum = work;

        const double fwd = usPer (2000, [&] { work = frame; jfft.performRealOnlyForwardTransform (work.data(), true); });
        const double inv = usPer (2000, [&] { work = spectrum; jfft.performRealOnlyInverseTransform (work.data()); });
        const double copy = usPer (2000, [&] { work = frame; });
        anneal::RealFft own;
        own.prepare (n);
        const double ownFwd = usPer (2000, [&] { work = frame; own.forward (work.data()); });
        const double ownInv = usPer (2000, [&] { work = spectrum; own.inverse (work.data()); });

        anneal::PerceptualModel model;
        model.prepare (fs, n);
        anneal::ModelSettings ms;
        ms.reveal = 0.4f; ms.smooth = 0.4f;
        const float fps = (float) (fs / anneal::SpectralEngine::hopFor (fs, n));
        const double analyse = usPer (2000, [&] { model.analyse (power.data(), ms, fps); });
        const double levels = usPer (2000, [&] { model.bandLevelsDb (power.data(), outDb.data()); });
        model.setExactMath (true);   // 5.1.0's per-value std::log10 / std::pow
        const double analyseExact = usPer (2000, [&] { model.analyse (power.data(), ms, fps); });
        const double levelsExact = usPer (2000, [&] { model.bandLevelsDb (power.data(), outDb.data()); });
        model.setExactMath (false);

        anneal::HarmonicSynth synth;
        synth.prepare (n, fs);
        std::vector<float> harm0 ((size_t) bins * 2), harm1 ((size_t) bins * 2), harmPower ((size_t) bins);
        const float* specs[2] = { spectrum.data(), spectrum.data() };
        float* harms[2] = { harm0.data(), harm1.data() };
        anneal::HeatSettings hs;
        hs.heat = 0.3f;
        const double heat = usPer (2000, [&] { synth.process (specs, 2, power.data(), model.getBinNeed(), hs, harms, harmPower.data()); });

        // The whole engine on the music, per frame.
        anneal::SpectralEngine e;
        e.prepare (fs, 2, mode);
        auto p = neutral();
        p.reveal = 0.4f; p.smooth = 0.4f; p.heat = 0.3f;
        e.setParameters (p);
        auto x = music;
        const auto t0 = std::chrono::steady_clock::now();
        runEngine (e, x, fixedBlocks (512));
        const double total = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        const double frames = (double) music[0].size() / e.getHopSize();
        const double perFrame = 1.0e6 * total / frames;

        const char* names[] = { "Low", "Normal", "Precise" };
        info (juce::String (names[mode]) + " (N " + juce::String (n) + ", " + juce::String (fps, 1) + " frames/s): engine "
              + juce::String (perFrame, 1) + " us/frame = " + juce::String (100.0 * total / 20.0, 2) + " % of one core");
        info ("  JUCE FFT forward " + juce::String (fwd - copy, 1) + " us, inverse " + juce::String (inv - copy, 1)
              + " us (6 per stereo frame with Heat: " + juce::String (2 * (fwd - copy) + 4 * (inv - copy), 1) + " us)");
        info ("  RealFft forward " + juce::String (ownFwd - copy, 1) + " us, inverse " + juce::String (ownInv - copy, 1)
              + " us (6: " + juce::String (2 * (ownFwd - copy) + 4 * (ownInv - copy), 1) + " us)");
        info ("  model.analyse " + juce::String (analyse, 1) + " us, bandLevelsDb " + juce::String (levels, 1)
              + " us (x3 for the display), synth.process " + juce::String (heat, 1) + " us");
        info ("  with 5.1.0's exact math: model.analyse " + juce::String (analyseExact, 1) + " us, bandLevelsDb "
              + juce::String (levelsExact, 1) + " us");
    }
    juce::Process::setPriority (juce::Process::NormalPriority);
}
}

//==============================================================================
// 3.0: sidechain, freeze, true-peak ceiling, undo/redo.

Channels runEngineWithKey (anneal::SpectralEngine& e, const Channels& in, const Channels& key, int block)
{
    Channels out = in;
    const int n = (int) in[0].size();
    for (int pos = 0; pos < n; pos += block)
    {
        const int b = std::min (block, n - pos);
        float* ptrs[2] = { out[0].data() + pos, out[1].data() + pos };
        const float* k[2] = { key[0].data() + pos, key.size() > 1 ? key[1].data() + pos : key[0].data() + pos };
        e.process (ptrs, 2, b, k, (int) key.size());
    }
    return out;
}

Channels pinkNoise (double fs, double seconds, float amp, unsigned seed)
{
    const int n = (int) (fs * seconds);
    auto out = testsig::silence (2, n);
    testsig::Pink pl (seed), pr (seed + 1);
    for (int i = 0; i < n; ++i)
    {
        out[0][(size_t) i] = amp * pl.next();
        out[1][(size_t) i] = amp * pr.next();
    }
    return out;
}

// True peak by 8x windowed-sinc interpolation (64 taps), independent of the limiter's own detector.
double truePeak (const Channels& x, int from)
{
    constexpr int over = 8, half = 32;
    double peak = 0.0;
    for (const auto& ch : x)
    {
        const int n = (int) ch.size();
        for (int i = std::max (from, half); i < n - half; ++i)
        {
            peak = std::max (peak, (double) std::abs (ch[(size_t) i]));
            for (int p = 1; p < over; ++p)
            {
                const double frac = (double) p / over;
                double y = 0.0;
                for (int k = -half + 1; k <= half; ++k)
                {
                    const double t = k - frac;
                    const double sinc = std::sin (testsig::pi * t) / (testsig::pi * t);
                    const double w = 0.5 * (1.0 + std::cos (testsig::pi * t / half));
                    y += ch[(size_t) (i + k)] * sinc * w;
                }
                peak = std::max (peak, std::abs (y));
            }
        }
    }
    return peak;
}

void testSidechain()
{
    section ("21. Sidechain (3.0)");
    const double fs = 48000.0;
    const auto main = pinkNoise (fs, 3.0, 0.03f, 21);
    auto silentKey = testsig::silence (2, (int) main[0].size());

    {
        // A sidechain mode with a silent key changes nothing (the model's key term is exactly 0).
        auto a = std::make_unique<anneal::SpectralEngine>();
        auto b = std::make_unique<anneal::SpectralEngine>();
        a->prepare (fs, 2, anneal::SpectralEngine::low);
        b->prepare (fs, 2, anneal::SpectralEngine::low);
        auto p = neutral();
        p.reveal = 0.4f; p.smooth = 0.4f; p.heat = 0.3f;
        a->setParameters (p);
        p.scMode = 1; p.scAmount = 1.0f;
        b->setParameters (p);
        const auto oa = runEngine (*a, main, fixedBlocks (512));
        const auto ob = runEngineWithKey (*b, main, silentKey, 512);
        double worst = 0.0;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < oa[0].size(); ++i)
                worst = std::max (worst, (double) std::abs (oa[(size_t) c][i] - ob[(size_t) c][i]));
        check (worst == 0.0, "sidechain on, silent key = identical output", "max diff " + juce::String (worst, 12));
    }

    {
        // Make room: a loud key tone at 2 kHz ducks the main there, not elsewhere.
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2, anneal::SpectralEngine::low);
        auto p = neutral();
        p.scMode = 1; p.scAmount = 1.0f;
        e->setParameters (p);
        const auto key = testsig::sines (fs, (int) main[0].size(), { { 2000.0, 0.2 } });
        runEngineWithKey (*e, main, key, 512);
        const float g2k = bandGain (*e, 2000.0), g300 = bandGain (*e, 300.0), g8k = bandGain (*e, 8000.0);
        check (g2k <= -3.0f && std::abs (g300) < 1.0f && std::abs (g8k) < 1.0f, "Make room: key at 2 kHz ducks the main there",
               "2 kHz " + db (g2k) + ", 300 Hz " + db (g300) + ", 8 kHz " + db (g8k));
    }

    {
        // Unmask: a quiet main tone under loud key noise is lifted.
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2, anneal::SpectralEngine::low);
        auto p = neutral();
        p.scMode = 2; p.scAmount = 1.0f;
        e->setParameters (p);
        const int n = (int) (3 * fs);
        const auto tone = testsig::sines (fs, n, { { 2000.0, 0.003 } });   // -50 dBFS, clearly under the key
        const auto key = pinkNoise (fs, 3.0, 0.2f, 33);
        runEngineWithKey (*e, tone, key, 512);
        const float g2k = bandGain (*e, 2000.0);
        check (g2k >= 1.0f, "Unmask: main masked by the key is lifted", "2 kHz " + db (g2k));
    }

    {
        // Through the processor: a stereo sidechain bus, routed by the host.
        Rig r;
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        const bool accepted = r.proc->setBusesLayout (l);
        r.setNeutral();
        r.set (ids::scMode, 1.0f);
        r.set (ids::scAmount, 100.0f);
        r.prepare (fs, 512);
        const int n = (int) (3 * fs);
        const auto keyTone = testsig::sines (fs, n, { { 2000.0, 0.2 } });
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        Channels out = main;
        for (int pos = 0; pos + 512 <= n; pos += 512)
        {
            for (int c = 0; c < 2; ++c)
            {
                buf.copyFrom (c, 0, main[(size_t) c].data() + pos, 512);
                buf.copyFrom (c + 2, 0, keyTone[(size_t) c].data() + pos, 512);
            }
            r.proc->processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + 512, out[(size_t) c].data() + pos);
        }
        const int lat = r.proc->getLatencySamples();
        const double inAmp = testsig::toneAmplitude (main[0].data() + n / 2, n / 3, 2000.0, fs) + 1.0e-12;
        const double outAmp = testsig::toneAmplitude (out[0].data() + n / 2 + lat, n / 3, 2000.0, fs) + 1.0e-12;
        const double change = toDb (outAmp / inAmp);
        check (accepted && r.proc->isSidechainConnected() && change < -2.0,
               "processor: sidechain bus routed, Make room ducks", "layout " + juce::String (accepted ? "ok" : "REJECTED")
                   + ", main at 2 kHz " + db (change));
    }
}

void testFreeze()
{
    section ("22. Freeze (3.0)");
    const double fs = 48000.0;
    const int n = (int) (2 * fs);
    Channels ringy = testsig::silence (2, n);
    {
        testsig::Pink pl (1), pr (2);
        for (int i = 0; i < n; ++i)
        {
            const float ring = 0.05f * (float) std::sin (2.0 * testsig::pi * 3150.0 * i / fs);
            ringy[0][(size_t) i] = 0.03f * pl.next() + ring;
            ringy[1][(size_t) i] = 0.03f * pr.next() + ring;
        }
    }
    const auto plain = pinkNoise (fs, 2.0, 0.03f, 9);

    float held = 0.0f, released = 0.0f, before = 0.0f;
    for (bool freeze : { true, false })
    {
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2, anneal::SpectralEngine::low);
        auto p = neutral();
        p.smooth = 1.0f;
        e->setParameters (p);
        runEngine (*e, ringy, fixedBlocks (512));
        before = bandGain (*e, 3150.0);
        p.freeze = freeze;
        e->setParameters (p);
        runEngine (*e, plain, fixedBlocks (512));   // the ring is gone
        (freeze ? held : released) = bandGain (*e, 3150.0);
    }
    check (before <= -3.0f && std::abs (held - before) < 0.05f && released > before + 2.0f,
           "Freeze holds the curve after the music changes", "cut " + db (before) + ", frozen " + db (held) + ", not frozen " + db (released));
}

void testCeiling()
{
    section ("23. True-peak ceiling (3.0)");
    const double fs = 48000.0;
    const int n = (int) (2 * fs);

    // fs/4 sine at 45 degrees: every sample lands at 0.707 of the real peak, the classic
    // inter-sample over. Driven +12 dB so the true peak is ~ +12 dBTP before the ceiling.
    Channels isp = testsig::silence (2, n);
    for (int i = 0; i < n; ++i)
        isp[0][(size_t) i] = isp[1][(size_t) i] = (float) (std::sin (2.0 * testsig::pi * 0.25 * i + testsig::pi / 4.0));
    const auto music = testsig::musicLike (fs, 2.0);

    bool ok = true;
    juce::String log;
    const Channels* signals[] = { &isp, &music };
    for (int choice : { 1, 2, 3 })
        for (const Channels* sig : signals)
        {
            Rig r;
            r.setNeutral();
            r.set (ids::heat, 100.0f);
            r.set (ids::output, 12.0f);
            r.set (ids::ceiling, (float) choice);
            r.prepare (fs, 512);
            const auto out = r.run (*sig, fixedBlocks (512));
            const double tp = toDb (truePeak (out, n / 2));
            const double ceiling = anneal::ceilingDbtp[choice];
            const float gr = r.proc->takeCeilingReductionDb();   // the meter's read-out
            const bool fine = allFinite (out) && tp <= ceiling + 0.2 && gr < -1.0f;
            ok = ok && fine;
            log << juce::String (ceiling, 1) << ":" << juce::String (tp, 2) << " (GR " << juce::String (gr, 1) << ")" << (fine ? "" : "!") << "  ";
        }
    check (ok, "true peak stays under the ceiling (+0.2 dB), sine at fs/4 and music, +12 dB", "dBTP " + log);

    // Off: exactly a delay of the look-ahead (the Mix 0 % null test covers the rest of the path).
    {
        anneal::TruePeakLimiter lim;
        lim.prepare (fs, 2);
        lim.setCeiling (false, 0.0f);
        auto x = music;
        float* ptrs[2] = { x[0].data(), x[1].data() };
        lim.process (ptrs, 2, n);
        const double err = delayedError (x, music, lim.getLatency());
        check (err == 0.0, "ceiling off = pure look-ahead delay", "latency " + juce::String (lim.getLatency()) + " samples, max err " + juce::String (err, 12));
    }
}

void testUndo()
{
    section ("24. Undo / Redo (3.0)");
    Rig r;
    auto* reveal = r.proc->apvts.getParameter (ids::reveal);
    auto plain = [reveal] { return reveal->convertFrom0to1 (reveal->getValue()); };

    r.set (ids::smooth, 10.0f);   // like automation: no gesture, not recorded
    const bool automationIgnored = ! r.proc->canUndo();

    reveal->beginChangeGesture();
    reveal->setValueNotifyingHost (reveal->convertTo0to1 (70.0f));
    reveal->endChangeGesture();
    r.proc->undo();
    const float undone = plain();
    r.proc->redo();
    const float redone = plain();

    r.proc->setCurrentProgram (4);   // Vocal air: one step
    const float afterPreset = plain();
    r.proc->undo();
    const float beforePreset = plain();
    check (automationIgnored && std::abs (undone - 40.0f) < 0.05f && std::abs (redone - 70.0f) < 0.05f
               && std::abs (afterPreset - 50.0f) < 0.05f && std::abs (beforePreset - 70.0f) < 0.05f,
           "undo/redo of an edit, a preset is one step, automation isn't recorded",
           "undo " + juce::String (undone, 1) + ", redo " + juce::String (redone, 1) + ", preset " + juce::String (afterPreset, 1)
               + " -> undo " + juce::String (beforePreset, 1));
}

void testFixes40()
{
    section ("26. 4.0 bug fixes");
    const double fs = 48000.0;

    // Freeze survives the host stopping and starting (reset()) and a latency mode change: the held
    // curve is a measurement, not audio. Before 4.0 a stop re-primed the curve from the first frame
    // after the restart, and the new latency mode's engine started from its own stale curve.
    {
        const int n = (int) (2 * fs);
        Channels ringy = testsig::silence (2, n);
        testsig::Pink pl (1), pr (2);
        for (int i = 0; i < n; ++i)
        {
            const float ring = 0.05f * (float) std::sin (2.0 * testsig::pi * 3150.0 * i / fs);
            ringy[0][(size_t) i] = 0.03f * pl.next() + ring;
            ringy[1][(size_t) i] = 0.03f * pr.next() + ring;
        }
        const auto plain = pinkNoise (fs, 1.0, 0.03f, 9);

        Rig r;
        r.setNeutral();
        r.set (ids::smooth, 100.0f);
        r.prepare (fs, 512);
        r.run (ringy, fixedBlocks (512));
        r.set (ids::freeze, 1.0f);
        r.run (pinkNoise (fs, 0.1, 0.03f, 4), fixedBlocks (512));
        const float held = bandGain (r.proc->getEngine(), 3150.0);
        r.proc->reset();   // the host stops and starts again
        r.run (plain, fixedBlocks (512));
        const float afterStop = bandGain (r.proc->getEngine(), 3150.0);
        r.set (ids::latency, 2.0f);   // Precise
        r.proc->applyLatencyModeIfChanged();
        r.run (plain, fixedBlocks (512));
        const float afterSwitch = bandGain (r.proc->getEngine(), 3150.0);
        check (held <= -3.0f && std::abs (afterStop - held) < 0.05f && std::abs (afterSwitch - held) < 0.05f,
               "Freeze holds across a host stop and a latency change",
               "held " + db (held) + ", after stop " + db (afterStop) + ", after Low -> Precise " + db (afterSwitch));
    }

    // Switching the Ceiling back on never measures audio from the last time it was on: a quiet
    // signal after a loud one passes untouched (before 4.0 the stale detector dipped it ~80 ms).
    {
        anneal::TruePeakLimiter lim;
        lim.prepare (fs, 2);
        const int part = (int) (0.2 * fs);
        auto loud = testsig::sines (fs, part, { { 997.0, 1.0 } });
        auto quiet = testsig::sines (fs, 2 * part, { { 997.0, 0.1 } });
        auto runLim = [&lim] (Channels& x) { float* p[2] = { x[0].data(), x[1].data() }; lim.process (p, 2, (int) x[0].size()); };
        lim.setCeiling (true, -1.0f);
        runLim (loud);
        lim.setCeiling (false, -1.0f);
        Channels first (2), second (2);
        for (int c = 0; c < 2; ++c)
        {
            first[(size_t) c].assign (quiet[(size_t) c].begin(), quiet[(size_t) c].begin() + part);
            second[(size_t) c].assign (quiet[(size_t) c].begin() + part, quiet[(size_t) c].end());
        }
        runLim (first);
        lim.takeGainReductionDb();
        lim.setCeiling (true, -1.0f);
        runLim (second);
        const float gr = lim.takeGainReductionDb();
        // The output now is the quiet input `latency` samples earlier, unchanged.
        double err = 0.0;
        const int lat = lim.getLatency();
        for (int c = 0; c < 2; ++c)
            for (int i = lat; i < part; ++i)
                err = std::max (err, (double) std::abs (second[(size_t) c][(size_t) i] - quiet[(size_t) c][(size_t) (part + i - lat)]));
        check (gr > -0.01f && err < 1.0e-6, "Ceiling switched on again: no dip from old audio",
               "reduction " + db (gr, 2) + ", max err vs delayed input " + juce::String (err, 9));
    }
}

//==============================================================================
// 4.0: Reference match and Paint.

// +gainDb high shelf at `hz` (RBJ, Q 0.707) on every channel.
Channels highShelf (const Channels& in, double fs, double hz, float gainDb)
{
    auto out = in;
    auto coeffs = juce::dsp::IIR::Coefficients<float>::makeHighShelf (fs, (float) hz, 0.7071f, juce::Decibels::decibelsToGain (gainDb));
    for (auto& ch : out)
    {
        juce::dsp::IIR::Filter<float> f (coeffs);
        for (auto& s : ch)
            s = f.processSample (s);
    }
    return out;
}

// Average power (dB) between lo and hi Hz over [from, from + n) of channel 0 (Hann, 8192, 50 %).
double bandPowerDb (const Channels& x, int from, int n, double fs, double lo, double hi)
{
    constexpr int order = 13, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> buf ((size_t) size * 2);
    double sum = 0.0;
    int frames = 0;
    for (int pos = from; pos + size <= from + n; pos += size / 2, ++frames)
    {
        for (int i = 0; i < size; ++i)
            buf[(size_t) i] = x[0][(size_t) (pos + i)] * (float) (0.5 - 0.5 * std::cos (2.0 * testsig::pi * i / size));
        std::fill (buf.begin() + size, buf.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform (buf.data());
        for (int k = (int) std::ceil (lo * size / fs); k <= (int) std::floor (hi * size / fs); ++k)
            sum += (double) buf[(size_t) k] * buf[(size_t) k];
    }
    return 10.0 * std::log10 (std::max (sum / std::max (1, frames), 1.0e-30));
}

// Treble (4.5-12 kHz) against body (200-2000 Hz), dB.
double tilt (const Channels& x, int from, int n, double fs)
{
    return bandPowerDb (x, from, n, fs, 4500.0, 12000.0) - bandPowerDb (x, from, n, fs, 200.0, 2000.0);
}

anneal::ReferenceCurve analyse (const Channels& x, double fs)
{
    anneal::ReferenceAnalyser a (fs);
    const float* ptrs[2] = { x[0].data(), x[1].data() };
    a.push (ptrs, 2, (int) x[0].size());
    return a.finish();
}

double maxDiff (const Channels& a, const Channels& b)
{
    double m = 0.0;
    for (size_t c = 0; c < a.size(); ++c)
        for (size_t i = 0; i < a[c].size(); ++i)
            m = std::max (m, (double) std::abs (a[c][i] - b[c][i]));
    return m;
}

// Paint 0 % over [loHz, hiHz] in one curve.
anneal::PaintCurves paintedOut (bool eq, double loHz, double hiHz)
{
    anneal::PaintCurves p;
    for (int i = 0; i < anneal::PaintCurves::numPoints; ++i)
    {
        const double z = anneal::refgrid::camAt (i, anneal::PaintCurves::numPoints);
        if (z >= anneal::erb::hzToCam (loHz) && z <= anneal::erb::hzToCam (hiHz))
            (eq ? p.eq : p.heat)[i] = 0.0f;
    }
    return p;
}

void testReference()
{
    section ("27. Reference match (4.0)");
    const double fs = 48000.0;
    const auto flat = pinkNoise (fs, 8.0, 0.03f, 41);
    const auto bright = highShelf (pinkNoise (fs, 8.0, 0.03f, 51), fs, 4000.0, 6.0f);   // the main: +6 dB above 4 kHz
    const auto reference = analyse (flat, fs);
    const int n = (int) bright[0].size();

    {
        // Off, Learn key with no key, and Match at Amount 0 are bit-identical to no reference at all.
        const auto music = testsig::musicLike (fs, 3.0);
        Rig plain;
        plain.prepare (fs, 512);
        const auto expected = plain.run (music, fixedBlocks (512));
        juce::String log;
        bool ok = reference.valid;
        for (auto [mode, amount] : { std::pair { 0.0f, 100.0f }, std::pair { 1.0f, 100.0f }, std::pair { 2.0f, 0.0f } })
        {
            Rig r;
            r.proc->setReference (reference, "flat");
            r.set (ids::refMode, mode);
            r.set (ids::refAmount, amount);
            r.prepare (fs, 512);
            const double d = maxDiff (r.run (music, fixedBlocks (512)), expected);
            ok = ok && d == 0.0;
            log << (mode == 0 ? "Off " : mode == 1 ? "Learn " : "Match 0 % ") << juce::String (d, 12) << "  ";
        }
        check (ok, "Reference Off / Learn / Amount 0 = identical output", log);
    }

    {
        // Match at 100 % moves the bright main at least 3 dB toward the flat reference, without overshooting.
        juce::String log;
        bool ok = true;
        for (int ms : { 0, 1 })
        {
            Rig r;
            r.setNeutral();
            r.set (ids::stereoMode, (float) ms);
            r.set (ids::refMode, 2.0f);
            r.set (ids::refAmount, 100.0f);
            r.proc->setReference (reference, "flat");
            r.prepare (fs, 512);
            const auto out = r.run (bright, fixedBlocks (512));
            const int lat = r.proc->getLatencySamples();
            const int from = n / 2, len = n / 2 - lat - 8192;
            const double tIn = tilt (bright, from, len, fs), tRef = tilt (flat, from, len, fs);
            const double tOut = tilt (out, from + lat, len, fs);
            const double moved = tIn - tOut;
            ok = ok && moved >= 3.0 && tOut >= tRef - 1.5;
            log << (ms ? "M/S" : "Stereo") << ": main " << juce::String (tIn - tRef, 1) << " dB brighter, moved "
                << juce::String (moved, 1) << " dB (EQ 8 kHz " << db (bandGain (r.proc->getEngine(), 8000.0))
                << ", 500 Hz " << db (bandGain (r.proc->getEngine(), 500.0)) << ")  ";
        }
        check (ok, "Match 100 %: +6 dB shelf moves >= 3 dB toward a flat reference", log);
    }

    {
        // Learn key through the processor's sidechain bus: the learned curve equals the key's own
        // analysis; then Match uses it. Choosing Learn key with a silent key keeps the reference.
        Rig r;
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        r.proc->setBusesLayout (l);
        r.setNeutral();
        r.set (ids::latency, 2.0f);   // Precise: bass detail close to the file analysis
        r.prepare (fs, 512);
        auto runWithKey = [&r] (const Channels& main, const Channels* key)
        {
            const int len = (int) main[0].size();
            Channels out = main;
            juce::AudioBuffer<float> buf (4, 512);
            juce::MidiBuffer midi;
            for (int pos = 0; pos + 512 <= len; pos += 512)
            {
                for (int c = 0; c < 2; ++c)
                {
                    buf.copyFrom (c, 0, main[(size_t) c].data() + pos, 512);
                    if (key != nullptr) buf.copyFrom (c + 2, 0, (*key)[(size_t) c].data() + pos, 512);
                    else                buf.clear (c + 2, 0, 512);
                }
                r.proc->processBlock (buf, midi);
                for (int c = 0; c < 2; ++c)
                    std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + 512, out[(size_t) c].data() + pos);
            }
            return out;
        };
        anneal::ReferenceCurve marker;
        marker.valid = true;
        std::fill (marker.db, marker.db + anneal::ReferenceCurve::numPoints, -3.0f);
        r.proc->setReference (marker, "marker");
        r.set (ids::refMode, 1.0f);   // Learn key, nothing playing on the key
        runWithKey (bright, nullptr);
        const auto kept = r.proc->getReference();
        const bool keptOld = kept.name == "marker" && kept.curve.db[500] == -3.0f;

        runWithKey (bright, &flat);   // the key plays: learn it
        r.set (ids::refMode, 0.0f);
        runWithKey (testsig::silence (2, 4096), nullptr);
        const auto learned = r.proc->getReference();
        // Same shape as the file analysis of the same noise (its overall offset may differ by how
        // the bass is resolved in the normalisation window).
        double worst = 0.0, offset = 0.0;
        int count = 0;
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < anneal::ReferenceCurve::numPoints; ++i)
            {
                const double hz = anneal::erb::camToHz (anneal::refgrid::camAt (i, anneal::ReferenceCurve::numPoints));
                if (hz < 200.0 || hz > 16000.0)
                    continue;
                const double d = learned.curve.db[i] - reference.db[i];
                if (pass == 0) { offset += d; ++count; }
                else           worst = std::max (worst, std::abs (d - offset / count));
            }

        r.set (ids::refMode, 2.0f);
        r.set (ids::refAmount, 100.0f);
        const auto out = runWithKey (bright, nullptr);
        const int lat = r.proc->getLatencySamples(), from = n / 2, len = n / 2 - lat - 8192;
        const double moved = tilt (bright, from, len, fs) - tilt (out, from + lat, len, fs);
        check (keptOld && learned.curve.valid && learned.name == "sidechain key" && worst < 1.0 && moved >= 3.0,
               "Learn key learns the key's spectrum, Match then uses it",
               juce::String (keptOld ? "silent key kept the old reference; " : "OLD REFERENCE LOST; ")
                   + "learned vs file analysis, 200 Hz-16 kHz: max " + db (worst, 2) + "; matched " + db (moved));
    }

    {
        // A reference file dropped on the display is decoded and analysed on another thread while
        // the audio thread keeps processing; it arrives without blocking it, and Reference
        // switches to Match. WAV, FLAC and AIFF (MP3 uses JUCE's decoder, like these).
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AnnealTests-reference");
        dir.createDirectory();
        const double fileRate = 44100.0;
        const auto fileAudio = pinkNoise (fileRate, 4.0, 0.05f, 61);
        const auto direct = analyse (fileAudio, fileRate);
        juce::String log;
        bool ok = direct.valid;
        for (auto* ext : { "wav", "flac", "aiff" })
        {
            const auto file = dir.getChildFile (juce::String ("ref.") + ext);
            file.deleteFile();
            {
                std::unique_ptr<juce::AudioFormat> format;
                if (juce::String (ext) == "wav")       format = std::make_unique<juce::WavAudioFormat>();
                else if (juce::String (ext) == "flac") format = std::make_unique<juce::FlacAudioFormat>();
                else                                   format = std::make_unique<juce::AiffAudioFormat>();
                auto stream = file.createOutputStream();
                std::unique_ptr<juce::AudioFormatWriter> writer (format->createWriterFor (stream.get(), fileRate, 2, 24, {}, 0));
                if (writer != nullptr)
                {
                    stream.release();
                    const float* ptrs[2] = { fileAudio[0].data(), fileAudio[1].data() };
                    writer->writeFromFloatArrays (ptrs, 2, (int) fileAudio[0].size());
                }
            }

            Rig r;
            r.prepare (fs, 256);
            const bool accepted = AnnealAudioProcessor::canLoadReferenceFile (file);
            r.proc->loadReferenceFile (file);
            const auto music = testsig::musicLike (fs, 0.5);
            int blocks = 0;
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            while ((r.proc->isAnalysingReference() || r.proc->apvts.getRawParameterValue (ids::refMode)->load() < 1.5f)
                   && juce::Time::getMillisecondCounterHiRes() - t0 < 30000.0)
            {
                r.run (music, fixedBlocks (256));   // the "audio thread" keeps going meanwhile
                blocks += (int) music[0].size() / 256;
                juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
            }
            r.run (music, fixedBlocks (256));
            const auto got = r.proc->getReference();
            double worst = 0.0;
            for (int i = 0; i < anneal::ReferenceCurve::numPoints; ++i)
                worst = std::max (worst, (double) std::abs (got.curve.db[i] - direct.db[i]));
            const bool fine = accepted && got.curve.valid && got.name == file.getFileName() && worst < 0.05
                              && r.proc->getLastAnalysisThread() != nullptr
                              && r.proc->getLastAnalysisThread() != juce::Thread::getCurrentThreadId()
                              && r.proc->getEngine().getModel().hasReference()
                              && r.proc->apvts.getRawParameterValue (ids::refMode)->load() > 1.5f;
            ok = ok && fine;
            log << ext << (fine ? "" : "!") << " (" << blocks << " blocks meanwhile, max " << juce::String (worst, 3) << " dB)  ";
        }

        // Not audio: nothing changes, and the reason is kept for the display.
        const auto junk = dir.getChildFile ("junk.wav");
        junk.replaceWithText ("not audio");
        Rig r;
        r.proc->setReference (direct, "kept");
        r.proc->loadReferenceFile (junk);
        for (int i = 0; i < 400 && r.proc->isAnalysingReference(); ++i)
            juce::Thread::sleep (5);
        const bool junkOk = r.proc->getReference().name == "kept" && r.proc->getReferenceError().contains ("junk.wav")
                            && AnnealAudioProcessor::canLoadReferenceFile (dir.getChildFile ("x.mp3")) == false   // doesn't exist
                            && ! AnnealAudioProcessor::canLoadReferenceFile (dir.getChildFile ("ref.txt"));
        dir.deleteRecursively();
        check (ok && junkOk, "dropped file analysed off the audio thread", log + (junkOk ? "; junk ignored" : "; JUNK NOT HANDLED"));
    }

    {
        // Freeze holds the match like the rest of the curve.
        Rig r;
        r.setNeutral();
        r.set (ids::refMode, 2.0f);
        r.set (ids::refAmount, 100.0f);
        r.proc->setReference (reference, "flat");
        r.prepare (fs, 512);
        r.run (bright, fixedBlocks (512));
        const float matched = bandGain (r.proc->getEngine(), 8000.0);
        r.set (ids::freeze, 1.0f);
        r.run (pinkNoise (fs, 0.2, 0.03f, 3), fixedBlocks (512));
        const float held0 = bandGain (r.proc->getEngine(), 8000.0);
        r.proc->setReference (analyse (bright, fs), "bright");   // now nothing to match ...
        r.run (pinkNoise (fs, 2.0, 0.03f, 5), fixedBlocks (512));
        const float held = bandGain (r.proc->getEngine(), 8000.0);
        check (matched <= -2.0f && std::abs (held - held0) < 0.05f, "Freeze holds the match",
               "8 kHz matched " + db (matched) + ", frozen " + db (held0) + " -> " + db (held) + " after the reference changed");
    }
}

void testPaint()
{
    section ("28. Paint (4.0)");
    const double fs = 48000.0;
    const auto music = testsig::musicLike (fs, 3.0);

    {
        // All 100 % is bit-identical to no paint, also after painting and erasing again.
        Rig plain, painted;
        plain.prepare (fs, 512);
        painted.proc->setPaint (paintedOut (true, 300.0, 900.0));
        painted.proc->setPaint (anneal::PaintCurves());
        painted.prepare (fs, 512);
        const double d = maxDiff (painted.run (music, fixedBlocks (512)), plain.run (music, fixedBlocks (512)));
        check (d == 0.0, "Paint at 100 % everywhere = identical output", "max diff " + juce::String (d, 12));
    }

    {
        // EQ paint 0 % over 2-5 kHz: the 3150 Hz Smooth cut is gone; outside, Smooth still works.
        const int n = (int) (3 * fs);
        Channels ringy = testsig::silence (2, n);
        testsig::Pink pl (1), pr (2);
        for (int i = 0; i < n; ++i)
        {
            const float ring = 0.05f * (float) (std::sin (2.0 * testsig::pi * 3150.0 * i / fs) + std::sin (2.0 * testsig::pi * 800.0 * i / fs));
            ringy[0][(size_t) i] = 0.03f * pl.next() + ring;
            ringy[1][(size_t) i] = 0.03f * pr.next() + ring;
        }
        float g3150[2] {}, g800[2] {};
        for (int painted : { 0, 1 })
        {
            Rig r;
            r.setNeutral();
            r.set (ids::smooth, 100.0f);
            if (painted)
                r.proc->setPaint (paintedOut (true, 2000.0, 5000.0));
            r.prepare (fs, 512);
            r.run (ringy, fixedBlocks (512));
            g3150[painted] = bandGain (r.proc->getEngine(), 3150.0);
            g800[painted] = bandGain (r.proc->getEngine(), 800.0);
        }
        check (g3150[0] <= -3.0f && std::abs (g3150[1]) < 0.05f && g800[1] <= -3.0f && std::abs (g800[1] - g800[0]) < 0.05f,
               "EQ paint 0 % over 2-5 kHz removes the 3150 Hz cut",
               "3150 Hz " + db (g3150[0]) + " -> " + db (g3150[1]) + ", 800 Hz " + db (g800[0]) + " -> " + db (g800[1]));
    }

    {
        // Heat paint 0 % over 2-5 kHz: a 1.5 kHz tone's 2nd and 3rd (3 / 4.5 kHz) are gone, its 4th
        // (6 kHz) stays. Listen: Heat only, so only harmonics are heard.
        const auto tone = testsig::sines (fs, (int) (2 * fs), { { 1500.0, 0.25 } });
        double h[2][3] {};
        for (int painted : { 0, 1 })
        {
            Rig r;
            r.setNeutral();
            r.set (ids::heat, 100.0f);
            r.set (ids::color, 50.0f);
            r.set (ids::listen, 3.0f);
            if (painted)
                r.proc->setPaint (paintedOut (false, 2000.0, 5000.0));
            r.prepare (fs, 512);
            const auto out = r.run (tone, fixedBlocks (512));
            const int from = (int) fs, len = (int) (0.8 * fs);
            int k = 0;
            for (double f : { 3000.0, 4500.0, 6000.0 })
                h[painted][k++] = toDb (testsig::toneAmplitude (out[0].data() + from, len, f, fs) / 0.25);
        }
        check (h[0][0] > -60.0 && h[0][1] > -60.0 && h[1][0] < -100.0 && h[1][1] < -100.0 && std::abs (h[1][2] - h[0][2]) < 0.5,
               "Heat paint 0 % over 2-5 kHz removes the harmonics there",
               "3 kHz " + db (h[0][0]) + " -> " + db (h[1][0]) + ", 4.5 kHz " + db (h[0][1]) + " -> " + db (h[1][1])
                   + ", 6 kHz " + db (h[0][2]) + " -> " + db (h[1][2]));
    }

    {
        // A stroke is one undo step; redo brings it back.
        Rig r;
        r.proc->beginPaintStroke();
        auto p = r.proc->getPaint();
        for (int i = 20; i < 30; ++i)
        {
            p.eq[i] = 0.25f;
            r.proc->setPaint (p);   // the drag paints point by point
        }
        r.proc->endPaintStroke();
        const bool painted = r.proc->getPaint().eq[25] == 0.25f;
        r.proc->undo();
        const bool undone = r.proc->getPaint().isDefault() && ! r.proc->canUndo();
        r.proc->redo();
        const bool redone = r.proc->getPaint() == p;
        check (painted && undone && redone, "a paint stroke is one undo step",
               juce::String (painted ? "painted" : "NOT PAINTED") + ", " + (undone ? "undone in one step" : "UNDO WRONG") + ", "
                   + (redone ? "redone" : "REDO WRONG"));
    }
}

void testReferenceState()
{
    section ("29. Reference and Paint in saved projects (4.0)");
    anneal::ReferenceCurve curve;
    curve.valid = true;
    for (int i = 0; i < anneal::ReferenceCurve::numPoints; ++i)
        curve.db[i] = -20.0f + 0.037f * (float) i;
    auto paint = paintedOut (true, 300.0, 600.0);
    paint.heat[40] = 0.375f;

    Rig a;
    a.proc->setReference (curve, juce::String::fromUTF8 ("Mix \xe2\x80\x94 final.wav"));
    a.proc->setPaint (paint);
    a.set (ids::refMode, 2.0f);
    a.set (ids::refAmount, 80.0f);
    juce::MemoryBlock blob;
    a.proc->getStateInformation (blob);

    Rig b;
    b.proc->setStateInformation (blob.getData(), (int) blob.getSize());
    const auto got = b.proc->getReference();
    float worst = 0.0f, worstPaint = 0.0f;
    for (int i = 0; i < anneal::ReferenceCurve::numPoints; ++i)
        worst = std::max (worst, std::abs (got.curve.db[i] - curve.db[i]));
    const auto gotPaint = b.proc->getPaint();
    for (int i = 0; i < anneal::PaintCurves::numPoints; ++i)
        worstPaint = std::max ({ worstPaint, std::abs (gotPaint.eq[i] - paint.eq[i]), std::abs (gotPaint.heat[i] - paint.heat[i]) });
    auto v = [] (Rig& r, const char* id) { return r.proc->apvts.getRawParameterValue (id)->load(); };
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    const bool versioned = xml != nullptr && (int) xml->getIntAttribute ("stateVersion") == 2;
    check (got.curve.valid && worst == 0.0f && got.name == juce::String::fromUTF8 ("Mix \xe2\x80\x94 final.wav") && worstPaint < 1.0e-4f
               && v (b, ids::refMode) == 2.0f && std::abs (v (b, ids::refAmount) - 80.0f) < 0.05f && versioned,
           "state round trip: reference, paint, mode and amount",
           "reference max diff " + juce::String (worst, 6) + ", paint " + juce::String (worstPaint, 6) + ", state "
               + juce::String ((int) blob.getSize()) + " bytes" + (versioned ? ", stateVersion 2" : ", WRONG VERSION"));

    // A 3.x project (stateVersion 1, none of the 4.0 data) into an instance with a reference, paint
    // and Match: the reference empty, no paint, Reference Off.
    for (auto id : { ids::refMode, ids::refAmount })
        if (auto* e = xml->getChildByAttribute ("id", id))
            xml->removeChildElement (e, true);
    while (auto* e = xml->getChildByName ("Reference")) xml->removeChildElement (e, true);
    while (auto* e = xml->getChildByName ("Paint"))     xml->removeChildElement (e, true);
    xml->setAttribute ("stateVersion", 1);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);
    b.proc->setStateInformation (old.getData(), (int) old.getSize());
    const bool cleared = ! b.proc->getReference().curve.valid && b.proc->getPaint().isDefault() && v (b, ids::refMode) == 0.0f
                         && std::abs (v (b, ids::refAmount) - 50.0f) < 0.05f;

    // A malformed reference (wrong size) loads as none; the saved state stays without it.
    auto bad = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    if (auto* e = bad->getChildByName ("Reference"))
        e->setAttribute ("db", "12.abc");
    juce::MemoryBlock badBlob;
    juce::AudioProcessor::copyXmlToBinary (*bad, badBlob);
    Rig c;
    c.proc->setStateInformation (badBlob.getData(), (int) badBlob.getSize());
    const bool badIgnored = ! c.proc->getReference().curve.valid && c.proc->getPaint() == gotPaint;
    juce::MemoryBlock resaved;
    c.proc->getStateInformation (resaved);
    auto resavedXml = juce::AudioProcessor::getXmlFromBinary (resaved.getData(), (int) resaved.getSize());
    const bool oneEach = resavedXml != nullptr && resavedXml->getChildByName ("Reference") == nullptr
                         && resavedXml->getNumChildElements() > 0;
    check (cleared && badIgnored && oneEach, "older projects load with no reference, no paint, Reference Off",
           juce::String (cleared ? "3.x state cleared it" : "3.x STATE KEPT 4.0 DATA") + ", " + (badIgnored ? "malformed reference ignored" : "MALFORMED ACCEPTED"));
}

void testOldProjects30()
{
    section ("25. Projects from before 3.0");
    Rig a;
    juce::MemoryBlock blob;
    a.proc->getStateInformation (blob);
    auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
    for (auto id : { ids::freeze, ids::scMode, ids::scAmount, ids::ceiling })
        if (auto* e = xml->getChildByAttribute ("id", id))
            xml->removeChildElement (e, true);
    juce::MemoryBlock old;
    juce::AudioProcessor::copyXmlToBinary (*xml, old);

    Rig b;
    b.set (ids::freeze, 1.0f);
    b.set (ids::scMode, 2.0f);
    b.set (ids::ceiling, 3.0f);
    b.proc->setStateInformation (old.getData(), (int) old.getSize());
    auto v = [&b] (const char* id) { return b.proc->apvts.getRawParameterValue (id)->load(); };
    check (v (ids::freeze) < 0.5f && v (ids::scMode) < 0.5f && v (ids::ceiling) < 0.5f,
           "older projects load with Freeze, Sidechain and Ceiling off", "freeze " + juce::String (v (ids::freeze))
               + ", sidechain " + juce::String (v (ids::scMode)) + ", ceiling " + juce::String (v (ids::ceiling)));
}

void testEditor40()
{
    section ("30. 4.0 editor: shortcuts, Paint on the display, dropped reference");
    Rig r;
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
    auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
    if (editor == nullptr)
    {
        check (false, "editor opens", "no AnnealEditor");
        return;
    }
    auto& content = editor->getContent();
    auto& screen = content.screen;

    {
        // Ctrl+Z undoes, Ctrl+Shift+Z and Ctrl+Y redo (Cmd on macOS); a plain Z does nothing.
        auto* reveal = r.proc->apvts.getParameter (ids::reveal);
        auto edit = [reveal] (float v)
        {
            reveal->beginChangeGesture();
            reveal->setValueNotifyingHost (reveal->convertTo0to1 (v));
            reveal->endChangeGesture();
        };
        auto value = [reveal] { return reveal->convertFrom0to1 (reveal->getValue()); };
        edit (55.0f);
        edit (70.0f);
        const auto cmd = juce::ModifierKeys::commandModifier, shift = juce::ModifierKeys::shiftModifier;
        const bool undone = editor->keyPressed (juce::KeyPress ('z', cmd, 0)) && std::abs (value() - 55.0f) < 0.05f;
        const bool redoneShiftZ = editor->keyPressed (juce::KeyPress ('Z', juce::ModifierKeys (cmd | shift), 0)) && std::abs (value() - 70.0f) < 0.05f;
        editor->keyPressed (juce::KeyPress ('z', cmd, 0));
        const bool redoneY = editor->keyPressed (juce::KeyPress ('y', cmd, 0)) && std::abs (value() - 70.0f) < 0.05f;
        const bool plainIgnored = ! editor->keyPressed (juce::KeyPress ('z')) && std::abs (value() - 70.0f) < 0.05f;
        check (undone && redoneShiftZ && redoneY && plainIgnored && editor->getWantsKeyboardFocus(),
               "Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y undo and redo",
               juce::String (undone ? "Ctrl+Z undid" : "CTRL+Z FAILED") + ", " + (redoneShiftZ ? "Ctrl+Shift+Z redid" : "CTRL+SHIFT+Z FAILED")
                   + ", " + (redoneY ? "Ctrl+Y redid" : "CTRL+Y FAILED") + ", " + (plainIgnored ? "plain Z ignored" : "PLAIN Z USED"));
    }

    // Paint, through the display's own mouse handling.
    const auto now = juce::Time::getCurrentTime();
    auto ev = [&screen, now] (juce::Point<float> at, juce::Point<float> down, bool dragged, juce::ModifierKeys extra = {}, int clicks = 1)
    {
        return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                 juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier | extra.getRawFlags()),
                                 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &screen, &screen, now, down, now, clicks, dragged);
    };
    auto stroke = [&] (juce::Point<float> from, juce::Point<float> to, juce::ModifierKeys extra = {})
    {
        screen.mouseDown (ev (from, from, false, extra));
        screen.mouseDrag (ev ({ (from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f }, from, true, extra));
        screen.mouseDrag (ev (to, from, true, extra));
        screen.mouseUp (ev (to, from, true, extra));
    };
    auto pointHz = [] (int i) { return anneal::erb::camToHz (anneal::refgrid::camAt (i, anneal::PaintCurves::numPoints)); };
    const auto eqLane = screen.getEqLane(), heatLane = screen.getHeatLane();

    {
        // Paint mode off: a drag in a lane paints nothing.
        content.setPaintMode (false);
        stroke ({ screen.xForHz (2000.0f), eqLane.getBottom() - 1.0f }, { screen.xForHz (5000.0f), eqLane.getBottom() - 1.0f });
        const bool untouched = r.proc->getPaint().isDefault();

        // Paint mode: a drag along the bottom of the EQ lane from 2 to 5 kHz paints 0 % there and
        // nothing else; it is one undo step; Alt-drag erases back to exactly 100 %.
        content.setPaintMode (true);
        while (r.proc->canUndo())   // start from an empty history (the shortcut test left steps)
            r.proc->undo();
        stroke ({ screen.xForHz (2000.0f), eqLane.getBottom() - 1.0f }, { screen.xForHz (5000.0f), eqLane.getBottom() - 1.0f });
        const auto painted = r.proc->getPaint();
        int zeros = 0, wrong = 0;
        for (int i = 0; i < anneal::PaintCurves::numPoints; ++i)
        {
            const double hz = pointHz (i);
            const bool inside = hz >= 2000.0 * 0.95 && hz <= 5000.0 * 1.05;   // half a point's spacing
            zeros += painted.eq[i] == 0.0f ? 1 : 0;
            if ((! inside && painted.eq[i] != 1.0f) || (hz >= 2100.0 && hz <= 4800.0 && painted.eq[i] != 0.0f) || painted.heat[i] != 1.0f)
                ++wrong;
        }
        const bool oneStep = r.proc->canUndo();
        r.proc->undo();
        const bool undone = r.proc->getPaint().isDefault() && ! r.proc->canUndo();
        r.proc->redo();
        const bool redone = r.proc->getPaint() == painted;

        // Heat lane, half height at 1 kHz: 50 %.
        stroke ({ screen.xForHz (1000.0f), heatLane.getCentreY() }, { screen.xForHz (1000.0f), heatLane.getCentreY() });
        const int i1k = juce::roundToInt ((anneal::erb::hzToCam (1000.0) - anneal::refgrid::zMin())
                                          / (anneal::refgrid::zMax() - anneal::refgrid::zMin()) * (anneal::PaintCurves::numPoints - 1));
        const float heatAt1k = r.proc->getPaint().heat[i1k];

        stroke ({ screen.xForHz (1800.0f), eqLane.getCentreY() }, { screen.xForHz (5500.0f), eqLane.getCentreY() },
                juce::ModifierKeys (juce::ModifierKeys::altModifier));
        const bool erased = anneal::PaintCurves::isFull (r.proc->getPaint().eq);
        content.tick (0.0f);
        const bool shown = screen.getPaint() == r.proc->getPaint();
        check (untouched && zeros >= 5 && wrong == 0 && oneStep && undone && redone && std::abs (heatAt1k - 0.5f) < 0.05f && erased && shown,
               "paint strokes on the display: one undo step each, Alt erases",
               juce::String (untouched ? "off: nothing painted" : "OFF PAINTED") + "; EQ 2-5 kHz: " + juce::String (zeros)
                   + " points at 0 %, " + juce::String (wrong) + " wrong; " + (undone && redone ? "undo/redo ok" : "UNDO WRONG")
                   + "; heat at 1 kHz " + juce::String (heatAt1k * 100.0f, 0) + " %; " + (erased ? "erased to 100 %" : "ERASE FAILED"));
    }

    {
        // Double-click resets the lane; with the click before it, that is a single undo step.
        const auto before = r.proc->getPaint();   // heat 50 % around 1 kHz
        const auto at = juce::Point<float> (screen.xForHz (6000.0f), heatLane.getY() + 4.0f);
        screen.mouseDown (ev (at, at, false));
        screen.mouseUp (ev (at, at, false));
        screen.mouseDown (ev (at, at, false, {}, 2));
        screen.mouseDoubleClick (ev (at, at, false, {}, 2));
        screen.mouseUp (ev (at, at, false, {}, 2));
        const bool reset = anneal::PaintCurves::isFull (r.proc->getPaint().heat);
        r.proc->undo();
        const bool oneStep = r.proc->getPaint() == before;
        r.proc->redo();
        check (reset && oneStep, "double-click resets a lane as one undo step",
               juce::String (reset ? "heat lane back to 100 %" : "NOT RESET") + ", " + (oneStep ? "one undo restores it" : "UNDO WRONG"));
        content.setPaintMode (false);
    }

    {
        // A file dropped on the display: only audio files are taken; it is analysed off the audio
        // thread and becomes the reference, Reference switches to Match and the display shows it.
        const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AnnealTests-drop");
        dir.createDirectory();
        const auto wav = dir.getChildFile ("Drop me.wav");
        wav.deleteFile();
        {
            const auto audio = pinkNoise (48000.0, 2.0, 0.05f, 77);
            auto stream = wav.createOutputStream();
            std::unique_ptr<juce::AudioFormatWriter> writer (juce::WavAudioFormat().createWriterFor (stream.get(), 48000.0, 2, 24, {}, 0));
            if (writer != nullptr)
            {
                stream.release();
                const float* ptrs[2] = { audio[0].data(), audio[1].data() };
                writer->writeFromFloatArrays (ptrs, 2, (int) audio[0].size());
            }
        }
        const auto text = dir.getChildFile ("notes.txt");
        text.replaceWithText ("not audio");
        const bool picky = screen.isInterestedInFileDrag ({ wav.getFullPathName() }) && ! screen.isInterestedInFileDrag ({ text.getFullPathName() });
        screen.fileDragEnter ({ wav.getFullPathName() }, 10, 10);
        const bool highlighted = screen.isDropHighlighted();
        screen.filesDropped ({ text.getFullPathName(), wav.getFullPathName() }, 10, 10);
        const auto t0 = juce::Time::getMillisecondCounterHiRes();
        r.prepare (48000.0, 512);
        const auto music = testsig::musicLike (48000.0, 0.25);
        while ((r.proc->isAnalysingReference() || r.proc->apvts.getRawParameterValue (ids::refMode)->load() < 1.5f)
               && juce::Time::getMillisecondCounterHiRes() - t0 < 30000.0)
        {
            r.run (music, fixedBlocks (512));
            juce::MessageManager::getInstance()->runDispatchLoopUntil (5);
        }
        content.tick (0.0f);
        const auto got = r.proc->getReference();
        const bool ok = picky && highlighted && ! screen.isDropHighlighted() && got.curve.valid && got.name == "Drop me.wav"
                        && r.proc->apvts.getRawParameterValue (ids::refMode)->load() > 1.5f;
        dir.deleteRecursively();
        check (ok && screen.hasReference() && screen.getReferenceName() == "Drop me.wav", "a dropped audio file becomes the reference (Match)",
               juce::String (picky ? "audio files only" : "ACCEPTS ANY FILE") + ", " + (highlighted ? "highlighted while dragged" : "NO HIGHLIGHT")
                   + ", reference \"" + got.name + "\", Reference " + juce::String (r.proc->apvts.getRawParameterValue (ids::refMode)->load(), 0)
                   + ", display shows \"" + screen.getReferenceName() + "\"");
    }

    {
        // While Learn key learns, the display follows the learned reference from the editor's own
        // timer (before 4.0 shipped it waited for the processor's timer, so a host that runs no
        // message loop for the processor's timer showed "waiting for the sidechain key").
        Rig k;
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        k.proc->setBusesLayout (l);
        k.set (ids::refMode, 1.0f);
        k.prepare (48000.0, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ked (k.proc->createEditor());
        auto& kc = dynamic_cast<AnnealEditor&> (*ked).getContent();
        const auto keySignal = pinkNoise (48000.0, 1.0, 0.05f, 5);
        juce::AudioBuffer<float> buf (4, 512);
        juce::MidiBuffer midi;
        for (int pos = 0; pos + 512 <= (int) keySignal[0].size(); pos += 512)
        {
            buf.clear();
            for (int c = 0; c < 2; ++c)
                buf.copyFrom (c + 2, 0, keySignal[(size_t) c].data() + pos, 512);
            k.proc->processBlock (buf, midi);
        }
        kc.tick (0.0f);
        check (kc.screen.hasReference() && kc.screen.getReferenceName() == "sidechain key",
               "the display follows Learn key without the processor's timer",
               kc.screen.hasReference() ? "shows \"" + kc.screen.getReferenceName() + "\"" : juce::String ("NOTHING SHOWN"));
    }
}

//==============================================================================
// 5.0: SHAPE (Punch, Width, Mono bass) and user presets.
void testPunch50()
{
    section ("33. 5.0 Punch");
    const double fs = 48000.0;
    std::vector<int> onsets;
    const auto plucks = pluckTrain (fs, 4.0, 2000.0, onsets);
    const int attackLen = (int) (0.015 * fs);
    auto measure = [&] (float punch, float rangeLo, double& att, double& body)
    {
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2, anneal::SpectralEngine::low);
        auto p = neutral();
        p.punch = punch;
        p.rangeLoHz = rangeLo;
        e->setParameters (p);
        const auto out = runEngine (*e, plucks, fixedBlocks (256));
        const int L = e->getLatencySamples();
        att = windowGainDb (out, plucks, onsets, L, 0, attackLen, 2000.0, fs);
        body = windowGainDb (out, plucks, onsets, L, (int) (0.08 * fs), (int) (0.08 * fs), 2000.0, fs);
    };
    double att[3] {}, body[3] {}, attRange = 0.0, bodyRange = 0.0;
    measure (0.0f, 20.0f, att[0], body[0]);
    measure (1.0f, 20.0f, att[1], body[1]);
    measure (-1.0f, 20.0f, att[2], body[2]);
    measure (1.0f, 5000.0f, attRange, bodyRange);
    info ("Low, 2 kHz plucks: attack (first 15 ms) Punch off " + db (att[0], 2) + ", +100 % " + db (att[1], 2) + ", -100 % "
          + db (att[2], 2) + ";  body (80-160 ms) " + db (body[0], 2) + " / " + db (body[1], 2) + " / " + db (body[2], 2));
    check (att[1] - att[0] >= 3.0 && att[0] - att[2] >= 3.0, "Punch +100 % makes attacks louder, -100 % softer",
           "attack " + db (att[1] - att[0], 2) + " / " + db (att[2] - att[0], 2));
    check (std::abs (body[1] - body[0]) <= 1.0 && std::abs (body[2] - body[0]) <= 1.0, "Punch leaves the body of the sound alone",
           "body " + db (body[1] - body[0], 2) + " / " + db (body[2] - body[0], 2));
    check (std::abs (attRange - att[0]) <= 0.5, "Punch works inside Range only (Range 5-20 kHz, 2 kHz plucks untouched)",
           "attack " + db (attRange - att[0], 2));
}

void testWidth50()
{
    section ("34. 5.0 Width and Mono bass");
    const double fs = 48000.0;
    const int n = (int) (2.0 * fs);
    const double twoPi = 6.283185307179586;
    // Centre (Mid) tones at 300 Hz and 3 kHz, side tones at 60 Hz and 1 kHz, each 0.1.
    Channels in (2, std::vector<float> ((size_t) n));
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double m = 0.1 * std::sin (twoPi * 300.0 * t) + 0.1 * std::sin (twoPi * 3000.0 * t);
        const double s = 0.1 * std::sin (twoPi * 60.0 * t) + 0.1 * std::sin (twoPi * 1000.0 * t);
        in[0][(size_t) i] = (float) (m + s);
        in[1][(size_t) i] = (float) (m - s);
    }
    struct Image { double mid300, mid3k, side60, side1k; };
    auto measure = [&] (int mode, float width, float monoHz)
    {
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2, mode);
        auto p = neutral();
        p.width = width;
        p.monoHz = monoHz;
        e->setParameters (p);
        const auto out = runEngine (*e, in, fixedBlocks (512));
        const int L = e->getLatencySamples(), from = L + n / 4, len = n / 2;
        std::vector<float> mid ((size_t) len), side ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            const float l = out[0][(size_t) (from + i)], r = out[1][(size_t) (from + i)];
            mid[(size_t) i] = 0.5f * (l + r);
            side[(size_t) i] = 0.5f * (l - r);
        }
        auto a = [&] (const std::vector<float>& x, double hz) { return toDb (testsig::toneAmplitude (x.data(), len, hz, fs) / 0.1); };
        return Image { a (mid, 300.0), a (mid, 3000.0), a (side, 60.0), a (side, 1000.0) };
    };
    auto midKept = [] (const Image& im) { return std::abs (im.mid300) <= 0.05 && std::abs (im.mid3k) <= 0.05; };
    auto show = [] (const Image& im)
    {
        return "mid 300 Hz " + db (im.mid300, 2) + ", 3 kHz " + db (im.mid3k, 2) + "; side 60 Hz " + db (im.side60, 1) + ", 1 kHz " + db (im.side1k, 2);
    };
    const auto w100 = measure (anneal::SpectralEngine::low, 1.0f, 0.0f);
    const auto w0 = measure (anneal::SpectralEngine::low, 0.0f, 0.0f);
    const auto w200 = measure (anneal::SpectralEngine::low, 2.0f, 0.0f);
    const auto mono = measure (anneal::SpectralEngine::low, 1.0f, 200.0f);
    const auto monoP = measure (anneal::SpectralEngine::precise, 1.0f, 120.0f);
    check (midKept (w100) && std::abs (w100.side60) <= 0.05 && std::abs (w100.side1k) <= 0.05, "Width 100 %: the image as it was", show (w100));
    check (midKept (w0) && w0.side60 < -90.0 && w0.side1k < -90.0, "Width 0 %: mono, the centre unchanged", show (w0));
    check (midKept (w200) && std::abs (w200.side1k - 6.02) <= 0.1 && std::abs (w200.side60 - 6.02) <= 0.2,
           "Width 200 %: sides +6 dB, the centre unchanged", show (w200));
    check (midKept (mono) && mono.side60 < -35.0 && std::abs (mono.side1k) <= 0.1, "Mono bass 200 Hz (Low): 60 Hz sides gone, 1 kHz kept", show (mono));
    check (midKept (monoP) && monoP.side60 < -40.0 && std::abs (monoP.side1k) <= 0.1, "Mono bass 120 Hz (Precise): 60 Hz sides gone, 1 kHz kept", show (monoP));

    // Width 0 % with Heat, Reveal and Smooth working (the defaults): still mono, harmonics included.
    {
        Channels wide (2, std::vector<float> ((size_t) n));
        for (int i = 0; i < n; ++i)
        {
            wide[0][(size_t) i] = 0.2f * (float) std::sin (twoPi * 500.0 * i / fs);
            wide[1][(size_t) i] = 0.2f * (float) std::sin (twoPi * 700.0 * i / fs);
        }
        double sideDb[2] {};
        for (int heatOn = 0; heatOn < 2; ++heatOn)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2, anneal::SpectralEngine::low);
            anneal::EngineParams p;   // defaults
            p.reveal = 0.4f;
            p.smooth = 0.4f;
            p.heat = heatOn ? 0.3f : 0.0f;
            p.width = 0.0f;
            e->setParameters (p);
            const auto out = runEngine (*e, wide, fixedBlocks (512));
            double side = 0.0, level = 0.0;
            for (size_t i = (size_t) n / 2; i < (size_t) n; ++i)
            {
                side = std::max (side, (double) std::abs (out[0][i] - out[1][i]) * 0.5);
                level = std::max (level, (double) std::abs (out[0][i]));
            }
            sideDb[heatOn] = toDb ((side + 1.0e-12) / (level + 1.0e-12));
        }
        check (sideDb[0] < -90.0 && sideDb[1] < -90.0, "Width 0 % with Reveal, Smooth and Heat working: still mono",
               "sides " + db (sideDb[0], 1) + " (Heat 0), " + db (sideDb[1], 1) + " (Heat 30 %) under the signal");
    }

    // A mono track has no sides: Width does nothing to it.
    {
        Channels one (1, in[0]);
        auto e0 = std::make_unique<anneal::SpectralEngine>();
        auto e1 = std::make_unique<anneal::SpectralEngine>();
        e0->prepare (fs, 1, anneal::SpectralEngine::low);
        e1->prepare (fs, 1, anneal::SpectralEngine::low);
        auto p = neutral();
        e0->setParameters (p);
        p.width = 0.0f;
        p.monoHz = 300.0f;
        e1->setParameters (p);
        const auto a = runEngine (*e0, one, fixedBlocks (512)), b = runEngine (*e1, one, fixedBlocks (512));
        double m = 0.0;
        for (size_t i = 0; i < a[0].size(); ++i)
            m = std::max (m, (double) std::abs (a[0][i] - b[0][i]));
        check (m == 0.0, "mono track: Width and Mono bass change nothing", "max diff " + juce::String (m, 12));
    }
}

void testShapeParams50()
{
    section ("35. 5.0 SHAPE parameters, older projects, factory presets");
    Rig r;
    auto* punch = r.proc->apvts.getParameter (ids::punch);
    auto* width = r.proc->apvts.getParameter (ids::width);
    auto* mono = r.proc->apvts.getParameter (ids::monoBass);
    auto plain = [] (juce::RangedAudioParameter* p, const juce::String& t) { return p->convertFrom0to1 (p->getValueForText (t)); };
    const juce::String minus40 = juce::String::fromUTF8 ("\xe2\x88\x92") + "40 %";
    const float a = plain (punch, minus40), b = plain (punch, "+25"), c = plain (width, "mono"), d = plain (width, "150"),
                e = plain (mono, "off"), f = plain (mono, "150 Hz");
    const bool texts = punch->getText (punch->convertTo0to1 (-40.0f), 32) == minus40 && punch->getText (0.5f, 32) == "Off"
                       && width->getText (0.0f, 32) == "Mono" && mono->getText (0.0f, 32) == "Off";
    check (std::abs (a + 40.0f) < 0.05f && std::abs (b - 25.0f) < 0.05f && c < 0.05f && std::abs (d - 150.0f) < 0.05f
               && e < anneal::monoBassMinHz && std::abs (f - 150.0f) < 0.6f && texts,
           "Punch, Width and Mono bass: typed values and read-outs",
           juce::String (a, 1) + ", " + juce::String (b, 1) + ", " + juce::String (c, 1) + ", " + juce::String (d, 1) + ", "
               + juce::String (e, 1) + ", " + juce::String (f, 1) + (texts ? "" : " (read-outs differ)"));

    // Projects from before 5.0 open with Punch off, Width 100 % and Mono bass off, whatever this
    // instance had.
    {
        juce::MemoryBlock blob;
        r.proc->getStateInformation (blob);
        auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
        for (auto id : { ids::punch, ids::width, ids::monoBass })
            if (auto* el = xml->getChildByAttribute ("id", id))
                xml->removeChildElement (el, true);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        Rig o;
        o.set (ids::punch, 60.0f);
        o.set (ids::width, 0.0f);
        o.set (ids::monoBass, 200.0f);
        o.proc->setStateInformation (old.getData(), (int) old.getSize());
        auto v = [&o] (const char* id) { return o.proc->apvts.getRawParameterValue (id)->load(); };
        check (std::abs (v (ids::punch)) < 0.05f && std::abs (v (ids::width) - 100.0f) < 0.05f && v (ids::monoBass) < anneal::monoBassMinHz,
               "older projects open with Punch off, Width 100 %, Mono bass off",
               juce::String (v (ids::punch), 1) + ", " + juce::String (v (ids::width), 1) + ", " + juce::String (v (ids::monoBass), 1));
    }

    // Width 0 % through the whole processor (default settings, Reveal 15 %): the output is mono.
    {
        Rig w;
        w.set (ids::reveal, 15.0f);
        w.set (ids::width, 0.0f);
        w.prepare (48000.0, 512);
        const int n = 48000;
        Channels wide (2, std::vector<float> ((size_t) n));
        for (int i = 0; i < n; ++i)
        {
            wide[0][(size_t) i] = 0.2f * (float) std::sin (6.283185307179586 * 500.0 * i / 48000.0);
            wide[1][(size_t) i] = 0.2f * (float) std::sin (6.283185307179586 * 700.0 * i / 48000.0);
        }
        const auto out = w.run (wide, fixedBlocks (512));
        double side = 0.0, level = 0.0;
        for (size_t i = (size_t) n / 2; i < (size_t) n; ++i)
        {
            side = std::max (side, (double) std::abs (out[0][i] - out[1][i]) * 0.5);
            level = std::max (level, (double) std::abs (out[0][i]));
        }
        const double sideDb = toDb ((side + 1.0e-12) / (level + 1.0e-12));
        check (sideDb < -90.0, "Width 0 % through the processor: mono", "sides " + db (sideDb, 1) + " under the signal");
    }

    // Factory presets set the new controls to neutral, so every preset sounds as it did.
    {
        Rig q;
        q.set (ids::punch, 50.0f);
        q.set (ids::width, 180.0f);
        q.set (ids::monoBass, 150.0f);
        q.proc->setCurrentProgram (4);
        auto v = [&q] (const char* id) { return q.proc->apvts.getRawParameterValue (id)->load(); };
        check (std::abs (v (ids::punch)) < 0.05f && std::abs (v (ids::width) - 100.0f) < 0.05f && v (ids::monoBass) < anneal::monoBassMinHz
                   && q.proc->getPresetDisplayName() == "Vocal air",
               "factory presets set Punch off, Width 100 %, Mono bass off", q.proc->getPresetDisplayName());
    }
}

void testUserPresets50()
{
    section ("36. 5.0 user presets");
    const auto folder = juce::File::createTempFile ("anneal-presets");
    folder.createDirectory();
    Rig r;
    r.proc->setUserPresetFolder (folder, false);
    auto v = [&r] (const char* id) { return r.proc->apvts.getRawParameterValue (id)->load(); };

    r.set (ids::reveal, 63.0f);
    r.set (ids::heat, 72.0f);
    r.set (ids::punch, 35.0f);
    r.set (ids::width, 140.0f);
    r.set (ids::monoBass, 110.0f);
    const bool saved = r.proc->saveUserPreset ("My: bus/glue?");
    const auto names = r.proc->getUserPresetNames();
    check (saved && names.size() == 1 && names[0] == "My bus glue" && folder.getChildFile ("My bus glue.annealpreset").existsAsFile()
               && r.proc->getPresetDisplayName() == "My bus glue",
           "save: a file named after the preset, the bar shows its name",
           names.joinIntoString (", ") + " / shows " + r.proc->getPresetDisplayName());

    // Edited, then loaded again: every value returns, the "*" goes, and loading is one undo step.
    r.set (ids::reveal, 10.0f);
    r.set (ids::width, 60.0f);
    const auto edited = r.proc->getPresetDisplayName();
    const bool loaded = r.proc->loadUserPreset ("My bus glue");
    const bool back = std::abs (v (ids::reveal) - 63.0f) < 0.1f && std::abs (v (ids::heat) - 72.0f) < 0.1f
                      && std::abs (v (ids::punch) - 35.0f) < 0.1f && std::abs (v (ids::width) - 140.0f) < 0.1f
                      && std::abs (v (ids::monoBass) - 110.0f) < 0.6f;
    const auto shown = r.proc->getPresetDisplayName();
    r.proc->undo();
    const bool undone = std::abs (v (ids::reveal) - 10.0f) < 0.1f && std::abs (v (ids::width) - 60.0f) < 0.1f;
    check (edited == "My bus glue *" && loaded && back && shown == "My bus glue" && undone,
           "load restores every value and the name; one undo step",
           "edited shows " + edited + ", loaded shows " + shown + (back ? "" : ", values differ") + (undone ? "" : ", undo failed"));

    // A project remembers the user preset it was set from.
    {
        r.proc->loadUserPreset ("My bus glue");
        juce::MemoryBlock blob;
        r.proc->getStateInformation (blob);
        Rig b;
        b.proc->setUserPresetFolder (folder, false);
        b.proc->setStateInformation (blob.getData(), (int) blob.getSize());
        check (b.proc->getPresetDisplayName() == "My bus glue" && b.proc->getUserPresetName() == "My bus glue",
               "a project remembers the user preset it was set from", b.proc->getPresetDisplayName());
    }

    // A broken file loads nothing; a preset without the newer values leaves those as they are.
    folder.getChildFile ("Broken.annealpreset").replaceWithText ("not a preset");
    folder.getChildFile ("Older.annealpreset").replaceWithText ("<AnnealPreset version=\"1\"><PARAM id=\"reveal\" value=\"22\"/></AnnealPreset>");
    r.set (ids::heat, 50.0f);
    const bool broken = ! r.proc->loadUserPreset ("Broken") && std::abs (v (ids::heat) - 50.0f) < 0.1f;
    const bool older = r.proc->loadUserPreset ("Older") && std::abs (v (ids::reveal) - 22.0f) < 0.1f && std::abs (v (ids::heat) - 50.0f) < 0.1f;
    check (broken && older, "a broken file loads nothing; values a preset lacks stay as they are",
           juce::String (broken ? "broken ok" : "broken LOADED") + ", " + (older ? "older ok" : "older wrong"));

    const bool clean = anneal::presets::sanitiseName ("  a/b:c*?  ") == "a b c" && anneal::presets::sanitiseName ("con") == "con preset"
                       && anneal::presets::sanitiseName ("///").isEmpty() && ! r.proc->saveUserPreset ("  //  ");
    check (clean, "names: path and wildcard characters dropped, reserved names made safe, empty names refused",
           "'" + anneal::presets::sanitiseName ("  a/b:c*?  ") + "', '" + anneal::presets::sanitiseName ("con") + "'");

    // The editor's preset bar: factory presets (the host's programs) first, then the user's.
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
        auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
        bool wired = editor != nullptr;
        const int nf = r.proc->getNumPrograms();
        if (wired)
        {
            auto& bar = editor->getContent().presets;
            wired = nf == anneal::presets::count() && bar.getCount() == nf + 3 && bar.getFactoryCount() == nf
                    && bar.getName (nf) == "Broken" && bar.getName (nf + 2) == "Older" && bar.getChosen() == nf + 2
                    && bar.onSaveAs != nullptr && bar.onDelete != nullptr && bar.onOpenFolder != nullptr;
            bar.onChoose (nf + 1);   // "My bus glue", chosen in the bar
        }
        check (wired && r.proc->getPresetDisplayName() == "My bus glue", "the preset bar lists factory then user presets and loads them",
               r.proc->getPresetDisplayName());
    }

    const bool deleted = r.proc->deleteUserPreset ("My bus glue") && ! folder.getChildFile ("My bus glue.annealpreset").exists()
                         && ! r.proc->getUserPresetNames().contains ("My bus glue") && r.proc->getPresetDisplayName() != "My bus glue"
                         && std::abs (v (ids::reveal) - 63.0f) < 0.1f;
    check (deleted, "delete removes the file; the settings stay", "now shows " + r.proc->getPresetDisplayName());
    folder.deleteRecursively();
}

//==============================================================================
// 5.1: Zero latency.
void testZeroLatency51()
{
    section ("37. 5.1 Zero latency");
    const double fs = 48000.0;
    const double twoPi = 6.283185307179586;

    // The filter design itself: a smooth -9 dB dip at 3 kHz comes out as asked, minimum phase.
    {
        anneal::ZeroLatencyFilter z;
        z.prepare (fs, 2, 1024, 256);
        std::vector<float> curve (513);
        auto target = [] (double f) { return -9.0 * std::exp (-0.5 * std::pow (std::log2 (std::max (f, 1.0) / 3000.0) / 0.3, 2.0)); };
        for (int k = 0; k < 513; ++k)
            curve[(size_t) k] = (float) std::pow (10.0, target (k * fs / 1024.0) / 20.0);
        std::vector<float> h ((size_t) z.getTaps());
        z.design (curve.data(), 513, 1.0f, 0.0f, h.data());
        double worst = 0.0, early = 0.0, total = 0.0;
        for (double f : { 200.0, 1000.0, 2000.0, 2500.0, 3000.0, 3500.0, 4000.0, 8000.0, 16000.0 })
        {
            double re = 0.0, im = 0.0;
            for (size_t n = 0; n < h.size(); ++n)
            {
                re += h[n] * std::cos (twoPi * f * (double) n / fs);
                im -= h[n] * std::sin (twoPi * f * (double) n / fs);
            }
            worst = std::max (worst, std::abs (10.0 * std::log10 (re * re + im * im) - target (f)));
        }
        for (size_t n = 0; n < h.size(); ++n)
        {
            total += (double) h[n] * h[n];
            if (n < h.size() / 4)
                early += (double) h[n] * h[n];
        }
        check (worst < 0.3 && early / total > 0.99, "zero-latency filter: the curve as asked, energy at the start (minimum phase)",
               "worst error " + db (worst, 2) + ", energy in the first quarter " + juce::String (100.0 * early / total, 2) + " %");
    }

    // Reported and applied latency 0; with neutral settings the output is the input.
    {
        Rig r;
        r.setNeutral();
        r.set (ids::zeroLatency, 1.0f);
        r.prepare (fs, 512);
        auto impulse = testsig::silence (2, 16384);
        impulse[0][1000] = impulse[1][1000] = 1.0f;
        const auto out = r.run (impulse, fixedBlocks (512));
        const auto music = testsig::musicLike (fs, 2.0);
        const auto mout = r.run (music, randomBlocks (1, 700, 3));
        double err = 0.0;
        for (size_t c = 0; c < 2; ++c)
            for (size_t i = 0; i < music[0].size(); ++i)
                err = std::max (err, (double) std::abs (mout[c][i] - music[c][i]));
        check (r.proc->isZeroLatency() && r.proc->getLatencySamples() == 0 && r.proc->getLatencyMs() == 0.0 && peakIndex (out) == 1000
                   && std::abs (out[0][1000] - 1.0f) < 1.0e-4f && err < 1.0e-4,
               "Zero latency: reports 0, an impulse comes out where it went in, neutral settings pass the input",
               "latency " + juce::String (r.proc->getLatencySamples()) + ", impulse at " + juce::String (peakIndex (out))
                   + ", max error " + juce::String (err, 7));
    }

    // Smooth cuts a sustained ring as it does in Low.
    {
        const int n = (int) (6.0 * fs);
        auto bed = pinkNoise (fs, 6.0, 0.03f, 5);
        for (int i = 0; i < n; ++i)
        {
            const float ring = 0.05f * (float) std::sin (twoPi * 3150.0 * i / fs);
            bed[0][(size_t) i] += ring;
            bed[1][(size_t) i] += ring;
        }
        double cut[2] {};
        for (int z = 0; z < 2; ++z)
        {
            Rig r;
            r.setNeutral();
            r.set (ids::smooth, 100.0f);
            r.set (ids::response, 40.0f);
            r.set (ids::zeroLatency, (float) z);
            r.prepare (fs, 512);
            const auto out = r.run (bed, fixedBlocks (512));
            const int L = r.proc->getLatencySamples();
            cut[z] = toDb (testsig::toneAmplitude (out[0].data() + n / 2 + L, n / 3, 3150.0, fs)
                           / testsig::toneAmplitude (bed[0].data() + n / 2, n / 3, 3150.0, fs));
        }
        check (cut[1] < -6.0 && std::abs (cut[1] - cut[0]) < 2.0, "Zero latency: Smooth cuts a ring as in Low",
               "3150 Hz: Low " + db (cut[0], 1) + ", Zero " + db (cut[1], 1));
    }

    // Heat rests: no harmonics at zero latency.
    {
        Rig r;
        r.setNeutral();
        r.set (ids::heat, 100.0f);
        r.set (ids::zeroLatency, 1.0f);
        r.prepare (fs, 512);
        const auto tone = testsig::sines (fs, (int) fs, { { 1000.0, 0.125 } });
        const auto out = r.run (tone, fixedBlocks (512));
        const double h2 = toDb (testsig::toneAmplitude (out[0].data() + 24000, 19200, 2000.0, fs) / 0.125);
        check (h2 < -90.0, "Zero latency: Heat rests (no harmonics)", "2nd harmonic " + db (h2, 1));
    }

    // Width and Mono bass, through the Side filter.
    {
        const int n = (int) (2.0 * fs);
        Channels in (2, std::vector<float> ((size_t) n));
        for (int i = 0; i < n; ++i)
        {
            const double t = i / fs;
            const double m = 0.1 * std::sin (twoPi * 300.0 * t) + 0.1 * std::sin (twoPi * 3000.0 * t);
            const double s = 0.1 * std::sin (twoPi * 60.0 * t) + 0.1 * std::sin (twoPi * 1000.0 * t);
            in[0][(size_t) i] = (float) (m + s);
            in[1][(size_t) i] = (float) (m - s);
        }
        struct Image { double mid300, mid3k, side60, side1k; };
        auto measure = [&] (float width, float monoHz)
        {
            Rig r;
            r.setNeutral();
            r.set (ids::zeroLatency, 1.0f);
            r.set (ids::width, width);
            r.set (ids::monoBass, monoHz);
            r.prepare (fs, 512);
            const auto out = r.run (in, fixedBlocks (512));
            const int from = n / 2, len = n / 3;
            std::vector<float> mid ((size_t) len), side ((size_t) len);
            for (int i = 0; i < len; ++i)
            {
                const float l = out[0][(size_t) (from + i)], rr = out[1][(size_t) (from + i)];
                mid[(size_t) i] = 0.5f * (l + rr);
                side[(size_t) i] = 0.5f * (l - rr);
            }
            auto a = [&] (const std::vector<float>& x, double hz) { return toDb (testsig::toneAmplitude (x.data(), len, hz, fs) / 0.1); };
            return Image { a (mid, 300.0), a (mid, 3000.0), a (side, 60.0), a (side, 1000.0) };
        };
        const auto w0 = measure (0.0f, 0.0f), w200 = measure (200.0f, 0.0f), mono = measure (100.0f, 200.0f);
        auto midOk = [] (const Image& im) { return std::abs (im.mid300) < 0.1 && std::abs (im.mid3k) < 0.1; };
        check (midOk (w0) && w0.side60 < -90.0 && w0.side1k < -90.0 && midOk (w200) && std::abs (w200.side1k - 6.02) < 0.2
                   && midOk (mono) && mono.side60 < -30.0 && std::abs (mono.side1k) < 0.3,
               "Zero latency: Width and Mono bass work, the centre unchanged",
               "Width 0 %: sides " + db (w0.side1k, 0) + "; 200 %: 1 kHz sides " + db (w200.side1k, 2) + "; Mono bass 200 Hz: 60 Hz sides "
                   + db (mono.side60, 1) + ", 1 kHz " + db (mono.side1k, 2) + "; centre within " + db (std::max (std::abs (mono.mid300), std::abs (w200.mid3k)), 2));
    }

    // Switching between Zero and the latency modes (and Mid/Side at Zero) while playing.
    {
        Rig r;
        r.prepare (fs, 512);
        const auto music = testsig::musicLike (fs, 1.0);
        juce::String log;
        bool ok = true;
        const std::pair<int, int> steps[] = { { 0, 0 }, { 1, 0 }, { 0, 2 }, { 1, 2 }, { 0, 1 }, { 1, 0 } };
        int step = 0;
        for (auto [z, m] : steps)
        {
            r.set (ids::latency, (float) m);
            r.set (ids::zeroLatency, (float) z);
            r.set (ids::stereoMode, step++ == 3 ? 1.0f : 0.0f);
            r.proc->applyLatencyModeIfChanged();
            const auto out = r.run (music, randomBlocks (16, 1024, (unsigned) step));
            const int lat = r.proc->getLatencySamples();
            const int expected = z ? 0 : anneal::SpectralEngine::fftSizeFor (fs, m) + r.proc->getCeilingLatency();
            ok = ok && lat == expected && allFinite (out) && maxAbs (out) < 4.0;
            log << (z ? "Zero" : m == 0 ? "Low" : m == 1 ? "Normal" : "Precise") << " " << lat << "  ";
        }
        check (ok, "switching Zero and the latency modes while playing (and Mid/Side at Zero): right latency, clean", log);
    }

    // The Ceiling at zero latency holds sample peaks (no look-ahead).
    {
        Rig r;
        r.setNeutral();
        r.set (ids::zeroLatency, 1.0f);
        r.set (ids::ceiling, 3.0f);   // -1.0 dB
        r.set (ids::output, 12.0f);
        r.prepare (fs, 512);
        const auto tone = testsig::sines (fs, (int) fs, { { 997.0, 0.5 } });
        const auto out = r.run (tone, fixedBlocks (512));
        const double peakDb = toDb (maxAbs (out));
        check (peakDb <= -0.99, "Zero latency Ceiling holds sample peaks (-1 dB, the signal 6 dB over 0 dBFS)", "peak " + db (peakDb, 2));
    }

    // Projects from before 5.1 open with Zero latency off.
    {
        Rig a;
        juce::MemoryBlock blob;
        a.proc->getStateInformation (blob);
        auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
        if (auto* el = xml->getChildByAttribute ("id", ids::zeroLatency))
            xml->removeChildElement (el, true);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        Rig b;
        b.set (ids::zeroLatency, 1.0f);
        b.prepare (fs, 512);
        b.proc->setStateInformation (old.getData(), (int) old.getSize());
        b.proc->applyLatencyModeIfChanged();
        check (! b.proc->isZeroLatency() && b.proc->getLatencySamples() == 1024 + b.proc->getCeilingLatency(),
               "projects from before 5.1 open with Zero latency off", "latency " + juce::String (b.proc->getLatencySamples()));
    }
}

// 5.1.1: the perceptual model's dB conversions batched and vectorised (fastmath). The curve must stay
// within 0.001 dB of the 5.1.0 math (std::log10 / std::pow per value, kept as the model's exact math).
void testFastMath511()
{
    section ("38. 5.1.1 faster model (batched log / exp)");
    namespace fm = anneal::fastmath;

    // The conversions over their whole range, with SIMD and as plain loops (non-x86 builds).
    {
        std::mt19937 rng (5);
        std::uniform_real_distribution<double> u (-20.0, 30.0);
        const int n = 200003;   // not a multiple of four: the tail runs too
        std::vector<float> x ((size_t) n), fast ((size_t) n), plain ((size_t) n);
        for (auto& v : x)
            v = (float) std::pow (10.0, u (rng));   // powers 1e-20 .. 1e30
        const float exactCases[] = { 1.0f, 2.0f, 0.5f, 1.0e-20f, 1.0e30f, 1.41421354f, 1.41421366f, 3.0e-7f };
        std::copy (std::begin (exactCases), std::end (exactCases), x.begin());
        fm::powerToDb (x.data(), fast.data(), n);
        fm::powerToDbScalar (x.data(), plain.data(), n);
        double worst = 0.0, worstPlain = 0.0;
        for (size_t i = 0; i < (size_t) n; ++i)
        {
            const double exact = 10.0 * std::log10 ((double) x[i]);
            worst = std::max (worst, std::abs (fast[i] - exact));
            worstPlain = std::max (worstPlain, std::abs (plain[i] - exact));
        }
        const float edges[8] = { 0.0f, -1.0f, std::numeric_limits<float>::quiet_NaN(), 1.0e-30f,
                                 1.0e35f, std::numeric_limits<float>::infinity(), 1.0e-40f, 1.0f };
        const float expected[8] = { -200.0f, -200.0f, -200.0f, -200.0f, 300.0f, 300.0f, -200.0f, 0.0f };
        float edgeFast[8], edgePlain[8];
        fm::powerToDb (edges, edgeFast, 8);
        fm::powerToDbScalar (edges, edgePlain, 8);
        bool edgesOk = true;
        for (int i = 0; i < 8; ++i)
            edgesOk = edgesOk && std::abs (edgeFast[i] - expected[i]) < 1.0e-3f && std::abs (edgePlain[i] - expected[i]) < 1.0e-3f;
        check (worst < 1.0e-4 && worstPlain < 1.0e-4 && edgesOk, "power to dB = 10 log10, 1e-20 .. 1e30",
               "largest error " + juce::String (worst, 7) + " dB (plain loops " + juce::String (worstPlain, 7)
               + " dB); 0, < 0, NaN -> -200, 1e35, inf -> +300: " + (edgesOk ? "yes" : "NO"));

        std::uniform_real_distribution<float> d (-200.0f, 200.0f);
        for (auto& v : x)
            v = d (rng);
        const float dbCases[] = { 0.0f, 20.0f, -20.0f, 6.0206f, -0.0001f, 0.5f, -199.9f, 199.9f };
        std::copy (std::begin (dbCases), std::end (dbCases), x.begin());
        fm::dbToGain (x.data(), fast.data(), n);
        fm::dbToGainScalar (x.data(), plain.data(), n);
        worst = worstPlain = 0.0;
        for (size_t i = 0; i < (size_t) n; ++i)
        {
            const double exact = std::pow (10.0, x[i] / 20.0);
            worst = std::max (worst, std::abs (20.0 * std::log10 (fast[i] / exact)));
            worstPlain = std::max (worstPlain, std::abs (20.0 * std::log10 (plain[i] / exact)));
        }
        const float gEdges[4] = { 0.0f, 1000.0f, -1000.0f, std::numeric_limits<float>::quiet_NaN() };
        float gFast[4], gPlain[4];
        fm::dbToGain (gEdges, gFast, 4);
        fm::dbToGainScalar (gEdges, gPlain, 4);
        bool gOk = true;
        for (const float* g : { gFast, gPlain })
            gOk = gOk && g[0] == 1.0f && std::isfinite (g[1]) && g[1] > 1.0e37f && g[2] > 0.0f && g[2] < 1.0e-37f
                      && std::isfinite (g[3]) && g[3] < 1.0e-37f;
        check (worst < 1.0e-4 && worstPlain < 1.0e-4 && gOk, "dB to gain = 10^(dB/20), -200 .. +200 dB",
               "largest error " + juce::String (worst, 7) + " dB (plain loops " + juce::String (worstPlain, 7)
               + " dB); 0 dB -> exactly 1, +-1000 dB and NaN stay finite: " + (gOk ? "yes" : "NO"));
    }

    // The model, fast against exact, frame by frame on music, at Low and Precise, with everything that
    // converts dB on: Reveal / Smooth / Tilt; Make room + Match; Unmask + Guard + Punch.
    {
        const double fs = 48000.0;
        const auto music = testsig::musicLike (fs, 6.0, 11);
        const auto keySig = testsig::musicLike (fs, 6.0, 23);
        anneal::ReferenceCurve ref;
        for (int i = 0; i < anneal::ReferenceCurve::numPoints; ++i)
            ref.db[i] = -12.0f * (float) i / (float) anneal::ReferenceCurve::numPoints;   // darker than the music
        ref.valid = true;

        double worstBand = 0.0, worstBin = 0.0, worstLevels = 0.0;
        bool matched = true, moved = true;
        int frames = 0;
        for (int mode : { 0, 2 })
            for (int variant = 0; variant < 3; ++variant)
            {
                const int nfft = anneal::SpectralEngine::fftSizeFor (fs, mode);
                const int hop = anneal::SpectralEngine::hopFor (fs, nfft);
                const int bins = nfft / 2 + 1;
                anneal::PerceptualModel fast, exact;
                fast.prepare (fs, nfft);
                exact.prepare (fs, nfft);
                exact.setExactMath (true);
                fast.setReference (ref);
                exact.setReference (ref);
                anneal::ModelSettings ms;
                ms.reveal = 0.8f;
                ms.smooth = 0.8f;
                ms.tiltDbPerOct = 1.5f;
                if (variant == 1)
                {
                    ms.scMode = 1; ms.scAmount = 1.0f;
                    ms.refMode = anneal::refMatch; ms.refAmount = 1.0f;
                }
                else if (variant == 2)
                {
                    ms.scMode = 2; ms.scAmount = 1.0f;
                    ms.guard = 1.0f; ms.punch = 1.0f; ms.responseMs = 30.0f;
                }

                anneal::RealFft fft;
                fft.prepare (nfft);
                std::vector<float> buf ((size_t) nfft * 2), power ((size_t) bins), keyPower ((size_t) bins),
                                   levelsFast (1024), levelsExact (1024);
                auto spectrum = [&] (const Channels& sig, size_t start, std::vector<float>& out)
                {
                    for (int i = 0; i < nfft; ++i)
                        buf[(size_t) i] = 0.5f * (sig[0][start + (size_t) i] + sig[1][start + (size_t) i])
                                        * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / nfft));
                    fft.forward (buf.data());
                    const float scale = 4.0f / (float) nfft;
                    for (int k = 0; k < bins; ++k)
                        out[(size_t) k] = (buf[2 * (size_t) k] * buf[2 * (size_t) k] + buf[2 * (size_t) k + 1] * buf[2 * (size_t) k + 1]) * scale * scale;
                };

                const float fps = (float) (fs / hop);
                float largestMove = 0.0f;
                for (size_t start = 0; start + (size_t) nfft <= music[0].size(); start += (size_t) hop, ++frames)
                {
                    spectrum (music, start, power);
                    spectrum (keySig, start, keyPower);
                    const float* key = variant > 0 ? keyPower.data() : nullptr;
                    fast.analyse (power.data(), ms, fps, key);
                    exact.analyse (power.data(), ms, fps, key);
                    for (int b = 0; b < anneal::PerceptualModel::numBands; ++b)
                    {
                        worstBand = std::max (worstBand, (double) std::abs (fast.getBandGainDb()[b] - exact.getBandGainDb()[b]));
                        largestMove = std::max (largestMove, std::abs (exact.getBandGainDb()[b]));
                    }
                    for (int k = 0; k < bins; ++k)
                        worstBin = std::max (worstBin, std::abs (20.0 * std::log10 ((double) fast.getBinGain()[k] / exact.getBinGain()[k])));
                    if (frames % 8 == 0)
                    {
                        fast.bandLevelsDb (power.data(), levelsFast.data());
                        exact.bandLevelsDb (power.data(), levelsExact.data());
                        for (int b = 0; b < 1024; ++b)
                            worstLevels = std::max (worstLevels, (double) std::abs (levelsFast[(size_t) b] - levelsExact[(size_t) b]));
                    }
                }
                if (variant == 1)
                    matched = matched && fast.isMatching() && exact.isMatching();
                moved = moved && largestMove > 3.0f;   // the curve really works (not a flat comparison)
            }
        check (worstBand < 0.001 && worstBin < 0.001 && worstLevels < 0.001 && matched && moved,
               "the curve stays within 0.001 dB of 5.1.0's math",
               juce::String (frames) + " frames, Low and Precise, sidechain, Match, Guard, Punch: bands "
               + juce::String (worstBand, 6) + " dB, bins " + juce::String (worstBin, 6) + " dB, display levels "
               + juce::String (worstLevels, 6) + " dB");
    }

    // Speed, reported (not a check: timings follow the machine's load). Both run the batched loops
    // (5.1.0 called std::log10 one value at a time inside the band loop); `AnnealTests profile` has
    // the full split.
    {
        const double fs = 48000.0;
        const int nfft = anneal::SpectralEngine::fftSizeFor (fs, anneal::SpectralEngine::precise), bins = nfft / 2 + 1;
        const auto music = testsig::musicLike (fs, 6.0, 1);
        std::vector<float> buf ((size_t) nfft * 2, 0.0f), power ((size_t) bins);
        for (int i = 0; i < nfft; ++i)
            buf[(size_t) i] = music[0][(size_t) (fs * 3.0) + (size_t) i] * (float) (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * i / nfft));
        anneal::RealFft fft;
        fft.prepare (nfft);
        fft.forward (buf.data());
        const float scale = 4.0f / (float) nfft;
        for (int k = 0; k < bins; ++k)
            power[(size_t) k] = (buf[2 * (size_t) k] * buf[2 * (size_t) k] + buf[2 * (size_t) k + 1] * buf[2 * (size_t) k + 1]) * scale * scale;

        anneal::PerceptualModel model;
        model.prepare (fs, nfft);
        anneal::ModelSettings ms;
        ms.reveal = 0.4f;
        ms.smooth = 0.4f;
        const float fps = (float) (fs / anneal::SpectralEngine::hopFor (fs, nfft));
        auto best = [&] (bool exactMath)
        {
            model.setExactMath (exactMath);
            double fastest = 1.0e9;
            for (int run = 0; run < 5; ++run)
            {
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < 300; ++i)
                    model.analyse (power.data(), ms, fps);
                fastest = std::min (fastest, 1.0e6 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 300.0);
            }
            return fastest;
        };
        const double exactUs = best (true), fastUs = best (false);
        info ("one Precise frame: model.analyse " + juce::String (fastUs, 1) + " us (batched exact math "
              + juce::String (exactUs, 1) + " us)");
    }
}

void testUpdateRate52()
{
    section ("39. 5.2 300 EQ updates a second");

    // Every mode at every rate: 300 frames a second (the hop rounds to whole samples), never
    // coarser than a quarter frame.
    {
        bool ok = true;
        juce::String log;
        for (double fs : { 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 })
            for (int mode = 0; mode < anneal::SpectralEngine::numModes; ++mode)
            {
                anneal::SpectralEngine e;
                e.prepare (fs, 2, mode);
                const double fps = fs / e.getHopSize();
                ok = ok && std::abs (fps - 300.0) < 3.0 && e.getHopSize() <= e.getFftSize() / 4;
                if (mode == 0)
                    log << juce::String (fs / 1000.0, 1) << "k hop " << e.getHopSize() << " (" << juce::String (fps, 1) << "/s)  ";
            }
        check (ok, "300 updates/s in Low, Normal and Precise at 44.1 / 48 / 88.2 / 96 / 192 kHz", log);
    }

    // The overlap-add stays exact although 160 doesn't divide N: a neutral engine passes noise
    // through untouched, delayed by N (the identity checks above cover more rates and blocks).
    {
        double worst = 0.0;
        for (int mode = 0; mode < anneal::SpectralEngine::numModes; ++mode)
        {
            anneal::SpectralEngine e;
            e.prepare (48000.0, 2, mode);
            e.setParameters (neutral());
            const int latency = e.getLatencySamples();
            const auto in = testsig::whiteNoise (2, 24000 + latency, 0.5f, 52);
            worst = std::max (worst, delayedError (runEngine (e, in, fixedBlocks (100)), in, latency));
        }
        check (worst < 1.0e-4, "hop 160 (N / 25.6): neutral output = input, every mode", "max err " + juce::String (worst, 9));
    }

    // A running engine (after two seconds of music-level noise with Reveal and Smooth) is still
    // at 300 frames a second.
    {
        const double fs = 48000.0;
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2, anneal::SpectralEngine::precise);
        auto p = neutral();
        p.reveal = 0.6f; p.smooth = 0.6f;
        e->setParameters (p);
        auto quiet = testsig::whiteNoise (2, (int) fs * 2, 0.05f, 9);
        runEngine (*e, quiet, fixedBlocks (480));
        const double framesPerSecond = fs / e->getHopSize();
        check (std::abs (framesPerSecond - 300.0) < 0.5, "Precise at 48 kHz runs 300 frames/s (was 93.75)",
               juce::String (framesPerSecond, 2) + " frames/s, hop " + juce::String (e->getHopSize()));
    }
}

// 5.2.1: Spread load. Each frame's work runs in stages through the next hop instead of all in the
// block where its hop ends; one hop more latency, the same output.
void testSpread521()
{
    section ("40. 5.2.1 Spread load");
    const double fs = 48000.0;
    namespace se = anneal;

    // The engine with Spread is the engine without it one hop later, sample for sample: neutral, the
    // auto EQ with Heat, and Mid/Side with Guard, Punch, Width, Mono bass and a sidechain key. Spread
    // runs in random blocks of 1 to 700 samples (several stages and hops in one block, or none).
    {
        const auto music = testsig::musicLike (fs, 1.5, 7);
        const auto keySig = testsig::musicLike (fs, 1.5, 19);
        const int n = (int) music[0].size();
        double worst = 0.0, worstHead = 0.0;
        bool moved = true;
        for (int mode = 0; mode < se::SpectralEngine::numModes; ++mode)
            for (int variant = 0; variant < 3; ++variant)
            {
                auto p = neutral();
                if (variant >= 1)
                {
                    p.reveal = 0.6f; p.smooth = 0.6f; p.heat = 0.5f;
                }
                if (variant == 2)
                {
                    p.midSide = 1.0f; p.guard = 1.0f; p.punch = 0.5f; p.width = 1.5f; p.monoHz = 120.0f;
                    p.scMode = se::scMakeRoom; p.scAmount = 1.0f;
                }
                Channels outs[2];
                int hop = 0, latency = 0;
                for (int sp = 0; sp < 2; ++sp)
                {
                    se::SpectralEngine e;
                    e.prepare (fs, 2, mode);
                    e.setSpread (sp == 1);
                    e.reset();
                    e.setParameters (p);
                    hop = e.getHopSize();
                    if (sp == 0)
                        latency = e.getLatencySamples();
                    const auto next = sp == 1 ? randomBlocks (1, 700, (unsigned) (10 * mode + variant)) : fixedBlocks (512);
                    Channels out = music;
                    for (int pos = 0; pos < n;)
                    {
                        const int b = std::min (next(), n - pos);
                        float* io[2] = { out[0].data() + pos, out[1].data() + pos };
                        const float* k[2] = { keySig[0].data() + pos, keySig[1].data() + pos };
                        e.process (io, 2, b, variant == 2 ? k : nullptr, variant == 2 ? 2 : 0);
                        pos += b;
                    }
                    outs[sp] = std::move (out);
                }
                for (size_t c = 0; c < 2; ++c)
                {
                    for (size_t i = 0; i + (size_t) hop < (size_t) n; ++i)
                        worst = std::max (worst, (double) std::abs (outs[1][c][i + (size_t) hop] - outs[0][c][i]));
                    for (size_t i = 0; i < (size_t) hop; ++i)
                        worstHead = std::max (worstHead, (double) std::abs (outs[1][c][i]));
                }
                if (variant > 0)
                    moved = moved && delayedError (outs[0], music, latency) > 0.01;   // the processing really works
            }
        check (worst < 1.0e-6 && worstHead < 1.0e-6 && moved, "Spread = the same output one hop later, every mode",
               "neutral, EQ + Heat, Mid/Side + Guard + Punch + Width + Mono bass + key; largest difference "
               + juce::String (worst, 9) + ", first hop " + juce::String (worstHead, 9));
    }

    // No small block carries a whole frame: at 32 samples (stages 40 apart in a 160-sample hop) a block
    // runs at most one of the four stages, at 64 at most two. Without Spread one block runs all four.
    {
        juce::String log;
        bool ok = true;
        for (int mode : { 0, 2 })
        {
            int most[3] {};   // off at 32, on at 32, on at 64
            for (int k = 0; k < 3; ++k)
            {
                se::SpectralEngine e;
                e.prepare (fs, 2, mode);
                e.setSpread (k > 0);
                e.reset();
                auto p = neutral();
                p.reveal = 0.4f; p.smooth = 0.4f; p.heat = 0.3f;
                e.setParameters (p);
                const int block = k == 2 ? 64 : 32;
                auto x = testsig::musicLike (fs, 0.5, 3);
                for (int pos = 0; pos + block <= (int) x[0].size(); pos += block)
                {
                    float* io[2] = { x[0].data() + pos, x[1].data() + pos };
                    e.process (io, 2, block);
                    most[k] = std::max (most[k], e.getStagesInLastCall());
                }
            }
            ok = ok && most[0] == se::SpectralEngine::numStages && most[1] == 1 && most[2] == 2;
            log << (mode == 0 ? "Low" : "Precise") << ": off " << most[0] << ", on at 32 " << most[1] << ", on at 64 " << most[2] << "  ";
        }
        check (ok, "Spread: a 32-sample block runs at most 1 of a frame's 4 stages", log + "(stages in one block, at most)");
    }

    // The engine's latency is one hop more and an impulse comes out there; analysis only (zero
    // latency) rests it.
    {
        se::SpectralEngine e;
        e.prepare (fs, 2, se::SpectralEngine::low);
        e.setSpread (true);
        e.reset();
        e.setParameters (neutral());
        auto imp = testsig::silence (2, 8000);
        imp[0][1000] = imp[1][1000] = 1.0f;
        const auto out = runEngine (e, imp, fixedBlocks (64));
        const int lat = e.getLatencySamples(), at = peakIndex (out);
        e.setAnalysisOnly (true);
        const int resting = e.getLatencySamples();
        e.setAnalysisOnly (false);
        check (lat == 1024 + 160 && at == 1000 + lat && std::abs (out[0][(size_t) at] - 1.0f) < 1.0e-4f && resting == 1024,
               "engine: Spread reports N + hop, the impulse arrives there", "latency " + juce::String (lat) + ", impulse +"
                   + juce::String (at - 1000) + "; analysis only " + juce::String (resting));
    }

    // Through the processor, default sound (Reveal, Smooth, Heat), 64-sample blocks: one hop more in
    // every mode, the output otherwise the same.
    {
        const auto music = testsig::musicLike (fs, 2.0, 5);
        bool ok = true;
        double worst = 0.0;
        juce::String log;
        for (int mode = 0; mode < se::SpectralEngine::numModes; ++mode)
        {
            Channels outs[2];
            int lat[2] {};
            for (int sp = 0; sp < 2; ++sp)
            {
                Rig r;
                r.set (ids::match, 0.0f);
                r.set (ids::latency, (float) mode);
                r.set (ids::spread, (float) sp);
                r.prepare (fs, 64);
                outs[sp] = r.run (music, fixedBlocks (64));
                lat[sp] = r.proc->getLatencySamples();
            }
            const int hop = se::SpectralEngine::hopFor (fs, se::SpectralEngine::fftSizeFor (fs, mode));
            for (size_t c = 0; c < 2; ++c)
                for (size_t i = 0; i + (size_t) hop < music[0].size(); ++i)
                    worst = std::max (worst, (double) std::abs (outs[1][c][i + (size_t) hop] - outs[0][c][i]));
            ok = ok && lat[1] == lat[0] + hop;
            log << juce::String (1000.0 * lat[0] / fs, 1) << " -> " << juce::String (1000.0 * lat[1] / fs, 1) << " ms  ";
        }
        check (ok && worst < 1.0e-6, "processor: Spread adds one hop in each mode, same sound",
               log + "largest difference " + juce::String (worst, 9));
    }

    // Wet (Mix 100 %) and dry (Mix 0 %) arrive at the reported latency; at zero latency Spread rests.
    {
        auto imp = testsig::silence (2, 16384);
        imp[0][1000] = imp[1][1000] = 1.0f;
        bool ok = true;
        juce::String log;
        for (float mix : { 100.0f, 0.0f })
        {
            Rig r;
            r.setNeutral();
            r.set (ids::mix, mix);
            r.set (ids::spread, 1.0f);
            r.prepare (fs, 64);
            const auto out = r.run (imp, fixedBlocks (64));
            const int lat = r.proc->getLatencySamples(), at = peakIndex (out);
            ok = ok && r.proc->isSpread() && lat == 1024 + 160 + r.proc->getCeilingLatency() && at == 1000 + lat
                    && std::abs (out[0][(size_t) at] - 1.0f) < 1.0e-4f;
            log << "Mix " << juce::String (mix, 0) << " %: latency " << lat << ", impulse +" << (at - 1000) << "  ";
        }
        Rig z;
        z.setNeutral();
        z.set (ids::spread, 1.0f);
        z.set (ids::zeroLatency, 1.0f);
        z.prepare (fs, 64);
        const auto zout = z.run (imp, fixedBlocks (64));
        ok = ok && z.proc->getLatencySamples() == 0 && peakIndex (zout) == 1000 && z.proc->getLatencyMs() == 0.0;
        log << "Zero: latency " << z.proc->getLatencySamples() << ", impulse +" << (peakIndex (zout) - 1000);
        check (ok, "Spread: wet and dry aligned at N + hop + look-ahead; rests at Zero", log);
    }

    // Switching Spread, the modes and Zero while playing (Mid/Side in one step): right latency, clean.
    {
        Rig r;
        r.prepare (fs, 256);
        const auto music = testsig::musicLike (fs, 1.0);
        juce::String log;
        bool ok = true;
        struct Step { int spread, mode, zero; };
        const Step steps[] = { { 1, 0, 0 }, { 0, 0, 0 }, { 1, 2, 0 }, { 1, 2, 1 }, { 1, 1, 0 }, { 0, 1, 0 }, { 1, 0, 0 } };
        int step = 0;
        for (auto [sp, m, z] : steps)
        {
            r.set (ids::spread, (float) sp);
            r.set (ids::latency, (float) m);
            r.set (ids::zeroLatency, (float) z);
            r.set (ids::stereoMode, step++ == 4 ? 1.0f : 0.0f);
            r.proc->applyLatencyModeIfChanged();
            const auto out = r.run (music, randomBlocks (16, 1024, (unsigned) (40 + step)));
            const int nfft = se::SpectralEngine::fftSizeFor (fs, m);
            const int expected = z ? 0 : nfft + (sp ? se::SpectralEngine::hopFor (fs, nfft) : 0) + r.proc->getCeilingLatency();
            ok = ok && r.proc->getLatencySamples() == expected && allFinite (out) && maxAbs (out) < 4.0;
            log << r.proc->getLatencySamples() << " ";
        }
        check (ok, "switching Spread, modes and Zero while playing: right latency, clean", "latencies " + log);
    }

    // Saved with the project; projects from before 5.2.1 open with it off (their latency as made).
    {
        Rig a;
        a.set (ids::spread, 1.0f);
        juce::MemoryBlock withSpread;
        a.proc->getStateInformation (withSpread);
        Rig b;
        b.prepare (fs, 512);
        b.proc->setStateInformation (withSpread.getData(), (int) withSpread.getSize());
        b.proc->applyLatencyModeIfChanged();
        const bool kept = b.proc->isSpread() && b.proc->getLatencySamples() == 1024 + 160 + b.proc->getCeilingLatency();

        auto xml = juce::AudioProcessor::getXmlFromBinary (withSpread.getData(), (int) withSpread.getSize());
        if (auto* el = xml->getChildByAttribute ("id", ids::spread))
            xml->removeChildElement (el, true);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        Rig c;
        c.set (ids::spread, 1.0f);
        c.prepare (fs, 512);
        c.proc->setStateInformation (old.getData(), (int) old.getSize());
        c.proc->applyLatencyModeIfChanged();
        const bool off = ! c.proc->isSpread() && c.proc->getLatencySamples() == 1024 + c.proc->getCeilingLatency();
        auto* prm = a.proc->apvts.getParameter (ids::spread);
        check (kept && off && prm != nullptr && ! prm->isAutomatable(), "Spread is saved; projects from before 5.2.1 open with it off",
               juce::String ("saved: ") + (kept ? "on" : "NOT KEPT") + "; older project: " + (off ? "off" : "ON")
                   + "; automatable: " + (prm != nullptr && prm->isAutomatable() ? "yes" : "no"));
    }
}

// 5.2.2: Paint the width. A third paint curve sets how much of Width acts in each region.
anneal::PaintCurves widthPainted (double loHz, double hiHz, float share)
{
    anneal::PaintCurves p;
    for (int i = 0; i < anneal::PaintCurves::numPoints; ++i)
    {
        const double z = anneal::refgrid::camAt (i, anneal::PaintCurves::numPoints);
        if (z >= anneal::erb::hzToCam (loHz) && z <= anneal::erb::hzToCam (hiHz))
            p.width[i] = share;
    }
    return p;
}

void testWidthPaint522()
{
    section ("41. 5.2.2 Paint the width");
    const double fs = 48000.0;
    const double twoPi = 6.283185307179586;

    // Mid tones at 500 Hz and 5 kHz, side tones at 300 Hz and 3 kHz, all 0.1.
    const int n = (int) (2.0 * fs);
    Channels image (2, std::vector<float> ((size_t) n));
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double m = 0.1 * std::sin (twoPi * 500.0 * t) + 0.1 * std::sin (twoPi * 5000.0 * t);
        const double s = 0.1 * std::sin (twoPi * 300.0 * t) + 0.1 * std::sin (twoPi * 3000.0 * t);
        image[0][(size_t) i] = (float) (m + s);
        image[1][(size_t) i] = (float) (m - s);
    }
    struct Image { double mid500, mid5k, side300, side3k; };
    auto measure = [&] (float width, const anneal::PaintCurves& paint, int mode, bool zero, bool spread = false)
    {
        Rig r;
        r.setNeutral();
        r.set (ids::width, width);
        r.set (ids::latency, (float) mode);
        r.set (ids::zeroLatency, zero ? 1.0f : 0.0f);
        r.set (ids::spread, spread ? 1.0f : 0.0f);
        r.proc->setPaint (paint);
        r.prepare (fs, 512);
        const auto out = r.run (image, fixedBlocks (512));
        const int from = n / 2, len = n / 3;
        std::vector<float> mid ((size_t) len), side ((size_t) len);
        for (int i = 0; i < len; ++i)
        {
            const float l = out[0][(size_t) (from + i)], rr = out[1][(size_t) (from + i)];
            mid[(size_t) i] = 0.5f * (l + rr);
            side[(size_t) i] = 0.5f * (l - rr);
        }
        auto a = [&] (const std::vector<float>& x, double hz) { return toDb (testsig::toneAmplitude (x.data(), len, hz, fs) / 0.1); };
        return Image { a (mid, 500.0), a (mid, 5000.0), a (side, 300.0), a (side, 3000.0) };
    };
    auto midOk = [] (const Image& im) { return std::abs (im.mid500) < 0.1 && std::abs (im.mid5k) < 0.1; };
    const auto lowsOut = widthPainted (20.0, 1000.0, 0.0f);   // Width acts only above 1 kHz

    {
        // Width paint at 100 % everywhere (also after painting and erasing) = identical output.
        Rig plain, painted;
        plain.set (ids::width, 150.0f);
        painted.set (ids::width, 150.0f);
        plain.prepare (fs, 512);
        painted.proc->setPaint (lowsOut);
        painted.proc->setPaint (anneal::PaintCurves());
        painted.prepare (fs, 512);
        const auto music = testsig::musicLike (fs, 2.0);
        const double d = maxDiff (painted.run (music, fixedBlocks (512)), plain.run (music, fixedBlocks (512)));
        check (d == 0.0, "Width paint at 100 % everywhere = identical output", "max diff " + juce::String (d, 12));
    }

    {
        // Width 0 % (mono) painted out below 1 kHz: the 300 Hz sides stay, the 3 kHz sides go; the
        // centre is untouched. Low and Precise, and with Spread load.
        bool ok = true;
        juce::String log;
        for (int mode : { 0, 2 })
            for (bool spread : { false, true })
            {
                const auto im = measure (0.0f, lowsOut, mode, false, spread);
                ok = ok && midOk (im) && std::abs (im.side300) < 0.1 && im.side3k < -90.0;
                if (! spread)
                    log << (mode == 0 ? "Low" : "Precise") << ": 300 Hz sides " << db (im.side300, 2) << ", 3 kHz " << db (im.side3k, 0) << "  ";
            }
        check (ok, "Width 0 % painted out below 1 kHz: lows stay stereo, highs mono", log + "(also with Spread)");
    }

    {
        // Width 200 % at half its share above 1 kHz: 3 kHz sides x1.5 (+3.52 dB), 300 Hz x2 (+6.02 dB).
        auto half = widthPainted (1000.0, 20000.0, 0.5f);
        const auto im = measure (200.0f, half, 0, false);
        check (midOk (im) && std::abs (im.side300 - 6.02) < 0.1 && std::abs (im.side3k - 3.52) < 0.1,
               "Width 200 % at a 50 % share above 1 kHz: +3.5 dB there, +6.0 dB below",
               "300 Hz sides " + db (im.side300, 2) + ", 3 kHz " + db (im.side3k, 2) + "; centre within "
                   + db (std::max (std::abs (im.mid500), std::abs (im.mid5k)), 2));
    }

    {
        // Zero latency: the Side filter follows the paint too.
        const auto im = measure (0.0f, lowsOut, 0, true);
        const auto plainMono = measure (0.0f, anneal::PaintCurves(), 0, true);
        check (midOk (im) && std::abs (im.side300) < 0.5 && im.side3k < -40.0 && plainMono.side300 < -90.0,
               "Zero latency: Width paint works through the Side filter",
               "painted: 300 Hz sides " + db (im.side300, 2) + ", 3 kHz " + db (im.side3k, 0) + "; unpainted Width 0 %: 300 Hz "
                   + db (plainMono.side300, 0));
    }

    {
        // Saved with the project; a project from before 5.2.2 (paint without "width") keeps its EQ and
        // Heat paint and gets Width paint at 100 %; a malformed width list loads as 100 %.
        auto paint = paintedOut (true, 300.0, 600.0);
        for (int i = 10; i < 20; ++i)
            paint.width[i] = 0.25f;
        Rig a;
        a.proc->setPaint (paint);
        juce::MemoryBlock blob;
        a.proc->getStateInformation (blob);
        Rig b;
        b.proc->setStateInformation (blob.getData(), (int) blob.getSize());
        const bool kept = b.proc->getPaint() == paint;

        auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
        auto* p = xml->getChildByName ("Paint");
        if (p != nullptr)
            p->removeAttribute ("width");
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        Rig c;
        c.proc->setPaint (widthPainted (20.0, 20000.0, 0.0f));
        c.proc->setStateInformation (old.getData(), (int) old.getSize());
        const auto olderPaint = c.proc->getPaint();
        const bool older = p != nullptr && anneal::PaintCurves::isFull (olderPaint.width)
                           && std::equal (paint.eq, paint.eq + anneal::PaintCurves::numPoints, olderPaint.eq);

        if (p != nullptr)
            p->setAttribute ("width", "0.5 0.5 nope");
        juce::MemoryBlock bad;
        juce::AudioProcessor::copyXmlToBinary (*xml, bad);
        c.proc->setStateInformation (bad.getData(), (int) bad.getSize());
        const bool badIgnored = anneal::PaintCurves::isFull (c.proc->getPaint().width);
        check (kept && older && badIgnored, "Width paint is saved; older projects and malformed lists load at 100 %",
               juce::String (kept ? "round trip exact" : "ROUND TRIP WRONG") + "; before 5.2.2: "
                   + (older ? "width 100 %, EQ paint kept" : "WRONG") + "; malformed: " + (badIgnored ? "100 %" : "TAKEN"));
    }

    // On the display: the WIDTH lane sits between the EQ and Heat lanes, shows the width heard, and
    // takes Width's paint in paint mode.
    Rig r;
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
    auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
    if (editor == nullptr)
    {
        check (false, "editor opens", "no AnnealEditor");
        return;
    }
    auto& content = editor->getContent();
    auto& screen = content.screen;
    const auto eqLane = screen.getEqLane(), widthLane = screen.getWidthLane(), heatLane = screen.getHeatLane();

    {
        r.set (ids::width, 150.0f);
        r.set (ids::monoBass, 120.0f);
        r.proc->setPaint (widthPainted (20.0, 400.0, 0.0f));
        content.tick (0.0f);
        const float at60 = screen.widthAtHz (60.0f), at300 = screen.widthAtHz (300.0f), at5k = screen.widthAtHz (5000.0f);
        const bool order = eqLane.getBottom() < widthLane.getY() && widthLane.getBottom() < heatLane.getY()
                           && widthLane.getHeight() >= 20.0f && eqLane.getHeight() > 4.0f * widthLane.getHeight();
        check (order && at60 < 0.01f && std::abs (at300 - 1.0f) < 0.01f && std::abs (at5k - 1.5f) < 0.01f,
               "display: a WIDTH lane between EQ and Heat shows the width heard",
               "lanes EQ " + juce::String (eqLane.getHeight(), 0) + " / width " + juce::String (widthLane.getHeight(), 0) + " / heat "
                   + juce::String (heatLane.getHeight(), 0) + " px; Width 150 %, painted out below 400 Hz, Mono bass 120 Hz: 60 Hz "
                   + juce::String (at60 * 100.0f, 0) + " %, 300 Hz " + juce::String (at300 * 100.0f, 0) + " %, 5 kHz "
                   + juce::String (at5k * 100.0f, 0) + " %");
        r.proc->setPaint (anneal::PaintCurves());
        content.tick (0.0f);
    }

    {
        const auto now = juce::Time::getCurrentTime();
        auto ev = [&screen, now] (juce::Point<float> at, juce::Point<float> down, bool dragged, int clicks = 1)
        {
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), at,
                                     juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &screen, &screen, now, down, now, clicks, dragged);
        };
        auto stroke = [&] (juce::Point<float> from, juce::Point<float> to)
        {
            screen.mouseDown (ev (from, from, false));
            screen.mouseDrag (ev ({ (from.x + to.x) * 0.5f, (from.y + to.y) * 0.5f }, from, true));
            screen.mouseDrag (ev (to, from, true));
            screen.mouseUp (ev (to, from, true));
        };
        content.setPaintMode (true);
        while (r.proc->canUndo())
            r.proc->undo();
        // Along the bottom of the WIDTH lane from 100 to 400 Hz: 0 % there, nothing else painted.
        stroke ({ screen.xForHz (100.0f), widthLane.getBottom() - 1.0f }, { screen.xForHz (400.0f), widthLane.getBottom() - 1.0f });
        const auto painted = r.proc->getPaint();
        int zeros = 0;
        for (int i = 0; i < anneal::PaintCurves::numPoints; ++i)
            zeros += painted.width[i] == 0.0f ? 1 : 0;
        const bool onlyWidth = anneal::PaintCurves::isFull (painted.eq) && anneal::PaintCurves::isFull (painted.heat);
        const bool oneStep = r.proc->canUndo();
        r.proc->undo();
        const bool undone = r.proc->getPaint().isDefault();
        r.proc->redo();

        // Double-click in the lane resets it as one step.
        const auto at = juce::Point<float> (screen.xForHz (6000.0f), widthLane.getCentreY());
        screen.mouseDown (ev (at, at, false));
        screen.mouseUp (ev (at, at, false));
        screen.mouseDown (ev (at, at, false, 2));
        screen.mouseDoubleClick (ev (at, at, false, 2));
        screen.mouseUp (ev (at, at, false, 2));
        const bool reset = anneal::PaintCurves::isFull (r.proc->getPaint().width);
        content.setPaintMode (false);
        check (zeros >= 4 && onlyWidth && oneStep && undone && reset, "paint strokes in the WIDTH lane: Width's paint only, one undo step",
               juce::String (zeros) + " points at 0 % between 100 and 400 Hz; " + (onlyWidth ? "EQ and Heat untouched" : "OTHER LANES PAINTED")
                   + "; " + (undone ? "undone in one step" : "UNDO WRONG") + "; " + (reset ? "double-click resets" : "NOT RESET"));
    }
}

// 5.2.3: the stereo image on the display, per ERB: the output's Side relative to its Mid.
void testStereoImage523()
{
    section ("42. 5.2.3 The stereo image on the display");
    const double fs = 48000.0;
    const double twoPi = 6.283185307179586;

    // Centre only at 500 Hz, sides only at 3 kHz, and at 200 Hz and 1.5 kHz the sides 6 dB under the centre.
    const int n = (int) (1.5 * fs);
    Channels music (2, std::vector<float> ((size_t) n));
    for (int i = 0; i < n; ++i)
    {
        const double t = i / fs;
        const double m = 0.1 * std::sin (twoPi * 500.0 * t) + 0.1 * std::sin (twoPi * 1500.0 * t) + 0.1 * std::sin (twoPi * 200.0 * t);
        const double s = 0.1 * std::sin (twoPi * 3000.0 * t) + 0.05 * std::sin (twoPi * 1500.0 * t) + 0.05 * std::sin (twoPi * 200.0 * t);
        music[0][(size_t) i] = (float) (m + s);
        music[1][(size_t) i] = (float) (m - s);
    }
    struct Image { bool stereo = false; float at200 = 0.0f, at500 = 0.0f, at1k5 = 0.0f, at3k = 0.0f; };
    auto measure = [&] (float width, float monoHz, bool zero, bool midSide = false, int channels = 2)
    {
        Rig r (channels);
        r.setNeutral();
        r.set (ids::width, width);
        r.set (ids::monoBass, monoHz);
        r.set (ids::zeroLatency, zero ? 1.0f : 0.0f);
        r.set (ids::stereoMode, midSide ? 1.0f : 0.0f);
        r.prepare (fs, 512);
        Channels in = music;
        in.resize ((size_t) channels);
        r.run (in, fixedBlocks (512));
        Image im;
        if (const auto* f = r.proc->getDisplay().read())
        {
            auto at = [f] (double hz) { return f->sideRelDb[juce::jlimit (0, anneal::DisplayFrame::numBands - 1,
                                                    juce::roundToInt ((anneal::erb::hzToCam (hz) - f->z0) / f->dz))]; };
            im = { f->stereo, at (200.0), at (500.0), at (1500.0), at (3000.0) };
        }
        return im;
    };

    {
        const auto plain = measure (100.0f, 0.0f, false), wide = measure (200.0f, 0.0f, false), narrow = measure (0.0f, 0.0f, false);
        check (plain.stereo && std::abs (plain.at1k5 + 6.02f) < 0.3f && plain.at500 < -40.0f && plain.at3k > 15.0f
                   && std::abs (wide.at1k5) < 0.3f && narrow.at1k5 < -60.0f && narrow.at3k < -60.0f,
               "the image: sides vs centre per band, through Width",
               "1.5 kHz: " + db (plain.at1k5, 2) + " (Width 200 %: " + db (wide.at1k5, 2) + ", 0 %: " + db (narrow.at1k5, 0)
                   + "); 500 Hz centre only: " + db (plain.at500, 0) + "; 3 kHz sides only: " + db (plain.at3k, 0));
    }

    {
        // Mono bass at 400 Hz (its top) takes 200 Hz to mono and leaves 1.5 kHz; at zero latency and in
        // Mid/Side (neutral EQ) the image is the same; a mono track has none.
        const auto plain = measure (100.0f, 0.0f, false);
        const auto mono = measure (100.0f, 400.0f, false);
        const auto zero = measure (200.0f, 0.0f, true);
        const auto ms = measure (100.0f, 0.0f, false, true);
        const auto oneChannel = measure (100.0f, 0.0f, false, false, 1);
        check (std::abs (plain.at200 + 6.02f) < 0.3f && mono.at200 < -40.0f && std::abs (mono.at1k5 + 6.02f) < 0.3f
                   && std::abs (zero.at1k5) < 0.3f && std::abs (ms.at1k5 + 6.02f) < 0.3f && ! oneChannel.stereo,
               "the image follows Mono bass, Zero latency and Mid/Side; none for mono tracks",
               "200 Hz " + db (plain.at200, 2) + ", with Mono bass 400 Hz " + db (mono.at200, 0) + " (1.5 kHz stays "
                   + db (mono.at1k5, 2) + "); Zero latency, Width 200 %: "
                   + db (zero.at1k5, 2) + "; Mid/Side: " + db (ms.at1k5, 2) + "; mono track: " + (oneChannel.stereo ? "IMAGE" : "none"));
    }

    {
        // The display draws it: the screen's value at 1.5 kHz and the hover read-out's "sides".
        Rig r;
        r.setNeutral();
        r.prepare (fs, 512);
        r.run (music, fixedBlocks (512));
        anneal::PerceptionScreen screen (r.proc->getDisplay(), r.proc->apvts);
        screen.setBounds (0, 0, 1440, 356);
        screen.refreshNow();
        const float shown = screen.sideRelAtHz (1500.0f), shownLow = screen.sideRelAtHz (500.0f);
        check (std::abs (shown + 6.02f) < 0.5f && shownLow < -40.0f, "the display shows the image (1.5 kHz sides -6 dB, 500 Hz mono)",
               "1.5 kHz " + db (shown, 2) + ", 500 Hz " + db (shownLow, 0));
    }
}

// 5.2.4: presets dropped on the preset bar join your presets; the menu copies the folder's path.
void testPresetImport524()
{
    section ("43. 5.2.4 Import presets by dropping them on the preset bar");
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AnnealTests-import");
    tmp.deleteRecursively();
    const auto mine = tmp.getChildFile ("mine"), elsewhere = tmp.getChildFile ("from a friend"), other = tmp.getChildFile ("other");
    elsewhere.createDirectory();
    other.createDirectory();

    Rig r;
    r.proc->setUserPresetFolder (mine, false);
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
    auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
    if (editor == nullptr)
    {
        check (false, "editor opens", "no AnnealEditor");
        return;
    }
    auto& bar = editor->getContent().presets;

    auto values = anneal::presets::currentValues (r.proc->apvts);
    values[0] = 77.0f;   // Reveal
    values[1] = 12.0f;   // Smooth
    anneal::presets::writeUser (elsewhere, "Bright Mix", values);
    auto different = values;
    different[0] = 33.0f;
    anneal::presets::writeUser (other, "Bright Mix", different);
    const auto notPreset = elsewhere.getChildFile ("Notes.annealpreset");
    notPreset.replaceWithText ("<NotAnneal><PARAM id=\"reveal\" value=\"10\"/></NotAnneal>");
    const auto audio = elsewhere.getChildFile ("Song.wav");
    audio.replaceWithText ("RIFF");
    const auto bright = elsewhere.getChildFile ("Bright Mix.annealpreset").getFullPathName();

    {
        // Only preset files are taken; a single dropped preset is added and loaded (one undo step).
        const bool wants = bar.isInterestedInFileDrag ({ bright }) && ! bar.isInterestedInFileDrag ({ audio.getFullPathName() });
        while (r.proc->canUndo())
            r.proc->undo();
        bar.fileDragEnter ({ bright }, 10, 10);
        const bool highlighted = bar.isDropHighlighted();
        bar.filesDropped ({ bright }, 10, 10);
        auto v = [&r] (const char* id) { return r.proc->apvts.getRawParameterValue (id)->load(); };
        const bool added = r.proc->getUserPresetNames().contains ("Bright Mix") && mine.getChildFile ("Bright Mix.annealpreset").existsAsFile();
        const bool loaded = r.proc->getUserPresetName() == "Bright Mix" && std::abs (v (ids::reveal) - 77.0f) < 0.05f
                            && std::abs (v (ids::smooth) - 12.0f) < 0.05f;
        const bool told = bar.getShownText().contains ("Added") && ! bar.isDropHighlighted();
        const bool oneStep = r.proc->canUndo();
        check (wants && highlighted && added && loaded && told && oneStep, "a dropped preset is added to your presets and loaded",
               juce::String (wants ? "takes .annealpreset, not .wav" : "WRONG FILES TAKEN") + "; " + (added ? "copied into your folder" : "NOT ADDED")
                   + "; " + (loaded ? "loaded (Reveal 77 %)" : "NOT LOADED") + "; bar said \"" + bar.getShownText() + "\"");
    }

    {
        // Dropped again: not added twice. A different preset with the same name: "Bright Mix 2". A file
        // that isn't an Anneal preset, and a .wav: not added, and the bar says why.
        juce::StringArray errors;
        const auto again = r.proc->importUserPresets ({ bright }, errors);
        const auto clash = r.proc->importUserPresets ({ other.getChildFile ("Bright Mix.annealpreset").getFullPathName() }, errors);
        const bool noDuplicate = again.size() == 1 && again[0] == "Bright Mix" && errors.isEmpty();
        const bool renamed = clash.size() == 1 && clash[0] == "Bright Mix 2";
        juce::StringArray bad;
        const auto none = r.proc->importUserPresets ({ notPreset.getFullPathName(), audio.getFullPathName(),
                                                       elsewhere.getChildFile ("Missing.annealpreset").getFullPathName() }, bad);
        bar.filesDropped ({ notPreset.getFullPathName() }, 10, 10);
        const bool explained = bar.getShownText().contains ("Notes.annealpreset");
        const auto names = r.proc->getUserPresetNames();
        check (noDuplicate && renamed && none.isEmpty() && bad.size() == 3 && explained && names.size() == 2,
               "the same preset isn't added twice; a clash becomes \"Name 2\"; non-presets are refused",
               "your presets: " + names.joinIntoString (", ") + "; refused " + juce::String (bad.size()) + "; bar: \"" + bar.getShownText() + "\"");
    }

    {
        // The menu can copy the presets folder's path (not run here: it would replace your clipboard).
        check (bar.onCopyFolderPath != nullptr && r.proc->getUserPresetFolder() == mine,
               "the preset menu has \"Copy the presets folder path\"", r.proc->getUserPresetFolder().getFullPathName());
    }
    ed.reset();
    tmp.deleteRecursively();
}

// 5.2.5: the zero-latency filter design's log, exp, sin and cos through fastmath, in batches.
void testZeroDesign525()
{
    section ("44. 5.2.5 faster zero-latency filter design");
    namespace fm = anneal::fastmath;

    {
        // sin / cos against the standard library (in double), with SIMD and as plain loops.
        std::mt19937 rng (31);
        const int n = 100003;
        std::vector<float> x ((size_t) n), s ((size_t) n), c ((size_t) n), sp ((size_t) n), cp ((size_t) n);
        double worstNear = 0.0, worstFar = 0.0, worstPlain = 0.0;
        for (double range : { 100.0, 10000.0 })
        {
            std::uniform_real_distribution<double> u (-range, range);
            for (auto& v : x)
                v = (float) u (rng);
            const float special[] = { 0.0f, -0.0f, 0.78539816f, -0.78539816f, 1.57079633f, 3.14159265f, -3.14159265f, 4.71238898f, 6.28318531f };
            std::copy (std::begin (special), std::end (special), x.begin());
            fm::sinCos (x.data(), s.data(), c.data(), n);
            fm::sinCosScalar (x.data(), sp.data(), cp.data(), n);
            double& worst = range < 1000.0 ? worstNear : worstFar;
            for (size_t i = 0; i < (size_t) n; ++i)
            {
                const double xs = std::sin ((double) x[i]), xc = std::cos ((double) x[i]);
                worst = std::max ({ worst, std::abs (s[i] - xs), std::abs (c[i] - xc) });
                worstPlain = std::max ({ worstPlain, std::abs (sp[i] - xs), std::abs (cp[i] - xc) });
            }
        }
        const float edges[4] = { std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
                                 -std::numeric_limits<float>::infinity(), 1.0e30f };
        float es[4], ec[4];
        fm::sinCos (edges, es, ec, 4);
        bool finite = true;
        for (int i = 0; i < 4; ++i)
            finite = finite && std::isfinite (es[i]) && std::isfinite (ec[i]) && std::abs (es[i] * es[i] + ec[i] * ec[i] - 1.0f) < 1.0e-5f;
        check (worstNear < 3.0e-7 && worstFar < 1.0e-6 && worstPlain < 1.0e-6 && finite, "sin / cos (fast) against the standard library",
               "largest error " + juce::String (worstNear, 9) + " for |x| <= 100, " + juce::String (worstFar, 9) + " up to 1e4 (plain loops "
                   + juce::String (worstPlain, 9) + "); NaN, inf, 1e30 stay finite: " + (finite ? "yes" : "NO"));
    }

    // The design, fast against exact (the standard library one value at a time, as up to 5.2.4), on
    // three curves: the 3 kHz dip of section 37, a curve moving +-12 dB, and a Side curve with Width
    // painted and Mono bass.
    const double fs = 48000.0, twoPi = 6.283185307179586;
    anneal::ZeroLatencyFilter fast, exact;
    fast.prepare (fs, 2, 1024, 160);
    exact.prepare (fs, 2, 1024, 160);
    exact.setExactMath (true);
    const int bins = 513, taps = fast.getTaps();
    std::vector<float> dip ((size_t) bins), wild ((size_t) bins), share ((size_t) bins);
    std::mt19937 rng (5);
    std::uniform_real_distribution<double> phaseRand (0.0, twoPi);
    const double p1 = phaseRand (rng), p2 = phaseRand (rng);
    for (int k = 0; k < bins; ++k)
    {
        const double f = std::max (1.0, k * fs / 1024.0), z = anneal::erb::hzToCam (f);
        dip[(size_t) k] = (float) std::pow (10.0, -9.0 * std::exp (-0.5 * std::pow (std::log2 (f / 3000.0) / 0.3, 2.0)) / 20.0);
        wild[(size_t) k] = (float) std::pow (10.0, (8.0 * std::sin (0.7 * z + p1) + 4.0 * std::sin (2.1 * z + p2)) / 20.0);
        share[(size_t) k] = f < 1000.0 ? 0.0f : f < 2000.0 ? (float) ((f - 1000.0) / 1000.0) : 1.0f;
    }
    auto response = [&] (const std::vector<float>& h, double f)
    {
        double re = 0.0, im = 0.0;
        for (size_t n = 0; n < h.size(); ++n)
        {
            re += h[n] * std::cos (twoPi * f * (double) n / fs);
            im -= h[n] * std::sin (twoPi * f * (double) n / fs);
        }
        return 10.0 * std::log10 (re * re + im * im + 1.0e-30);
    };
    {
        double worstDb = 0.0, worstTap = 0.0;
        struct Case { const std::vector<float>* curve; float scale, monoHz; const float* widthShare; };
        const Case cases[] = { { &dip, 1.0f, 0.0f, nullptr }, { &wild, 1.0f, 0.0f, nullptr }, { &wild, 1.5f, 120.0f, share.data() } };
        for (const auto& cs : cases)
        {
            std::vector<float> hf ((size_t) taps), he ((size_t) taps);
            fast.design (cs.curve->data(), bins, cs.scale, cs.monoHz, hf.data(), cs.widthShare);
            exact.design (cs.curve->data(), bins, cs.scale, cs.monoHz, he.data(), cs.widthShare);
            float peak = 0.0f;
            for (int n = 0; n < taps; ++n)
            {
                peak = std::max (peak, std::abs (he[(size_t) n]));
                worstTap = std::max (worstTap, (double) std::abs (hf[(size_t) n] - he[(size_t) n]));
            }
            worstTap /= std::max (peak, 1.0e-9f);
            for (int i = 0; i < 60; ++i)
            {
                const double f = 30.0 * std::pow (600.0, i / 59.0);   // 30 Hz .. 18 kHz
                if (cs.monoHz > 0.0f && f < 160.0)
                    continue;   // the Mono bass notch: both far below -60 dB
                worstDb = std::max (worstDb, std::abs (response (hf, f) - response (he, f)));
            }
        }
        check (worstDb < 0.001 && worstTap < 1.0e-4, "the fast design = the exact one (3 curves, Width paint, Mono bass)",
               "largest response difference " + juce::String (worstDb, 6) + " dB, taps " + juce::String (worstTap, 8) + " of the peak");
    }

    {
        // Speed, reported (not a check: timings follow the machine's load).
        std::vector<float> h ((size_t) taps);
        auto best = [&] (anneal::ZeroLatencyFilter& z)
        {
            double fastest = 1.0e9;
            for (int run = 0; run < 5; ++run)
            {
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < 200; ++i)
                    z.design (wild.data(), bins, 1.0f, 0.0f, h.data());
                fastest = std::min (fastest, 1.0e6 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 200.0);
            }
            return fastest;
        };
        const double exactUs = best (exact), fastUs = best (fast);
        info ("one zero-latency filter design: " + juce::String (fastUs, 1) + " us (exact math " + juce::String (exactUs, 1) + " us)");
    }
}

//==============================================================================
// 5.2.6: zero latency lighter, part 2: the filter design on half-size transforms, and the old
// filters run only while the crossfade lasts.
void testZeroLighter526()
{
    section ("45. 5.2.6 lighter zero latency (half-size design transforms, crossfade)");
    const double fs = 48000.0, twoPi = 6.283185307179586;
    anneal::ZeroLatencyFilter now, before;
    before.setFullTransforms (true);   // 5.2.5's design
    now.prepare (fs, 2, 1024, 160);
    before.prepare (fs, 2, 1024, 160);
    const int bins = 513, taps = now.getTaps();

    // Section 44's curves, and the Mid and Side curves the engine measures on music (Mid/Side,
    // Reveal and Smooth at 100 %, zero latency).
    std::vector<float> dip ((size_t) bins), wild ((size_t) bins), share ((size_t) bins);
    std::mt19937 rng (5);
    std::uniform_real_distribution<double> phaseRand (0.0, twoPi);
    const double p1 = phaseRand (rng), p2 = phaseRand (rng);
    for (int k = 0; k < bins; ++k)
    {
        const double f = std::max (1.0, k * fs / 1024.0), z = anneal::erb::hzToCam (f);
        dip[(size_t) k] = (float) std::pow (10.0, -9.0 * std::exp (-0.5 * std::pow (std::log2 (f / 3000.0) / 0.3, 2.0)) / 20.0);
        wild[(size_t) k] = (float) std::pow (10.0, (8.0 * std::sin (0.7 * z + p1) + 4.0 * std::sin (2.1 * z + p2)) / 20.0);
        share[(size_t) k] = f < 1000.0 ? 0.0f : f < 2000.0 ? (float) ((f - 1000.0) / 1000.0) : 1.0f;
    }
    std::vector<float> realMid ((size_t) bins, 1.0f), realSide ((size_t) bins, 1.0f);
    bool measured = false;
    double realSpread = 0.0;   // how far the engine's curve moves (dB), so the check isn't on a flat curve
    {
        Rig r;
        r.setNeutral();
        r.set (ids::reveal, 100.0f);
        r.set (ids::smooth, 100.0f);
        r.set (ids::stereoMode, 1.0f);
        r.set (ids::zeroLatency, 1.0f);
        r.prepare (fs, 512);
        r.run (testsig::musicLike (fs, 3.0), fixedBlocks (512));
        const auto& e = r.proc->getEngine();
        measured = e.getNumBins() == bins;
        if (measured)
        {
            std::copy_n (e.getCurveMid(), bins, realMid.begin());
            std::copy_n (e.getCurveSide(), bins, realSide.begin());
        }
        const auto [lo, hi] = std::minmax_element (realMid.begin(), realMid.end());
        realSpread = 20.0 * std::log10 (std::max (*hi, 1.0e-6f) / std::max (*lo, 1.0e-6f));
    }

    auto response = [&] (const std::vector<float>& h, double f)
    {
        double re = 0.0, im = 0.0;
        for (size_t n = 0; n < h.size(); ++n)
        {
            re += h[n] * std::cos (twoPi * f * (double) n / fs);
            im -= h[n] * std::sin (twoPi * f * (double) n / fs);
        }
        return 10.0 * std::log10 (re * re + im * im + 1.0e-30);
    };
    {
        // Responses compared from 30 Hz to 18 kHz wherever 5.2.5's is above -60 dB (not in the Mono
        // bass notch or where the sides are painted out), taps everywhere.
        struct Case { const std::vector<float>* curve; float scale, monoHz; const float* widthShare; };
        const Case cases[] = {
            { &dip, 1.0f, 0.0f, nullptr },        { &wild, 1.0f, 0.0f, nullptr },
            { &wild, 1.5f, 120.0f, share.data() }, { &wild, 1.5f, 20.0f, nullptr },   // Mono bass at its lowest
            { &wild, 2.0f, 400.0f, nullptr },      // Mono bass at its highest, Width 200 %
            { &dip, 0.0f, 0.0f, share.data() },    // Width 0 % painted to act above 1 kHz
            { &realMid, 1.0f, 0.0f, nullptr },     { &realSide, 1.0f, 0.0f, nullptr },
            { &realSide, 1.5f, 120.0f, share.data() } };
        double worstDb = 0.0, worstDb40 = 0.0, worstTap = 0.0;   // above -60 dB, above -40 dB
        int compared = 0;
        for (const auto& cs : cases)
        {
            std::vector<float> hn ((size_t) taps), hb ((size_t) taps);
            now.design (cs.curve->data(), bins, cs.scale, cs.monoHz, hn.data(), cs.widthShare);
            before.design (cs.curve->data(), bins, cs.scale, cs.monoHz, hb.data(), cs.widthShare);
            float peak = 0.0f;
            double tap = 0.0;
            for (int n = 0; n < taps; ++n)
            {
                peak = std::max (peak, std::abs (hb[(size_t) n]));
                tap = std::max (tap, (double) std::abs (hn[(size_t) n] - hb[(size_t) n]));
            }
            worstTap = std::max (worstTap, tap / std::max (peak, 1.0e-9f));
            for (int i = 0; i < 120; ++i)
            {
                const double f = 30.0 * std::pow (600.0, i / 119.0);
                const double rb = response (hb, f);
                if (rb < -60.0)
                    continue;
                const double d = std::abs (response (hn, f) - rb);
                worstDb = std::max (worstDb, d);
                if (rb >= -40.0)
                    worstDb40 = std::max (worstDb40, d);
                ++compared;
            }
        }
        // The running sums of the half-size transforms round a little more than 5.2.5's full ones
        // (its distance from the exact math 0.00004 dB at 0 dB, now 0.0003 dB): far below hearing.
        check (measured && realSpread > 3.0 && compared > 900 && worstDb40 < 0.001 && worstDb < 0.005 && worstTap < 1.0e-5,
               "the design on half-size transforms = 5.2.5's (9 curves: music, Width, paint, Mono bass 20-400 Hz)",
               "largest response difference " + juce::String (worstDb40, 6) + " dB above -40 dB, " + juce::String (worstDb, 6)
                   + " dB down to -60 dB; taps " + juce::String (worstTap, 8) + " of the peak (the engine's curve moving "
                   + db (realSpread, 1) + ")");
    }

    {
        // The old filters run only during the 1.33 ms crossfade after each design (up to 5.2.5, at
        // big buffers, for the whole 3.3 ms between designs), and the output doesn't depend on the
        // block sizes. Mid/Side, Reveal and Smooth 100 %, Width 150 %, Mono bass 120 Hz.
        const auto music = testsig::musicLike (fs, 3.0);
        Channels outs[3];
        double perFrame[3] {};
        int b = 0;
        for (const auto& blocks : { fixedBlocks (512), fixedBlocks (64), randomBlocks (1, 700, 11) })
        {
            Rig r;
            r.setNeutral();
            r.set (ids::reveal, 100.0f);
            r.set (ids::smooth, 100.0f);
            r.set (ids::stereoMode, 1.0f);
            r.set (ids::width, 150.0f);
            r.set (ids::monoBass, 120.0f);
            r.set (ids::zeroLatency, 1.0f);
            r.prepare (fs, 512);
            const auto faded0 = r.proc->getZeroFilter().getCrossfadedSamples();
            const auto frames0 = r.proc->getEngine().getFrameCount();
            outs[b] = r.run (music, blocks);
            const auto frames = r.proc->getEngine().getFrameCount() - frames0;
            perFrame[b] = (double) (r.proc->getZeroFilter().getCrossfadedSamples() - faded0) / std::max (1u, frames);
            ++b;
        }
        double diff = 0.0;
        for (int v = 1; v < 3; ++v)
            for (size_t c = 0; c < 2; ++c)
                for (size_t i = 0; i < music[0].size(); ++i)
                    diff = std::max (diff, (double) std::abs (outs[v][c][i] - outs[0][c][i]));
        const double fadeLength = std::round (0.00133 * fs);
        const double worstPerFrame = std::max ({ perFrame[0], perFrame[1], perFrame[2] });
        check (worstPerFrame <= fadeLength + 0.5 && perFrame[0] > 0.5 * fadeLength && diff < 1.0e-6,
               "zero latency: the old filters run only during the crossfade, the same output at any block size",
               "samples with both filters per design: " + juce::String (perFrame[0], 1) + " (512-sample blocks), "
                   + juce::String (perFrame[1], 1) + " (64), " + juce::String (perFrame[2], 1) + " (random); the fade is "
                   + juce::String ((int) fadeLength) + "; outputs differ by " + juce::String (diff, 9));
    }

    {
        // Speed, reported (not a check: timings follow the machine's load).
        std::vector<float> h ((size_t) taps);
        auto best = [&] (anneal::ZeroLatencyFilter& z)
        {
            double fastest = 1.0e9;
            for (int run = 0; run < 5; ++run)
            {
                const auto t0 = std::chrono::steady_clock::now();
                for (int i = 0; i < 200; ++i)
                    z.design (wild.data(), bins, 1.0f, 0.0f, h.data());
                fastest = std::min (fastest, 1.0e6 * std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count() / 200.0);
            }
            return fastest;
        };
        const double beforeUs = best (before), nowUs = best (now);
        info ("one zero-latency filter design: " + juce::String (nowUs, 1) + " us (5.2.5's full-size transforms: " + juce::String (beforeUs, 1) + " us)");
    }
}

// The Punch knob of an open editor (a compact SHAPE row), or nullptr.
anneal::Knob* punchKnobOf (AnnealEditorContent& content)
{
    for (auto* c : content.getChildren())
        if (auto* k = dynamic_cast<anneal::Knob*> (c); k != nullptr && k->slider.getTitle() == "Punch")
            return k;
    return nullptr;
}

// Pixels that differ between the Punch ring drawn as it is now and with no activity shown (2x scale).
int punchArcPixels (anneal::Knob& knob)
{
    auto& s = knob.slider;
    const auto shown = s.createComponentSnapshot (s.getLocalBounds(), true, 2.0f);
    const double was = s.getProperties().getWithDefault ("activity", -1.0);
    s.getProperties().set ("activity", -1.0);
    const auto plain = s.createComponentSnapshot (s.getLocalBounds(), true, 2.0f);
    s.getProperties().set ("activity", was);
    int differ = 0;
    for (int y = 0; y < std::min (shown.getHeight(), plain.getHeight()); ++y)
        for (int x = 0; x < std::min (shown.getWidth(), plain.getWidth()); ++x)
            differ += shown.getPixelAt (x, y) != plain.getPixelAt (x, y) ? 1 : 0;
    return differ;
}

// Plucks that stop 10 ms after their last attack, so the newest display frame holds that attack.
Channels plucksEndingInAnAttack (double fs)
{
    std::vector<int> onsets;
    auto x = pluckTrain (fs, 2.0, 2000.0, onsets);
    for (auto& ch : x)
        ch.resize ((size_t) (onsets.back() + (int) (0.010 * fs)));
    return x;
}

//==============================================================================
// 5.2.7 bug pass. The Punch ring's inner arc (5.0, docs/DESIGN.md: "its inner arc shows how hard the
// latest attack is shaped") was never drawn: a compact knob dropped its activity in setActivity, and
// the look-and-feel drew the arc for big rings only.
void testFixes527()
{
    section ("46. 5.2.7 bug fixes");
    const double fs = 48000.0;
    Rig r;
    r.setNeutral();
    r.set (ids::punch, 100.0f);
    r.prepare (fs, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
    auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
    auto* knob = editor != nullptr ? punchKnobOf (editor->getContent()) : nullptr;
    if (knob == nullptr)
    {
        check (false, "the Punch ring shows its attacks (inner arc)", "no editor or no Punch knob");
        return;
    }
    auto& content = editor->getContent();
    r.run (plucksEndingInAnAttack (fs), fixedBlocks (64));
    content.screen.refreshNow();
    content.tick (0.0f);
    const double shown = knob->slider.getProperties().getWithDefault ("activity", -1.0);
    const int arcPixels = punchArcPixels (*knob);
    check (shown > 0.25 && arcPixels > 20, "the Punch ring shows how hard the latest attack is shaped (its inner arc)",
           "Punch +100 %, Low, 10 ms into a 2 kHz pluck: activity " + juce::String (shown, 2) + " ("
               + juce::String (content.screen.getActivity().punchDb, 1) + " dB), " + juce::String (arcPixels) + " pixels of arc");
}

//==============================================================================
// 5.2.7 Punch at zero latency (roadmap item 16): a time-domain transient shaper after the zero-latency
// filters, inside Range, by the EQ routing. Punch 0 leaves the audio exactly as it is.
void testZeroPunch527()
{
    section ("47. 5.2.7 Punch at zero latency");
    const double fs = 48000.0;
    const double twoPi = 6.283185307179586;
    std::vector<int> onsets;
    const auto plucks = pluckTrain (fs, 4.0, 2000.0, onsets);
    const int attackLen = (int) (0.015 * fs), bodyFrom = (int) (0.08 * fs), bodyLen = (int) (0.08 * fs);

    struct Setup { float punch = 0.0f, rangeLo = 20.0f, rangeHi = 20000.0f; int stereoMode = 0, eqTarget = 0; float zeroPunch = 1.0f; };
    auto runZero = [&] (const Channels& in, const Setup& s, const std::function<int()>& blocks)
    {
        Rig r;
        r.setNeutral();
        r.set (ids::zeroLatency, 1.0f);
        r.set (ids::punch, s.punch);
        r.set (ids::rangeLo, s.rangeLo);
        r.set (ids::rangeHi, s.rangeHi);
        r.set (ids::stereoMode, (float) s.stereoMode);
        r.set (ids::eqTarget, (float) s.eqTarget);
        r.set (ids::zeroPunch, s.zeroPunch);
        r.prepare (fs, 512);
        return r.run (in, blocks);
    };
    // Attack (first 15 ms) and body (80-160 ms) of the plucks' tone, relative to the input (no latency).
    auto shape = [&] (const Channels& in, const Setup& s, double hz)
    {
        const auto out = runZero (in, s, fixedBlocks (256));
        return std::pair { windowGainDb (out, in, onsets, 0, 0, attackLen, hz, fs), windowGainDb (out, in, onsets, 0, bodyFrom, bodyLen, hz, fs) };
    };

    // Attacks louder or softer, the body left alone.
    {
        const auto off = shape (plucks, {}, 2000.0), up = shape (plucks, { 100.0f }, 2000.0), down = shape (plucks, { -100.0f }, 2000.0);
        info ("Zero latency, 2 kHz plucks: attack (first 15 ms) Punch off " + db (off.first, 2) + ", +100 % " + db (up.first, 2)
              + ", -100 % " + db (down.first, 2) + ";  body (80-160 ms) " + db (off.second, 3) + " / " + db (up.second, 3) + " / " + db (down.second, 3));
        check (up.first - off.first >= 3.0 && off.first - down.first >= 3.0
                   && std::abs (up.second - off.second) <= 0.1 && std::abs (down.second - off.second) <= 0.1,
               "Zero latency: Punch +100 % makes attacks louder, -100 % softer, the body within 0.1 dB",
               "attack " + db (up.first - off.first, 2) + " / " + db (down.first - off.first, 2) + ", body "
                   + db (up.second - off.second, 3) + " / " + db (down.second - off.second, 3));
    }

    // Punch 0 is the audio as it is, bit for bit (the shaper alone, every layout and block size), and so
    // is a steady sound with Punch on once its start has passed: the gain is only on attacks.
    {
        const auto music = testsig::musicLike (fs, 3.0);
        bool same = true;
        for (int layout = 0; layout < 3; ++layout)
        {
            anneal::TransientShaper t;
            t.prepare (fs);
            anneal::TransientShaper::Settings s;
            s.midSide = layout == 1;
            s.rangeLoHz = 30.0f;
            s.rangeHiHz = 18000.0f;
            Channels y = music;
            const int nch = layout == 2 ? 1 : 2;
            auto blocks = randomBlocks (1, 700, (unsigned) (11 + layout));
            for (int pos = 0; pos < (int) y[0].size();)
            {
                const int b = std::min (blocks(), (int) y[0].size() - pos);
                float* ptrs[2] = { y[0].data() + pos, y[1].data() + pos };
                t.process (ptrs, nch, b, s);
                pos += b;
            }
            same = same && y == music;
        }
        // Tones from 0.1 s (after silence, so their start is an attack); "after" is from 0.35 s on.
        const int n = (int) (2.0 * fs), from = (int) (0.1 * fs), after = (int) (0.35 * fs);
        Channels tones (2, std::vector<float> ((size_t) n, 0.0f));
        for (int i = from; i < n; ++i)
        {
            const double t = i / fs;
            tones[0][(size_t) i] = (float) (0.1 * std::sin (twoPi * 60.0 * t) + 0.1 * std::sin (twoPi * 2000.0 * t));
            tones[1][(size_t) i] = (float) (0.1 * std::sin (twoPi * 60.0 * t) - 0.05 * std::sin (twoPi * 3000.0 * t));
        }
        const auto still = runZero (tones, {}, fixedBlocks (512)), punched = runZero (tones, { 100.0f }, fixedBlocks (512));
        double diff = 0.0, start = 0.0;
        for (size_t c = 0; c < 2; ++c)
            for (int i = 0; i < n; ++i)
            {
                double& slot = i < after ? start : diff;
                slot = std::max (slot, (double) std::abs (punched[c][(size_t) i] - still[c][(size_t) i]));
            }
        check (same && diff == 0.0 && start > 0.01,
               "Zero latency: Punch 0 leaves the audio as it is; Punch on touches attacks only",
               juce::String (same ? "Punch 0: bit for bit in Stereo, Mid/Side and mono, random blocks" : "PUNCH 0 CHANGED THE AUDIO")
                   + "; steady tones at +100 %: " + juce::String (diff, 9) + " difference from 250 ms after their start (at the start "
                   + juce::String (start, 3) + ")");
    }

    // Range: the shaper splits off what is inside Range with gentle (6 dB/oct) edges, so a pluck two
    // octaves above Range High moves a little, in the same direction, and never the wrong way.
    {
        std::vector<int> o4;
        const auto high = pluckTrain (fs, 4.0, 4000.0, o4);
        const auto off = shape (high, { 0.0f, 20.0f, 1000.0f }, 4000.0), up = shape (high, { 100.0f, 20.0f, 1000.0f }, 4000.0),
                   down = shape (high, { -100.0f, 20.0f, 1000.0f }, 4000.0), inUp = shape (high, { 100.0f }, 4000.0);
        const double moveUp = up.first - off.first, moveDown = down.first - off.first, inside = inUp.first - off.first;
        check (moveUp >= 0.0 && moveDown <= 0.0 && std::max (moveUp, -moveDown) <= inside / 3.0,
               "Zero latency: Punch works inside Range (gentle edges, never the wrong way)",
               "4 kHz plucks, Range 20 Hz-1 kHz: +100 % " + db (moveUp, 2) + ", -100 % " + db (moveDown, 2) + "; inside Range "
                   + db (inside, 2));
    }

    // Mid/Side: Punch follows the EQ routing (EQ: Mid leaves a pluck in the sides alone).
    {
        Channels mid = plucks, side = plucks;
        for (size_t i = 0; i < plucks[0].size(); ++i)
        {
            mid[1][i] = plucks[0][i];
            side[1][i] = -plucks[0][i];
        }
        const auto midOff = shape (mid, { 0.0f, 20.0f, 20000.0f, 1, 1 }, 2000.0), midUp = shape (mid, { 100.0f, 20.0f, 20000.0f, 1, 1 }, 2000.0);
        const auto sideOff = shape (side, { 0.0f, 20.0f, 20000.0f, 1, 1 }, 2000.0), sideUp = shape (side, { 100.0f, 20.0f, 20000.0f, 1, 1 }, 2000.0);
        check (midUp.first - midOff.first >= 3.0 && std::abs (sideUp.first - sideOff.first) < 0.01,
               "Zero latency, Mid/Side: Punch follows the EQ routing",
               "EQ: Mid, +100 %: a pluck in the centre " + db (midUp.first - midOff.first, 2) + ", in the sides "
                   + db (sideUp.first - sideOff.first, 3));
    }

    // The same output at any block size; Punch, Range and Mid/Side automated every block stay clean.
    {
        const Setup s { 60.0f, 30.0f, 18000.0f, 0, 0 };
        const auto a = runZero (plucks, s, fixedBlocks (512)), b = runZero (plucks, s, fixedBlocks (64)), c = runZero (plucks, s, randomBlocks (1, 700, 5));
        double diff = 0.0;
        for (size_t ch = 0; ch < 2; ++ch)
            for (size_t i = 0; i < plucks[0].size(); ++i)
                diff = std::max ({ diff, (double) std::abs (b[ch][i] - a[ch][i]), (double) std::abs (c[ch][i] - a[ch][i]) });

        Rig r;
        r.set (ids::zeroLatency, 1.0f);
        r.prepare (fs, 512);
        const auto music = testsig::musicLike (fs, 6.0, 3);
        std::mt19937 rng (21);
        std::uniform_real_distribution<float> u (0.0f, 1.0f);
        Channels out = music;
        juce::AudioBuffer<float> buf (2, 1024);
        juce::MidiBuffer midi;
        for (int pos = 0; pos < (int) music[0].size();)
        {
            const int len = std::min (1 + (int) (u (rng) * 700.0f), (int) music[0].size() - pos);
            r.set (ids::punch, -100.0f + 200.0f * u (rng));
            r.set (ids::rangeLo, 20.0f + 1500.0f * u (rng));
            r.set (ids::rangeHi, 1000.0f + 19000.0f * u (rng));
            r.set (ids::stereoMode, u (rng) < 0.1f ? 1.0f - r.proc->apvts.getRawParameterValue (ids::stereoMode)->load() : r.proc->apvts.getRawParameterValue (ids::stereoMode)->load());
            r.set (ids::eqTarget, (float) (int) (u (rng) * 2.99f));
            buf.setSize (2, len, false, false, true);
            for (int ch = 0; ch < 2; ++ch)
                buf.copyFrom (ch, 0, music[(size_t) ch].data() + pos, len);
            r.proc->processBlock (buf, midi);
            for (int ch = 0; ch < 2; ++ch)
                std::copy (buf.getReadPointer (ch), buf.getReadPointer (ch) + len, out[(size_t) ch].data() + pos);
            pos += len;
        }
        check (diff < 1.0e-6 && allFinite (out) && maxAbs (out) < 8.0 * maxAbs (music),
               "Zero latency Punch: same output at any block size, clean under automation",
               "512 / 64 / random blocks differ by " + juce::String (diff, 9) + "; automated every block: peak "
                   + db (toDb (maxAbs (out)), 1) + " (input " + db (toDb (maxAbs (music)), 1) + ")");
    }

    // Projects from before 5.2.7 keep Punch resting at zero latency (as they were made); new ones have it.
    {
        Rig a;
        a.set (ids::zeroLatency, 1.0f);
        a.set (ids::punch, 100.0f);
        juce::MemoryBlock blob;
        a.proc->getStateInformation (blob);
        auto xml = juce::AudioProcessor::getXmlFromBinary (blob.getData(), (int) blob.getSize());
        if (auto* el = xml->getChildByAttribute ("id", ids::zeroPunch))
            xml->removeChildElement (el, true);
        juce::MemoryBlock old;
        juce::AudioProcessor::copyXmlToBinary (*xml, old);
        Rig b;
        b.proc->setStateInformation (old.getData(), (int) old.getSize());
        const bool fresh = Rig().proc->apvts.getRawParameterValue (ids::zeroPunch)->load() > 0.5f;
        const bool oldOff = b.proc->apvts.getRawParameterValue (ids::zeroPunch)->load() < 0.5f;
        const auto restingOld = shape (plucks, { 100.0f, 20.0f, 20000.0f, 0, 0, 0.0f }, 2000.0), plain = shape (plucks, {}, 2000.0);
        auto* zp = b.proc->apvts.getParameter (ids::zeroPunch);
        check (fresh && oldOff && std::abs (restingOld.first - plain.first) < 1.0e-9 && ! zp->isAutomatable(),
               "projects from before 5.2.7: Punch at Zero off (rests, as made); new instances: on",
               juce::String ("new instance ") + (fresh ? "on" : "OFF") + ", old project " + (oldOff ? "off" : "ON")
                   + "; Punch +100 % with it off moves attacks by " + db (restingOld.first - plain.first, 6));
    }

    // The editor: the Punch ring shows the shaper's move at zero latency; the tag no longer says Punch rests.
    {
        Rig r;
        r.setNeutral();
        r.set (ids::zeroLatency, 1.0f);
        r.set (ids::punch, 100.0f);
        r.prepare (fs, 512);
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
        auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
        auto* knob = editor != nullptr ? punchKnobOf (editor->getContent()) : nullptr;
        if (knob == nullptr)
        {
            check (false, "Zero latency: the Punch ring shows the shaper's move", "no editor or no Punch knob");
            return;
        }
        r.run (plucksEndingInAnAttack (fs), fixedBlocks (64));
        editor->getContent().tick (0.0f);
        const double on = knob->slider.getProperties().getWithDefault ("activity", -1.0);
        r.set (ids::zeroPunch, 0.0f);
        r.run (plucksEndingInAnAttack (fs), fixedBlocks (64));
        editor->getContent().tick (0.0f);
        const double rests = knob->slider.getProperties().getWithDefault ("activity", -1.0);
        check (on > 0.5 && rests < 0.0, "Zero latency: the Punch ring shows the shaper's move (none with Punch at Zero off)",
               "10 ms into a pluck at +100 %: activity " + juce::String (on, 2) + "; with it off " + juce::String (rests, 2));
    }
}

// 5.3.1 bug pass. The preset bar's notes after a drop (5.2.4) showed garbled characters: the curly
// quotes, apostrophes and the middle dot were saved in the source twice-encoded (UTF-8 read as Windows-1252
// and saved as UTF-8 again), so each showed as an "a" with a circumflex and invisible control characters.
void testFixes531()
{
    section ("48. 5.3.1 bug fixes");
    const auto tmp = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("AnnealTests-fixes531");
    tmp.deleteRecursively();
    const auto mine = tmp.getChildFile ("mine"), elsewhere = tmp.getChildFile ("elsewhere");
    elsewhere.createDirectory();

    Rig r;
    r.proc->setUserPresetFolder (mine, false);
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
    auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
    if (editor == nullptr)
    {
        check (false, "the preset bar's notes read correctly", "no AnnealEditor");
        return;
    }
    auto& bar = editor->getContent().presets;
    anneal::presets::writeUser (elsewhere, "Bright Mix", anneal::presets::currentValues (r.proc->apvts));
    const auto notPreset = elsewhere.getChildFile ("Notes.annealpreset");
    notPreset.replaceWithText ("<NotAnneal/>");

    // Only letters, digits, ASCII punctuation and the typographic marks the notes use.
    auto clean = [] (const juce::String& s)
    {
        for (auto ch : s)
            if (ch >= 0x80 && ch != 0x201c && ch != 0x201d && ch != 0x2019 && ch != 0x00b7)
                return false;
        return true;
    };
    bar.filesDropped ({ elsewhere.getChildFile ("Bright Mix.annealpreset").getFullPathName() }, 10, 10);
    const auto added = bar.getShownText();
    bar.filesDropped ({ notPreset.getFullPathName() }, 10, 10);
    const auto refused = bar.getShownText();
    bar.filesDropped ({ elsewhere.getChildFile ("Bright Mix.annealpreset").getFullPathName(), notPreset.getFullPathName() }, 10, 10);
    const auto mixed = bar.getShownText();
    const bool ok = added == juce::String::fromUTF8 ("Added \xe2\x80\x9c" "Bright Mix\xe2\x80\x9d")
                    && refused == juce::String::fromUTF8 ("Notes.annealpreset isn\xe2\x80\x99t an Anneal preset")
                    && mixed == juce::String::fromUTF8 ("Added \xe2\x80\x9c" "Bright Mix\xe2\x80\x9d \xc2\xb7 1 not added")
                    && clean (added) && clean (refused) && clean (mixed);
    check (ok, "the preset bar's notes after a drop read correctly",
           "\"" + added + "\" / \"" + refused + "\" / \"" + mixed + "\"");
    ed.reset();
    tmp.deleteRecursively();
}

//==============================================================================
// 5.3.1 Loudness readout (roadmap item 17): EBU R128 short-term and integrated loudness of the input
// and the output, in the header beside MATCH. Checked against EBU Tech 3341's test signals.
namespace loudtest
{
    struct Part { double seconds, dbfs; };

    // EBU Tech 3341's signals: a 1 kHz sine at the given peak level on every channel, part after part.
    Channels sines1k (double fs, std::initializer_list<Part> parts, int channels = 2)
    {
        std::vector<float> x;
        double phase = 0.0;
        for (const auto& p : parts)
        {
            const auto n = (size_t) std::llround (p.seconds * fs);
            const double a = std::pow (10.0, p.dbfs / 20.0);
            for (size_t i = 0; i < n; ++i)
            {
                x.push_back ((float) (a * std::sin (phase)));
                phase += 6.283185307179586 * 1000.0 / fs;
            }
        }
        return Channels ((size_t) channels, x);
    }

    // Integrated and the short-term values every 100 ms, fed in random blocks.
    struct Result { float integrated = 0.0f; std::vector<float> shortTerm; };
    Result measure (const Channels& in, double fs, unsigned seed)
    {
        anneal::LoudnessMeter m;
        m.prepare (fs);
        Result r;
        auto blocks = randomBlocks (1, 1500, seed);
        const int n = (int) in[0].size(), step = (int) std::lround (0.1 * fs);
        int nextRead = step;
        for (int pos = 0; pos < n;)
        {
            const int b = std::min ({ blocks(), n - pos, nextRead - pos });
            const float* ptrs[2] = { in[0].data() + pos, in.size() > 1 ? in[1].data() + pos : nullptr };
            m.process (ptrs, (int) in.size(), b);
            pos += b;
            if (pos == nextRead)
            {
                r.shortTerm.push_back (m.getShortTerm());
                nextRead += step;
            }
        }
        r.integrated = m.getIntegrated();
        return r;
    }
}

void testLoudness531()
{
    section ("49. 5.3.1 Loudness readout (EBU R128)");
    using loudtest::sines1k;
    using loudtest::measure;

    {
        // EBU Tech 3341, cases 1-5: integrated loudness within +-0.1 LU (the gates at -70 LUFS and
        // 10 LU under the mean leave the quiet parts out).
        const double fs = 48000.0;
        struct Case { const char* name; Channels in; double expected; };
        const Case cases[] = {
            { "1 (-23 dBFS)", sines1k (fs, { { 20.0, -23.0 } }), -23.0 },
            { "2 (-33 dBFS)", sines1k (fs, { { 20.0, -33.0 } }), -33.0 },
            { "3 (-36/-23/-36)", sines1k (fs, { { 10.0, -36.0 }, { 60.0, -23.0 }, { 10.0, -36.0 } }), -23.0 },
            { "4 (-72/-36/-23/-36/-72)", sines1k (fs, { { 10.0, -72.0 }, { 10.0, -36.0 }, { 60.0, -23.0 }, { 10.0, -36.0 }, { 10.0, -72.0 } }), -23.0 },
            { "5 (-26/-20/-26)", sines1k (fs, { { 20.0, -26.0 }, { 20.1, -20.0 }, { 20.0, -26.0 } }), -23.0 },
        };
        bool ok = true;
        juce::String detail;
        unsigned seed = 1;
        for (const auto& c : cases)
        {
            const float got = measure (c.in, fs, seed++).integrated;
            ok = ok && std::abs (got - c.expected) <= 0.1;
            detail << "case " << c.name << ": " << juce::String (got, 2) << "  ";
        }
        check (ok, "integrated loudness: EBU Tech 3341 cases 1-5", detail + "(LUFS; each within 0.1 LU)");
    }

    {
        // Case 9: 1.34 s at -20 dBFS, 1.66 s at -30, twenty times. Short-term is -23.0 +-0.1 LUFS after 3 s.
        // A steady -23 dBFS reads -23.0 short-term too, at 44.1, 48 and 96 kHz; one channel (mono) 3 dB less.
        const double fs = 48000.0;
        Channels in;
        {
            std::vector<float> x;
            for (int i = 0; i < 20; ++i)
            {
                const auto a = sines1k (fs, { { 1.34, -20.0 } }, 1)[0], b = sines1k (fs, { { 1.66, -30.0 } }, 1)[0];
                x.insert (x.end(), a.begin(), a.end());
                x.insert (x.end(), b.begin(), b.end());
            }
            in = Channels (2, x);
        }
        const auto r = measure (in, fs, 7);
        float lo = 0.0f, hi = -200.0f;
        for (size_t i = 30; i < r.shortTerm.size(); ++i)
        {
            lo = std::min (lo, r.shortTerm[i]);
            hi = std::max (hi, r.shortTerm[i]);
        }
        float worstRate = 0.0f;
        juce::String rates;
        for (double rate : { 44100.0, 48000.0, 96000.0 })
        {
            const auto s = measure (sines1k (rate, { { 5.0, -23.0 } }), rate, 3);
            worstRate = std::max ({ worstRate, std::abs (s.shortTerm.back() + 23.0f), std::abs (s.integrated + 23.0f) });
            rates << juce::String (rate / 1000.0, 1) << " kHz " << juce::String (s.shortTerm.back(), 2) << "  ";
        }
        const auto mono = measure (sines1k (fs, { { 5.0, -20.0 } }, 1), fs, 4);
        check (std::abs (lo + 23.0f) <= 0.1f && std::abs (hi + 23.0f) <= 0.1f && worstRate <= 0.1f
                   && std::abs (mono.integrated + 23.01f) <= 0.1f,
               "short-term loudness: EBU case 9, rates, mono",
               "case 9 after 3 s: " + juce::String (lo, 2) + " to " + juce::String (hi, 2) + " LUFS; steady -23 dBFS: " + rates
                   + "; mono -20 dBFS: " + juce::String (mono.integrated, 2));
    }

    // Through the plug-in: input and output, Output -6 dB, in a latency mode and at zero latency;
    // Bypass reads the same in and out.
    const double fs = 48000.0;
    const auto tone = sines1k (fs, { { 8.0, -23.0 } });
    auto runRig = [&] (float outputDb, bool zero, bool bypass, Rig& r)
    {
        r.setNeutral();
        r.set (ids::output, outputDb);
        r.set (ids::zeroLatency, zero ? 1.0f : 0.0f);
        r.set (ids::bypass, bypass ? 1.0f : 0.0f);
        r.prepare (fs, 512);
        r.run (tone, randomBlocks (32, 1024, 5));
        return r.proc->getLoudness();
    };
    {
        Rig a, b, c;
        const auto low = runRig (-6.0f, false, false, a);
        const auto zero = runRig (-6.0f, true, false, b);
        const auto byp = runRig (-6.0f, false, true, c);
        auto near = [] (float v, float want) { return std::abs (v - want) <= 0.1f; };
        const bool ok = near (low.shortIn, -23.0f) && near (low.integratedIn, -23.0f) && near (low.shortOut, -29.0f)
                        && near (low.integratedOut, -29.0f) && near (zero.shortIn, -23.0f) && near (zero.integratedOut, -29.0f)
                        && near (byp.shortOut, -23.0f) && near (byp.integratedOut, -23.0f) && near (byp.integratedIn, -23.0f);
        check (ok, "the plug-in measures its input and its output",
               "-23 dBFS tone, Output -6 dB, Low: in " + juce::String (low.shortIn, 2) + " / " + juce::String (low.integratedIn, 2)
                   + ", out " + juce::String (low.shortOut, 2) + " / " + juce::String (low.integratedOut, 2) + " (S / I); Zero latency out "
                   + juce::String (zero.integratedOut, 2) + "; Bypass out " + juce::String (byp.integratedOut, 2));
    }
    {
        // A click starts the integrated values over (at once on screen); what's played after it is
        // measured alone (8 s at -23 dBFS, then -33: without the click it would read about -25.4).
        // A host stop or a new buffer size keeps them; short-term starts over then.
        Rig r;
        runRig (0.0f, false, false, r);
        r.run (sines1k (fs, { { 0.5, -33.0 } }), fixedBlocks (256));   // the latency's worth of -23 has left
        r.proc->resetLoudness();
        const auto cleared = r.proc->getLoudness();
        r.run (sines1k (fs, { { 6.0, -33.0 } }), fixedBlocks (256));
        const auto after = r.proc->getLoudness();
        r.proc->reset();
        r.prepare (fs, 128);
        r.run (sines1k (fs, { { 0.05, -33.0 } }), fixedBlocks (128));
        const auto kept = r.proc->getLoudness();
        const bool ok = cleared.integratedIn == anneal::LoudnessMeter::none && cleared.integratedOut == anneal::LoudnessMeter::none
                        && std::abs (after.integratedIn + 33.0f) <= 0.1f && std::abs (after.integratedOut + 33.0f) <= 0.1f
                        && std::abs (kept.integratedOut - after.integratedOut) < 0.01f && kept.shortOut == anneal::LoudnessMeter::none;
        check (ok, "a click starts it over; a host stop keeps it",
               "after the click " + juce::String (cleared.integratedIn, 0) + ", then 6 s at -33 dBFS: " + juce::String (after.integratedIn, 2)
                   + " / " + juce::String (after.integratedOut, 2) + "; after a stop and a new buffer size " + juce::String (kept.integratedOut, 2)
                   + " (short-term " + juce::String (kept.shortOut, 0) + ")");
    }
    {
        // What the two meters (input and output) cost: 60 s of stereo noise at 48 kHz in 64-sample blocks.
        const auto noise = pinkNoise (fs, 60.0, 0.1f, 9);
        anneal::LoudnessMeter in, out;
        const auto t0 = std::chrono::steady_clock::now();
        for (size_t pos = 0; pos + 64 <= noise[0].size(); pos += 64)
        {
            const float* ptrs[2] = { noise[0].data() + pos, noise[1].data() + pos };
            in.process (ptrs, 2, 64);
            out.process (ptrs, 2, 64);
        }
        const double seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        info ("the two meters: " + juce::String (100.0 * seconds / 60.0, 3) + " % of one core (stereo, 48 kHz, 64-sample blocks); "
              "pink noise reads " + juce::String (in.getIntegrated(), 2) + " LUFS");
    }
    {
        // On screen: the header, between Paint and MATCH, S over I, input -> output; a click resets I.
        Rig r;
        runRig (-6.0f, false, false, r);
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
        auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
        if (editor == nullptr)
        {
            check (false, "the loudness readout shows in the header", "no AnnealEditor");
            return;
        }
        auto& content = editor->getContent();
        content.tick (0.0f);
        const auto shown = content.loudness.getShownText();
        const auto m = juce::String::fromUTF8 ("\xe2\x88\x92"), dash = juce::String::fromUTF8 ("\xe2\x80\x94");
        const bool values = shown == "S " + m + "23.0 > " + m + "29.0 | I " + m + "23.0 > " + m + "29.0";
        content.loudness.onReset();
        content.tick (0.0f);
        const auto afterClick = content.loudness.getShownText();
        const bool reset = afterClick == "S " + m + "23.0 > " + m + "29.0 | I " + dash + " > " + dash;
        juce::Component* paint = nullptr;
        juce::Component* match = nullptr;
        for (auto* c : content.getChildren())
            if (auto* b = dynamic_cast<juce::TextButton*> (c))
            {
                if (b->getButtonText() == "PAINT")
                    paint = c;
                if (b->getButtonText().startsWith ("MATCH"))
                    match = c;
            }
        const auto box = content.loudness.getBounds();
        const bool placed = paint != nullptr && match != nullptr && box.getX() > paint->getRight() && box.getRight() < match->getX()
                            && box.getY() >= 0 && box.getBottom() <= 48 && content.loudness.isVisible()
                            && content.loudness.getTooltip().contains ("EBU R128");
        check (values && reset && placed, "the loudness readout in the header, reset with a click",
               "\"" + shown + "\", after a click \"" + afterClick + "\"; at " + box.toString());
    }
}

//==============================================================================
// 5.3.2 bug fix: in Mid/Side the sidechain follows the EQ routing, like the rest of the auto EQ. It used to
// duck (or lift) a component the EQ wasn't routed to: the sides with EQ: Mid, the centre with EQ: Side.
void testFixes532()
{
    section ("50. 5.3.2 bug fixes");
    const double fs = 48000.0;
    const auto noise = pinkNoise (fs, 3.0, 0.03f, 41);
    const int n = (int) noise[0].size();
    const auto key = testsig::sines (fs, n, { { 2000.0, 0.2 } });   // a loud key at 2 kHz: Make room ducks there
    Channels sides = noise, centre = noise;                          // pure sides (R = -L), pure centre (R = L)
    for (int i = 0; i < n; ++i)
    {
        sides[1][(size_t) i] = -noise[0][(size_t) i];
        centre[1][(size_t) i] = noise[0][(size_t) i];
    }
    struct Case { const char* name; const Channels* in; float eqMid, eqSide; };
    juce::String detail;
    double untargetedError = 0.0, routedDb = 0.0;
    for (const auto& c : { Case { "EQ: Mid, sides", &sides, 1.0f, 0.0f }, Case { "EQ: Side, centre", &centre, 0.0f, 1.0f },
                           Case { "EQ: Both, sides", &sides, 1.0f, 1.0f } })
    {
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2, anneal::SpectralEngine::low);
        auto p = neutral();
        p.midSide = 1.0f;
        p.eqMid = c.eqMid;
        p.eqSide = c.eqSide;
        p.scMode = 1;
        p.scAmount = 1.0f;
        e->setParameters (p);
        const auto out = runEngineWithKey (*e, *c.in, key, 512);
        const int lat = e->getLatencySamples();
        const double err = delayedError (out, *c.in, lat);
        const double change = toDb (testsig::toneAmplitude (out[0].data() + n / 2 + lat, n / 3, 2000.0, fs)
                                    / testsig::toneAmplitude ((*c.in)[0].data() + n / 2, n / 3, 2000.0, fs));
        if (c.eqMid == 1.0f && c.eqSide == 1.0f)
            routedDb = change;
        else
            untargetedError = std::max (untargetedError, err);
        detail << c.name << " " << db (change) << " at 2 kHz (error " << juce::String (err, 7) << ")  ";
    }
    check (untargetedError < 1.0e-4 && routedDb < -2.0, "M/S: the sidechain follows the EQ routing", detail);
}

//==============================================================================
// 5.3.2 Max cut / Max boost (roadmap item 18): how far the auto EQ (Reveal, Smooth, the sidechain and the
// reference together) may move. No limit by default, as before.
void testLimits532()
{
    section ("51. 5.3.2 Max cut / Max boost");
    const double fs = 48000.0;
    const int n = (int) (4 * fs);
    // Section 7's signals: pink noise with a sustained ring at 3150 Hz (Smooth cuts it), and a pink bed
    // under a dense 150-350 Hz build-up (Reveal lifts the shadowed band above it).
    Channels ringy = testsig::silence (2, n), muddy = testsig::silence (2, n);
    {
        testsig::Pink pl (1), pr (2), ml (3), mr (4);
        std::mt19937 rng (9);
        std::uniform_real_distribution<double> ph (0.0, 2.0 * testsig::pi);
        std::vector<std::pair<double, double>> partials;
        for (double f = 150.0; f <= 350.0; f += 2.5)
            partials.push_back ({ f, ph (rng) });
        for (int i = 0; i < n; ++i)
        {
            const float ring = 0.05f * (float) std::sin (2.0 * testsig::pi * 3150.0 * i / fs);
            ringy[0][(size_t) i] = 0.03f * pl.next() + ring;
            ringy[1][(size_t) i] = 0.03f * pr.next() + ring;
            double bump = 0.0;
            for (auto [f, phase] : partials)
                bump += std::sin (2.0 * testsig::pi * f * i / fs + phase);
            muddy[0][(size_t) i] = 0.01f * ml.next() + (float) (0.004 * bump);
            muddy[1][(size_t) i] = 0.01f * mr.next() + (float) (0.004 * bump);
        }
    }
    const float none = anneal::noLimitDb;
    auto engineFor = [fs] (const Channels& in, float reveal, float smooth, float maxCut, float maxBoost,
                           int mode = anneal::SpectralEngine::precise)
    {
        auto e = std::make_unique<anneal::SpectralEngine>();
        e->prepare (fs, 2, mode);
        auto p = neutral();
        p.reveal = reveal;
        p.smooth = smooth;
        p.maxCutDb = maxCut;
        p.maxBoostDb = maxBoost;
        e->setParameters (p);
        runEngine (*e, in, fixedBlocks (512));
        return e;
    };
    auto gains = [] (const anneal::SpectralEngine& e) { return e.getModel().getBandGainDb(); };
    auto lowest = [&gains] (const anneal::SpectralEngine& e)
    {
        const float* g = gains (e);
        return *std::min_element (g, g + anneal::PerceptualModel::numBands);
    };
    auto highest = [&gains] (const anneal::SpectralEngine& e)
    {
        const float* g = gains (e);
        return *std::max_element (g, g + anneal::PerceptualModel::numBands);
    };

    // Smooth: the deepest cut stops at Max cut (set to half of it); what Smooth cuts less than the limit
    // stays as it was.
    {
        const auto unlimited = engineFor (ringy, 0.0f, 1.0f, none, none);
        const float freeCut = lowest (*unlimited);
        const float limit = std::round (-freeCut * 5.0f) / 10.0f;
        const auto lim = engineFor (ringy, 0.0f, 1.0f, limit, none);
        const float cut = lowest (*lim);
        float elsewhere = 0.0f;
        for (int b = 0; b < anneal::PerceptualModel::numBands; ++b)
            if (gains (*unlimited)[b] > -limit + 0.5f)
                elsewhere = std::max (elsewhere, std::abs (gains (*unlimited)[b] - gains (*lim)[b]));
        check (freeCut < -4.0f && cut >= -limit - 1.0e-4f && cut <= -limit + 0.05f && elsewhere < 0.25f,
               "Max cut: Smooth's cut stops at the limit",
               "no limit " + db (freeCut, 2) + ", Max cut " + db (limit, 1) + " -> " + db (cut, 3) + "; bands cut less move "
                   + db (elsewhere, 3) + "; 3150 Hz " + db (bandGain (*lim, 3150.0), 2));
    }

    // Reveal: the strongest lift stops at Max boost.
    {
        const auto unlimited = engineFor (muddy, 1.0f, 0.0f, none, none);
        const float freeLift = highest (*unlimited);
        const float limit = std::round (freeLift * 5.0f) / 10.0f;
        const auto lim = engineFor (muddy, 1.0f, 0.0f, none, limit);
        const float lift = highest (*lim);
        check (freeLift > 1.0f && lift <= limit + 1.0e-4f && lift >= limit - 0.05f, "Max boost: Reveal's lift stops at the limit",
               "no limit " + db (freeLift, 2) + ", Max boost " + db (limit, 1) + " -> " + db (lift, 3) + "; 470 Hz "
                   + db (bandGain (*lim, 470.0), 2));
    }

    // No limit is no limit: with everything that moves the auto EQ at full (Reveal, Smooth, Make room) the
    // output is bit-identical whether the limits are skipped (24 dB) or clamp just under it (23.94 dB),
    // because no move reaches that far.
    {
        const auto key = testsig::sines (fs, n, { { 3150.0, 0.3 }, { 470.0, 0.3 } });
        double worst = 0.0;
        float largest = 0.0f;
        Channels outs[2];
        for (int i = 0; i < 2; ++i)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2, anneal::SpectralEngine::low);
            auto p = neutral();
            p.reveal = 1.0f; p.smooth = 1.0f; p.heat = 0.5f; p.scMode = 1; p.scAmount = 1.0f;
            p.maxCutDb = p.maxBoostDb = i == 0 ? none : none - 0.06f;
            e->setParameters (p);
            outs[i] = runEngineWithKey (*e, ringy, key, 512);
            largest = std::max ({ largest, -lowest (*e), highest (*e) });
        }
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < outs[0][(size_t) c].size(); ++i)
                worst = std::max (worst, (double) std::abs (outs[0][(size_t) c][i] - outs[1][(size_t) c][i]));
        const anneal::EngineParams defaults;
        check (worst == 0.0 && largest < none - 0.06f && defaults.maxCutDb == none && defaults.maxBoostDb == none,
               "no limit (the default) changes nothing", "largest move " + db (largest, 2) + ", max diff " + juce::String (worst, 12));
    }

    // Freeze: a frozen curve is limited too, and comes back whole when the limit is raised again.
    {
        auto e = engineFor (ringy, 0.0f, 1.0f, none, none, anneal::SpectralEngine::low);
        const float frozenCut = lowest (*e);
        auto p = neutral();
        p.smooth = 1.0f;
        p.freeze = true;
        p.maxCutDb = 3.0f;
        e->setParameters (p);
        runEngine (*e, ringy, fixedBlocks (512));
        const float limitedCut = lowest (*e);
        p.maxCutDb = none;
        e->setParameters (p);
        runEngine (*e, ringy, fixedBlocks (512));
        const float back = lowest (*e);
        check (frozenCut < -4.0f && limitedCut >= -3.0001f && limitedCut <= -2.95f && std::abs (back - frozenCut) < 0.001f,
               "a frozen curve is limited, and whole again without the limit",
               "frozen " + db (frozenCut, 2) + ", Max cut 3 dB " + db (limitedCut, 3) + ", no limit again " + db (back, 2));
    }

    // Mid/Side and zero latency: each component's curve (what the zero-latency filters apply) is limited.
    {
        float worst[2] = { 0.0f, 0.0f }, freeMid = 0.0f;
        for (int i = 0; i < 2; ++i)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2, anneal::SpectralEngine::low);
            e->setAnalysisOnly (true);
            auto p = neutral();
            p.smooth = 1.0f; p.reveal = 1.0f; p.midSide = 1.0f;
            p.maxCutDb = i == 0 ? none : 3.0f;
            p.maxBoostDb = i == 0 ? none : 1.0f;
            e->setParameters (p);
            runEngine (*e, ringy, fixedBlocks (512));
            const float* mid = e->getCurveMid();
            const float* side = e->getCurveSide();
            float lo = 0.0f, hi = -100.0f;
            for (int k = 0; k < e->getNumBins(); ++k)
                for (float g : { mid[k], side[k] })
                {
                    lo = std::min (lo, (float) toDb (g));
                    hi = std::max (hi, (float) toDb (g));
                }
            if (i == 0)
                freeMid = lo;
            worst[0] = lo;
            worst[1] = hi;
        }
        check (freeMid < -4.0f && worst[0] >= -3.001f && worst[1] <= 1.001f, "Mid/Side and zero-latency curves stay inside the limits",
               "no limit " + db (freeMid, 2) + " deepest; Max cut 3 / Max boost 1 dB: " + db (worst[0], 3) + " .. " + db (worst[1], 3));
    }

    // Through the processor at zero latency: the ring is cut by no more than Max cut.
    {
        double change[2] {};
        for (int i = 0; i < 2; ++i)
        {
            Rig r;
            r.setNeutral();
            r.set (ids::smooth, 100.0f);
            r.set (ids::zeroLatency, 1.0f);
            if (i == 1)
                r.set (ids::maxCut, 3.0f);
            r.prepare (fs, 512);
            const auto out = r.run (ringy, fixedBlocks (512));
            const int lat = r.proc->getLatencySamples(), from = n - (int) fs;
            change[i] = toDb (testsig::toneAmplitude (out[0].data() + from + lat, (int) fs - lat, 3150.0, fs)
                              / testsig::toneAmplitude (ringy[0].data() + from, (int) fs - lat, 3150.0, fs));
        }
        check (change[0] < -4.0 && change[1] >= -3.4 && change[1] < -2.0, "zero latency: the ring is cut no more than Max cut",
               "3150 Hz: no limit " + db (change[0]) + ", Max cut 3 dB " + db (change[1]));
    }

    // The parameters: "No limit" by default, typed values, older projects open without limits (whatever this
    // instance had), a project with limits keeps them, factory presets leave them alone.
    {
        Rig r;
        auto* mc = r.proc->apvts.getParameter (ids::maxCut);
        auto* mb = r.proc->apvts.getParameter (ids::maxBoost);
        const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
        const bool texts = mc->getCurrentValueAsText() == "No limit" && mb->getCurrentValueAsText() == "No limit"
                           && mc->getText (mc->convertTo0to1 (6.0f), 32) == minus + "6 dB" && mb->getText (mb->convertTo0to1 (4.5f), 32) == "+4.5 dB"
                           && anneal::parseLimit ("6") == 6.0f && anneal::parseLimit ("-6 dB") == 6.0f && anneal::parseLimit (minus + "3 dB") == 3.0f
                           && anneal::parseLimit ("none") == none && anneal::parseLimit ("No limit") == none && anneal::parseLimit ("100") == none;

        auto old = r.proc->apvts.copyState();
        old.removeChild (old.getChildWithProperty ("id", ids::maxCut), nullptr);
        old.removeChild (old.getChildWithProperty ("id", ids::maxBoost), nullptr);
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (*old.createXml(), oldState);
        Rig b;
        b.set (ids::maxCut, 3.0f);
        b.set (ids::maxBoost, 2.0f);
        b.proc->setStateInformation (oldState.getData(), (int) oldState.getSize());
        auto raw = [] (Rig& x, const char* id) { return x.proc->apvts.getRawParameterValue (id)->load(); };
        const bool oldOpen = raw (b, ids::maxCut) == none && raw (b, ids::maxBoost) == none;

        Rig c;
        c.set (ids::maxCut, 6.0f);
        c.set (ids::maxBoost, 1.5f);
        juce::MemoryBlock saved;
        c.proc->getStateInformation (saved);
        Rig d;
        d.proc->setStateInformation (saved.getData(), (int) saved.getSize());
        const bool kept = std::abs (raw (d, ids::maxCut) - 6.0f) < 1.0e-4f && std::abs (raw (d, ids::maxBoost) - 1.5f) < 1.0e-4f;
        d.proc->setCurrentProgram (2);
        const bool presetKeeps = std::abs (raw (d, ids::maxCut) - 6.0f) < 1.0e-4f && d.proc->getPresetDisplayName() == "Mix bus glue";
        check (texts && oldOpen && kept && presetKeeps, "Max cut / Max boost: texts, older projects, saved, presets",
               "default " + mc->getCurrentValueAsText() + ", 6 -> " + mc->getText (mc->convertTo0to1 (6.0f), 32) + ", older project -> "
                   + juce::String (raw (b, ids::maxCut), 1) + " / " + juce::String (raw (b, ids::maxBoost), 1) + ", saved -> "
                   + juce::String (raw (d, ids::maxCut), 1) + " / " + juce::String (raw (d, ids::maxBoost), 1) + ", after a preset "
                   + d.proc->getPresetDisplayName());
    }

    // The Limits pill: on the AUTO EQ title row before Freeze, its menu sets the parameters, the display
    // draws what is set.
    {
        Rig r;
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
        auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
        anneal::LimitMenu* menu = nullptr;
        juce::Component* freeze = nullptr;
        if (editor != nullptr)
            for (auto* c : editor->getContent().getChildren())
            {
                if (auto* m = dynamic_cast<anneal::LimitMenu*> (c))
                    menu = m;
                if (auto* t = dynamic_cast<juce::TextButton*> (c); t != nullptr && t->getButtonText() == "FREEZE")
                    freeze = c;
            }
        if (menu == nullptr || freeze == nullptr)
        {
            check (false, "the Limits pill on the AUTO EQ title row", "missing");
            return;
        }
        auto raw = [&r] (const char* id) { return r.proc->apvts.getRawParameterValue (id)->load(); };
        auto shown = [menu] { return anneal::LimitMenu::pillText (menu->getMaxBoostDb(), menu->getMaxCutDb()); };
        const bool placed = menu->isVisible() && menu->getY() == freeze->getY() && menu->getX() > 100 && menu->getRight() < freeze->getX();
        const auto atRest = shown();
        menu->choose (4);     // Max boost: +3 dB (1 no limit, then 1, 2, 3 dB)
        menu->choose (106);   // Max cut: -6 dB (101 no limit, then 1, 2, 3, 4, 6 dB)
        const bool set = raw (ids::maxBoost) == 3.0f && raw (ids::maxCut) == 6.0f;
        const auto both = shown();
        const auto lines = editor->getContent().screen.getShownLimitsDb();
        menu->choose (1);
        const auto cutOnly = shown();
        menu->choose (101);
        const bool off = raw (ids::maxBoost) == none && raw (ids::maxCut) == none && shown() == "Limits: none";
        const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");
        check (placed && atRest == "Limits: none" && set && both == "Max +3 / " + minus + "6 dB" && cutOnly == "Max " + minus + "6 dB"
                   && anneal::LimitMenu::pillText (4.5f, none) == "Max +4.5 dB" && lines == juce::Point<float> (3.0f, 6.0f) && off,
               "the Limits pill on the AUTO EQ title row",
               menu->getBounds().toString() + ": \"" + atRest + "\", \"" + both + "\", \"" + cutOnly + "\"; display lines +"
                   + juce::String (lines.x, 1) + " / -" + juce::String (lines.y, 1) + " dB");
    }
}

void testTiltPivot533()
{
    section ("52. 5.3.3 Tilt pivot");
    const double fs = 48000.0;

    // The model: +1 dB/oct around 300 Hz is 0 dB at 300 Hz and +2 dB at 1.2 kHz (around 1 kHz, as before:
    // -1.74 and +0.26 dB).
    {
        float g[2][2] {};
        for (int i = 0; i < 2; ++i)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2);
            auto p = neutral();
            p.tiltDbPerOct = 1.0f;
            p.tiltPivotOct = i == 0 ? 0.0f : anneal::tiltPivotOctaves (300.0f);
            e->setParameters (p);
            runEngine (*e, testsig::musicLike (fs, 1.0), fixedBlocks (512));
            g[i][0] = bandGain (*e, 300.0);
            g[i][1] = bandGain (*e, 1200.0);
        }
        const float at1k[2] = { (float) std::log2 (0.3), (float) std::log2 (1.2) };
        check (std::abs (g[1][0]) < 0.05f && std::abs (g[1][1] - 2.0f) < 0.05f
                   && std::abs (g[0][0] - at1k[0]) < 0.05f && std::abs (g[0][1] - at1k[1]) < 0.05f,
               "Tilt +1 dB/oct around 300 Hz: 0 dB @300, +2 dB @1.2k",
               "around 300 Hz: 300 Hz " + db (g[1][0], 3) + ", 1.2 kHz " + db (g[1][1], 3) + "; around 1 kHz: "
                   + db (g[0][0], 2) + " / " + db (g[0][1], 2));
    }

    // 1 kHz is exactly as before: the default is 0 octaves, and a pivot moved away and back lands exactly,
    // so a project that turned it to 300 Hz and back to 1 kHz plays bit for bit like one that never did
    // (music, Reveal, Smooth, Tilt and Heat on, Low latency).
    {
        const auto in = testsig::musicLike (fs, 4.0, 11);
        const int half = (int) in[0].size() / 2;
        Channels first = in, second = in;
        for (int c = 0; c < 2; ++c)
        {
            first[(size_t) c].resize ((size_t) half);
            second[(size_t) c].erase (second[(size_t) c].begin(), second[(size_t) c].begin() + half);
        }
        Channels outs[2];
        for (int i = 0; i < 2; ++i)
        {
            Rig r;
            r.set (ids::reveal, 60.0f); r.set (ids::smooth, 60.0f); r.set (ids::tilt, 0.8f); r.set (ids::heat, 30.0f);
            if (i == 1)
                r.set (ids::tiltPivot, 300.0f);
            r.prepare (fs, 512);
            r.run (first, fixedBlocks (512));
            if (i == 1)
                r.set (ids::tiltPivot, 1000.0f);
            outs[i] = r.run (second, fixedBlocks (512));
        }
        double worst = 0.0;
        for (int c = 0; c < 2; ++c)
            for (size_t k = (size_t) fs; k < outs[0][(size_t) c].size(); ++k)
                worst = std::max (worst, (double) std::abs (outs[0][(size_t) c][k] - outs[1][(size_t) c][k]));
        Rig d;
        const anneal::EngineParams ep;
        const anneal::ModelSettings ms;
        const bool zero = ep.tiltPivotOct == 0.0f && ms.tiltPivotOct == 0.0f && anneal::tiltPivotOctaves (1000.0f) == 0.0f
                          && anneal::tiltPivotOctaves (d.proc->apvts.getRawParameterValue (ids::tiltPivot)->load()) == 0.0f;
        check (worst == 0.0 && zero, "1 kHz bit-identical to before (also back from 300 Hz)",
               "max diff after returning to 1 kHz " + juce::String (worst, 12) + (zero ? ", default 0 octaves" : ", DEFAULT NOT 0"));
    }

    // Through the plug-in, in Low, Mid/Side and Zero latency: tones at 300 Hz and 1.2 kHz with Tilt +1 dB/oct.
    {
        const int n = (int) (3.0 * fs);
        const auto in = testsig::sines (fs, n, { { 300.0, 0.03 }, { 1200.0, 0.03 } });
        juce::String log;
        bool ok = true;
        for (int mode = 0; mode < 3; ++mode)
            for (float pivot : { 300.0f, 1000.0f })
            {
                Rig r;
                r.setNeutral();
                r.set (ids::tilt, 1.0f);
                r.set (ids::tiltPivot, pivot);
                if (mode == 1)
                    r.set (ids::stereoMode, 1.0f);
                if (mode == 2)
                    r.set (ids::zeroLatency, 1.0f);
                r.prepare (fs, 512);
                const auto out = r.run (in, fixedBlocks (512));
                const int lat = r.proc->getLatencySamples(), from = 2 * (int) fs, len = (int) fs / 2;
                double g[2];
                for (int t = 0; t < 2; ++t)
                {
                    const double f = t == 0 ? 300.0 : 1200.0;
                    g[t] = toDb (testsig::toneAmplitude (out[0].data() + from + lat, len, f, fs)
                                 / testsig::toneAmplitude (in[0].data() + from, len, f, fs));
                }
                const double want[2] = { pivot == 300.0f ? 0.0 : std::log2 (0.3), pivot == 300.0f ? 2.0 : std::log2 (1.2) };
                ok = ok && std::abs (g[0] - want[0]) < 0.15 && std::abs (g[1] - want[1]) < 0.15;
                if (pivot == 300.0f)
                    log << (mode == 0 ? "Low" : mode == 1 ? "Mid/Side" : "Zero") << " " << db (g[0], 2) << " / " << db (g[1], 2) << "  ";
            }
        check (ok, "through the plug-in: Low, Mid/Side, Zero latency", "around 300 Hz (300 Hz / 1.2 kHz): " + log);
    }

    // The parameter: "1.00 kHz" by default, typed values, older projects open at 1 kHz (whatever this
    // instance had), a project keeps its pivot, factory presets leave it alone.
    {
        Rig r;
        auto* tp = r.proc->apvts.getParameter (ids::tiltPivot);
        auto raw = [] (Rig& x) { return x.proc->apvts.getRawParameterValue (ids::tiltPivot)->load(); };
        const bool texts = tp->getCurrentValueAsText() == "1.00 kHz" && tp->getText (tp->convertTo0to1 (300.0f), 32) == "300 Hz"
                           && std::abs (tp->convertFrom0to1 (tp->getValueForText ("2k")) - 2000.0f) < 0.5f
                           && std::abs (tp->convertFrom0to1 (tp->getValueForText ("300 Hz")) - 300.0f) < 0.5f
                           && tp->convertFrom0to1 (tp->getValueForText ("50 Hz")) == 200.0f
                           && tp->convertFrom0to1 (tp->getValueForText ("20 kHz")) == 5000.0f
                           && tp->getName (32) == "Tilt Pivot" && tp->isAutomatable();

        auto old = r.proc->apvts.copyState();
        old.removeChild (old.getChildWithProperty ("id", ids::tiltPivot), nullptr);
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (*old.createXml(), oldState);
        Rig b;
        b.set (ids::tiltPivot, 300.0f);
        b.proc->setStateInformation (oldState.getData(), (int) oldState.getSize());
        const bool oldOpen = std::abs (raw (b) - 1000.0f) < 0.01f && anneal::tiltPivotOctaves (raw (b)) == 0.0f;

        Rig c;
        c.set (ids::tiltPivot, 2500.0f);
        juce::MemoryBlock saved;
        c.proc->getStateInformation (saved);
        Rig d;
        d.proc->setStateInformation (saved.getData(), (int) saved.getSize());
        const bool kept = std::abs (raw (d) - 2500.0f) < 0.5f;
        d.proc->setCurrentProgram (2);
        const bool presetKeeps = std::abs (raw (d) - 2500.0f) < 0.5f && d.proc->getPresetDisplayName() == "Mix bus glue";
        check (texts && oldOpen && kept && presetKeeps, "Tilt Pivot: texts, older projects, saved, presets",
               "default " + tp->getCurrentValueAsText() + ", 300 -> " + tp->getText (tp->convertTo0to1 (300.0f), 32)
                   + ", older project -> " + juce::String (raw (b), 0) + " Hz, saved -> " + juce::String (raw (d), 0)
                   + " Hz, after a preset " + d.proc->getPresetDisplayName());
    }

    // The Pivot row: hidden while Tilt is 0, under Tilt inside AUTO EQ while it isn't (5.3 calmer deck),
    // nothing overlapping with everything shown.
    {
        Rig r;
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
        auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
        anneal::Knob* pivot = nullptr;
        anneal::Knob* tilt = nullptr;
        if (editor != nullptr)
            for (auto* c : editor->getContent().getChildren())
                if (auto* k = dynamic_cast<anneal::Knob*> (c))
                {
                    if (k->slider.getTooltip().startsWith ("Pivot:"))
                        pivot = k;
                    if (k->slider.getTooltip().startsWith ("Tilt:"))
                        tilt = k;
                }
        if (pivot == nullptr || tilt == nullptr)
        {
            check (false, "the Pivot row under Tilt", "missing");
            return;
        }
        auto& content = editor->getContent();
        content.tick (0.0f);
        const bool hiddenAtRest = ! pivot->isVisible();
        r.set (ids::tilt, -0.6f);
        r.set (ids::stereoMode, 1.0f);
        r.set (ids::scMode, 1.0f);
        r.set (ids::refMode, 2.0f);
        content.tick (0.0f);
        const bool shown = pivot->isVisible();
        juce::String overlaps;
        auto kids = content.getChildren();
        for (int i = 0; i < kids.size(); ++i)
            for (int j = i + 1; j < kids.size(); ++j)
                if (kids[i]->isVisible() && kids[j]->isVisible() && kids[i]->getBounds().intersects (kids[j]->getBounds()))
                    overlaps << kids[i]->getBounds().toString() << " x " << kids[j]->getBounds().toString() << "  ";
        const bool placed = pivot->getX() == tilt->getX() && pivot->getY() == tilt->getBottom() && pivot->getX() > 312
                            && pivot->getRight() < 490 && pivot->getBottom() <= 628;
        r.set (ids::tilt, 0.0f);
        content.tick (0.0f);
        const bool hiddenAgain = ! pivot->isVisible();
        check (hiddenAtRest && shown && placed && overlaps.isEmpty() && hiddenAgain, "the Pivot row under Tilt, only while Tilt isn't 0",
               pivot->getBounds().toString() + " under Tilt " + tilt->getBounds().toString() + "; at rest "
                   + (hiddenAtRest ? "hidden" : "SHOWN") + ", Tilt -0.6 " + (shown ? "shown" : "HIDDEN") + ", Tilt 0 again "
                   + (hiddenAgain ? "hidden" : "SHOWN") + (overlaps.isEmpty() ? juce::String (", no overlaps") : ", overlaps " + overlaps));
    }
}

// 5.3.4: Key range for the sidechain. Only the part of the key inside it ducks (Make room) or lifts
// (Unmask); the whole key, the default, is exactly as before.
void testKeyRange534()
{
    section ("53. 5.3.4 Key range for the sidechain");
    const double fs = 48000.0;
    const auto main = pinkNoise (fs, 3.0, 0.03f, 21);
    const int n = (int) main[0].size();
    const auto keyTones = testsig::sines (fs, n, { { 300.0, 0.2 }, { 2000.0, 0.2 } });

    // The model: Make room with a key at 300 Hz and 2 kHz. With the key range at 1-5 kHz the 300 Hz part
    // ducks nothing and 2 kHz ducks as with the whole key; Unmask with the key up to 150 Hz lifts nothing
    // at 2 kHz, where the whole key lifts.
    {
        float g[2][2] {};
        for (int i = 0; i < 2; ++i)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2, anneal::SpectralEngine::low);
            auto p = neutral();
            p.scMode = 1; p.scAmount = 1.0f;
            if (i == 1) { p.scKeyLoHz = 1000.0f; p.scKeyHiHz = 5000.0f; }
            e->setParameters (p);
            runEngineWithKey (*e, main, keyTones, 512);
            g[i][0] = bandGain (*e, 300.0);
            g[i][1] = bandGain (*e, 2000.0);
        }
        float lift[2] {};
        for (int i = 0; i < 2; ++i)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2, anneal::SpectralEngine::low);
            auto p = neutral();
            p.scMode = 2; p.scAmount = 1.0f;
            if (i == 1) p.scKeyHiHz = 150.0f;
            e->setParameters (p);
            runEngineWithKey (*e, testsig::sines (fs, n, { { 2000.0, 0.003 } }), pinkNoise (fs, 3.0, 0.2f, 33), 512);
            lift[i] = bandGain (*e, 2000.0);
        }
        check (g[0][0] <= -3.0f && g[0][1] <= -3.0f && std::abs (g[1][0]) < 0.05f && std::abs (g[1][1] - g[0][1]) < 0.05f
                   && lift[0] >= 1.0f && std::abs (lift[1]) < 0.05f,
               "key range 1-5 kHz: a 300 Hz key ducks nothing, 2 kHz as before; Unmask up to 150 Hz lifts nothing at 2 kHz",
               "Make room 300 Hz / 2 kHz: whole key " + db (g[0][0], 2) + " / " + db (g[0][1], 2) + ", 1-5 kHz " + db (g[1][0], 3)
                   + " / " + db (g[1][1], 2) + "; Unmask at 2 kHz: whole key " + db (lift[0], 2) + ", up to 150 Hz " + db (lift[1], 3));
    }

    // The whole key is exactly as before: the defaults are the open ends, and a key range moved away and
    // back lands exactly (music with Reveal, Smooth and Make room on; the key, silent while the range was
    // away, plays from 2 s on): bit for bit the output of an engine that never moved it.
    {
        const auto in = testsig::musicLike (fs, 4.0, 7);
        const int len = (int) in[0].size();
        auto key = testsig::sines (fs, len, { { 300.0, 0.2 }, { 2000.0, 0.2 } });
        for (auto& ch : key)
            std::fill (ch.begin(), ch.begin() + 2 * (int) fs, 0.0f);
        Channels outs[2];
        for (int i = 0; i < 2; ++i)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2, anneal::SpectralEngine::low);
            auto p = neutral();
            p.reveal = 0.6f; p.smooth = 0.6f; p.scMode = 1; p.scAmount = 1.0f;
            auto moved = p;
            moved.scKeyLoHz = 1000.0f; moved.scKeyHiHz = 5000.0f;
            e->setParameters (i == 1 ? moved : p);
            outs[i] = in;
            bool back = i == 0;
            for (int pos = 0; pos < len; pos += 512)
            {
                if (! back && pos >= (int) fs)
                {
                    e->setParameters (p);
                    back = true;
                }
                const int b = std::min (512, len - pos);
                float* ptrs[2] = { outs[i][0].data() + pos, outs[i][1].data() + pos };
                const float* k[2] = { key[0].data() + pos, key[1].data() + pos };
                e->process (ptrs, 2, b, k, 2);
            }
        }
        double worst = 0.0;
        for (int c = 0; c < 2; ++c)
            for (size_t k = 0; k < outs[0][(size_t) c].size(); ++k)
                worst = std::max (worst, (double) std::abs (outs[0][(size_t) c][k] - outs[1][(size_t) c][k]));
        Rig d;
        const anneal::EngineParams ep;
        const anneal::ModelSettings ms;
        const float lo = d.proc->apvts.getRawParameterValue (ids::scKeyLo)->load();
        const float hi = d.proc->apvts.getRawParameterValue (ids::scKeyHi)->load();
        const bool open = ep.scKeyLoHz == 20.0f && ep.scKeyHiHz == 20000.0f && ms.scKeyLoHz == 20.0f && ms.scKeyHiHz == 20000.0f
                          && lo == 20.0f && hi == 20000.0f && anneal::keyLoIsOpen (lo) && anneal::keyHiIsOpen (anneal::effectiveRangeHi (lo, hi));
        check (worst == 0.0 && open, "the whole key bit-identical to before (also back from 1-5 kHz)",
               "max diff " + juce::String (worst, 12) + (open ? ", defaults 20 Hz - 20 kHz" : ", DEFAULTS NOT OPEN"));
    }

    // Through the plug-in's sidechain bus, in Low, Mid/Side and at Zero latency: the key range 1-5 kHz
    // leaves the main's 300 Hz alone and ducks 2 kHz; the whole key ducks both.
    {
        juce::String log;
        bool ok = true;
        for (int mode = 0; mode < 3; ++mode)
            for (int ranged = 0; ranged < 2; ++ranged)
            {
                Rig r;
                juce::AudioProcessor::BusesLayout l;
                l.inputBuses.add (juce::AudioChannelSet::stereo());
                l.inputBuses.add (juce::AudioChannelSet::stereo());
                l.outputBuses.add (juce::AudioChannelSet::stereo());
                const bool accepted = r.proc->setBusesLayout (l);
                r.setNeutral();
                r.set (ids::scMode, 1.0f);
                r.set (ids::scAmount, 100.0f);
                if (ranged == 1)
                {
                    r.set (ids::scKeyLo, 1000.0f);
                    r.set (ids::scKeyHi, 5000.0f);
                }
                if (mode == 1)
                    r.set (ids::stereoMode, 1.0f);
                if (mode == 2)
                    r.set (ids::zeroLatency, 1.0f);
                r.prepare (fs, 512);
                juce::AudioBuffer<float> buf (4, 512);
                juce::MidiBuffer midi;
                Channels out = main;
                for (int pos = 0; pos + 512 <= n; pos += 512)
                {
                    for (int c = 0; c < 2; ++c)
                    {
                        buf.copyFrom (c, 0, main[(size_t) c].data() + pos, 512);
                        buf.copyFrom (c + 2, 0, keyTones[(size_t) c].data() + pos, 512);
                    }
                    r.proc->processBlock (buf, midi);
                    for (int c = 0; c < 2; ++c)
                        std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + 512, out[(size_t) c].data() + pos);
                }
                const int lat = r.proc->getLatencySamples();
                double change[2];
                for (int t = 0; t < 2; ++t)
                {
                    const double f = t == 0 ? 300.0 : 2000.0;
                    change[t] = toDb ((testsig::toneAmplitude (out[0].data() + n / 2 + lat, n / 3, f, fs) + 1.0e-12)
                                      / (testsig::toneAmplitude (main[0].data() + n / 2, n / 3, f, fs) + 1.0e-12));
                }
                ok = ok && accepted && (mode != 2 || lat == 0) && change[1] < -2.0
                     && (ranged == 1 ? std::abs (change[0]) < 0.3 : change[0] < -2.0);
                log << (mode == 0 ? "Low" : mode == 1 ? "Mid/Side" : "Zero") << (ranged == 1 ? " 1-5 kHz " : " whole key ")
                    << db (change[0], 2) << " / " << db (change[1], 2) << "  ";
            }
        check (ok, "through the plug-in: the key range in Low, Mid/Side and at Zero latency", "main at 300 Hz / 2 kHz: " + log);
    }

    // The parameters: "20 Hz" and "20.0 kHz" by default, typed values ("1k", "all"), older projects open
    // with the whole key (whatever this instance had), a project keeps its range, factory presets leave it.
    {
        Rig r;
        auto* lp = r.proc->apvts.getParameter (ids::scKeyLo);
        auto* hp = r.proc->apvts.getParameter (ids::scKeyHi);
        auto raw = [] (Rig& x, const char* id) { return x.proc->apvts.getRawParameterValue (id)->load(); };
        const bool texts = lp->getCurrentValueAsText() == "20 Hz" && hp->getCurrentValueAsText() == "20.0 kHz"
                           && std::abs (lp->convertFrom0to1 (lp->getValueForText ("1k")) - 1000.0f) < 0.5f
                           && std::abs (hp->convertFrom0to1 (hp->getValueForText ("5 kHz")) - 5000.0f) < 0.5f
                           && lp->convertFrom0to1 (lp->getValueForText ("all")) == 20.0f
                           && hp->convertFrom0to1 (hp->getValueForText ("full")) == 20000.0f
                           && lp->getName (32) == "Sidechain Key Low" && hp->getName (32) == "Sidechain Key High"
                           && lp->isAutomatable() && hp->isAutomatable();

        auto old = r.proc->apvts.copyState();
        for (auto id : { ids::scKeyLo, ids::scKeyHi })
            old.removeChild (old.getChildWithProperty ("id", id), nullptr);
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (*old.createXml(), oldState);
        Rig b;
        b.set (ids::scKeyLo, 1000.0f);
        b.set (ids::scKeyHi, 5000.0f);
        b.proc->setStateInformation (oldState.getData(), (int) oldState.getSize());
        const bool oldOpen = raw (b, ids::scKeyLo) == 20.0f && raw (b, ids::scKeyHi) == 20000.0f;

        Rig c;
        c.set (ids::scKeyLo, 1000.0f);
        c.set (ids::scKeyHi, 5000.0f);
        juce::MemoryBlock saved;
        c.proc->getStateInformation (saved);
        Rig d;
        d.proc->setStateInformation (saved.getData(), (int) saved.getSize());
        const bool kept = std::abs (raw (d, ids::scKeyLo) - 1000.0f) < 0.5f && std::abs (raw (d, ids::scKeyHi) - 5000.0f) < 0.5f;
        d.proc->setCurrentProgram (2);
        const bool presetKeeps = std::abs (raw (d, ids::scKeyLo) - 1000.0f) < 0.5f && std::abs (raw (d, ids::scKeyHi) - 5000.0f) < 0.5f;
        check (texts && oldOpen && kept && presetKeeps, "Sidechain Key Low / High: texts, older projects, saved, presets",
               "defaults " + lp->getCurrentValueAsText() + " / " + hp->getCurrentValueAsText() + ", older project -> "
                   + juce::String (raw (b, ids::scKeyLo), 0) + " / " + juce::String (raw (b, ids::scKeyHi), 0) + " Hz, saved -> "
                   + juce::String (raw (d, ids::scKeyLo), 0) + " / " + juce::String (raw (d, ids::scKeyHi), 0) + " Hz"
                   + (presetKeeps ? ", a preset leaves it" : ", A PRESET CHANGED IT"));
    }

    // The Key pill: hidden while the sidechain is off, on the Sidechain menu's row inside the column
    // while a mode is on (5.3 calmer deck), nothing overlapping with everything shown; its menu items and
    // texts; the display draws the range the engine uses.
    {
        Rig r;
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.inputBuses.add (juce::AudioChannelSet::stereo());
        l.outputBuses.add (juce::AudioChannelSet::stereo());
        r.proc->setBusesLayout (l);
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
        auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
        anneal::KeyRangeMenu* menu = nullptr;
        juce::Component* scMenu = nullptr;
        if (editor != nullptr)
            for (auto* c : editor->getContent().getChildren())
            {
                if (auto* m = dynamic_cast<anneal::KeyRangeMenu*> (c))
                    menu = m;
                if (auto* m = dynamic_cast<anneal::RoutingMenu*> (c); m != nullptr && m->getTooltip().startsWith ("Sidechain:"))
                    scMenu = c;
            }
        if (menu == nullptr || scMenu == nullptr)
        {
            check (false, "the Key pill on the Sidechain row", "missing");
            return;
        }
        auto& content = editor->getContent();
        content.tick (0.0f);
        const bool hiddenAtRest = ! menu->isVisible();
        r.set (ids::scMode, 1.0f);
        r.set (ids::stereoMode, 1.0f);
        r.set (ids::refMode, 2.0f);
        r.set (ids::tilt, 0.5f);
        content.tick (0.0f);
        const bool shown = menu->isVisible();
        juce::String overlaps;
        auto kids = content.getChildren();
        for (int i = 0; i < kids.size(); ++i)
            for (int j = i + 1; j < kids.size(); ++j)
                if (kids[i]->isVisible() && kids[j]->isVisible() && kids[i]->getBounds().intersects (kids[j]->getBounds()))
                    overlaps << kids[i]->getBounds().toString() << " x " << kids[j]->getBounds().toString() << "  ";
        const bool placed = menu->getY() == scMenu->getY() && menu->getX() > scMenu->getRight() && menu->getRight() < 1032
                            && scMenu->getX() > 832;

        auto raw = [&r] (const char* id) { return r.proc->apvts.getRawParameterValue (id)->load(); };
        const auto atRest = anneal::KeyRangeMenu::pillText (raw (ids::scKeyLo), raw (ids::scKeyHi));
        menu->choose (3);     // Vocal presence (1-5 kHz)
        const bool vocal = raw (ids::scKeyLo) == 1000.0f && raw (ids::scKeyHi) == 5000.0f;
        const auto vocalText = anneal::KeyRangeMenu::pillText (menu->getLoHz(), menu->getHiHz());

        // The display: half a second of audio with a key, then the range the key's line is bright in.
        r.prepare (fs, 512);
        {
            juce::AudioBuffer<float> buf (4, 512);
            juce::MidiBuffer midi;
            for (int pos = 0; pos + 512 <= (int) fs / 2; pos += 512)
            {
                for (int c = 0; c < 2; ++c)
                {
                    buf.copyFrom (c, 0, main[(size_t) c].data() + pos, 512);
                    buf.copyFrom (c + 2, 0, keyTones[(size_t) c].data() + pos, 512);
                }
                r.proc->processBlock (buf, midi);
            }
        }
        content.screen.refreshNow();
        const auto drawn = content.screen.getShownKeyRangeHz();

        menu->choose (106);   // From 2 kHz (102 = 100 Hz ... 106 = 2 kHz)
        menu->choose (204);   // Up to 800 Hz: below From, so From opens
        const auto crossed = anneal::KeyRangeMenu::pillText (raw (ids::scKeyLo), raw (ids::scKeyHi));
        const bool crossedOk = raw (ids::scKeyLo) == 20.0f && raw (ids::scKeyHi) == 800.0f;
        menu->choose (1);
        const bool whole = raw (ids::scKeyLo) == 20.0f && raw (ids::scKeyHi) == 20000.0f;
        r.set (ids::scMode, 0.0f);
        content.tick (0.0f);
        const bool hiddenAgain = ! menu->isVisible();
        const auto dash = juce::String::fromUTF8 ("\xe2\x80\x93");
        check (hiddenAtRest && shown && placed && overlaps.isEmpty() && hiddenAgain && atRest == "Key: all" && vocal
                   && vocalText == "Key 1k" + dash + "5k" && crossedOk && crossed == "Key < 800" && whole
                   && anneal::KeyRangeMenu::pillText (5000.0f, 20000.0f) == "Key > 5k"
                   && anneal::KeyRangeMenu::pillText (2500.0f, 12000.0f) == "Key 2.5k" + dash + "12k"
                   && std::abs (drawn.x - 1000.0f) < 1.0f && std::abs (drawn.y - 5000.0f) < 1.0f,
               "the Key pill on the Sidechain row, only while a mode is on",
               menu->getBounds().toString() + " beside " + scMenu->getBounds().toString() + "; \"" + atRest + "\", \"" + vocalText
                   + "\", crossed \"" + crossed + "\"; display " + juce::String (drawn.x, 0) + " - " + juce::String (drawn.y, 0)
                   + " Hz; " + (hiddenAtRest && hiddenAgain ? "hidden with the sidechain off" : "SHOWN WITH THE SIDECHAIN OFF")
                   + (overlaps.isEmpty() ? juce::String (", no overlaps") : ", overlaps " + overlaps));
    }
}

void testFixes535()
{
    section ("54. 5.3.5 bug fixes");
    // Tooltips were drawn on one line in a box shrunk to the window: the display's own tooltip (about 4000 px
    // of text) showed only its first third, cut mid-word, and every other tooltip longer than the window was
    // cut too. Now a long tooltip wraps; check that every tooltip's text, laid out at its box's inner width
    // (10 px a side, as drawTooltip draws it), fits inside the box, at the full and the smallest window.
    Rig r;
    std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
    auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
    if (editor == nullptr)
    {
        check (false, "every tooltip fits in its box", "no editor");
        return;
    }
    anneal::AnnealLookAndFeel lf;
    const auto f = anneal::theme::font (13.0f);
    juce::String cut;
    int tips = 0, wrapped = 0, displayLines = 0;
    for (auto parent : { juce::Rectangle<int> (0, 0, 1464, 640), juce::Rectangle<int> (0, 0, 1098, 480) })
    {
        std::function<void (juce::Component&)> visit = [&] (juce::Component& c)
        {
            if (auto* t = dynamic_cast<juce::TooltipClient*> (&c); t != nullptr && t->getTooltip().isNotEmpty())
            {
                const auto tip = t->getTooltip();
                const auto box = lf.getTooltipBounds (tip, { parent.getCentreX() - 200, 60 }, parent);
                juce::GlyphArrangement ga;
                ga.addJustifiedText (f, tip, 0.0f, f.getAscent(), (float) box.getWidth() - 20.0f + 6.0f, juce::Justification::left);
                const auto need = ga.getBoundingBox (0, -1, false);
                const int lines = juce::roundToInt (need.getHeight() / f.getHeight());
                ++tips;
                wrapped += lines > 1 ? 1 : 0;
                if (&c == &editor->getContent().screen)
                    displayLines = lines;
                if (need.getBottom() + 8.0f > (float) box.getHeight() + 0.5f || ! parent.contains (box))
                    cut << "\"" << tip.substring (0, 32) << "...\" in " << box.toString() << " needs "
                        << juce::roundToInt (need.getBottom()) << " px  ";
            }
            for (auto* k : c.getChildren())
                visit (*k);
        };
        visit (*editor);
    }
    check (tips > 60 && cut.isEmpty() && displayLines > 1, "every tooltip fits in its box (long ones wrap)",
           juce::String (tips) + " tooltips at two window sizes, " + juce::String (wrapped) + " of them wrapped, the display's on "
               + juce::String (displayLines) + " lines" + (cut.isEmpty() ? juce::String() : "; CUT: " + cut));
}

void testPeakHold535()
{
    section ("55. 5.3.5 Peak hold on the display");
    constexpr int nb = anneal::DisplayFrame::numBands;
    using Frames = anneal::TripleBuffer<anneal::DisplayFrame>;

    // Two displays fed the same frames by hand: one with the peak hold, one that never had it.
    {
        Rig r;
        auto bufA = std::make_unique<Frames>(), bufB = std::make_unique<Frames>();
        anneal::PerceptionScreen held (*bufA, r.proc->apvts), plain (*bufB, r.proc->apvts);
        held.setSize (1440, 356);
        plain.setSize (1440, 356);
        const float z0 = (float) anneal::erb::hzToCam (20.0);
        const float dz = (float) ((anneal::erb::hzToCam (20000.0) - z0) / (nb - 1));
        const auto camOf = [] (double hz) { return (float) anneal::erb::hzToCam (hz); };
        // A falling, pink-like slope at levelDb (at 0 Cam), with +12 dB over +-1 Cam around bumpHz.
        auto push = [&] (float levelDb, double bumpHz)
        {
            for (auto* b : { bufA.get(), bufB.get() })
            {
                auto& fr = b->beginWrite();
                fr.z0 = z0;
                fr.dz = dz;
                for (int i = 0; i < nb; ++i)
                {
                    const float cam = z0 + (float) i * dz;
                    const float bump = bumpHz > 0.0 && std::abs (cam - camOf (bumpHz)) < 1.0f ? 12.0f : 0.0f;
                    fr.inDb[i] = fr.outDb[i] = levelDb + bump - 0.5f * cam;
                    fr.gainDb[i] = fr.sideGainDb[i] = 0.0f;
                    fr.heatDb[i] = -200.0f;
                }
                b->endWrite();
            }
            held.refreshNow();
            plain.refreshNow();
        };
        const auto expected = [&camOf] (float levelDb, double hz, bool bump) { return levelDb + (bump ? 12.0f : 0.0f) - 0.5f * camOf (hz); };

        const bool offAtStart = ! held.isPeakHoldOn() && ! held.hasHeldPeaks() && ! held.getPeakHoldButton().getToggleState();
        held.choosePeakHold (2);   // as HOLD's menu, Output (5.3.8; 5.3.5: a click on the pill)
        const bool onByPill = held.isPeakHoldOn() && held.getPeakHoldButton().getToggleState();

        // Loud with a bump moving 200 Hz -> 2 kHz -> 8 kHz, then 40 dB quieter: the line never falls.
        std::vector<float> was;
        int frames = 0, fell = 0;
        float worst = 0.0f;
        auto watch = [&]
        {
            ++frames;
            for (int i = 0; i < 400; ++i)
            {
                const float v = held.heldPeakAtHz (20.0f * std::pow (1000.0f, (float) i / 399.0f));
                if ((int) was.size() == 400 && v < was[(size_t) i])
                {
                    ++fell;
                    worst = juce::jmin (worst, v - was[(size_t) i]);
                }
                if ((int) was.size() < 400) was.push_back (v); else was[(size_t) i] = v;
            }
        };
        for (double hz : { 200.0, 2000.0, 8000.0 })
            for (int k = 0; k < 6; ++k) { push (-30.0f, hz); watch(); }
        for (int k = 0; k < 18; ++k) { push (-70.0f, 0.0); watch(); }
        float bumpErr = 0.0f;
        for (double hz : { 200.0, 2000.0, 8000.0 })
            bumpErr = juce::jmax (bumpErr, std::abs (held.heldPeakAtHz ((float) hz) - expected (-30.0f, hz, true)));
        const float loud1k = held.heldPeakAtHz (1000.0f), loudErr = std::abs (loud1k - expected (-30.0f, 1000.0, false));
        check (offAtStart && onByPill && fell == 0 && frames == 36 && bumpErr < 0.1f && loudErr < 0.1f,
               "the held line never falls while playing",
               juce::String (frames) + " frames (loud, then 40 dB quieter): " + juce::String (fell) + " falls (worst "
                   + juce::String (worst, 3) + " dB); 1 kHz held at " + juce::String (loud1k, 2) + " dB (loud: "
                   + juce::String (expected (-30.0f, 1000.0, false), 2) + ", now " + juce::String (expected (-70.0f, 1000.0, false), 2)
                   + "), the 200 Hz / 2 kHz / 8 kHz bumps within " + juce::String (bumpErr, 3) + " dB"
                   + (offAtStart ? "; off by default" : "; ON BY DEFAULT"));

        // While on, it draws: only inside the EQ lane (and the HOLD pill), nowhere else.
        const auto imgHeld = held.createComponentSnapshot (held.getLocalBounds());
        const auto imgPlain = plain.createComponentSnapshot (plain.getLocalBounds());
        const auto pill = held.getPeakHoldButton().getBounds();
        const auto lane = held.getEqLane().toNearestInt();
        auto diff = [&pill, &lane] (const juce::Image& a, const juce::Image& b, int& outside)
        {
            int n = 0;
            outside = 0;
            for (int y = 0; y < a.getHeight(); ++y)
                for (int x = 0; x < a.getWidth(); ++x)
                    if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    {
                        ++n;
                        outside += lane.contains (x, y) || pill.contains (x, y) ? 0 : 1;
                    }
            return n;
        };
        int outsideOn = 0;
        const int drawnOn = diff (imgHeld, imgPlain, outsideOn);

        // A click on a range handle or in the Heat lane keeps it; a click on the spectrum clears it, and it
        // holds again from there.
        auto eventAt = [&held] (juce::Point<float> p)
        {
            const auto now = juce::Time::getCurrentTime();
            return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(), p,
                                     juce::ModifierKeys (juce::ModifierKeys::leftButtonModifier),
                                     1.0f, 0.0f, 0.0f, 0.0f, 0.0f, &held, &held, now, p, now, 1, false);
        };
        auto click = [&] (juce::Point<float> p) { held.mouseDown (eventAt (p)); held.mouseUp (eventAt (p)); };
        const float laneY = held.getEqLane().getCentreY();
        click ({ held.xForHz (held.getShownRange().x), laneY });
        click ({ held.xForHz (700.0f), held.getHeatLane().getCentreY() });
        const bool keptByOthers = held.hasHeldPeaks() && held.heldPeakAtHz (1000.0f) == loud1k;
        click ({ held.xForHz (700.0f), laneY });
        const bool cleared = ! held.hasHeldPeaks() && held.heldPeakAtHz (1000.0f) <= -139.0f && held.isPeakHoldOn();
        push (-70.0f, 0.0);
        const float again = held.heldPeakAtHz (1000.0f);
        const bool holdsAgain = std::abs (again - expected (-70.0f, 1000.0, false)) < 0.1f;
        check (keptByOthers && cleared && holdsAgain, "a click on the spectrum clears it",
               juce::String (keptByOthers ? "kept by a click on a range handle and in the Heat lane" : "LOST TO ANOTHER CLICK")
                   + (cleared ? "; cleared by a click on the spectrum" : "; NOT CLEARED") + ", then held from "
                   + juce::String (again, 2) + " dB at 1 kHz (the quiet output: " + juce::String (expected (-70.0f, 1000.0, false), 2) + ")");

        // Off: nothing held and nothing drawn, pixel for pixel the display that never had it.
        push (-30.0f, 2000.0);
        held.choosePeakHold (1);   // Off
        int outsideOff = 0;
        const int drawnOff = diff (held.createComponentSnapshot (held.getLocalBounds()),
                                   plain.createComponentSnapshot (plain.getLocalBounds()), outsideOff);
        const bool offEmpty = ! held.isPeakHoldOn() && ! held.hasHeldPeaks() && held.heldPeakAtHz (1000.0f) <= -139.0f;
        check (drawnOn > 300 && outsideOn == 0 && drawnOff == 0 && offEmpty, "off draws nothing",
               "on: " + juce::String (drawnOn) + " pixels differ, " + juce::String (outsideOn) + " outside the EQ lane and the pill; off: "
                   + juce::String (drawnOff) + " pixels differ from a display that never held");

        const auto pillText = anneal::theme::caps (9.5f);
        juce::GlyphArrangement ga;
        ga.addLineOfText (pillText, "HOLD", 0.0f, 0.0f);
        const float textW = ga.getBoundingBox (0, -1, false).getWidth();
        const bool placed = pill.getX() >= held.xForHz (20000.0f) + 6.0f && pill.getRight() <= held.getWidth()
                            && pill.getBottom() <= held.getEqLane().getY() && pill.getY() >= 0 && textW <= pill.getWidth() - 6.0f;
        check (placed && held.getPeakHoldButton().getTooltip().contains ("Click the spectrum"),
               "the HOLD pill in the display's top right corner",
               pill.toString() + ", clear of the 20 kHz grip and above the EQ lane; \"HOLD\" " + juce::String (textW, 1) + " px wide");
    }

    // Through the plug-in: the processor keeps the hold, so closing and opening the window keeps the line, and
    // the sound is the same with it on.
    {
        const double fs = 48000.0;
        const auto loud = pinkNoise (fs, 0.6, 0.3f, 51), quiet = pinkNoise (fs, 0.6, 0.003f, 52);
        Rig a, b;
        a.prepare (fs, 512);
        b.prepare (fs, 512);
        float before = 0.0f, after = 0.0f, later = 0.0f;
        bool reopenedOn = false;
        Channels outA, outB;
        auto playWith = [] (Rig& r, const Channels& sig, anneal::PerceptionScreen* screen, Channels& out)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (int pos = 0; pos + 512 <= (int) sig[0].size(); pos += 512)
            {
                for (int c = 0; c < 2; ++c)
                    buf.copyFrom (c, 0, sig[(size_t) c].data() + pos, 512);
                r.proc->processBlock (buf, midi);
                for (int c = 0; c < 2; ++c)
                    out[(size_t) c].insert (out[(size_t) c].end(), buf.getReadPointer (c), buf.getReadPointer (c) + 512);
                if (screen != nullptr && (pos / 512) % 8 == 7)
                    screen->refreshNow();
            }
        };
        outA.resize (2);
        outB.resize (2);
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (a.proc->createEditor());
            auto& screen = dynamic_cast<AnnealEditor&> (*ed).getContent().screen;
            screen.choosePeakHold (2);
            playWith (a, loud, &screen, outA);
            before = screen.heldPeakAtHz (1000.0f);
        }
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (a.proc->createEditor());
            auto& screen = dynamic_cast<AnnealEditor&> (*ed).getContent().screen;
            reopenedOn = screen.isPeakHoldOn() && screen.getPeakHoldButton().getToggleState();
            after = screen.heldPeakAtHz (1000.0f);
            playWith (a, quiet, &screen, outA);
            later = screen.heldPeakAtHz (1000.0f);
        }
        playWith (b, loud, nullptr, outB);
        playWith (b, quiet, nullptr, outB);
        double maxDiff = 0.0;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < outA[(size_t) c].size(); ++i)
                maxDiff = std::max (maxDiff, (double) std::abs (outA[(size_t) c][i] - outB[(size_t) c][i]));
        const bool defaultOff = ! Rig().proc->displayPeakHold.on;
        check (before > -80.0f && reopenedOn && after == before && later == before && maxDiff == 0.0 && defaultOff,
               "the window keeps it, and the sound doesn't change",
               "1 kHz held at " + juce::String (before, 2) + " dB; after closing and opening the window "
                   + juce::String (after, 2) + " dB (" + (reopenedOn ? "HOLD still on" : "HOLD OFF") + "), after 0.6 s 40 dB quieter "
                   + juce::String (later, 2) + " dB; output with it on vs. off: difference " + juce::String (maxDiff, 12));
    }
}

// 5.3.6: Detail, how narrow Reveal's and Smooth's moves are. Fine looks through a narrower window (and smooths
// the curve over less), Broad through a wider one; Normal, the default, is the model's own and bit-identical.
void testDetail536()
{
    section ("56. 5.3.6 Detail");
    const double fs = 48000.0;
    const int n = (int) (4 * fs);

    // Pink noise bed + a sustained ring at 3150 Hz (as in section 7), mono in both channels.
    Channels ringy = testsig::silence (2, n);
    {
        testsig::Pink pink (5);
        for (int i = 0; i < n; ++i)
            ringy[0][(size_t) i] = ringy[1][(size_t) i] = 0.03f * pink.next() + 0.05f * (float) std::sin (2.0 * testsig::pi * 3150.0 * i / fs);
    }
    const double camRing = anneal::erb::hzToCam (3150.0);
    const double nearHz = anneal::erb::camToHz (camRing + 0.6);   // a neighbour 0.6 Cam above the ring

    // The model (Precise): Smooth 100 % cuts the ring at every Detail; the width where the cut is at least half
    // its deepest is narrower at Fine and wider at Broad. 500 Hz stays untouched.
    {
        float width[3] {}, deepest[3] {}, at500[3] {};
        const float details[3] = { 0.0f, 1.0f, -1.0f };
        for (int i = 0; i < 3; ++i)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2);
            auto p = neutral();
            p.smooth = 1.0f;
            p.detail = details[i];
            e->setParameters (p);
            runEngine (*e, ringy, fixedBlocks (512));
            const auto& m = e->getModel();
            const float* g = m.getBandGainDb();
            const int ring = m.bandForHz (3150.0);
            for (int b = ring - 120; b <= ring + 120; ++b)
                deepest[i] = std::min (deepest[i], g[b]);
            int count = 0;
            for (int b = ring - 120; b <= ring + 120; ++b)
                count += g[b] <= 0.5f * deepest[i] ? 1 : 0;
            width[i] = (float) count * m.getDz();
            at500[i] = bandGain (*e, 500.0);
        }
        const bool cut = deepest[0] <= -3.0f && deepest[1] <= -3.0f && deepest[2] <= -3.0f;
        const bool quiet = std::abs (at500[0]) < 1.0f && std::abs (at500[1]) < 1.0f && std::abs (at500[2]) < 1.0f;
        check (cut && quiet && width[1] < 0.6f * width[0] && width[2] > 1.5f * width[0],
               "the model: a single ring is cut over a narrower band at Fine, a wider one at Broad",
               "half-depth width Normal " + juce::String (width[0], 2) + " Cam (" + db (deepest[0]) + "), Fine 100 "
                   + juce::String (width[1], 2) + " Cam (" + db (deepest[1]) + "), Broad 100 " + juce::String (width[2], 2)
                   + " Cam (" + db (deepest[2]) + "); 500 Hz " + db (at500[0], 2) + " / " + db (at500[1], 2) + " / " + db (at500[2], 2));
    }

    // Normal is bit-identical to before: the defaults are 0 (a width scale of exactly 1, the model's own windows),
    // and a Detail moved to Fine 80 and back plays exactly like one that never moved (music, Reveal, Smooth and
    // Heat on, Low latency) once the curve it held then has settled.
    {
        const auto in = testsig::musicLike (fs, 8.0, 12);
        const int split = (int) (1.5 * fs);
        Channels first = in, second = in;
        for (int c = 0; c < 2; ++c)
        {
            first[(size_t) c].resize ((size_t) split);
            second[(size_t) c].erase (second[(size_t) c].begin(), second[(size_t) c].begin() + split);
        }
        Channels outs[2];
        for (int i = 0; i < 2; ++i)
        {
            Rig r;
            r.set (ids::reveal, 60.0f); r.set (ids::smooth, 60.0f); r.set (ids::heat, 30.0f);
            if (i == 1)
                r.set (ids::detail, 80.0f);
            r.prepare (fs, 512);
            r.run (first, fixedBlocks (512));
            if (i == 1)
                r.set (ids::detail, 0.0f);
            outs[i] = r.run (second, fixedBlocks (512));
        }
        double worst = 0.0;
        for (int c = 0; c < 2; ++c)
            for (size_t k = (size_t) (4 * fs); k < outs[0][(size_t) c].size(); ++k)
                worst = std::max (worst, (double) std::abs (outs[0][(size_t) c][k] - outs[1][(size_t) c][k]));
        Rig d;
        const anneal::EngineParams ep;
        const anneal::ModelSettings ms;
        const bool zero = ep.detail == 0.0f && ms.detail == 0.0f && anneal::detailWidthScale (0.0f) == 1.0
                          && d.proc->apvts.getRawParameterValue (ids::detail)->load() == 0.0f;
        check (worst == 0.0 && zero, "Normal bit-identical to before (also back from Fine 80)",
               "max diff after returning to Normal " + juce::String (worst, 12) + (zero ? ", defaults 0" : ", DEFAULT NOT 0"));
    }

    // Through the plug-in, in Low, Mid/Side and Zero latency: the ring is cut at Normal and at Fine 100, and its
    // neighbour 0.6 Cam above (about 3.37 kHz) far less at Fine than at Normal, far more at Broad 100.
    {
        juce::String log;
        bool ok = true;
        for (int mode = 0; mode < 3; ++mode)
        {
            double atRing[3] {}, atNear[3] {};
            for (int i = 0; i < 3; ++i)
            {
                Rig r;
                r.setNeutral();
                r.set (ids::smooth, 100.0f);
                r.set (ids::detail, i == 0 ? 0.0f : i == 1 ? 100.0f : -100.0f);
                if (mode == 1)
                    r.set (ids::stereoMode, 1.0f);
                if (mode == 2)
                    r.set (ids::zeroLatency, 1.0f);
                r.prepare (fs, 512);
                const auto out = r.run (ringy, fixedBlocks (512));
                const int lat = r.proc->getLatencySamples(), from = 2 * (int) fs, len = (int) (1.5 * fs);
                atRing[i] = testsig::transferDb (ringy[0].data() + from, out[0].data() + from + lat, len, 3150.0, fs);
                atNear[i] = testsig::transferDb (ringy[0].data() + from, out[0].data() + from + lat, len, nearHz, fs);
            }
            ok = ok && atRing[0] < -3.0 && atRing[1] < -3.0 && atNear[1] > atNear[0] + 1.0 && atNear[2] < atNear[0] - 1.0;
            log << (mode == 0 ? "Low" : mode == 1 ? "Mid/Side" : "Zero") << ": ring " << db (atRing[0]) << " / " << db (atRing[1])
                << ", neighbour " << db (atNear[0]) << " / " << db (atNear[1]) << " / broad " << db (atNear[2]) << "  ";
        }
        check (ok, "through the plug-in: Low, Mid/Side, Zero latency", "Normal / Fine 100: " + log);
    }

    // What it costs (informational): the engine on 10 s of music, Precise and Low, Normal against Fine 100 and
    // Broad 100 (the extra place / context pass runs only while the window differs from the model's own).
    {
        const auto music = testsig::musicLike (fs, 10.0, 13);
        juce::String line = "CPU, % of one core (Normal / Fine 100 / Broad 100):";
        for (auto mode : { anneal::SpectralEngine::low, anneal::SpectralEngine::precise })
        {
            double pct[3] {};
            for (int i = 0; i < 3; ++i)
            {
                auto e = std::make_unique<anneal::SpectralEngine>();
                e->prepare (fs, 2, mode);
                auto p = neutral();
                p.reveal = 0.6f; p.smooth = 0.6f; p.heat = 0.3f;
                p.detail = i == 0 ? 0.0f : i == 1 ? 1.0f : -1.0f;
                e->setParameters (p);
                double best = 1.0e9;
                for (int rep = 0; rep < 3; ++rep)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    runEngine (*e, music, fixedBlocks (512));
                    best = std::min (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
                }
                pct[i] = 100.0 * best / 10.0;
            }
            line << (mode == anneal::SpectralEngine::low ? " Low " : ", Precise ") << juce::String (pct[0], 2) << " / "
                 << juce::String (pct[1], 2) << " / " << juce::String (pct[2], 2);
        }
        info (line);
    }

    // The parameter: "Normal" by default, "Fine 40" / "Broad 40", typed values; older projects open at Normal
    // (whatever this instance had), a project keeps its Detail, factory presets leave it alone.
    {
        Rig r;
        auto* dp = r.proc->apvts.getParameter (ids::detail);
        auto raw = [] (Rig& x) { return x.proc->apvts.getRawParameterValue (ids::detail)->load(); };
        auto typed = [dp] (const char* t) { return dp->convertFrom0to1 (dp->getValueForText (t)); };
        const bool texts = dp->getCurrentValueAsText() == "Normal" && dp->getText (dp->convertTo0to1 (40.0f), 32) == "Fine 40"
                           && dp->getText (dp->convertTo0to1 (-40.0f), 32) == "Broad 40"
                           && std::abs (typed ("fine 30") - 30.0f) < 0.1f && std::abs (typed ("Broad 25") + 25.0f) < 0.1f
                           && std::abs (typed ("-60") + 60.0f) < 0.1f && typed ("fine") == 100.0f && typed ("broad") == -100.0f
                           && std::abs (typed ("normal")) < 0.1f && dp->getName (32) == "Detail" && dp->isAutomatable();

        auto old = r.proc->apvts.copyState();
        old.removeChild (old.getChildWithProperty ("id", ids::detail), nullptr);
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (*old.createXml(), oldState);
        Rig b;
        b.set (ids::detail, 70.0f);
        b.proc->setStateInformation (oldState.getData(), (int) oldState.getSize());
        const bool oldOpen = raw (b) == 0.0f;

        Rig c;
        c.set (ids::detail, -35.0f);
        juce::MemoryBlock saved;
        c.proc->getStateInformation (saved);
        Rig d;
        d.proc->setStateInformation (saved.getData(), (int) saved.getSize());
        const bool kept = std::abs (raw (d) + 35.0f) < 0.1f;
        d.proc->setCurrentProgram (2);
        const bool presetKeeps = std::abs (raw (d) + 35.0f) < 0.1f && d.proc->getPresetDisplayName() == "Mix bus glue";
        check (texts && oldOpen && kept && presetKeeps, "Detail: texts, older projects, saved, presets",
               "default " + dp->getCurrentValueAsText() + ", 40 -> " + dp->getText (dp->convertTo0to1 (40.0f), 32)
                   + ", older project -> " + juce::String (raw (b), 1) + ", saved -> " + juce::String (raw (d), 1)
                   + ", after a preset " + d.proc->getPresetDisplayName());
    }

    // The Detail row: one slim row under Reveal and Smooth, inside AUTO EQ, shown while either is on and hidden
    // while both are off (5.3 calmer deck); nothing overlapping with everything shown.
    {
        Rig r;
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
        auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
        anneal::Fader* detail = nullptr;
        anneal::Knob* reveal = nullptr;
        anneal::Knob* smooth = nullptr;
        if (editor != nullptr)
            for (auto* c : editor->getContent().getChildren())
            {
                if (auto* f = dynamic_cast<anneal::Fader*> (c); f != nullptr && f->slider.getTooltip().startsWith ("Detail:"))
                    detail = f;
                if (auto* k = dynamic_cast<anneal::Knob*> (c))
                {
                    if (k->slider.getTooltip().startsWith ("Reveal:")) reveal = k;
                    if (k->slider.getTooltip().startsWith ("Smooth:")) smooth = k;
                }
            }
        if (detail == nullptr || reveal == nullptr || smooth == nullptr)
        {
            check (false, "the Detail row under Reveal and Smooth", "missing");
            return;
        }
        auto& content = editor->getContent();
        content.tick (0.0f);
        const bool shownAtDefault = detail->isVisible();   // Reveal and Smooth are 40 % by default
        r.set (ids::reveal, 0.0f);
        r.set (ids::smooth, 0.0f);
        content.tick (0.0f);
        const bool hiddenWhenOff = ! detail->isVisible();
        r.set (ids::smooth, 30.0f);
        r.set (ids::tilt, -0.6f);
        r.set (ids::stereoMode, 1.0f);
        r.set (ids::scMode, 1.0f);
        r.set (ids::refMode, 2.0f);
        content.tick (0.0f);
        const bool shownWithSmooth = detail->isVisible();
        juce::String overlaps;
        auto kids = content.getChildren();
        for (int i = 0; i < kids.size(); ++i)
            for (int j = i + 1; j < kids.size(); ++j)
                if (kids[i]->isVisible() && kids[j]->isVisible() && kids[i]->getBounds().intersects (kids[j]->getBounds()))
                    overlaps << kids[i]->getBounds().toString() << " x " << kids[j]->getBounds().toString() << "  ";
        const bool placed = detail->getY() >= std::max (reveal->getBottom(), smooth->getBottom()) && detail->getBottom() <= 628
                            && detail->getX() >= reveal->getX() && detail->getRight() <= smooth->getRight() && detail->getRight() < 490
                            && detail->getHeight() <= 26;
        r.set (ids::detail, 40.0f);
        const auto shownText = detail->slider.getTextFromValue (detail->slider.getValue());
        check (shownAtDefault && hiddenWhenOff && shownWithSmooth && placed && overlaps.isEmpty() && shownText == "Fine 40",
               "the Detail row under Reveal and Smooth, only while either is on",
               detail->getBounds().toString() + " under " + reveal->getBounds().toString() + " / " + smooth->getBounds().toString()
                   + "; default " + (shownAtDefault ? "shown" : "HIDDEN") + ", both off " + (hiddenWhenOff ? "hidden" : "SHOWN")
                   + ", Smooth 30 " + (shownWithSmooth ? "shown" : "HIDDEN") + ", reads \"" + shownText + "\""
                   + (overlaps.isEmpty() ? juce::String (", no overlaps") : ", overlaps " + overlaps));
    }
}

// 5.3.7 bug pass. At Zero latency the Listen solos rest (the engine plays the full result), but prepareToPlay
// started the Listen crossfade at "solo" whenever a solo was chosen, while every block aims it at "off". So after
// each prepare (a new buffer size, the start of an export) the dry share of a Mix under 100 % faded back in over
// 30 ms. A resting solo must sound exactly like Listen off, from the first sample, after every prepare.
void testFixes537()
{
    section ("57. 5.3.7 bug fixes");
    const double fs = 48000.0;
    Channels in = testsig::silence (2, (int) (0.25 * fs));
    for (int i = 0; i < (int) in[0].size(); ++i)
        in[0][(size_t) i] = in[1][(size_t) i] = 0.25f * (float) std::sin (2.0 * testsig::pi * 440.0 * i / fs);
    Channels outs[2][2];
    for (int i = 0; i < 2; ++i)
    {
        Rig r;
        r.set (ids::zeroLatency, 1.0f);
        r.set (ids::mix, 50.0f);
        r.set (ids::heat, 40.0f);
        r.set (ids::listen, i == 0 ? 0.0f : 3.0f);   // Off / Heat only (resting at zero latency)
        r.prepare (fs, 512);
        outs[i][0] = r.run (in, fixedBlocks (512));
        r.prepare (fs, 256);                           // the host prepares again (another buffer size, an export)
        outs[i][1] = r.run (in, fixedBlocks (256));
    }
    double worst = 0.0;
    int firstDiff = -1;
    for (int pass = 0; pass < 2; ++pass)
        for (int c = 0; c < 2; ++c)
            for (size_t k = 0; k < outs[0][pass][(size_t) c].size(); ++k)
            {
                const double d = std::abs (outs[0][pass][(size_t) c][k] - outs[1][pass][(size_t) c][k]);
                if (d > 0.0 && firstDiff < 0)
                    firstDiff = (int) k;
                worst = std::max (worst, d);
            }
    check (worst == 0.0, "a solo resting at zero latency sounds like Listen off",
           "Mix 50 %, Heat only chosen vs. Listen off, after two prepares: max difference " + juce::String (worst, 6)
               + (firstDiff >= 0 ? " (from sample " + juce::String (firstDiff) + ")" : juce::String()));
}

// 5.3.7 Heat limit (roadmap item 23): in every band (one ERB around each bin) Heat's harmonics stay at least the
// limit under the music there (the main path after the EQ). Off, the default, is bit-identical to before.
namespace heatlimit
{
    // Three partials at -18 dBFS (220 Hz, 1 kHz, 3.5 kHz, so every flavour has something to grow from) over quiet
    // pink noise; with 'wide' the noise differs between left and right, so the sides carry something (Mid/Side).
    // Without 'thousand' there is no 1 kHz partial, whose skirt in Low latency's 21 ms frame (47 Hz bins) covers
    // 220 Hz's 5th harmonic at 1.1 kHz: Low can't tell them apart and counts the skirt as music there.
    Channels signal (double fs, double seconds, bool wide, bool thousand = true)
    {
        const int n = (int) (seconds * fs);
        Channels x = testsig::silence (2, n);
        testsig::Pink pl (21), pr (wide ? 22u : 21u);
        for (int i = 0; i < n; ++i)
        {
            const double t = i / fs;
            const float tones = 0.125f * (float) (std::sin (2.0 * testsig::pi * 220.0 * t) + std::sin (2.0 * testsig::pi * 3500.0 * t)
                                                  + (thousand ? std::sin (2.0 * testsig::pi * 1000.0 * t) : 0.0));
            x[0][(size_t) i] = tones + 0.003f * pl.next();
            x[1][(size_t) i] = tones + 0.003f * pr.next();
        }
        return x;
    }

    // Power per bin (left and right together): Hann frames of 8192, hop 4096, averaged over [from, from + len).
    std::vector<double> powerOf (const Channels& x, int from, int len)
    {
        constexpr int order = 13, n = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> buf (2 * n);
        std::vector<double> p (n / 2 + 1, 0.0);
        int frames = 0;
        for (int start = from; start + n <= from + len; start += n / 2, ++frames)
            for (const auto& ch : x)
            {
                for (int i = 0; i < n; ++i)
                    buf[(size_t) i] = ch[(size_t) (start + i)] * (float) (0.5 - 0.5 * std::cos (2.0 * testsig::pi * i / n));
                std::fill (buf.begin() + n, buf.end(), 0.0f);
                fft.performRealOnlyForwardTransform (buf.data());
                for (int k = 0; k <= n / 2; ++k)
                    p[(size_t) k] += (double) buf[(size_t) 2 * k] * buf[(size_t) 2 * k] + (double) buf[(size_t) 2 * k + 1] * buf[(size_t) 2 * k + 1];
            }
        for (auto& v : p)
            v /= std::max (1, frames);
        return p;
    }

    struct Ratio { double worstDb = -200.0, atHz = 0.0; int bands = 0; };

    // The harmonics against the main path in one-ERB bands every 0.1 Cam from 100 Hz to 15 kHz, where the
    // harmonics are above -110 dB of a full-scale sine's frame power: the largest ratio and where it is. A band is
    // never narrower than minHz, the engine's own resolution (4 bins of its frame: 47 Hz in Precise, 188 Hz in Low).
    Ratio worstRatio (const std::vector<double>& harm, const std::vector<double>& main, double fs, double minHz)
    {
        const int n = 2 * ((int) harm.size() - 1);
        const double binHz = fs / n, fullScale = (double) n * n / 16.0;   // a 0 dBFS sine's peak bin under Hann
        Ratio r;
        for (double z = anneal::erb::hzToCam (100.0); z <= anneal::erb::hzToCam (15000.0); z += 0.1)
        {
            const double centre = anneal::erb::camToHz (z);
            const double loHz = std::min (anneal::erb::camToHz (z - 0.5), centre - 0.5 * minHz);
            const double hiHz = std::max (anneal::erb::camToHz (z + 0.5), centre + 0.5 * minHz);
            const int lo = (int) std::ceil (loHz / binHz);
            const int hi = (int) std::floor (hiHz / binHz);
            double h = 0.0, m = 0.0;
            for (int k = lo; k <= hi; ++k)
            {
                h += harm[(size_t) k];
                m += main[(size_t) k];
            }
            if (h < fullScale * 1.0e-11)
                continue;
            ++r.bands;
            const double d = 10.0 * std::log10 (h / std::max (m, 1.0e-30));
            if (d > r.worstDb)
            {
                r.worstDb = d;
                r.atHz = anneal::erb::camToHz (z);
            }
        }
        return r;
    }

    // The engine twice on the same input, once normally and once with Listen Heat only: the harmonics are the
    // solo, the main path the difference (normal - Heat solo = the main path exactly, section on Listen).
    Ratio measure (anneal::EngineParams p, int mode, const Channels& in, double fs)
    {
        Channels outs[2];
        for (int i = 0; i < 2; ++i)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2, mode);
            p.listen = i == 0 ? anneal::listenOff : anneal::listenHarmonics;
            e->setParameters (p);
            outs[i] = runEngine (*e, in, fixedBlocks (512));
        }
        Channels main = outs[0];
        for (int c = 0; c < 2; ++c)
            for (size_t k = 0; k < main[(size_t) c].size(); ++k)
                main[(size_t) c][k] -= outs[1][(size_t) c][k];
        const int from = (int) (2.0 * fs), len = (int) (2.0 * fs);
        return worstRatio (powerOf (outs[1], from, len), powerOf (main, from, len), fs,
                           4.0 * fs / anneal::SpectralEngine::fftSizeFor (fs, mode));
    }

    juce::String show (const Ratio& r)
    {
        return db (r.worstDb) + " at " + anneal::formatHz ((float) r.atHz);
    }
}

void testHeatLimit537()
{
    section ("58. 5.3.7 Heat limit");
    const double fs = 48000.0;
    constexpr double tolerance = 1.5;   // dB over the limit: the engine's frames and bands against this measurement's
    const auto in = heatlimit::signal (fs, 4.5, false);
    const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");

    // Off, every flavour grows harmonics far louder than the noise between the partials; with the limit at -12 dB
    // the harmonics in every band stay at least 12 dB under the music there.
    {
        juce::String log;
        bool ok = true;
        for (int f = 0; f < anneal::HarmonicSynth::numFlavours; ++f)
        {
            auto p = neutral();
            p.heat = 1.0f;
            p.flavour = f;
            const auto off = heatlimit::measure (p, anneal::SpectralEngine::precise, in, fs);
            p.heatLimitDb = -12.0f;
            const auto at12 = heatlimit::measure (p, anneal::SpectralEngine::precise, in, fs);
            ok = ok && off.worstDb > -12.0 + 10.0 && at12.bands > 10 && at12.worstDb <= -12.0 + tolerance;
            log << juce::StringArray { "Clean", "Tape", "Tube", "Transformer", "Exciter" }[f] << " " << heatlimit::show (off)
                << " -> " << heatlimit::show (at12) << "  ";
        }
        check (ok, "the harmonics stop at the limit, band by band, every flavour",
               "worst band, harmonics against the music (Off -> " + minus + "12 dB): " + log);
    }

    // Each limit holds where it is set (Clean, -6 / -18 / -30 dB).
    {
        juce::String log;
        bool ok = true;
        for (float limit : { -6.0f, -18.0f, -30.0f })
        {
            auto p = neutral();
            p.heat = 1.0f;
            p.heatLimitDb = limit;
            const auto r = heatlimit::measure (p, anneal::SpectralEngine::precise, in, fs);
            ok = ok && r.bands > 10 && r.worstDb <= limit + tolerance;
            log << minus << juce::String (juce::roundToInt (-limit)) << ": " << heatlimit::show (r) << "  ";
        }
        check (ok, "every limit holds (-6, -18, -30 dB)", log);
    }

    // Low latency (partials farther apart than its 47 Hz bins can blur; with the 1 kHz partial next to 220 Hz's 5th
    // harmonic, shown for the record), Mid/Side (the sides carrying something), and Spread load: the same output
    // one hop later.
    {
        auto p = neutral();
        p.heat = 1.0f;
        p.heatLimitDb = -12.0f;
        const auto low = heatlimit::measure (p, anneal::SpectralEngine::low, heatlimit::signal (fs, 4.5, false, false), fs);
        const auto lowClose = heatlimit::measure (p, anneal::SpectralEngine::low, in, fs);
        auto pm = p;
        pm.midSide = 1.0f;
        const auto ms = heatlimit::measure (pm, anneal::SpectralEngine::precise, heatlimit::signal (fs, 4.5, true), fs);

        Channels outs[2];
        int hop = 0;
        for (int i = 0; i < 2; ++i)
        {
            auto e = std::make_unique<anneal::SpectralEngine>();
            e->prepare (fs, 2, anneal::SpectralEngine::normal);
            e->setSpread (i == 1);
            e->reset();
            e->setParameters (p);
            hop = e->getHopSize();
            outs[i] = runEngine (*e, in, randomBlocks (1, 700, 58));
        }
        double worst = 0.0;
        for (int c = 0; c < 2; ++c)
            for (size_t k = 0; k + (size_t) hop < outs[0][(size_t) c].size(); ++k)
                worst = std::max (worst, (double) std::abs (outs[1][(size_t) c][k + (size_t) hop] - outs[0][(size_t) c][k]));
        check (low.worstDb <= -12.0 + tolerance && ms.worstDb <= -12.0 + tolerance && worst < 1.0e-6,
               "Low latency, Mid/Side, Spread load",
               minus + "12 dB: Low " + heatlimit::show (low) + " (with the 1 kHz partial " + heatlimit::show (lowClose)
                   + "), Mid/Side " + heatlimit::show (ms)
                   + "; Spread vs. one hop later: largest difference " + juce::String (worst, 9));
    }

    // Off is bit-identical: the defaults are Off, and a limit moved to -12 dB and back plays exactly like one that
    // never moved (music, Reveal, Smooth, Heat 50 % Tape) once its overlap-add tail has passed. At Zero latency, where
    // Heat rests, the limit changes nothing either.
    {
        const auto music = testsig::musicLike (fs, 6.0, 31);
        const int split = (int) (1.5 * fs);
        Channels first = music, second = music;
        for (int c = 0; c < 2; ++c)
        {
            first[(size_t) c].resize ((size_t) split);
            second[(size_t) c].erase (second[(size_t) c].begin(), second[(size_t) c].begin() + split);
        }
        Channels outs[2];
        for (int i = 0; i < 2; ++i)
        {
            Rig r;
            r.set (ids::reveal, 60.0f); r.set (ids::smooth, 60.0f); r.set (ids::heat, 50.0f); r.set (ids::flavour, 1.0f);
            if (i == 1)
                r.set (ids::heatLimit, -12.0f);
            r.prepare (fs, 512);
            r.run (first, fixedBlocks (512));
            if (i == 1)
                r.set (ids::heatLimit, 0.0f);
            outs[i] = r.run (second, fixedBlocks (512));
        }
        double worst = 0.0;
        for (int c = 0; c < 2; ++c)
            for (size_t k = (size_t) fs; k < outs[0][(size_t) c].size(); ++k)
                worst = std::max (worst, (double) std::abs (outs[0][(size_t) c][k] - outs[1][(size_t) c][k]));

        Channels zeroOuts[2];
        for (int i = 0; i < 2; ++i)
        {
            Rig r;
            r.set (ids::zeroLatency, 1.0f);
            r.set (ids::heat, 80.0f);
            r.set (ids::heatLimit, i == 0 ? 0.0f : -24.0f);
            r.prepare (fs, 512);
            zeroOuts[i] = r.run (in, fixedBlocks (512));
        }
        double zeroDiff = 0.0;
        for (int c = 0; c < 2; ++c)
            for (size_t k = 0; k < zeroOuts[0][(size_t) c].size(); ++k)
                zeroDiff = std::max (zeroDiff, (double) std::abs (zeroOuts[0][(size_t) c][k] - zeroOuts[1][(size_t) c][k]));

        Rig d;
        const bool defaults = anneal::EngineParams().heatLimitDb == anneal::heatLimitOffDb && anneal::heatLimitIsOff (0.0f)
                              && ! anneal::heatLimitIsOff (-0.1f) && d.proc->apvts.getRawParameterValue (ids::heatLimit)->load() == 0.0f;
        check (worst == 0.0 && zeroDiff == 0.0 && defaults, "Off bit-identical to before (also back from -12 dB)",
               "max diff after returning to Off " + juce::String (worst, 12) + "; Zero latency with " + minus + "24 dB vs. Off "
                   + juce::String (zeroDiff, 12) + (defaults ? "; defaults Off" : "; DEFAULT NOT OFF"));
    }

    // What it costs (informational): the engine on 10 s of music with Heat 60 %, Precise and Low, Off against -12 dB.
    {
        const auto music = testsig::musicLike (fs, 10.0, 32);
        juce::String line = "CPU, % of one core (Off / " + minus + "12 dB):";
        for (auto mode : { anneal::SpectralEngine::low, anneal::SpectralEngine::precise })
        {
            double pct[2] {};
            for (int i = 0; i < 2; ++i)
            {
                auto e = std::make_unique<anneal::SpectralEngine>();
                e->prepare (fs, 2, mode);
                auto p = neutral();
                p.reveal = 0.6f; p.smooth = 0.6f; p.heat = 0.6f;
                p.heatLimitDb = i == 0 ? 0.0f : -12.0f;
                e->setParameters (p);
                double best = 1.0e9;
                for (int rep = 0; rep < 3; ++rep)
                {
                    const auto t0 = std::chrono::steady_clock::now();
                    runEngine (*e, music, fixedBlocks (512));
                    best = std::min (best, std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count());
                }
                pct[i] = 100.0 * best / 10.0;
            }
            line << (mode == anneal::SpectralEngine::low ? " Low " : ", Precise ") << juce::String (pct[0], 2) << " / " << juce::String (pct[1], 2);
        }
        info (line);
    }

    // The parameter: "Off" by default, "−12 dB", typed values; older projects open with Off (whatever this instance
    // had), a project keeps its limit, factory presets leave it alone.
    {
        Rig r;
        auto* hp = r.proc->apvts.getParameter (ids::heatLimit);
        auto raw = [] (Rig& x) { return x.proc->apvts.getRawParameterValue (ids::heatLimit)->load(); };
        auto typed = [hp] (const juce::String& t) { return hp->convertFrom0to1 (hp->getValueForText (t)); };
        const bool texts = hp->getCurrentValueAsText() == "Off" && hp->getText (hp->convertTo0to1 (-12.0f), 32) == minus + "12 dB"
                           && hp->getText (hp->convertTo0to1 (-4.5f), 32) == minus + "4.5 dB"
                           && std::abs (typed ("-18") + 18.0f) < 0.1f && std::abs (typed ("9 dB") + 9.0f) < 0.1f
                           && std::abs (typed (minus + "24 dB") + 24.0f) < 0.1f && typed ("off") == 0.0f
                           && typed ("none") == 0.0f && std::abs (typed ("45") + 30.0f) < 0.1f
                           && hp->getName (32) == "Heat Limit" && hp->isAutomatable();

        auto old = r.proc->apvts.copyState();
        old.removeChild (old.getChildWithProperty ("id", ids::heatLimit), nullptr);
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (*old.createXml(), oldState);
        Rig b;
        b.set (ids::heatLimit, -18.0f);
        b.proc->setStateInformation (oldState.getData(), (int) oldState.getSize());
        const bool oldOpen = raw (b) == 0.0f;

        Rig c;
        c.set (ids::heatLimit, -9.0f);
        juce::MemoryBlock saved;
        c.proc->getStateInformation (saved);
        Rig d;
        d.proc->setStateInformation (saved.getData(), (int) saved.getSize());
        const bool kept = std::abs (raw (d) + 9.0f) < 0.1f;
        d.proc->setCurrentProgram (2);
        const bool presetKeeps = std::abs (raw (d) + 9.0f) < 0.1f && d.proc->getPresetDisplayName() == "Mix bus glue";
        check (texts && oldOpen && kept && presetKeeps, "Heat limit: texts, older projects, saved, presets",
               "default " + hp->getCurrentValueAsText() + ", -12 -> " + hp->getText (hp->convertTo0to1 (-12.0f), 32)
                   + ", typed 45 -> " + juce::String (typed ("45"), 1) + ", older project -> " + juce::String (raw (b), 1)
                   + ", saved -> " + juce::String (raw (d), 1) + ", after a preset " + d.proc->getPresetDisplayName());
    }

    // The Flavour menu: its Heat limit section sets the parameter, the pill adds the limit ("Tape · −12 dB") and
    // its text fits the pill for every flavour and every step; Off reads as before. No new control on the deck.
    {
        Rig r;
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
        auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
        anneal::FlavourMenu* menu = nullptr;
        if (editor != nullptr)
            for (auto* c : editor->getContent().getChildren())
                if (auto* f = dynamic_cast<anneal::FlavourMenu*> (c))
                    menu = f;
        if (menu == nullptr)
        {
            check (false, "the Heat limit in the Flavour menu", "no Flavour menu");
            return;
        }
        const float width = menu->textArea().getWidth();
        const auto offText = anneal::FlavourMenu::shown (0, 0.0f, width);
        const bool offAsBefore = offText.name == "Flavour: clean" && offText.limit.isEmpty() && offText.fontSize == 12.0f;
        juce::String tooWide;
        float widest = 0.0f;
        for (int f = 0; f < anneal::FlavourMenu::numChoices; ++f)
            for (float step : { 4.5f, 6.0f, 9.0f, 12.0f, 18.0f, 24.0f, 30.0f })
            {
                const auto s = anneal::FlavourMenu::shown (f, -step, width);
                const float w = anneal::FlavourMenu::textWidth (s.name + s.limit, s.fontSize);
                widest = std::max (widest, w);
                if (w > width || s.limit.isEmpty())
                    tooWide << s.name << s.limit << " (" << juce::String (w, 1) << " px)  ";
            }
        menu->choose (102 + 2);   // the third step, -12 dB
        const float chosen = r.proc->apvts.getRawParameterValue (ids::heatLimit)->load();
        menu->choose (2);         // Tape
        const auto tape = anneal::FlavourMenu::shown (menu->getChoice(), menu->getLimitDb(), width);
        const juce::String tapeText = tape.name + tape.limit;
        menu->choose (101);       // Off
        const bool backOff = r.proc->apvts.getRawParameterValue (ids::heatLimit)->load() == 0.0f;
        const bool tip = menu->getTooltip().contains ("Heat limit");
        check (offAsBefore && tooWide.isEmpty() && chosen == -12.0f && tapeText == juce::String::fromUTF8 ("Tape \xc2\xb7 ") + minus + "12 dB"
                   && backOff && tip,
               "the Heat limit in the Flavour menu",
               "pill text area " + juce::String (width, 1) + " px, widest text " + juce::String (widest, 1) + " px; menu -12 dB -> "
                   + juce::String (chosen, 1) + ", reads \"" + tapeText + "\", Off -> " + (backOff ? "off" : "NOT OFF")
                   + (tooWide.isEmpty() ? juce::String() : "; TOO WIDE: " + tooWide));
    }
}

// 5.3.8: Peak hold for the input too. HOLD's menu: Off / Output / Input and output / Clear. The input is held
// beside the output all along; Input and output draws it too, as a fainter dotted line.
void testPeakHoldInput538()
{
    section ("59. 5.3.8 Peak hold for the input too");
    constexpr int nb = anneal::DisplayFrame::numBands;
    using Frames = anneal::TripleBuffer<anneal::DisplayFrame>;
    const auto minus = juce::String::fromUTF8 ("\xe2\x88\x92");

    // Two displays fed the same frames by hand: one showing the input's line too, one the output's only.
    {
        Rig r;
        auto bufA = std::make_unique<Frames>(), bufB = std::make_unique<Frames>();
        anneal::PerceptionScreen both (*bufA, r.proc->apvts), outOnly (*bufB, r.proc->apvts);
        both.setSize (1440, 356);
        outOnly.setSize (1440, 356);
        const float z0 = (float) anneal::erb::hzToCam (20.0);
        const float dz = (float) ((anneal::erb::hzToCam (20000.0) - z0) / (nb - 1));
        const auto camOf = [] (double hz) { return (float) anneal::erb::hzToCam (hz); };
        // A falling, pink-like slope: the input at levelDb (at 0 Cam) with +12 dB over +-1 Cam around inHz, the
        // output 6 dB lower with its +12 dB around outHz, so the two lines have their bumps in different places.
        auto push = [&] (float levelDb, double inHz, double outHz)
        {
            for (auto* b : { bufA.get(), bufB.get() })
            {
                auto& fr = b->beginWrite();
                fr.z0 = z0;
                fr.dz = dz;
                for (int i = 0; i < nb; ++i)
                {
                    const float cam = z0 + (float) i * dz;
                    auto bump = [&] (double hz) { return hz > 0.0 && std::abs (cam - camOf (hz)) < 1.0f ? 12.0f : 0.0f; };
                    fr.inDb[i] = levelDb + bump (inHz) - 0.5f * cam;
                    fr.outDb[i] = levelDb - 6.0f + bump (outHz) - 0.5f * cam;
                    fr.gainDb[i] = fr.sideGainDb[i] = 0.0f;
                    fr.heatDb[i] = -200.0f;
                }
                b->endWrite();
            }
            both.refreshNow();
            outOnly.refreshNow();
        };
        const auto expectedIn = [&camOf] (float levelDb, double hz, bool bump) { return levelDb + (bump ? 12.0f : 0.0f) - 0.5f * camOf (hz); };

        const bool offAtStart = ! both.isPeakHoldOn() && ! both.isPeakHoldInputOn() && ! Rig().proc->displayPeakHold.input;
        both.choosePeakHold (3);      // Input and output
        outOnly.choosePeakHold (2);   // Output
        const bool menuOn = both.isPeakHoldOn() && both.isPeakHoldInputOn() && both.getPeakHoldButton().getToggleState()
                            && outOnly.isPeakHoldOn() && ! outOnly.isPeakHoldInputOn();

        // Loud, the input's bump at 200 Hz -> 2 kHz -> 8 kHz and the output's at 500 Hz -> 5 kHz -> 500 Hz, then 40 dB
        // quieter: neither line falls; the input's has the input's bumps only, the output's the output's only.
        std::vector<float> was;
        int frames = 0, fell = 0;
        auto watch = [&]
        {
            ++frames;
            for (int i = 0; i < 400; ++i)
            {
                const float hz = 20.0f * std::pow (1000.0f, (float) i / 399.0f);
                for (int line = 0; line < 2; ++line)
                {
                    const float v = line == 0 ? both.heldInputPeakAtHz (hz) : both.heldPeakAtHz (hz);
                    const size_t at = (size_t) (2 * i + line);
                    if (was.size() == 800 && v < was[at])
                        ++fell;
                    if (was.size() < 800) was.push_back (v); else was[at] = v;
                }
            }
        };
        const std::pair<double, double> bumps[] = { { 200.0, 500.0 }, { 2000.0, 5000.0 }, { 8000.0, 500.0 } };
        for (auto [inHz, outHz] : bumps)
            for (int k = 0; k < 6; ++k) { push (-30.0f, inHz, outHz); watch(); }
        for (int k = 0; k < 12; ++k) { push (-70.0f, 0.0, 0.0); watch(); }

        float inErr = 0.0f, outErr = 0.0f;
        for (double hz : { 200.0, 2000.0, 8000.0 })   // the input's bumps: in the input's line, not in the output's
        {
            inErr = juce::jmax (inErr, std::abs (both.heldInputPeakAtHz ((float) hz) - expectedIn (-30.0f, hz, true)));
            outErr = juce::jmax (outErr, std::abs (both.heldPeakAtHz ((float) hz) - (expectedIn (-30.0f, hz, false) - 6.0f)));
        }
        for (double hz : { 500.0, 5000.0 })           // the output's bumps: the other way round
        {
            inErr = juce::jmax (inErr, std::abs (both.heldInputPeakAtHz ((float) hz) - expectedIn (-30.0f, hz, false)));
            outErr = juce::jmax (outErr, std::abs (both.heldPeakAtHz ((float) hz) - (expectedIn (-30.0f, hz, true) - 6.0f)));
        }
        float sameOut = 0.0f;
        for (int i = 0; i < 400; ++i)
        {
            const float hz = 20.0f * std::pow (1000.0f, (float) i / 399.0f);
            sameOut = juce::jmax (sameOut, std::abs (both.heldPeakAtHz (hz) - outOnly.heldPeakAtHz (hz)));
        }
        const bool noInputShown = outOnly.heldInputPeakAtHz (1000.0f) <= -139.0f;
        check (offAtStart && menuOn && fell == 0 && frames == 30 && inErr < 0.1f && outErr < 0.1f && sameOut == 0.0f && noInputShown,
               "the input's held line follows the input",
               juce::String (frames) + " frames, " + juce::String (fell) + " falls in either line; input's line within "
                   + juce::String (inErr, 3) + " dB of the input's peaks (its bumps at 200 Hz / 2 kHz / 8 kHz, none at 500 Hz / "
                   "5 kHz), output's within " + juce::String (outErr, 3) + " dB of the output's; the output's line the same as with "
                   "Output alone (" + juce::String (sameOut, 3) + " dB); "
                   + (offAtStart ? "off by default" : "ON BY DEFAULT") + (menuOn ? "" : "; MENU DIDN'T SET IT")
                   + (noInputShown ? "" : "; OUTPUT SHOWS AN INPUT LINE"));

        // Drawing: Input and output adds a line inside the EQ lane only; back on Output the display is pixel for
        // pixel the one that only ever held the output, and the output's line goes on (no new start). Input and
        // output again shows the input held all along.
        auto diff = [] (const juce::Image& a, const juce::Image& b, const juce::Rectangle<int>& lane, int& outside)
        {
            int n = 0;
            outside = 0;
            for (int y = 0; y < a.getHeight(); ++y)
                for (int x = 0; x < a.getWidth(); ++x)
                    if (a.getPixelAt (x, y) != b.getPixelAt (x, y))
                    {
                        ++n;
                        outside += lane.contains (x, y) ? 0 : 1;
                    }
            return n;
        };
        const auto lane = both.getEqLane().toNearestInt();
        int outsideBoth = 0, outsideBack = 0;
        const int drawnBoth = diff (both.createComponentSnapshot (both.getLocalBounds()),
                                    outOnly.createComponentSnapshot (outOnly.getLocalBounds()), lane, outsideBoth);
        const float in2k = both.heldInputPeakAtHz (2000.0f), out1k = both.heldPeakAtHz (1000.0f);
        both.choosePeakHold (2);
        const int drawnBack = diff (both.createComponentSnapshot (both.getLocalBounds()),
                                    outOnly.createComponentSnapshot (outOnly.getLocalBounds()), lane, outsideBack);
        const bool keptOut = both.heldPeakAtHz (1000.0f) == out1k && both.heldInputPeakAtHz (2000.0f) <= -139.0f;
        push (-70.0f, 0.0, 0.0);
        both.choosePeakHold (3);
        const bool inAgain = both.heldInputPeakAtHz (2000.0f) == in2k;
        check (drawnBoth > 300 && outsideBoth == 0 && drawnBack == 0 && keptOut && inAgain,
               "Input and output draws a second line; Output as before",
               "Input and output: " + juce::String (drawnBoth) + " pixels more, " + juce::String (outsideBoth)
                   + " outside the EQ lane; Output: " + juce::String (drawnBack) + " pixels differ from a display that only "
                   "held the output" + (keptOut ? ", its line kept" : ", ITS LINE STARTED OVER") + "; the input's 2 kHz peak "
                   + (inAgain ? "back as held" : "LOST") + " (" + juce::String (in2k, 2) + " dB)");

        // The read-out: "peak in -x / out -y dB" with the input, "peak -y dB" without (as in 5.3.5).
        const float hIn = both.heldInputPeakAtHz (2000.0f), hOut = both.heldPeakAtHz (2000.0f);
        const auto withIn = both.getReadoutText (2000.0f);
        const juce::String wantIn = "peak in " + minus + juce::String (std::abs (hIn), 1) + " / out " + minus + juce::String (std::abs (hOut), 1) + " dB";
        both.choosePeakHold (2);
        const auto outText = both.getReadoutText (2000.0f);
        const juce::String wantOut = "    peak " + minus + juce::String (std::abs (hOut), 1) + " dB";
        // Clear empties both lines, and the read-out loses the peak.
        both.choosePeakHold (3);
        both.choosePeakHold (4);
        const bool cleared = ! both.hasHeldPeaks() && both.heldInputPeakAtHz (2000.0f) <= -139.0f && both.heldPeakAtHz (2000.0f) <= -139.0f
                             && both.isPeakHoldOn() && both.isPeakHoldInputOn() && ! both.getReadoutText (2000.0f).contains ("peak");
        check (withIn.contains (wantIn) && outText.endsWith (wantOut) && ! outText.contains ("peak in") && cleared,
               "the read-out names both peaks; Clear empties both",
               "\"" + withIn.fromFirstOccurrenceOf ("peak", true, false) + "\"; Output: \"" + outText.fromFirstOccurrenceOf ("peak", true, false)
                   + "\"" + (cleared ? "; cleared" : "; NOT CLEARED"));

        // The longest read-out (Mid/Side, heat, width, sides, both peaks, paint mode) still fits the display.
        r.set (ids::width, 150.0f);
        auto pushLevels = [&] (float levelDb)   // a flat frame at levelDb, Mid/Side, with heat, the stereo image
        {
            auto& fr = bufA->beginWrite();
            fr.z0 = z0;
            fr.dz = dz;
            fr.midSide = true;
            fr.stereo = true;
            for (int i = 0; i < nb; ++i)
            {
                fr.inDb[i] = fr.outDb[i] = levelDb;
                fr.gainDb[i] = fr.sideGainDb[i] = -10.5f;
                fr.heatDb[i] = levelDb - 4.0f;
                fr.sideRelDb[i] = -10.0f;
            }
            bufA->endWrite();
            both.refreshNow();
        };
        pushLevels (-19.5f);    // the peaks held
        pushLevels (-100.5f);   // what is drawn now (three-digit levels)
        both.setPaintMode (true);
        auto longest = both.getReadoutText (2000.0f);
        both.setPaintMode (false);
        const auto fnt = anneal::theme::font (anneal::theme::size::caption + 0.5f);
        const float w = anneal::theme::tabularWidths (fnt, longest) + 20.0f, room = both.getEqLane().getWidth();
        check (longest.contains ("peak in") && longest.contains ("paint") && longest.contains ("sides") && w <= room,
               "the longest read-out fits the display",
               juce::String (w, 1) + " px of " + juce::String (room, 1) + ": \"" + longest + "\"");
    }

    // Through the plug-in: a ring Smooth cuts shows as the input's line over the output's there; the window keeps the
    // choice and the lines; the sound doesn't change.
    {
        const double fs = 48000.0;
        auto sig = pinkNoise (fs, 1.2, 0.03f, 61);
        for (auto& ch : sig)
            for (size_t i = 0; i < ch.size(); ++i)
                ch[i] += 0.1f * (float) std::sin (2.0 * juce::MathConstants<double>::pi * 3150.0 * (double) i / fs);
        Rig a, b;
        for (auto* rig : { &a, &b })
        {
            rig->set (ids::reveal, 0.0f);
            rig->set (ids::smooth, 100.0f);
            rig->prepare (fs, 512);
        }
        auto playWith = [] (Rig& r, const Channels& in, anneal::PerceptionScreen* screen, Channels& out)
        {
            juce::AudioBuffer<float> buf (2, 512);
            juce::MidiBuffer midi;
            for (int pos = 0; pos + 512 <= (int) in[0].size(); pos += 512)
            {
                for (int c = 0; c < 2; ++c)
                    buf.copyFrom (c, 0, in[(size_t) c].data() + pos, 512);
                r.proc->processBlock (buf, midi);
                for (int c = 0; c < 2; ++c)
                    out[(size_t) c].insert (out[(size_t) c].end(), buf.getReadPointer (c), buf.getReadPointer (c) + 512);
                if (screen != nullptr && (pos / 512) % 8 == 7)
                    screen->refreshNow();
            }
        };
        Channels outA (2), outB (2);
        float ringIn = 0.0f, ringOut = 0.0f, lowIn = 0.0f, lowOut = 0.0f, ringInAfter = 0.0f;
        bool reopened = false;
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (a.proc->createEditor());
            auto& screen = dynamic_cast<AnnealEditor&> (*ed).getContent().screen;
            screen.choosePeakHold (3);
            playWith (a, sig, &screen, outA);
            ringIn = screen.heldInputPeakAtHz (3150.0f);
            ringOut = screen.heldPeakAtHz (3150.0f);
            lowIn = screen.heldInputPeakAtHz (500.0f);
            lowOut = screen.heldPeakAtHz (500.0f);
        }
        {
            std::unique_ptr<juce::AudioProcessorEditor> ed (a.proc->createEditor());
            auto& screen = dynamic_cast<AnnealEditor&> (*ed).getContent().screen;
            reopened = screen.isPeakHoldOn() && screen.isPeakHoldInputOn();
            ringInAfter = screen.heldInputPeakAtHz (3150.0f);
        }
        playWith (b, sig, nullptr, outB);
        double maxDiff = 0.0;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < outA[(size_t) c].size(); ++i)
                maxDiff = std::max (maxDiff, (double) std::abs (outA[(size_t) c][i] - outB[(size_t) c][i]));
        check (ringIn - ringOut >= 3.0f && std::abs (lowIn - lowOut) < 1.5f && reopened && ringInAfter == ringIn && maxDiff == 0.0,
               "through the plug-in: the input's line shows what Smooth took",
               "3150 Hz ring held in " + db (ringIn, 1) + " / out " + db (ringOut, 1) + ", 500 Hz in " + db (lowIn, 1) + " / out "
                   + db (lowOut, 1) + "; after closing and opening the window " + (reopened ? "Input and output still on" : "MODE LOST")
                   + ", ring in " + db (ringInAfter, 1) + "; output with it on vs. off: difference " + juce::String (maxDiff, 12));
    }
}

// 5.3.9 Listen check (roadmap item 25): hear the output in Stereo (as is), summed to Mono, (L + R) / 2, or its Sides
// only, (L - R) / 2, on both speakers; after the Ceiling, before Bypass. Stereo, the default, is bit-identical.
void testListenCheck539()
{
    section ("60. 5.3.9 Listen check: Mono / Sides only");
    const double fs = 48000.0;
    const auto music = testsig::musicLike (fs, 1.0, 41);

    // Plays `in` through a fresh instance with the check set before prepare (so nothing fades in).
    auto play = [&] (int check, const std::function<void (Rig&)>& setup, const Channels& in, int channels = 2)
    {
        Rig r (channels);
        setup (r);
        r.set (ids::listenCheck, (float) check);
        r.prepare (fs, 512);
        return r.run (in, fixedBlocks (512));
    };

    // Mono and Sides are exactly the sum and the difference of what Stereo plays, the same on both speakers, in
    // Low (the defaults), Precise Mid/Side with Heat, Width and the Ceiling, and at Zero latency with Punch.
    {
        const std::pair<const char*, std::function<void (Rig&)>> setups[] = {
            { "Low", [] (Rig&) {} },
            { "Precise Mid/Side", [] (Rig& r)
              {
                  r.set (ids::latency, 2.0f); r.set (ids::stereoMode, 1.0f); r.set (ids::heat, 60.0f);
                  r.set (ids::width, 150.0f); r.set (ids::ceiling, 2.0f);
              } },
            { "Zero latency", [] (Rig& r) { r.set (ids::zeroLatency, 1.0f); r.set (ids::punch, 50.0f); } },
        };
        juce::String line;
        bool ok = true;
        for (const auto& [name, setup] : setups)
        {
            const auto st = play (anneal::checkStereo, setup, music);
            const auto mo = play (anneal::checkMono, setup, music);
            const auto si = play (anneal::checkSides, setup, music);
            double monoErr = 0.0, sidesErr = 0.0, lr = 0.0, sideLevel = 0.0;
            for (size_t i = 0; i < st[0].size(); ++i)
            {
                monoErr = std::max (monoErr, (double) std::abs (mo[0][i] - 0.5f * (st[0][i] + st[1][i])));
                sidesErr = std::max (sidesErr, (double) std::abs (si[0][i] - 0.5f * (st[0][i] - st[1][i])));
                lr = std::max ({ lr, (double) std::abs (mo[0][i] - mo[1][i]), (double) std::abs (si[0][i] - si[1][i]) });
                sideLevel = std::max (sideLevel, (double) std::abs (si[0][i]));
            }
            ok = ok && monoErr == 0.0 && sidesErr == 0.0 && lr == 0.0 && sideLevel > 0.01 && allFinite (mo) && allFinite (si);
            line << name << ": mono " << juce::String (monoErr, 9) << ", sides " << juce::String (sidesErr, 9) << ", L-R "
                 << juce::String (lr, 9) << ", sides peak " << db (toDb (sideLevel)) << ";  ";
        }
        check (ok, "Mono = (L+R)/2, Sides = (L-R)/2 of Stereo's output", line);
    }

    // What you hear: a tone upside down in the right channel (all side) vanishes in Mono and plays at its level in Sides
    // only; a tone alike in both (all centre) the other way round.
    {
        const int n = (int) fs;
        Channels side = testsig::silence (2, n), centre = testsig::silence (2, n);
        for (int i = 0; i < n; ++i)
        {
            const float s = 0.25f * (float) std::sin (2.0 * testsig::pi * 1000.0 * i / fs);
            side[0][(size_t) i] = s;
            side[1][(size_t) i] = -s;
            centre[0][(size_t) i] = centre[1][(size_t) i] = s;
        }
        auto neutralSetup = [] (Rig& r) { r.setNeutral(); };
        auto level = [&] (int check, const Channels& in)
        {
            const auto out = play (check, neutralSetup, in);
            return toDb (testsig::toneAmplitude (out[0].data() + n / 2, n / 3, 1000.0, fs) / 0.25);
        };
        const double sideMono = level (anneal::checkMono, side), sideSides = level (anneal::checkSides, side);
        const double centreMono = level (anneal::checkMono, centre), centreSides = level (anneal::checkSides, centre);
        check (sideMono < -100.0 && std::abs (sideSides) < 0.01 && std::abs (centreMono) < 0.01 && centreSides < -100.0,
               "a side tone vanishes in Mono, a centred one in Sides",
               "side tone: Mono " + db (sideMono) + ", Sides " + db (sideSides, 3) + "; centred tone: Mono " + db (centreMono, 3)
                   + ", Sides " + db (centreSides));
    }

    // A mono track is its own mono (bit for bit) and has no sides (silence).
    {
        const Channels in1 { music[0] };
        const auto st = play (anneal::checkStereo, [] (Rig&) {}, in1, 1);
        const auto mo = play (anneal::checkMono, [] (Rig&) {}, in1, 1);
        const auto si = play (anneal::checkSides, [] (Rig&) {}, in1, 1);
        double d = 0.0;
        for (size_t i = 0; i < st[0].size(); ++i)
            d = std::max (d, (double) std::abs (mo[0][i] - st[0][i]));
        check (d == 0.0 && maxAbs (si) == 0.0 && maxAbs (st) > 0.05, "a mono track: Mono as is, Sides silent",
               "Mono vs. Stereo " + juce::String (d, 12) + ", Sides peak " + juce::String (maxAbs (si), 12));
    }

    // Stereo is bit-identical: the default, and a check moved to Mono and Sides and back (once its 30 ms fade is over)
    // plays exactly like one that never moved; Bypass plays the input as it is whatever the check.
    {
        Rig a, b;
        for (auto* r : { &a, &b })
            r->prepare (fs, 512);
        const bool defaultStereo = a.proc->apvts.getRawParameterValue (ids::listenCheck)->load() == 0.0f;
        const auto outA = a.run (music, fixedBlocks (512));
        Channels outB = music;
        const int seg = (int) (0.25 * fs);
        for (int part = 0; part < 4; ++part)
        {
            b.set (ids::listenCheck, part == 1 ? 1.0f : part == 2 ? 2.0f : 0.0f);   // Stereo, Mono, Sides, Stereo
            const int from = part * seg, len = part == 3 ? (int) music[0].size() - from : seg;
            Channels piece { std::vector<float> (music[0].begin() + from, music[0].begin() + from + len),
                             std::vector<float> (music[1].begin() + from, music[1].begin() + from + len) };
            const auto o = b.run (piece, fixedBlocks (512));
            for (int c = 0; c < 2; ++c)
                std::copy (o[(size_t) c].begin(), o[(size_t) c].end(), outB[(size_t) c].begin() + from);
        }
        double before = 0.0, after = 0.0, during = 0.0;
        const int settled = 3 * seg + (int) (0.04 * fs);
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < outA[(size_t) c].size(); ++i)
            {
                const double d = std::abs (outA[(size_t) c][i] - outB[(size_t) c][i]);
                if ((int) i < seg)            before = std::max (before, d);
                else if ((int) i >= settled)  after = std::max (after, d);
                else                          during = std::max (during, d);
            }

        Rig s, m;
        for (auto* r : { &s, &m })
        {
            r->set (ids::bypass, 1.0f);
            r->prepare (fs, 512);
        }
        m.set (ids::listenCheck, 1.0f);
        const auto bs = s.run (music, fixedBlocks (512)), bm = m.run (music, fixedBlocks (512));
        double bypassDiff = 0.0;
        for (int c = 0; c < 2; ++c)
            for (size_t i = 0; i < bs[(size_t) c].size(); ++i)
                bypassDiff = std::max (bypassDiff, (double) std::abs (bs[(size_t) c][i] - bm[(size_t) c][i]));
        const double bypassDry = delayedError (bm, music, s.proc->getLatencySamples());
        // Bypass crossfades (wet + 1 x (dry - wet)), so it is the input to float rounding (section 12), whatever the wet.
        check (defaultStereo && before == 0.0 && after == 0.0 && during > 0.01 && bypassDiff < 1.0e-7 && bypassDry < 1.0e-6,
               "Stereo bit-identical (also back from Mono and Sides)",
               juce::String (defaultStereo ? "Stereo by default" : "NOT STEREO BY DEFAULT") + "; vs. never moved: before "
                   + juce::String (before, 12) + ", while checking " + juce::String (during, 3) + ", back on Stereo "
                   + juce::String (after, 12) + "; Bypass with Mono vs. Stereo " + juce::String (bypassDiff, 12)
                   + ", vs. the input " + juce::String (bypassDry, 9));
    }

    // Switching while playing fades (30 ms): no step bigger than the tones' own; the loudness match measures the
    // signal before the check, so its gain is the same whatever you hear.
    {
        const int n = (int) (1.2 * fs);
        Channels tones = testsig::silence (2, n);
        for (int i = 0; i < n; ++i)
        {
            tones[0][(size_t) i] = 0.25f * (float) std::sin (2.0 * testsig::pi * 440.0 * i / fs);
            tones[1][(size_t) i] = 0.25f * (float) std::sin (2.0 * testsig::pi * 554.37 * i / fs + 1.0);
        }
        auto maxStep = [] (const Channels& x, int from)
        {
            double m = 0.0;
            for (auto& ch : x)
                for (size_t i = (size_t) from + 1; i < ch.size(); ++i)
                    m = std::max (m, (double) std::abs (ch[i] - ch[i - 1]));
            return m;
        };
        Rig steady, sw;
        for (auto* r : { &steady, &sw })
        {
            r->setNeutral();
            r->prepare (fs, 256);
        }
        const auto still = steady.run (tones, fixedBlocks (256));
        int k = 0;
        Channels moved = tones;
        juce::AudioBuffer<float> buf (2, 256);
        juce::MidiBuffer midi;
        for (int pos = 0; pos + 256 <= n; pos += 256, ++k)
        {
            if (k % 40 == 20)   // every ~0.2 s: Mono, Sides, Stereo, Sides, Mono ... (Sides <-> Mono directly too)
                sw.set (ids::listenCheck, (float) ((k / 40) % 3 == 0 ? 1 : (k / 40) % 3 == 1 ? 2 : 0));
            for (int c = 0; c < 2; ++c)
                buf.copyFrom (c, 0, tones[(size_t) c].data() + pos, 256);
            sw.proc->processBlock (buf, midi);
            for (int c = 0; c < 2; ++c)
                std::copy (buf.getReadPointer (c), buf.getReadPointer (c) + 256, moved[(size_t) c].data() + pos);
        }
        const int lat = steady.proc->getLatencySamples() + 64;
        const double stepStill = maxStep (still, lat), stepMoved = maxStep (moved, lat);

        // The match gain, read from two instances that played the same 1 s, one heard in Sides only.
        double gain[2] {};
        Rig ms, mm;
        for (auto* r : { &ms, &mm })
        {
            r->set (ids::match, 1.0f);
            r->set (ids::reveal, 80.0f);
            r->prepare (fs, 512);
        }
        mm.set (ids::listenCheck, 2.0f);
        ms.run (music, fixedBlocks (512));
        mm.run (music, fixedBlocks (512));
        gain[0] = ms.proc->getMatchGainDb();
        gain[1] = mm.proc->getMatchGainDb();
        check (stepMoved <= stepStill * 1.05 + 1.0e-4 && gain[0] == gain[1] && std::abs (gain[0]) > 0.001,
               "switching fades; Match measures before the check",
               "largest step switching every 0.2 s " + juce::String (stepMoved, 5) + " vs. " + juce::String (stepStill, 5)
                   + " steady; match gain Stereo " + db (gain[0], 3) + ", Sides " + db (gain[1], 3));
    }

    // The parameter: "Stereo" by default, its texts and typed values; older projects open in Stereo (whatever this
    // instance had), a saved check comes back, factory presets leave it alone.
    {
        Rig r;
        auto* cp = r.proc->apvts.getParameter (ids::listenCheck);
        auto raw = [] (Rig& x) { return x.proc->apvts.getRawParameterValue (ids::listenCheck)->load(); };
        auto typed = [cp] (const juce::String& t) { return juce::roundToInt (cp->convertFrom0to1 (cp->getValueForText (t))); };
        const bool texts = cp->getCurrentValueAsText() == "Stereo" && cp->getText (cp->convertTo0to1 (1.0f), 32) == "Mono"
                           && cp->getText (cp->convertTo0to1 (2.0f), 32) == "Sides only"
                           && typed ("mono") == 1 && typed ("Sum") == 1 && typed ("sides") == 2 && typed ("Side only") == 2
                           && typed ("S") == 2 && typed ("stereo") == 0 && typed ("off") == 0 && typed ("2") == 2
                           && cp->getName (32) == "Listen Check" && cp->isAutomatable();

        auto old = r.proc->apvts.copyState();
        old.removeChild (old.getChildWithProperty ("id", ids::listenCheck), nullptr);
        juce::MemoryBlock oldState;
        juce::AudioProcessor::copyXmlToBinary (*old.createXml(), oldState);
        Rig b;
        b.set (ids::listenCheck, 1.0f);
        b.proc->setStateInformation (oldState.getData(), (int) oldState.getSize());
        const bool oldOpen = raw (b) == 0.0f;

        Rig c;
        c.set (ids::listenCheck, 2.0f);
        juce::MemoryBlock saved;
        c.proc->getStateInformation (saved);
        Rig d;
        d.proc->setStateInformation (saved.getData(), (int) saved.getSize());
        const bool kept = raw (d) == 2.0f;
        d.proc->setCurrentProgram (3);
        const bool presetKeeps = raw (d) == 2.0f;
        check (texts && oldOpen && kept && presetKeeps, "Listen check: texts, older projects, saved, presets",
               "default " + cp->getCurrentValueAsText() + ", 1 -> " + cp->getText (cp->convertTo0to1 (1.0f), 32) + ", typed S -> "
                   + juce::String (typed ("S")) + ", older project -> " + juce::String (raw (b), 0) + ", saved -> "
                   + juce::String (raw (d), 0) + ", after a preset " + juce::String (raw (d), 0));
    }

    // The Listen menu: its Check section sets the parameter (one undo step); the pill reads "Mono" alone, "Heat only ·
    // mono" with a solo, and every text fits; the display names it ("MONO", "... , sides only"); no new control.
    {
        Rig r;
        std::unique_ptr<juce::AudioProcessorEditor> ed (r.proc->createEditor());
        auto* editor = dynamic_cast<AnnealEditor*> (ed.get());
        anneal::ListenMenu* menu = nullptr;
        if (editor != nullptr)
            for (auto* c : editor->getContent().getChildren())
                if (auto* l = dynamic_cast<anneal::ListenMenu*> (c))
                    menu = l;
        if (menu == nullptr)
        {
            check (false, "the Listen check in the Listen menu", "no Listen menu");
            return;
        }
        auto& screen = editor->getContent().screen;
        const float width = menu->textArea().getWidth();
        juce::String tooWide;
        float widest = 0.0f;
        for (int c = 0; c < anneal::ListenMenu::numChoices; ++c)
            for (int k = 0; k < anneal::ListenMenu::numChecks; ++k)
            {
                const auto s = anneal::ListenMenu::shown (c, k, width);
                const float w = anneal::ListenMenu::textWidth (s.name + s.check, s.fontSize);
                widest = std::max (widest, w);
                if (w > width || (k > 0 && s.check.isEmpty()))
                    tooWide << s.name << s.check << " (" << juce::String (w, 1) << " px)  ";
            }
        const auto offText = anneal::ListenMenu::shown (0, 0, width);
        const bool offAsBefore = offText.name == "Listen: off" && offText.check.isEmpty() && screen.getListenTag().first.isEmpty();

        menu->choose (102);   // Mono
        const float chosen = r.proc->apvts.getRawParameterValue (ids::listenCheck)->load();
        const auto monoShown = anneal::ListenMenu::shown (menu->getChoice(), menu->getCheck(), width);
        const auto monoTag = screen.getListenTag();
        r.proc->undo();
        const bool undone = r.proc->apvts.getRawParameterValue (ids::listenCheck)->load() == 0.0f;
        r.proc->redo();
        menu->choose (4);     // Heat only
        const auto both = anneal::ListenMenu::shown (menu->getChoice(), menu->getCheck(), width);
        menu->choose (103);   // Sides only
        const auto sidesTag = screen.getListenTag();
        const float tagW = anneal::theme::tabularWidths (anneal::theme::caps (11.0f), sidesTag.first)
                           + anneal::theme::tabularWidths (anneal::theme::font (11.0f), sidesTag.second) + 30.0f;
        menu->choose (101);   // Stereo
        menu->choose (1);     // Listen off
        const bool backOff = r.proc->apvts.getRawParameterValue (ids::listenCheck)->load() == 0.0f
                             && screen.getListenTag().first.isEmpty();
        const auto dotText = juce::String::fromUTF8 ("Heat only \xc2\xb7 mono");
        check (offAsBefore && tooWide.isEmpty() && chosen == 1.0f && monoShown.check == "Mono" && monoTag.first == "MONO" && undone
                   && both.name + both.check == dotText && sidesTag.first == "HEAT ONLY" && sidesTag.second.endsWith ("sides only")
                   && tagW < screen.getEqLane().getWidth() * 0.5f && backOff && menu->getTooltip().contains ("Mono"),
               "the Listen check in the Listen menu",
               "pill text area " + juce::String (width, 1) + " px, widest text " + juce::String (widest, 1) + " px; menu Mono -> "
                   + juce::String (chosen, 0) + " (\"" + monoShown.check + "\", display \"" + monoTag.first + " " + monoTag.second
                   + "\"), undo -> " + (undone ? "Stereo" : "NOT UNDONE") + "; with Heat only \"" + both.name + both.check
                   + "\", Sides: \"" + sidesTag.first + " " + sidesTag.second + "\" (" + juce::String (tagW, 0) + " px)"
                   + (tooWide.isEmpty() ? juce::String() : "; TOO WIDE: " + tooWide));
    }
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // APVTS uses timers / message-thread objects

    if (argc > 1 && juce::String (argv[1]) == "profile")
    {
        testRealFft();
        profile();
        return 0;
    }
    if (argc > 1 && juce::String (argv[1]) == "fixes")   // just the newest fixes' checks
    {
        testFixes537();
        std::printf ("\n%d checks, %d failed\n", checks, failures);
        return failures;
    }
    if (argc > 1 && juce::String (argv[1]) == "new")   // just the newest item's checks
    {
        testListenCheck539();
        std::printf ("\n%d checks, %d failed\n", checks, failures);
        return failures;
    }

    testIdentity();
    testLatency();
    testNulls();
    testHarmonics();
    testAliasing();
    testImd();
    testModel();
    testBlockInvariance();
    testStress();
    testReprepare();
    testState();
    testBypassAndMono();
    testPreEcho();
    testPresets();
    testGuard();
    testDisplayRange();
    testRangeHandleDrag();
    testMidSide();
    testListen();
    testFlavours();
    testEditorLayout();
    testSidechain();
    testFreeze();
    testCeiling();
    testUndo();
    testOldProjects30();
    testFixes40();
    testReference();
    testPaint();
    testReferenceState();
    testEditor40();
    testRealFft();
    testFixes402();
    testPunch50();
    testWidth50();
    testShapeParams50();
    testUserPresets50();
    testZeroLatency51();
    testFastMath511();
    testUpdateRate52();
    testSpread521();
    testWidthPaint522();
    testStereoImage523();
    testPresetImport524();
    testZeroDesign525();
    testZeroLighter526();
    testFixes527();
    testZeroPunch527();
    testFixes531();
    testLoudness531();
    testFixes532();
    testLimits532();
    testTiltPivot533();
    testKeyRange534();
    testFixes535();
    testPeakHold535();
    testDetail536();
    testFixes537();
    testHeatLimit537();
    testPeakHoldInput538();
    testListenCheck539();
    benchmark();

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures;
}
