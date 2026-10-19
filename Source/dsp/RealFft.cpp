#include "RealFft.h"

#include <cassert>
#include <cmath>

namespace anneal
{
namespace
{
    constexpr double twoPi = 6.283185307179586476925286766559;

    // One radix-4 decimation-in-frequency Stockham stage on split complex data: sub-transforms of
    // `len` points, `s` of them interleaved (stride s). Output in natural order after the last stage.
    void radix4 (const float* __restrict xr, const float* __restrict xi,
                 float* __restrict yr, float* __restrict yi,
                 int len, int s, const float* __restrict tw) noexcept
    {
        const int q = len / 4;
        const float* __restrict w1r = tw;
        const float* __restrict w1i = tw + q;
        const float* __restrict w2r = tw + 2 * q;
        const float* __restrict w2i = tw + 3 * q;
        const float* __restrict w3r = tw + 4 * q;
        const float* __restrict w3i = tw + 5 * q;

        if (s == 1)
        {
            // First stage: one long sub-transform, so the loop over p is the one that vectorises.
            for (int p = 0; p < q; ++p)
            {
                const float a0 = xr[p],         a1 = xi[p];
                const float b0 = xr[p + q],     b1 = xi[p + q];
                const float c0 = xr[p + 2 * q], c1 = xi[p + 2 * q];
                const float d0 = xr[p + 3 * q], d1 = xi[p + 3 * q];
                const float apc0 = a0 + c0, apc1 = a1 + c1, amc0 = a0 - c0, amc1 = a1 - c1;
                const float bpd0 = b0 + d0, bpd1 = b1 + d1, bmd0 = b0 - d0, bmd1 = b1 - d1;
                const float t10 = amc0 + bmd1, t11 = amc1 - bmd0;   // (a - c) - j (b - d)
                const float t20 = apc0 - bpd0, t21 = apc1 - bpd1;   // (a + c) - (b + d)
                const float t30 = amc0 - bmd1, t31 = amc1 + bmd0;   // (a - c) + j (b - d)
                yr[4 * p]     = apc0 + bpd0;
                yi[4 * p]     = apc1 + bpd1;
                yr[4 * p + 1] = w1r[p] * t10 - w1i[p] * t11;
                yi[4 * p + 1] = w1r[p] * t11 + w1i[p] * t10;
                yr[4 * p + 2] = w2r[p] * t20 - w2i[p] * t21;
                yi[4 * p + 2] = w2r[p] * t21 + w2i[p] * t20;
                yr[4 * p + 3] = w3r[p] * t30 - w3i[p] * t31;
                yi[4 * p + 3] = w3r[p] * t31 + w3i[p] * t30;
            }
            return;
        }

        for (int p = 0; p < q; ++p)
        {
            const float c1r = w1r[p], c1i = w1i[p], c2r = w2r[p], c2i = w2i[p], c3r = w3r[p], c3i = w3i[p];
            const float* __restrict ar = xr + s * p;
            const float* __restrict ai = xi + s * p;
            const float* __restrict brr = ar + s * q;
            const float* __restrict bii = ai + s * q;
            const float* __restrict crr = ar + 2 * s * q;
            const float* __restrict cii = ai + 2 * s * q;
            const float* __restrict drr = ar + 3 * s * q;
            const float* __restrict dii = ai + 3 * s * q;
            float* __restrict y0r = yr + s * 4 * p;
            float* __restrict y0i = yi + s * 4 * p;
            float* __restrict y1r = y0r + s;
            float* __restrict y1i = y0i + s;
            float* __restrict y2r = y0r + 2 * s;
            float* __restrict y2i = y0i + 2 * s;
            float* __restrict y3r = y0r + 3 * s;
            float* __restrict y3i = y0i + 3 * s;
            for (int k = 0; k < s; ++k)
            {
                const float apc0 = ar[k] + crr[k], apc1 = ai[k] + cii[k], amc0 = ar[k] - crr[k], amc1 = ai[k] - cii[k];
                const float bpd0 = brr[k] + drr[k], bpd1 = bii[k] + dii[k], bmd0 = brr[k] - drr[k], bmd1 = bii[k] - dii[k];
                const float t10 = amc0 + bmd1, t11 = amc1 - bmd0;
                const float t20 = apc0 - bpd0, t21 = apc1 - bpd1;
                const float t30 = amc0 - bmd1, t31 = amc1 + bmd0;
                y0r[k] = apc0 + bpd0;
                y0i[k] = apc1 + bpd1;
                y1r[k] = c1r * t10 - c1i * t11;
                y1i[k] = c1r * t11 + c1i * t10;
                y2r[k] = c2r * t20 - c2i * t21;
                y2i[k] = c2r * t21 + c2i * t20;
                y3r[k] = c3r * t30 - c3i * t31;
                y3i[k] = c3r * t31 + c3i * t30;
            }
        }
    }

