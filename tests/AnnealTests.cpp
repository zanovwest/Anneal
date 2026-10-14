// Headless acceptance tests and benchmark for Anneal.
// Exit code = number of failed checks.

#include "PluginProcessor.h"
#include "Params.h"
#include "Presets.h"
#include "dsp/SpectralEngine.h"
#include "dsp/ErbScale.h"
#include "ui/PerceptionScreen.h"
#include "TestSignals.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <chrono>
#include <cstdio>
#include <functional>
#include <random>

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
        ok = ok && lat == anneal::SpectralEngine::fftSizeFor (48000.0, mode) && pPeak == 1000 + lat;
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
        check (b.proc->getLatencyMode() == anneal::SpectralEngine::precise && b.proc->getLatencySamples() == 4096,
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
                          ids::color, ids::focus, ids::mix, ids::output, ids::match, ids::delta, ids::bypass, ids::guard };

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
        const int expected = anneal::SpectralEngine::fftSizeFor (fs, r.proc->getLatencyMode());
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
        { ids::guard, 55.0f } };
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
}

void benchmark()
{
    section ("18. Benchmark (Release build, this machine)");
    // The single slowest block mostly measures Windows pre-empting this test, not Anneal: the
    // percentiles (and running at high priority, as DAW audio threads do) show the real cost.
    juce::Process::setPriority (juce::Process::HighPriority);
    struct Case { double fs; int block; int mode; };
    const char* modeNames[] = { "Low", "Normal", "Precise" };
    for (auto [fs, block, mode] : { Case { 48000.0, 64, 0 }, Case { 48000.0, 128, 0 }, Case { 48000.0, 512, 0 },
                                    Case { 48000.0, 512, 1 }, Case { 48000.0, 64, 2 }, Case { 48000.0, 512, 2 },
                                    Case { 96000.0, 512, 0 } })
    {
        Rig r;   // default settings: Reveal 40, Smooth 40, Heat 30
        r.set (ids::latency, (float) mode);
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
        info (juce::String (modeNames[mode]) + " " + juce::String (fs / 1000.0, 0) + " kHz / " + juce::String (block) + ": CPU " + juce::String (100.0 * total / audio, 2)
              + " % of one core; deadline " + juce::String (deadline * 1000.0, 3) + " ms; p99 " + ms (pct (0.99))
              + ", p99.9 " + ms (pct (0.999)) + ", max " + ms (times.back()) + " at block " + juce::String (slowestBlock));
        juce::String f8;
        for (double t : first8) f8 << juce::String (t * 1000.0, 3) << " ";
        info ("first blocks (ms): " + f8);
    }
    juce::Process::setPriority (juce::Process::NormalPriority);
}
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;   // APVTS uses timers / message-thread objects

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
    benchmark();

    std::printf ("\n%d checks, %d failed\n", checks, failures);
    return failures;
}
