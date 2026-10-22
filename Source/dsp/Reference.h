#pragma once

#include <algorithm>

// 4.0 Reference match and Paint: fixed-size data that the message thread hands to the audio
// thread (and, for a reference learned from the sidechain key, back). Both are stored on grids
// spaced evenly on the ERB-rate scale from 20 Hz to 20 kHz (the display's axis), so a saved
// project means the same thing at every sample rate.
namespace anneal
{
    namespace refgrid
    {
        double zMin() noexcept;   // Cam of 20 Hz
        double zMax() noexcept;   // Cam of 20 kHz
        inline double camAt (int i, int numPoints) noexcept { return zMin() + (zMax() - zMin()) * i / (numPoints - 1); }

        // Linear interpolation of a curve on the grid at Cam position z (held flat past either end).
        float sampleAt (const float* curve, int numPoints, double z) noexcept;
    }

    // A reference's long-term spectrum: the one-ERB level (a power mean over time) in dB,
    // level-normalised (normaliseLevels), at 1024 points.
    struct ReferenceCurve
    {
        static constexpr int numPoints = 1024;
        float db[numPoints] {};
        bool valid = false;
        // Hand-over only, not saved. epoch: the processor's count of references handed over, so a
        // curve learned from the key before a newer reference arrived is ignored. holdLearning: a
        // dropped file ends Learn key until Learn key is chosen again.
        unsigned int epoch = 0;
        bool holdLearning = false;
    };

    // Paint: the share of the auto-EQ change, of Heat and (5.2.2) of Width that applies in each
    // region, 0..1, at 64 points. All 1 (the default) = no paint, bit-identical to Anneal before 4.0.
    struct PaintCurves
    {
        static constexpr int numPoints = 64;
        float eq[numPoints], heat[numPoints], width[numPoints];

        PaintCurves() noexcept { reset(); }
        void reset() noexcept
        {
            std::fill (eq, eq + numPoints, 1.0f);
            std::fill (heat, heat + numPoints, 1.0f);
            std::fill (width, width + numPoints, 1.0f);
        }
        static bool isFull (const float* curve) noexcept
        {
            return std::all_of (curve, curve + numPoints, [] (float v) { return v == 1.0f; });
        }
        bool isDefault() const noexcept { return isFull (eq) && isFull (heat) && isFull (width); }
        bool operator== (const PaintCurves& o) const noexcept
        {
            return std::equal (eq, eq + numPoints, o.eq) && std::equal (heat, heat + numPoints, o.heat)
                && std::equal (width, width + numPoints, o.width);
        }
        bool operator!= (const PaintCurves& o) const noexcept { return ! (*this == o); }
    };

    // Level normalisation shared by every reference and by the main signal's long-term spectrum:
    // the levels minus their mean over 50 Hz - 16 kHz, where each band counts as at least 40 dB under
    // the window's power mean (so an empty region, e.g. above an MP3's cut-off, doesn't drag the mean
    // down). n bands, band i at Cam z0 + i * dz. power: the same levels as power, or nullptr.
    // In place is fine (out == db). Real-time safe.
    void normaliseLevels (const float* db, const float* power, int n, double z0, double dz, float* out) noexcept;
}