    // The last stage when log2 of the size is odd: two-point sub-transforms, stride s (no twiddles).
    void radix2 (const float* __restrict xr, const float* __restrict xi,
                 float* __restrict yr, float* __restrict yi, int s) noexcept
    {
        for (int k = 0; k < s; ++k)
        {
            const float a0 = xr[k], a1 = xi[k], b0 = xr[k + s], b1 = xi[k + s];
            yr[k] = a0 + b0;
            yi[k] = a1 + b1;
            yr[k + s] = a0 - b0;
            yi[k + s] = a1 - b1;
        }
    }
}

void RealFft::prepare (int size)
{
    assert (size >= 8 && (size & (size - 1)) == 0);
    n = size;
    m = size / 2;

    stages.clear();
    twiddles.clear();
    int len = m, stride = 1;
    while (len >= 4)
    {
        const int q = len / 4;
        stages.push_back ({ len, stride, twiddles.size() });
        for (int h = 1; h <= 3; ++h)
        {
            const size_t re = twiddles.size();
            twiddles.resize (re + 2 * (size_t) q);
            for (int p = 0; p < q; ++p)
            {
                const double phase = -twoPi * h * p / len;   // w = e^(-2 pi i / len)
                twiddles[re + (size_t) p]     = (float) std::cos (phase);
                twiddles[re + (size_t) (q + p)] = (float) std::sin (phase);
            }
        }
        // Stored as w1r, w1i, w2r, w2i, w3r, w3i (q each), as radix4() reads them.
        len /= 4;
        stride *= 4;
    }
    if (len == 2)
        stages.push_back ({ 2, stride, 0 });

    splitCos.resize ((size_t) m);
    splitSin.resize ((size_t) m);
    for (int k = 0; k < m; ++k)
    {
        splitCos[(size_t) k] = (float) std::cos (twoPi * k / n);
        splitSin[(size_t) k] = (float) std::sin (twoPi * k / n);
    }
    for (auto* v : { &ar, &ai, &br, &bi })
        v->assign ((size_t) m, 0.0f);
}

bool RealFft::transform (float* re, float* im, float* re2, float* im2) noexcept
{
    bool inSecond = false;
    for (const auto& st : stages)
    {
        const float* xr = inSecond ? re2 : re;
        const float* xi = inSecond ? im2 : im;
        float* yr = inSecond ? re : re2;
        float* yi = inSecond ? im : im2;
        if (st.length == 2)
            radix2 (xr, xi, yr, yi, st.stride);
        else
            radix4 (xr, xi, yr, yi, st.length, st.stride, twiddles.data() + st.twiddle);
        inSecond = ! inSecond;
    }
    return inSecond;
}

void RealFft::forward (float* data) noexcept
{
    // z[j] = x[2j] + i x[2j+1]; Z = FFT_m (z).
    float* __restrict zr0 = ar.data();
    float* __restrict zi0 = ai.data();
    for (int j = 0; j < m; ++j)
    {
        zr0[j] = data[2 * j];
        zi0[j] = data[2 * j + 1];
    }
    const bool second = transform (ar.data(), ai.data(), br.data(), bi.data());
    const float* zr = second ? br.data() : ar.data();
    const float* zi = second ? bi.data() : ai.data();

    // X[k] = E[k] + e^(-2 pi i k / N) O[k], with E and O the transforms of the even and odd samples:
    // E = (Z[k] + conj Z[m-k]) / 2, O = (Z[k] - conj Z[m-k]) / 2i.
    const float r0 = zr[0], i0 = zi[0];
    for (int k = 1; k < m; ++k)
    {
        const float xr = zr[k], xi = zi[k], yr = zr[m - k], yi = zi[m - k];
        const float er = 0.5f * (xr + yr), ei = 0.5f * (xi - yi);
        const float orr = 0.5f * (xi + yi), oi = -0.5f * (xr - yr);
        const float c = splitCos[(size_t) k], s = splitSin[(size_t) k];
        data[2 * k]     = er + c * orr + s * oi;
        data[2 * k + 1] = ei + c * oi - s * orr;
    }
    data[0] = r0 + i0;
    data[1] = 0.0f;
    data[n] = r0 - i0;
    data[n + 1] = 0.0f;
}

void RealFft::inverse (float* data) noexcept
{
    // Back to Z[k] = E[k] + i O[k] (scaled by 1/m for the inverse), then z = IFFT_m (Z), computed
    // as swap (FFT (swap Z)) where swap exchanges the real and imaginary parts.
    const float scale = 1.0f / (float) m;
    float* __restrict zr = ar.data();
    float* __restrict zi = ai.data();
    {
        const float x0 = data[0], xm = data[n];
        zr[0] = 0.5f * (x0 + xm) * scale;
        zi[0] = 0.5f * (x0 - xm) * scale;
    }
    for (int k = 1; k < m; ++k)
    {
        const float xr = data[2 * k], xi = data[2 * k + 1], yr = data[2 * (m - k)], yi = data[2 * (m - k) + 1];
        const float er = 0.5f * (xr + yr), ei = 0.5f * (xi - yi);
        const float dr = 0.5f * (xr - yr), di = 0.5f * (xi + yi);
        const float c = splitCos[(size_t) k], s = splitSin[(size_t) k];
        const float orr = c * dr - s * di, oi = c * di + s * dr;   // O = e^(+2 pi i k / N) D
        zr[k] = (er - oi) * scale;
        zi[k] = (ei + orr) * scale;
    }
    // Swapped: the real parts go in as the imaginary ones, and come out as them.
    const bool second = transform (ai.data(), ar.data(), bi.data(), br.data());
    const float* gr = second ? br.data() : ar.data();
    const float* gi = second ? bi.data() : ai.data();
    for (int j = 0; j < m; ++j)
    {
        data[2 * j]     = gr[j];
        data[2 * j + 1] = gi[j];
    }
}
}
