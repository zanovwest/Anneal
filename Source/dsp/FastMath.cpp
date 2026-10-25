#include "FastMath.h"

#include <cmath>
#include <cstdint>
#include <cstring>

#if defined (__SSE2__) || defined (_M_X64) || defined (_M_AMD64) || (defined (_M_IX86_FP) && _M_IX86_FP >= 2)
 #define ANNEAL_FASTMATH_SSE2 1
 #include <emmintrin.h>
#else
 #define ANNEAL_FASTMATH_SSE2 0
#endif

namespace anneal::fastmath
{
namespace
{
    constexpr float dbPerLog2 = 3.0102999566398120f;    // 10 log10 (2)
    constexpr float log2PerDb = 0.16609640474436813f;   // log2 (10) / 20
    constexpr float powerFloor = 1.0e-20f, powerCeiling = 1.0e30f;
    constexpr float exp2Limit = 126.0f;
    constexpr float sqrt2 = 1.41421356f;

    // log2 (m), m in [1/sqrt2, sqrt2): 2/ln2 atanh (t) with t = (m - 1) / (m + 1), |t| <= 0.1716;
    // the odd series to t^9 is exact to 3e-10.
    constexpr float l1 = 2.8853900817779268f, l3 = 0.9617966939259756f, l5 = 0.5770780163555854f,
                    l7 = 0.4121985831111324f, l9 = 0.3205988979753252f;

    // 2^f = e^(f ln2), f in [-0.5, 0.5]: Taylor to f^7, exact to 6e-9.
    constexpr float e1 = 0.6931471805599453f, e2 = 0.2402265069591007f, e3 = 0.0555041086648216f,
                    e4 = 0.0096181291076285f, e5 = 0.0013333558146428f, e6 = 0.0001540353039338f,
                    e7 = 0.0000152525273380f;

    // sin / cos: 2/pi, pi/2 = a + b + c (a and b exact in few bits, so k * a and k * b are exact for
    // the k that occur), and the Taylor coefficients.
    constexpr float twoOverPi = 0.63661977236758134f;
    constexpr float pio2a = 1.5703125f, pio2b = 4.837512969970703125e-4f, pio2c = 7.54978995489188216e-8f;
    constexpr float angleLimit = 1.0e6f;
    constexpr float s3 = -1.6666666666666666e-1f, s5 = 8.3333333333333333e-3f, s7 = -1.9841269841269841e-4f,
                    s9 = 2.7557319223985893e-6f;
    constexpr float c2 = -0.5f, c4 = 4.1666666666666667e-2f, c6 = -1.3888888888888889e-3f,
                    c8 = 2.4801587301587302e-5f, c10 = -2.7557319223985891e-7f;

    inline float bitsToFloat (std::uint32_t b) noexcept { float f; std::memcpy (&f, &b, sizeof f); return f; }
    inline std::uint32_t floatToBits (float f) noexcept { std::uint32_t b; std::memcpy (&b, &f, sizeof b); return b; }
}

float log2 (float x) noexcept
{
    const std::uint32_t bits = floatToBits (x);
    int e = (int) ((bits >> 23) & 0xffu) - 127;
    float m = bitsToFloat ((bits & 0x007fffffu) | 0x3f800000u);   // [1, 2)
    if (m > sqrt2)
    {
        m *= 0.5f;
        ++e;
    }
    const float t = (m - 1.0f) / (m + 1.0f), t2 = t * t;
    return (float) e + t * (l1 + t2 * (l3 + t2 * (l5 + t2 * (l7 + t2 * l9))));
}

float exp2 (float x) noexcept
{
    const float r = std::nearbyint (x);
    const float f = x - r;
    const float p = 1.0f + f * (e1 + f * (e2 + f * (e3 + f * (e4 + f * (e5 + f * (e6 + f * e7))))));
    return p * bitsToFloat ((std::uint32_t) ((int) r + 127) << 23);
}

void powerToDbScalar (const float* x, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        float v = x[i] >= powerFloor ? x[i] : powerFloor;   // NaN -> the floor
        v = v < powerCeiling ? v : powerCeiling;
        out[i] = dbPerLog2 * log2 (v);
    }
}

void dbToGainScalar (const float* db, float* out, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        float y = db[i] * log2PerDb;
        y = y > -exp2Limit ? y : -exp2Limit;   // NaN -> the floor
        y = y < exp2Limit ? y : exp2Limit;
        out[i] = exp2 (y);
    }
}

