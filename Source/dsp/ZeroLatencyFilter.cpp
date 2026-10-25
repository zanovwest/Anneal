#include "ZeroLatencyFilter.h"
#include "FastMath.h"
#include "SpectralEngine.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace anneal
{
void ZeroLatencyFilter::prepare (double sr, int numChannels, int analysisFftSize, int analysisHop)
{
    sampleRate = sr;
    channels = std::clamp (numChannels, 1, 2);
    taps = std::max (64, analysisFftSize);   // 21 ms at 48 kHz: long enough for the curve's low end
    designSize = 4 * taps;                   // room for the cepstrum (little time aliasing)
    maxSegment = std::max (16, analysisHop);
    fadeLength = std::max (16, (int) std::lround (0.00133 * sr));
    fft.prepare (designSize);
    fftHalf.prepare (designSize / 2);
    work.assign ((size_t) (2 * designSize), 0.0f);
    logMag.assign ((size_t) (designSize / 2 + 1), 0.0f);
    for (auto* v : { &phase, &sinPart, &cosPart, &gain })
        v->assign ((size_t) (designSize / 2 + 1), 0.0f);
    const int half = designSize / 2;
    halfSin.resize ((size_t) half);
    halfCos.resize ((size_t) half);
    for (int k = 0; k < half; ++k)
    {
        halfSin[(size_t) k] = (float) std::sin (3.141592653589793 * k / half);
        halfCos[(size_t) k] = (float) std::cos (3.141592653589793 * k / half);
    }
    // The impulse response's raised-cosine fade over its last eighth, as a table (the same values the
    // design computed per sample up to 5.2.4).
    const int fade = taps / 8;
    constexpr float pi = 3.14159265358979f;
    fadeOut.resize ((size_t) fade);
    for (int i = 0; i < fade; ++i)
        fadeOut[(size_t) i] = 0.5f * (1.0f + std::cos (pi * (float) (i + 1) / (float) fade));
    for (auto& f : coef)
        for (auto& set : f)
            set.assign ((size_t) taps, 0.0f);
    for (auto& h : hist)
        h.assign ((size_t) (taps - 1 + maxSegment), 0.0f);
    outMid.assign ((size_t) maxSegment, 0.0f);
    outSide.assign ((size_t) maxSegment, 0.0f);
    scratch.assign ((size_t) maxSegment, 0.0f);
    reset();
}

void ZeroLatencyFilter::reset() noexcept
{
    for (int f = 0; f < 2; ++f)
        for (int s = 0; s < 2; ++s)
        {
            std::fill (coef[f][s].begin(), coef[f][s].end(), 0.0f);
            coef[f][s][0] = 1.0f;
            active[f][s] = 1;
            muted[f][s] = false;
        }
    for (auto& h : hist)
        std::fill (h.begin(), h.end(), 0.0f);
    current = 0;
    fadePos = -1;
}

void ZeroLatencyFilter::design (const float* binGain, int analysisBins, float sideScale, float monoHz, float* out,
                                const float* widthShare) noexcept
{
    const int m = designSize, half = m / 2;
    float* w = work.data();
    const double binHzAnalysis = sampleRate / (2.0 * (analysisBins - 1));
    const double binHz = sampleRate / m;
    const bool mono = monoHz >= 20.0f;
    const double lo = mono ? monoHz * 0.84089642 : 0.0, hi = mono ? monoHz * 1.18920712 : 0.0;

    // 1. Log magnitude on the design grid: the analysis curve interpolated, Width and Mono bass on top.
    for (int k = 0; k <= half; ++k)
    {
        const double f = k * binHz;
        const double pos = std::min (f / binHzAnalysis, (double) (analysisBins - 1));
        const int i = std::min ((int) pos, analysisBins - 2);
        const float fr = (float) (pos - i);
        float scale = sideScale;
        if (widthShare != nullptr)   // 5.2.2 Width paint
            scale = 1.0f + (widthShare[i] + fr * (widthShare[i + 1] - widthShare[i])) * (sideScale - 1.0f);
        float g = (binGain[i] + fr * (binGain[i + 1] - binGain[i])) * scale;
        if (mono && f < hi)
        {
            float t = f <= lo ? 0.0f : (float) (2.0 * std::log2 (f / lo));
            t = std::clamp (t, 0.0f, 1.0f);
            g *= t * t * (3.0f - 2.0f * t);
        }
        logMag[(size_t) k] = std::max (g, 1.0e-6f);   // floor -120 dB
    }
    const bool halfTransforms = ! exactMath && ! fullTransforms;
    if (halfTransforms)   // 5.2.6: the magnitude for step 4, as it is
        std::copy (logMag.begin(), logMag.begin() + half + 1, gain.begin());
    // Natural log: 5.2.5 batched through fastmath (ln g = 10 log10 g * ln 10 / 10).
    if (exactMath)
        for (int k = 0; k <= half; ++k)
            logMag[(size_t) k] = std::log (logMag[(size_t) k]);
    else
    {
        constexpr float lnPerDb = 0.23025850929940457f;
        fastmath::powerToDb (logMag.data(), logMag.data(), half + 1);
        for (int k = 0; k <= half; ++k)
            logMag[(size_t) k] *= lnPerDb;
    }

    // 2. to 4. The minimum-phase spectrum with that magnitude, then its impulse response.
    if (halfTransforms)
        spectrumHalf (w);
    else
        spectrumFull (w);
    fft.inverse (w);

    // 5. The first `taps` samples, faded out over the last eighth (a minimum-phase response has
    //    its energy at the start, so little is lost).
    const int fade = (int) fadeOut.size();
    for (int n = 0; n < taps; ++n)
        out[n] = n >= taps - fade ? w[n] * fadeOut[(size_t) (n - (taps - fade))] : w[n];
}

void ZeroLatencyFilter::spectrumFull (float* w) noexcept
{
    const int m = designSize, half = m / 2;

    // 2. Real cepstrum: the inverse transform of the (real, even) log magnitude.
    for (int k = 0; k <= half; ++k)
    {
        w[2 * k] = logMag[(size_t) k];
        w[2 * k + 1] = 0.0f;
    }
    fft.inverse (w);   // scaled 1/N: w[0..m) is the cepstrum

    // 3. Fold it onto positive time: the cepstrum of the minimum-phase filter with that magnitude.
    for (int n = 1; n < half; ++n)
        w[n] *= 2.0f;
    std::fill (w + half + 1, w + m, 0.0f);

    // 4. Back to a (complex) log spectrum; exponentiate.
    fft.forward (w);
    if (exactMath)
    {
        for (int k = 0; k <= half; ++k)
        {
            const float mag = std::exp (w[2 * k]), ph = w[2 * k + 1];
            w[2 * k] = mag * std::cos (ph);
            w[2 * k + 1] = mag * std::sin (ph);
        }
    }
    else
    {
        // 5.2.5: e^x = 10^(x * 20 / ln 10 / 20) through dbToGain, sin and cos together, four at a time.
        constexpr float dbPerNeper = 8.6858896380650366f;
        float* mag = logMag.data();
        for (int k = 0; k <= half; ++k)
        {
            mag[k] = w[2 * k] * dbPerNeper;
            phase[(size_t) k] = w[2 * k + 1];
        }
        fastmath::dbToGain (mag, mag, half + 1);
        fastmath::sinCos (phase.data(), sinPart.data(), cosPart.data(), half + 1);
        for (int k = 0; k <= half; ++k)
        {
            w[2 * k] = mag[k] * cosPart[(size_t) k];
            w[2 * k + 1] = mag[k] * sinPart[(size_t) k];
        }
    }
}

void ZeroLatencyFilter::spectrumHalf (float* w) noexcept
{
    // 5.2.6: the same spectrum from two N-point real FFTs instead of two 2N-point ones (N = half).
    // The log magnitude L is real and even, so its cepstrum is a DCT, and the minimum phase only
    // needs the imaginary part of the folded cepstrum's transform, a DST (the real part is L again).
    // Both come from an N-point real FFT of a mix of the sequence and its mirror image: one half of
    // the outputs from the FFT directly, the other by a running sum (in double).
    const int m = designSize, half = m / 2;
    const float* L = logMag.data();
    const float* sn = halfSin.data();
    float* d = w + m;   // the folded cepstrum, in the upper half of the work buffer

    // 2. The cepstrum, C[n] = sum over k < m of L[k] cos (2 pi k n / m), n < N.
    double sum = 0.0;   // C[1]
    for (int k = 0; k < half; ++k)
    {
        const float a = 0.5f * (L[k] + L[half - k]), b = 0.5f * (L[k] - L[half - k]);
        w[k] = a - 2.0f * sn[k] * b;
        sum += 2.0 * b * halfCos[(size_t) k];
    }
    fftHalf.forward (w);   // C[2j] = 2 Re Y[j], C[2j + 1] = C[2j - 1] - 2 Im Y[j]

    // 3. Folded onto positive time: d[n] = 2 c[n] = 2 C[n] / m for 0 < n < N (d[0] and d[N] don't
    //    reach the phase).
    const float toFolded = 2.0f / (float) m;
    d[0] = d[half] = 0.0f;
    d[1] = (float) sum * toFolded;
    for (int j = 1; j < half / 2; ++j)
    {
        d[2 * j] = 2.0f * w[2 * j] * toFolded;
        sum -= 2.0 * w[2 * j + 1];
        d[2 * j + 1] = (float) sum * toFolded;
    }

    // 4. The phase, P[k] = -sum over 0 < n < N of d[n] sin (pi k n / N), k <= N.
    for (int n = 0; n < half; ++n)
    {
        const float s = 0.5f * (d[n] + d[half - n]), t = 0.5f * (d[n] - d[half - n]);
        w[n] = 2.0f * sn[n] * s + t;
    }
    fftHalf.forward (w);   // P[2j] = Im Z[j], P[2j + 1] = P[2j - 1] - Re Z[j], P[1] = -Re Z[0] / 2
    float* ph = phase.data();
    for (int j = 0; j <= half / 2; ++j)
        ph[2 * j] = w[2 * j + 1];
    ph[0] = ph[half] = 0.0f;
    sum = -0.5 * w[0];
    ph[1] = (float) sum;
    for (int j = 1; j < half / 2; ++j)
    {
        sum -= w[2 * j];
        ph[2 * j + 1] = (float) sum;
    }

    //    The spectrum: the curve's own magnitude (no exp (log)) with that phase.
    fastmath::sinCos (ph, sinPart.data(), cosPart.data(), half + 1);
    for (int k = 0; k <= half; ++k)
    {
        w[2 * k] = gain[(size_t) k] * cosPart[(size_t) k];
        w[2 * k + 1] = gain[(size_t) k] * sinPart[(size_t) k];
    }
}

int ZeroLatencyFilter::filter (int which, int set, const float* x, float* out, int n) noexcept
{
    // out[i] = sum_k c[k] x[i - k], x[0] being this segment's first sample (x[-1].. the history).
    if (muted[which][set])
    {
        std::fill (out, out + n, 0.0f);
        return 0;
    }
    const float* c = coef[which][set].data();
    std::fill (out, out + n, 0.0f);
    for (int k = 0; k < active[which][set]; ++k)
        juce::FloatVectorOperations::addWithMultiply (out, x - k, c[k], n);
    return 1;
}

void ZeroLatencyFilter::process (float* const* io, int numChannels, int numSamples, SpectralEngine& analysis,
                                 const float* const* key, int keyChannels, float width, float monoHz) noexcept
{
    numChannels = std::min (numChannels, channels);
    const bool stereo = numChannels == 2;
    int done = 0;

    while (done < numSamples)
    {
        int n = std::min ({ numSamples - done, analysis.samplesToNextFrame(), maxSegment });
        if (fadePos >= 0)   // 5.2.6: a segment ends with the crossfade, so the old filters run only while it lasts
            n = std::min (n, fadeLength - fadePos);

        // 1. This segment's Mid (and Side) after the history.
        float* xm = hist[0].data() + (taps - 1);
        float* xs = hist[1].data() + (taps - 1);
        if (stereo)
            for (int i = 0; i < n; ++i)
            {
                const float l = io[0][done + i], r = io[1][done + i];
                xm[i] = 0.5f * (l + r);
                xs[i] = 0.5f * (l - r);
            }
        else
            std::memcpy (xm, io[0] + done, sizeof (float) * (size_t) n);

        // 2. The analysis takes in the input (analysis-only: it does not change it).
        {
            float* ptrs[2] = { io[0] + done, stereo ? io[1] + done : nullptr };
            const float* keys[2] = { nullptr, nullptr };
            for (int c = 0; c < std::min (keyChannels, 2); ++c)
                keys[c] = key[c] + done;
            analysis.process (ptrs, numChannels, n, keyChannels > 0 ? keys : nullptr, keyChannels);
        }

        // 3. Filter with the current filters; after an update, crossfade to the new ones.
        for (int f = 0; f < (stereo ? 2 : 1); ++f)
        {
            const float* x = f == 0 ? xm : xs;
            float* y = f == 0 ? outMid.data() : outSide.data();
            filter (f, current, x, y, n);
            if (fadePos >= 0)
            {
                filter (f, 1 - current, x, scratch.data(), n);
                for (int i = 0; i < n; ++i)
                {
                    const float t = std::min (1.0f, (float) (fadePos + i + 1) / (float) fadeLength);
                    y[i] += t * (scratch[(size_t) i] - y[i]);
                }
            }
        }
        if (fadePos >= 0)
            crossfaded += (std::uint64_t) n;
        if (fadePos >= 0 && (fadePos += n) >= fadeLength)
        {
            current = 1 - current;
            fadePos = -1;
        }

        // 4. Back to left / right.
        if (stereo)
            for (int i = 0; i < n; ++i)
            {
                io[0][done + i] = outMid[(size_t) i] + outSide[(size_t) i];
                io[1][done + i] = outMid[(size_t) i] - outSide[(size_t) i];
            }
        else
            std::memcpy (io[0] + done, outMid.data(), sizeof (float) * (size_t) n);

        // 5. Keep the last taps - 1 inputs for the next segment.
        for (int f = 0; f < (stereo ? 2 : 1); ++f)
            std::memmove (hist[f].data(), hist[f].data() + n, sizeof (float) * (size_t) (taps - 1));
        done += n;

        // 6. A new analysis frame: design the next filters and fade them in.
        if (analysis.getFrameCount() != lastFrame)
        {
            lastFrame = analysis.getFrameCount();
            if (fadePos >= 0)   // the previous fade isn't over (only with very short frames): finish it now
            {
                current = 1 - current;
                fadePos = -1;
            }
            const int next = 1 - current;
            const int bins = analysis.getNumBins();
            auto trim = [this] (const std::vector<float>& c)
            {
                float peak = 0.0f;
                for (float v : c)
                    peak = std::max (peak, std::abs (v));
                int last = taps;
                while (last > 1 && std::abs (c[(size_t) last - 1]) < peak * 1.0e-6f)
                    --last;
                return last;
            };
            design (analysis.getCurveMid(), bins, 1.0f, 0.0f, coef[0][next].data());
            active[0][next] = trim (coef[0][next]);
            muted[0][next] = false;
            if (stereo)
            {
                const bool sameAsMid = analysis.getCurveSide() == analysis.getCurveMid()
                                       && std::abs (width - 1.0f) < 1.0e-4f && monoHz < 20.0f;
                const float* share = analysis.getWidthShare();   // 5.2.2: painted Width
                muted[1][next] = width < 1.0e-4f && share == nullptr;
                if (sameAsMid)
                {
                    coef[1][next] = coef[0][next];
                    active[1][next] = active[0][next];
                }
                else if (! muted[1][next])
                {
                    design (analysis.getCurveSide(), bins, width, monoHz, coef[1][next].data(), share);
                    active[1][next] = trim (coef[1][next]);
                }
            }
            fadePos = 0;
        }
    }
}
}
