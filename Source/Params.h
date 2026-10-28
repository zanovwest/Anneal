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
    inline constexpr const char* punch      = "punch";      // 5.0: spectral transient shaper, -100..+100 %, 0 = off
    inline constexpr const char* width      = "width";      // 5.0: stereo width of the processed sound, %, 100 = as is
    inline constexpr const char* monoBass   = "monoBass";   // 5.0: mono below this frequency, Hz, under 20 = off
    inline constexpr const char* zeroLatency = "zeroLatency"; // 5.1: no latency (Heat and Listen off), off = as before
    inline constexpr const char* spread      = "spread";      // 5.2.1: frame work spread over the hop (+1 hop latency), off = as before
    inline constexpr const char* zeroPunch   = "zeroPunch";   // 5.2.7: Punch works at zero latency (time domain); older projects: off
    inline constexpr const char* maxCut      = "maxCut";      // 5.3.2: how far the auto EQ may cut, dB (24 = no limit, as before)
    inline constexpr const char* maxBoost    = "maxBoost";    // 5.3.2: how far the auto EQ may lift, dB (24 = no limit, as before)
    inline constexpr const char* tiltPivot   = "tiltPivot";   // 5.3.3: where Tilt turns, Hz (1 kHz = as before)
    inline constexpr const char* scKeyLo     = "scKeyLo";     // 5.3.4: the sidechain key counts from here, Hz (20 = as before)
    inline constexpr const char* scKeyHi     = "scKeyHi";     // 5.3.4: ... up to here, Hz (20 kHz = as before)
    inline constexpr const char* detail      = "detail";      // 5.3.6: how narrow the auto EQ's moves are, -100 broad .. +100 fine (0 = as before)

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
    // 5.3.2 Max cut / Max boost: "No limit" at the top of the range, else "−6.0 dB" (cut) / "+3.0 dB"
    // (boost); typed text takes the number's size ("-6", "6 dB") or "none" / "no limit" / "off".
    juce::String formatLimit (float db, bool cut);
    float parseLimit (const juce::String& text);
    // 5.3.6 Detail: "Normal" at 0, else "Broad 40" / "Fine 40"; typed text takes "broad" / "fine" / "normal"
    // with or without a number, or a plain number (negative = broad).
    juce::String formatDetail (float v);
    float parseDetail (const juce::String& text);

    // Sidechain modes and true-peak ceilings (choice indices of scMode / ceiling).
    enum SidechainMode { scOff = 0, scMakeRoom = 1, scUnmask = 2 };
    inline constexpr float ceilingDbtp[] = { 0.0f, 0.0f, -0.3f, -1.0f };   // index 0 = Off

    // 5.0 Mono bass: values under this (the knob's bottom end) mean Off.
    inline constexpr float monoBassMinHz = 20.0f;

    // 5.3.3 Tilt pivot: the octaves from 1 kHz the engine turns Tilt around. Within half a hertz of
    // 1 kHz (the default) it is exactly 0, so the curve is bit-identical to before.
    inline float tiltPivotOctaves (float hz) noexcept
    {
        return std::abs (hz - 1000.0f) < 0.5f ? 0.0f : std::log2 (std::max (1.0f, hz) / 1000.0f);
    }
}