void sinCosScalar (const float* x, float* s, float* c, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        float v = x[i] > -angleLimit ? x[i] : -angleLimit;   // NaN -> -limit
        v = v < angleLimit ? v : angleLimit;
        const float k = std::nearbyint (v * twoOverPi);
        const float r = ((v - k * pio2a) - k * pio2b) - k * pio2c;
        const float r2 = r * r;
        const float sr = r + r * r2 * (s3 + r2 * (s5 + r2 * (s7 + r2 * s9)));
        const float cr = 1.0f + r2 * (c2 + r2 * (c4 + r2 * (c6 + r2 * (c8 + r2 * c10))));
        const int q = (int) k & 3;
        s[i] = q == 0 ? sr : q == 1 ? cr : q == 2 ? -sr : -cr;
        c[i] = q == 0 ? cr : q == 1 ? -sr : q == 2 ? -cr : sr;
    }
}

#if ANNEAL_FASTMATH_SSE2
void sinCos (const float* x, float* s, float* c, int n) noexcept
{
    const __m128 lo = _mm_set1_ps (-angleLimit), hi = _mm_set1_ps (angleLimit), k2pi = _mm_set1_ps (twoOverPi);
    const __m128 pa = _mm_set1_ps (pio2a), pb = _mm_set1_ps (pio2b), pc = _mm_set1_ps (pio2c), one = _mm_set1_ps (1.0f);
    const __m128i i1 = _mm_set1_epi32 (1), i2 = _mm_set1_epi32 (2), i3 = _mm_set1_epi32 (3);
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        const __m128 v = _mm_min_ps (_mm_max_ps (_mm_loadu_ps (x + i), lo), hi);   // max: NaN -> lo
        const __m128i ki = _mm_cvtps_epi32 (_mm_mul_ps (v, k2pi));                 // to nearest, like std::nearbyint
        const __m128 k = _mm_cvtepi32_ps (ki);
        __m128 r = _mm_sub_ps (v, _mm_mul_ps (k, pa));
        r = _mm_sub_ps (r, _mm_mul_ps (k, pb));
        r = _mm_sub_ps (r, _mm_mul_ps (k, pc));
        const __m128 r2 = _mm_mul_ps (r, r);
        __m128 ps = _mm_add_ps (_mm_set1_ps (s7), _mm_mul_ps (r2, _mm_set1_ps (s9)));
        ps = _mm_add_ps (_mm_set1_ps (s5), _mm_mul_ps (r2, ps));
        ps = _mm_add_ps (_mm_set1_ps (s3), _mm_mul_ps (r2, ps));
        const __m128 sr = _mm_add_ps (r, _mm_mul_ps (_mm_mul_ps (r, r2), ps));
        __m128 pcos = _mm_add_ps (_mm_set1_ps (c8), _mm_mul_ps (r2, _mm_set1_ps (c10)));
        pcos = _mm_add_ps (_mm_set1_ps (c6), _mm_mul_ps (r2, pcos));
        pcos = _mm_add_ps (_mm_set1_ps (c4), _mm_mul_ps (r2, pcos));
        pcos = _mm_add_ps (_mm_set1_ps (c2), _mm_mul_ps (r2, pcos));
        const __m128 cr = _mm_add_ps (one, _mm_mul_ps (r2, pcos));
        // Quadrant q = k & 3: odd q swaps sin and cos; sin is negative for q = 2, 3, cos for q = 1, 2.
        const __m128i q = _mm_and_si128 (ki, i3);
        const __m128 swap = _mm_castsi128_ps (_mm_cmpeq_epi32 (_mm_and_si128 (q, i1), i1));
        const __m128 sv = _mm_or_ps (_mm_and_ps (swap, cr), _mm_andnot_ps (swap, sr));
        const __m128 cv = _mm_or_ps (_mm_and_ps (swap, sr), _mm_andnot_ps (swap, cr));
        const __m128 sinSign = _mm_castsi128_ps (_mm_slli_epi32 (_mm_and_si128 (q, i2), 30));
        const __m128 cosSign = _mm_castsi128_ps (_mm_slli_epi32 (_mm_and_si128 (_mm_add_epi32 (q, i1), i2), 30));
        _mm_storeu_ps (s + i, _mm_xor_ps (sv, sinSign));
        _mm_storeu_ps (c + i, _mm_xor_ps (cv, cosSign));
    }
    sinCosScalar (x + i, s + i, c + i, n - i);
}

