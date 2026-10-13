#pragma once

namespace anneal
{
    class Biquad
    {
    public:
        void setHighShelf (double sampleRate, double f0, double gainDb, double q) noexcept;
        void setHighPass (double sampleRate, double f0, double q) noexcept;
        void reset() noexcept { z1 = z2 = 0.0; }

        double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

    private:
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    };

    // Slow loudness match: compares K-weighted (BS.1770 pre-filter) mean square of the
    // latency-aligned dry signal with the unmatched wet signal over ~3 s and returns the gain
    // that brings the wet back to the dry loudness (limited to +-12 dB, held in silence).
    class LoudnessMatch
    {
    public:
        void prepare (double sampleRate);
        void reset();

        // Returns the target gain for the wet signal after this block.
        float process (const float* const* dry, const float* const* wet, int numChannels, int numSamples) noexcept;
        float getGain() const noexcept { return gain; }

    private:
        struct KFilter
        {
            Biquad shelf, hp;
            double process (double x) noexcept { return hp.process (shelf.process (x)); }
            void reset() noexcept { shelf.reset(); hp.reset(); }
        };

        static constexpr int maxChannels = 2;
        KFilter kDry[maxChannels], kWet[maxChannels];
        double msDry = 0.0, msWet = 0.0, alpha = 0.0;
        float gain = 1.0f;
    };
}
