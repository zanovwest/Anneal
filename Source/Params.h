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

    inline constexpr int stateVersion = 1;
}

namespace anneal
{
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Text helpers shared by parameters and the display (exposed for tests).
    juce::String formatHz (float hz);
    float parseHz (const juce::String& text);
}
