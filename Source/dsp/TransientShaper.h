#pragma once

#include <cstdint>

namespace anneal
{
    // 5.2.7 Punch at zero latency. The spectral Punch works on the FFT frames, which zero latency
    // doesn't have; this shapes attacks in the time domain instead, after the zero-latency filters.
    //  - Range: the part of the signal inside Range is split off with first-order edges (6 dB/oct
    //    each), b = LP1 (Range High) of HP1 (Range Low), and only that part takes the gain:
    //    y = x + (g - 1) b. With first-order edges b stays in phase with x enough that a boost never
    //    turns into a cut anywhere (|y / x| always lies between 1 and g), at the price of gentle edges.
    //  - Detector, per component (Stereo: one for both channels, the same gain on both; Mid/Side:
    //    Mid and Side each their own, by the EQ routing): the band's power through a fast envelope
    //    (1 ms attack, 30 ms release) against a slow one (40 ms) of that. A rise of 4 dB starts to
    //    count and 12 dB is a full attack, as in the spectral detector; the weight lets go over
    //    ~12 ms once the rise is over. Gain = Punch * 8 dB * weight, smoothed over 0.3 ms.
    //  - Punch 0 (or a component routed out) leaves the audio exactly as it is; the detector keeps
    //    running, so turning Punch up mid-song starts from the sound's real level.
    // All state is fixed-size; process() is real-time safe and never allocates.
    class TransientShaper
    {
    public:
        struct Settings
        {
            float punch = 0.0f;                 // -1..1
            bool midSide = false;               // independent Mid / Side (stereo only)
            float midAmount = 1.0f, sideAmount = 1.0f;   // EQ routing in Mid/Side (0 or 1)
            float rangeLoHz = 20.0f, rangeHiHz = 20000.0f;
        };

        static constexpr float maxDb = 8.0f;

        void prepare (double sampleRate) noexcept;
        void reset() noexcept;   // forget the level (the next 2 ms prime it) and any attack

        // In place on 1 or 2 channels (left / right).
        void process (float* const* io, int numChannels, int numSamples, const Settings& s) noexcept;

        // The strongest gain applied since the last call (dB, signed: + louder attacks, - softer).
        float takePeakDb() noexcept;

    private:
        struct Detector
        {
            float fast = 0.0f, slow = 0.0f, weight = 0.0f, gainDb = 0.0f;
        };
        void setRange (float loHz, float hiHz) noexcept;
        float gainFor (Detector& d, float power, float amount) noexcept;   // linear gain this sample

        double sampleRate = 48000.0;
        float shownLo = -1.0f, shownHi = -1.0f;   // the Range the edge coefficients are for
        float hpG = 0.0f, lpG = 0.0f;             // one-pole (TPT) coefficients g / (1 + g)
        float hpS[2] {}, lpS[2] {};               // their states per channel
        Detector det[2];                          // [0] Stereo / Mid / mono, [1] Side
        float fastAttack = 0.0f, fastRelease = 0.0f, slowCoef = 0.0f, letGo = 0.0f, gainCoef = 0.0f;
        int primeLeft = 0, primeLength = 96;      // samples during which the slow level just follows
        bool lastMidSide = false;
        float peakDb = 0.0f;
    };
}
