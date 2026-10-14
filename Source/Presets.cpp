#include "Presets.h"
#include "Params.h"

namespace anneal::presets
{
const std::array<const char*, numValues> paramIds {
    ids::reveal, ids::smooth, ids::tilt, ids::response, ids::rangeLo, ids::rangeHi,
    ids::heat, ids::color, ids::focus, ids::mix, ids::output, ids::flavour
};

namespace
{
    // reveal %, smooth %, tilt dB/oct, response ms, range low Hz, range high Hz,
    // heat %, color % odd, focus (-100 body .. +100 air), mix %, output dB, flavour (0 = Clean;
    // every factory preset predates flavours and keeps the Clean saturation it was made with)
    const Preset factory[] = {
        // The parameter defaults, so a fresh instance shows "Default".
        { "Default",        { 40, 40,  0.0f, 120,  30, 18000, 30, 35,   0, 100,  0.0f, 0 } },
        // Barely there: a little masking relief and warmth.
        { "Gentle",         { 20, 25,  0.0f, 200,  30, 18000, 10, 30,   0, 100,  0.0f, 0 } },
        // Calms what pokes out, warm even harmonics, slow so the bus doesn't pump.
        { "Mix bus glue",   { 30, 45,  0.0f, 180,  30, 16000, 25, 25,   0, 100,  0.0f, 0 } },
        // Light touch for a finished track: slow, a hint of air, blended in.
        { "Master polish",  { 25, 35,  0.2f, 250,  25, 20000, 15, 30,  10,  85,  0.0f, 0 } },
        // Opens up the top of a vocal: more reveal, harmonics toward the air, lows left alone.
        { "Vocal air",      { 50, 50,  0.4f,  90, 150, 20000, 35, 40,  50, 100,  0.0f, 0 } },
        // Low end only: harmonics of the sub so the 808 reads on small speakers.
        { "808 weight",     { 30, 20, -0.3f, 150,  25,  1500, 55, 45, -60, 100, -1.0f, 0 } },
        // Fast response for hits, odd harmonics for edge.
        { "Drum bus snap",  { 45, 55,  0.2f,  40,  40, 18000, 35, 70,  20, 100,  0.0f, 0 } },
        // Darker, rounder, lots of even harmonics in the body, top rolled away.
        { "Lo-fi warmth",   { 20, 60, -0.8f, 300,  60,  9000, 60, 10, -40, 100, -1.5f, 0 } },
    };
    constexpr int numFactory = (int) (sizeof (factory) / sizeof (factory[0]));

    bool equals (juce::AudioProcessorValueTreeState& state, const Preset& p)
    {
        for (int i = 0; i < numValues; ++i)
        {
            auto* param = state.getParameter (paramIds[(size_t) i]);
            if (param == nullptr || std::abs (param->getValue() - param->convertTo0to1 (p.values[(size_t) i])) > 1.0e-3f)
                return false;
        }
        return true;
    }
}

int count() noexcept { return numFactory; }

const Preset& get (int index) noexcept
{
    return factory[juce::jlimit (0, numFactory - 1, index)];
}

void apply (juce::AudioProcessorValueTreeState& state, int index)
{
    const auto& p = get (index);
    for (int i = 0; i < numValues; ++i)
        if (auto* param = state.getParameter (paramIds[(size_t) i]))
        {
            param->beginChangeGesture();
            param->setValueNotifyingHost (param->convertTo0to1 (p.values[(size_t) i]));
            param->endChangeGesture();
        }
}

int findMatching (juce::AudioProcessorValueTreeState& state, int preferred)
{
    if (juce::isPositiveAndBelow (preferred, numFactory) && equals (state, factory[preferred]))
        return preferred;
    for (int i = 0; i < numFactory; ++i)
        if (equals (state, factory[i]))
            return i;
    return -1;
}
}
