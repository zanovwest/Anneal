#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

// Factory presets. Each sets the tone controls; Match, Delta and Bypass are listening aids and stay
// as they are. Presets are also the host's program list (FL Studio's preset menu).
namespace anneal::presets
{
    // The parameters a preset sets, in the order of Preset::values.
    inline constexpr int numValues = 12;
    extern const std::array<const char*, numValues> paramIds;

    struct Preset
    {
        const char* name;
        std::array<float, numValues> values;   // plain values (%, dB/oct, ms, Hz, ...)
    };

    int count() noexcept;
    const Preset& get (int index) noexcept;   // index is clamped

    // Message thread. Sets every value as one gesture per parameter, so hosts record it.
    void apply (juce::AudioProcessorValueTreeState& state, int index);

    // The preset the current settings equal (preferring `preferred` when several do), or -1.
    int findMatching (juce::AudioProcessorValueTreeState& state, int preferred = -1);
}
