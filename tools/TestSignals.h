#pragma once

// Deterministic test material shared by AnnealTests and AnnealSnapshot.

#include <cmath>
#include <random>
#include <vector>

namespace testsig
{
    constexpr double pi = 3.14159265358979323846;

    using Channels = std::vector<std::vector<float>>;

    // Paul Kellet's refined pink-noise filter on white Gaussian noise (unit-ish level).
    class Pink
    {
    public:
        explicit Pink (unsigned seed) : rng (seed) {}
        float next()
        {
            const float w = dist (rng) * 0.25f;
            b0 = 0.99886f * b0 + w * 0.0555179f;
            b1 = 0.99332f * b1 + w * 0.0750759f;
            b2 = 0.96900f * b2 + w * 0.1538520f;
            b3 = 0.86650f * b3 + w * 0.3104856f;
            b4 = 0.55000f * b4 + w * 0.5329522f;
            b5 = -0.7616f * b5 - w * 0.0168980f;
            const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362f;
            b6 = w * 0.115926f;
            return out;
        }

    private:
        std::mt19937 rng;
        std::normal_distribution<float> dist { 0.0f, 1.0f };
        float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
    };

    inline Channels silence (int channels, int n) { return Channels ((size_t) channels, std::vector<float> ((size_t) n, 0.0f)); }

    inline Channels sines (double fs, int n, std::initializer_list<std::pair<double, double>> freqAmp, int channels = 2)
    {
        auto out = silence (channels, n);
        for (int i = 0; i < n; ++i)
        {
            double v = 0.0;
            for (auto [f, a] : freqAmp)
                v += a * std::sin (2.0 * pi * f * i / fs);
            for (auto& ch : out)
                ch[(size_t) i] = (float) v;
        }
        return out;
    }

    inline Channels whiteNoise (int channels, int n, float amp, unsigned seed)
    {
        std::mt19937 rng (seed);
        std::uniform_real_distribution<float> d (-amp, amp);
        auto out = silence (channels, n);
        for (auto& ch : out)
            for (auto& s : ch)
                s = d (rng);
        return out;
    }

    // A short, mix-like stereo excerpt: plucked saw bass, triad pad, kick, hats,
    // a sustained 3.15 kHz ring (a typical "harsh" resonance) and a pink-noise bed.
    inline Channels musicLike (double fs, double seconds, unsigned seed = 7)
    {
        const int n = (int) (fs * seconds);
        auto out = silence (2, n);
        Pink pinkL (seed), pinkR (seed + 1);
        std::mt19937 rng (seed + 2);
        std::uniform_real_distribution<float> white (-1.0f, 1.0f);
        float hatPrev = 0.0f;
        double kickPhase = 0.0;

        const double padHz[] = { 220.0, 277.18, 329.63 };
        for (int i = 0; i < n; ++i)
        {
            const double t = i / fs;
            const double beat = std::fmod (t, 0.5);       // 120 bpm
            const double eighth = std::fmod (t, 0.25);

            double bass = 0.0;
            const double bassEnv = std::exp (-beat * 3.0);
            for (int h = 1; h <= 12; ++h)
                bass += std::sin (2.0 * pi * 55.0 * h * t) / h;
            bass *= 0.11 * bassEnv;

            double padL = 0.0, padR = 0.0;
            for (double f : padHz)
                for (int h = 1; h <= 8; ++h)
                {
                    padL += std::sin (2.0 * pi * f * h * t) / (h * h);
                    padR += std::sin (2.0 * pi * f * 1.003 * h * t + 0.7) / (h * h);
                }
            padL *= 0.035;
            padR *= 0.035;

            const double kf = 45.0 + 110.0 * std::exp (-beat * 30.0);
            kickPhase += 2.0 * pi * kf / fs;
            const double kick = (beat < 0.35 ? 0.35 * std::sin (kickPhase) * std::exp (-beat * 9.0) : 0.0);

            const float w = white (rng);
            const float hat = (w - hatPrev) * 0.5f;      // first difference = crude high-pass
            hatPrev = w;
            const double hats = hat * 0.09 * std::exp (-eighth * 60.0);

            const double ring = 0.025 * std::sin (2.0 * pi * 3150.0 * t);
            const double bedL = 0.004 * pinkL.next(), bedR = 0.004 * pinkR.next();

            out[0][(size_t) i] = (float) (bass + padL + kick + hats * 0.8 + ring + bedL);
            out[1][(size_t) i] = (float) (bass + padR + kick + hats * 1.0 + ring + bedR);
        }
        return out;
    }

    // Amplitude of the component at `freq` (Hann-windowed DFT bin, amplitude units).
    inline double toneAmplitude (const float* x, int n, double freq, double fs)
    {
        double re = 0.0, im = 0.0, wsum = 0.0;
        for (int i = 0; i < n; ++i)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * pi * i / (n - 1));
            const double ph = 2.0 * pi * freq * i / fs;
            re += w * x[i] * std::cos (ph);
            im -= w * x[i] * std::sin (ph);
            wsum += w;
        }
        return 2.0 * std::sqrt (re * re + im * im) / wsum;
    }

    inline double toDb (double a) { return 20.0 * std::log10 (std::max (a, 1.0e-15)); }
}
