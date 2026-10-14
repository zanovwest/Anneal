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
    inline constexpr const char* stereoMode = "stereoMode"; // 0.2.4: linked Stereo / Mid-Side
    inline constexpr const char* eqTarget   = "eqTarget";   // Both / Mid / Side (M/S only)
    inline constexpr const char* heatTarget = "heatTarget";
    inline constexpr const char* listen     = "listen";     // 0.2.5: Off / Reveal / Smooth / Heat only (solo)
    inline constexpr const char* flavour    = "flavour";    // 0.2.6: saturation flavour, Clean = as before
    inline constexpr const char* freeze     = "freeze";     // 3.0: hold the auto-EQ curve (off = as before)
    inline constexpr const char* scMode     = "scMode";     // 3.0: sidechain Off / Make room / Unmask
    inline constexpr const char* scAmount   = "scAmount";   // 3.0: sidechain amount, %
    inline constexpr const char* ceiling    = "ceiling";    // 3.0: true-peak ceiling Off / 0.0 / -0.3 / -1.0 dBTP
    inline constexpr const char* refMode    = "refMode";    // 4.0: reference Off / Learn key / Match
    inline constexpr const char* refAmount  = "refAmount";  // 4.0: how far Match steers, %

    // 2 (4.0): optional "Reference" and "Paint" children (see AnnealAudioProcessor). Version 1
    // states have neither and load with the reference empty and no paint.
    inline constexpr int stateVersion = 2;
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
    int parseListen (const juce::String& text);   // "reveal", "smooth", "harmonics"/"heat", "off" or 0-3
    int parseFlavour (const juce::String& text);  // "clean", "tape", "tube", "transformer", "exciter" or 0-4
    int parseReference (const juce::String& text); // "off", "learn"/"key", "match" or 0-2

    // Sidechain modes and true-peak ceilings (choice indices of scMode / ceiling).
    enum SidechainMode { scOff = 0, scMakeRoom = 1, scUnmask = 2 };
    inline constexpr float ceilingDbtp[] = { 0.0f, 0.0f, -0.3f, -1.0f };   // index 0 = Off
}
