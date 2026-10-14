#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Host-facing parameter IDs. These are permanent: renaming one breaks saved sessions.
namespace anneal::ids
{
    inline constexpr const char* reveal   = "reveal";
    inline constexpr const char* smooth   = "smooth";
    inline constexpr const char* tilt     = "tilt";
    inline constexpr const char* response = "response";
    inline constexpr const char* rangeLo  = "rangeLo";
    inline constexpr const char* rangeHi  = "rangeHi";
    inline constexpr const char* heat     = "heat";
    inline constexpr const char* color    = "color";
    inline constexpr const char* focus    = "focus";
    inline constexpr const char* mix      = "mix";
    inline constexpr const char* output   = "output";
    inline constexpr const char* match    = "match";
    inline constexpr const char* delta    = "delta";
    inline constexpr const char* bypass   = "bypass";
    inline constexpr const char* latency  = "latency";   // 0.2.0: Low / Normal / Precise (choice)
    inline constexpr const char* guard    = "guard";     // 0.2.2: transient guard, 0 = off (as before)

    inline constexpr int stateVersion = 1;
}

namespace anneal
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // The range the engine works on. Automation can put Range Low above Range High; the engine
    // then keeps High at least 1.5x Low, and the display must show that same range.
    inline float effectiveRangeHi (float rangeLoHz, float rangeHiHz) noexcept
    {
        return std::max (rangeHiHz, rangeLoHz * 1.5f);
    }

    // Text helpers shared by parameters and the display (exposed for tests).
    juce::String formatHz (float hz);
    float parseHz (const juce::String& text);
}