void powerToDb (const float* x, float* out, int n) noexcept
{
    const __m128 lo = _mm_set1_ps (powerFloor), hi = _mm_set1_ps (powerCeiling);
    const __m128 one = _mm_set1_ps (1.0f), half = _mm_set1_ps (0.5f), root2 = _mm_set1_ps (sqrt2);
    const __m128i mantissa = _mm_set1_epi32 (0x007fffff), oneBits = _mm_set1_epi32 (0x3f800000), bias = _mm_set1_epi32 (127);
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        const __m128 v = _mm_min_ps (_mm_max_ps (_mm_loadu_ps (x + i), lo), hi);   // max: NaN -> lo
        const __m128i bits = _mm_castps_si128 (v);
        __m128 e = _mm_cvtepi32_ps (_mm_sub_epi32 (_mm_srli_epi32 (bits, 23), bias));
        __m128 m = _mm_castsi128_ps (_mm_or_si128 (_mm_and_si128 (bits, mantissa), oneBits));
        const __m128 big = _mm_cmpgt_ps (m, root2);
        m = _mm_sub_ps (m, _mm_and_ps (big, _mm_mul_ps (m, half)));   // m / 2 where m > sqrt2 (exact)
        e = _mm_add_ps (e, _mm_and_ps (big, one));
        const __m128 t = _mm_div_ps (_mm_sub_ps (m, one), _mm_add_ps (m, one));
        const __m128 t2 = _mm_mul_ps (t, t);
        __m128 p = _mm_add_ps (_mm_set1_ps (l7), _mm_mul_ps (t2, _mm_set1_ps (l9)));
        p = _mm_add_ps (_mm_set1_ps (l5), _mm_mul_ps (t2, p));
        p = _mm_add_ps (_mm_set1_ps (l3), _mm_mul_ps (t2, p));
        p = _mm_add_ps (_mm_set1_ps (l1), _mm_mul_ps (t2, p));
        _mm_storeu_ps (out + i, _mm_mul_ps (_mm_set1_ps (dbPerLog2), _mm_add_ps (e, _mm_mul_ps (t, p))));
    }
    powerToDbScalar (x + i, out + i, n - i);
}

void dbToGain (const float* db, float* out, int n) noexcept
{
    const __m128 k = _mm_set1_ps (log2PerDb), lo = _mm_set1_ps (-exp2Limit), hi = _mm_set1_ps (exp2Limit);
    const __m128 one = _mm_set1_ps (1.0f);
    const __m128i bias = _mm_set1_epi32 (127);
    int i = 0;
    for (; i + 4 <= n; i += 4)
    {
        const __m128 y = _mm_min_ps (_mm_max_ps (_mm_mul_ps (_mm_loadu_ps (db + i), k), lo), hi);   // max: NaN -> lo
        const __m128i r = _mm_cvtps_epi32 (y);   // to nearest, like std::nearbyint
        const __m128 f = _mm_sub_ps (y, _mm_cvtepi32_ps (r));
        __m128 p = _mm_add_ps (_mm_set1_ps (e6), _mm_mul_ps (f, _mm_set1_ps (e7)));
        p = _mm_add_ps (_mm_set1_ps (e5), _mm_mul_ps (f, p));
        p = _mm_add_ps (_mm_set1_ps (e4), _mm_mul_ps (f, p));
        p = _mm_add_ps (_mm_set1_ps (e3), _mm_mul_ps (f, p));
        p = _mm_add_ps (_mm_set1_ps (e2), _mm_mul_ps (f, p));
        p = _mm_add_ps (_mm_set1_ps (e1), _mm_mul_ps (f, p));
        p = _mm_add_ps (one, _mm_mul_ps (f, p));
        const __m128 scale = _mm_castsi128_ps (_mm_slli_epi32 (_mm_add_epi32 (r, bias), 23));
        _mm_storeu_ps (out + i, _mm_mul_ps (p, scale));
    }
    dbToGainScalar (db + i, out + i, n - i);
}
#else
void powerToDb (const float* x, float* out, int n) noexcept { powerToDbScalar (x, out, n); }
void dbToGain (const float* db, float* out, int n) noexcept { dbToGainScalar (db, out, n); }
void sinCos (const float* x, float* s, float* c, int n) noexcept { sinCosScalar (x, s, c, n); }
#endif
}
