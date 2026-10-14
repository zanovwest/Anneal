#include "Reference.h"
#include "ErbScale.h"

#include <cmath>

namespace anneal
{
namespace refgrid
{
    double zMin() noexcept { static const double z = erb::hzToCam (20.0);    return z; }
    double zMax() noexcept { static const double z = erb::hzToCam (20000.0); return z; }

    float sampleAt (const float* curve, int numPoints, double z) noexcept
    {
        const double u = std::clamp ((z - zMin()) / (zMax() - zMin()) * (numPoints - 1), 0.0, (double) (numPoints - 1));
        const int i = std::min ((int) u, numPoints - 2);
        const float f = (float) (u - i);
        return curve[i] + f * (curve[i + 1] - curve[i]);
    }
}

void normaliseLevels (const float* db, const float* power, int n, double z0, double dz, float* out) noexcept
{
    static const double zLo = erb::hzToCam (50.0), zHi = erb::hzToCam (16000.0);
    const int lo = std::clamp ((int) std::ceil ((zLo - z0) / dz), 0, n - 1);
    const int hi = std::clamp ((int) std::floor ((zHi - z0) / dz), lo, n - 1);
    const int count = hi - lo + 1;

    double sumPower = 0.0;
    for (int i = lo; i <= hi; ++i)
        sumPower += power != nullptr ? (double) power[i] : std::pow (10.0, db[i] / 10.0);
    const double floorDb = 10.0 * std::log10 (std::max (sumPower / count, 1.0e-30)) - 40.0;

    double sum = 0.0;
    for (int i = lo; i <= hi; ++i)
        sum += std::max ((double) db[i], floorDb);
    const float mean = (float) (sum / count);
    for (int i = 0; i < n; ++i)
        out[i] = db[i] - mean;
}
}
