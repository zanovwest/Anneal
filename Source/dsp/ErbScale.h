#pragma once

#include <algorithm>
#include <cmath>

// Auditory frequency scales and ear curves used by the perceptual model.
namespace anneal::erb
{
    // ERB-rate (Cam) scale, Glasberg & Moore (1990).
    inline double hzToCam (double hz)  { return 21.4 * std::log10 (1.0 + 0.00437 * std::max (0.0, hz)); }
    inline double camToHz (double cam) { return (std::pow (10.0, cam / 21.4) - 1.0) / 0.00437; }

    // Terhardt (1979) threshold in quiet, dB SPL. Capped at 40 dB SPL above 4 kHz so the
    // top octave stays inside the model's audible range (a deliberate choice for mixing use).
    inline double thresholdInQuietDb (double hz)
    {
        const double f = std::max (hz, 20.0) / 1000.0;
        const double t = 3.64 * std::pow (f, -0.8)
                       - 6.5 * std::exp (-0.6 * (f - 3.3) * (f - 3.3))
                       + 1.0e-3 * std::pow (f, 4.0);
        return f > 4.0 ? std::min (t, 40.0) : t;
    }

    // Outer/middle-ear transfer (PEAQ, ITU-R BS.1387 shape), dB. High-frequency roll-off
    // term capped at -15 dB for the same reason as above.
    inline double outerEarDb (double hz)
    {
        const double f = std::max (hz, 20.0) / 1000.0;
        const double hf = std::min (1.0e-3 * std::pow (f, 3.6), 15.0);
        return -0.6 * 3.64 * std::pow (f, -0.8)
             + 6.5 * std::exp (-0.6 * (f - 3.3) * (f - 3.3))
             - hf;
    }

    // Calibration: a full-scale sine is taken to play at this level.
    constexpr double fullScaleSineSpl = 92.0;
}
