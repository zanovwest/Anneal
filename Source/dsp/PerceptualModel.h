#pragma once

#include <vector>

namespace anneal
{
    struct ModelSettings
    {
        float reveal       = 0.0f;     // 0..1, lift bands in a masking shadow
        float smooth       = 0.0f;     // 0..1, pull down dominant bands
        float tiltDbPerOct = 0.0f;     // static tilt around 1 kHz
        float responseMs   = 120.0f;   // gain-curve release (attack = 1/3)
        float rangeLoHz    = 20.0f;
        float rangeHiHz    = 20000.0f;
        float focus        = 0.0f;     // -1 body .. +1 air (heat placement)
        float guard        = 0.0f;     // 0..1 transient guard (0 = off: exactly the old behaviour)
        bool freeze        = false;    // hold the auto-EQ curve where it is (3.0)
        int scMode         = 0;        // sidechain: 0 off, 1 make room (duck where the key needs space), 2 unmask
        float scAmount     = 0.5f;     // 0..1
    };

    // ERB-rate band model. Turns a linked power spectrum into
    //  - a per-bin auto-EQ gain, and
    //  - a per-bin "heat need" weight that tells the harmonic synth where enrichment is wanted.
    // All storage is allocated in prepare(); analyse() is real-time safe.
    class PerceptualModel
    {
    public:
        static constexpr int numBands = 1024;

        void prepare (double sampleRate, int fftSize);
        void reset();

        // binPower: fftSize/2+1 values in amplitude^2 units (a full-scale sine peaks at 1.0).
        // keyPower: the sidechain key's spectrum in the same units, or nullptr (no key).
        void analyse (const float* binPower, const ModelSettings& settings, float framesPerSecond,
                      const float* keyPower = nullptr);

        const float* getBinGain() const noexcept     { return binGain.data(); }   // linear
        // Listen modes: the same per-bin gain without Tilt (Reveal/Smooth only), so that
        // getBinGain() = auto gain x tilt gain. Computed on request after analyse(); real-time safe.
        void computeAutoGain() noexcept;
        const float* getBinAutoGain() const noexcept { return binAutoGain.data(); }   // linear
        const float* getBinNeed() const noexcept     { return binNeed.data(); }   // 0..2
        const float* getBandGainDb() const noexcept  { return gainOut.data(); }
        const float* getBandProminence() const noexcept { return prom.data(); }  // dB, last frame
        // Transient guard: 0..1 per band, how much of an attack is happening there right now
        // (rise of the one-ERB level over its ~150 ms average, held while the attack is inside the
        // analysis frame, then a ~50 ms decay). Always measured.
        const float* getBandTransient() const noexcept { return transient.data(); }
        float getMaxTransient() const noexcept { return transientPeak; }   // inside the range

        // Display helper: level within one ERB around each band centre, dBFS. Real-time safe.
        void bandLevelsDb (const float* binPower, float* outDb);

        // Strongest Reveal lift / Smooth cut in the last frame, inside the range, without Tilt (dB).
        float getMaxLiftDb() const noexcept { return liftPeakDb; }
        float getMaxCutDb() const noexcept  { return cutPeakDb; }   // <= 0
        // Strongest sidechain move this frame inside the range (dB: < 0 ducking, > 0 lifting).
        float getSidechainPeakDb() const noexcept { return sidePeakDb; }

        float getZ0() const noexcept { return (float) z0; }
        float getDz() const noexcept { return (float) dz; }
        double getBandHz (int band) const noexcept { return bandHz[(size_t) band]; }
        int bandForHz (double hz) const noexcept;

    private:
        void binPowerToSlices (const float* binPower, float* slices) const noexcept;
        void boxSum (const float* in, float* out, int halfWidth, bool mean) noexcept;
        void contextOf (const float* placeLevel, float* context) noexcept;

        double sampleRate = 48000.0, binHz = 0.0, z0 = 0.0, dz = 0.0;
        int fftSize = 0, numBins = 0, boxHalf = 12, exclusion = 25, smoothHalf = 6;
        double aUp = 0.0, aDn = 0.0, aUpM = 0.0, aDnM = 0.0;
        bool primed = false;
        float liftPeakDb = 0.0f, cutPeakDb = 0.0f;   // (not maxCutDb/maxBoostDb: those are the limits)
        float sidePeakDb = 0.0f;
        float transientPeak = 0.0f;

        // band -> bin partition (CSR)
        std::vector<int> partStart, partBin;
        std::vector<float> partWeight;

        // bin -> band interpolation
        std::vector<int> binBand;
        std::vector<float> binFrac, binEdge;

        std::vector<double> bandHz, prefix;
        std::vector<float> earW, noiseSlice, noiseBox, flatNormDb;
        std::vector<float> slices, smoothed, place, context, fwd, bwd, prom, target, targetSmoothed,
                           gain, promSlow, gainOut, needOut, tmp;
        std::vector<float> raw, fastPlace, slowPlace, transient;   // transient guard
        std::vector<int> transientAge;                             // frames since the band's last rise
        std::vector<float> binGainDb, binGain, binNeed;
        std::vector<float> autoOut, binAutoDb, binAutoGain;        // listen modes (no Tilt)
        std::vector<float> keySlices, keySmoothed, keyPlace;       // sidechain key (3.0)
        bool keyPrimed = false;

        // Band dB values -> smoothed linear per-bin gain (the band->bin step of analyse()).
        void bandsToBinGain (const float* bandDb, float* binDb, float* linear) noexcept;
    };
}
