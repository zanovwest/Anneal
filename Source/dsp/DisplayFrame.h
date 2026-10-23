#pragma once

#include <cstdint>

namespace anneal
{
    // One STFT frame of analysis data, published from the audio thread to the editor.
    // Every value here is measured by the engine; nothing is synthesised for the display.
    struct DisplayFrame
    {
        static constexpr int numBands = 1024;

        float z0 = 0.0f;              // Cam position of band 0
        float dz = 0.0f;              // Cam spacing between bands
        float rangeLoHz = 20.0f;
        float rangeHiHz = 20000.0f;
        std::uint32_t counter = 0;

        // How hard each primary control is working in this frame (for the knobs' live read-outs).
        float liftDb = 0.0f;          // strongest Reveal lift inside the range, dB (no Tilt)
        float cutDb = 0.0f;           // strongest Smooth cut inside the range, dB (<= 0)
        float heatRelDb = -200.0f;    // harmonic energy written, relative to the whole output, dB
        bool midSide = false;        // gainDb = Mid, sideGainDb = Side when true
        bool keyActive = false;      // 3.0: a sidechain mode is on and the key carries signal
        bool frozen = false;         // 3.0: the auto-EQ curve is held (Freeze)
        float scDb = 0.0f;           // 3.0: strongest sidechain move, dB (< 0 ducking, > 0 lifting)
        int refMode = 0;             // 4.0: Reference Off / Learn key / Match
        bool refValid = false;       // 4.0: a reference is loaded
        float refSeconds = 0.0f;     // 4.0: Learn key, seconds of key learned so far
        float matchDb = 0.0f;        // 4.0: Match, strongest move inside the range, dB
        bool attacks = false;        // 5.0: Guard or Punch is on; transient[] is valid
        bool zeroLatency = false;    // 5.1: Zero latency (Heat, Punch and Listen are off)
        float punchDb = 0.0f;        // 5.0: strongest Punch move inside the range, dB
        bool stereo = false;         // 5.2.3: two channels; sideRelDb[] is valid

        float inDb[numBands]   {};    // input level per ERB, dBFS (signal power within +-0.5 Cam)
        float outDb[numBands]  {};    // output level per ERB, dBFS
        float gainDb[numBands] {};    // live auto-EQ gain incl. tilt and range gate, dB
        float heatDb[numBands] {};    // harmonic energy written per ERB, dBFS
        float sideGainDb[numBands] {};
        float keyDb[numBands] {};    // sidechain key level per ERB, dBFS (valid when keyActive)
        float transient[numBands] {}; // 5.0: 0..1, an attack in progress per ERB (valid when attacks)
        // 5.2.3: the stereo image heard: per ERB, the output's Side level relative to its Mid, dB (the
        // input's Mid and Side through the EQ curves, Width and Mono bass; -200 = no signal). Updated
        // 37.5 times a second, valid when stereo.
        float sideRelDb[numBands] {};
    };
}
