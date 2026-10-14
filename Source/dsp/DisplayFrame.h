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

        float inDb[numBands]   {};    // input level per ERB, dBFS (signal power within +-0.5 Cam)
        float outDb[numBands]  {};    // output level per ERB, dBFS
        float gainDb[numBands] {};    // live auto-EQ gain incl. tilt and range gate, dB
        float heatDb[numBands] {};    // harmonic energy written per ERB, dBFS
        float sideGainDb[numBands] {};
    };
}
