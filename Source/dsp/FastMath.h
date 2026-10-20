#pragma once

namespace anneal::fastmath
{
    // 5.1.1: the perceptual model's dB conversions, batched (SSE2 four at a time on x86, plain loops
    // elsewhere). No tables, no allocation, real-time safe. Accurate to about 1e-6 dB, far inside the
    // 0.001 dB the curve may move (tests, section 38).

    // out[i] = 10 log10 (x[i]), x clamped to [1e-20, 1e30] (NaN counts as 1e-20). In place is fine.
    void powerToDb (const float* x, float* out, int n) noexcept;
    // out[i] = 10^(db[i] / 20), db clamped to +-758 dB (NaN counts as -758). In place is fine.
    void dbToGain (const float* db, float* out, int n) noexcept;

    // One value at a time (also the tails of the batched loops).
    float log2 (float x) noexcept;   // x > 0 and normal
    float exp2 (float x) noexcept;   // x in [-126, 126]

    // The batched conversions without SIMD, as non-x86 builds run them (exposed for the tests).
    void powerToDbScalar (const float* x, float* out, int n) noexcept;
    void dbToGainScalar (const float* db, float* out, int n) noexcept;
}
