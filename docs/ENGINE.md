# Anneal — engine plan

## Signal graph (per block, audio thread)

```
input ──► dry delay (N samples) ───────────────────────────────┐
   └────► SpectralEngine (STFT, N, hop N/8) ─► wet ─► LoudnessMatch gain ─► mix / Δ ─► output gain ─► bypass xfade ─► out
                 per frame:
                 1. zero-phase Hann frame → FFT (each channel)
                 2. linked power  p[k] = mean_c |X_c[k]|² (amplitude² units)
                 3. PerceptualModel  → per-band auto-EQ gain (dB) + heat-need (0..2)
                 4. HarmonicSynth    → H_c[k]  (peak-locked harmonic regions, 2nd–6th)
                 5. Y_c[k] = G[k]·X_c[k] + H_c[k]
                 6. iFFT → Hann synthesis window → overlap-add (×1/3)
```

| Item | Value |
|---|---|
| FFT size N | 4096 (fs ≤ 50 kHz), 8192 (≤ 100 kHz), 16384 above |
| Hop | N/8 (≈ 94 frames/s at 48 kHz) |
| Window | periodic Hann analysis + synthesis, OLA gain 3 → scaled by 1/3 |
| Latency | N samples (85.3 ms at 48 kHz) — reported to the host, dry path delayed by the same amount |
| Tail | N samples |
| Channels | mono or stereo, in = out; analysis is stereo-linked, synthesis per channel (image preserved) |
| Parameter resolution | parameters are sampled once per host block, smoothed per frame (τ 40 ms) |

## Perceptual model (heuristic, model-derived — not a calibrated loudness model)

1024 bands, uniform on the ERB-rate (Cam) scale from 20 Hz to min(20 kHz, 0.49·fs)
(≈ 0.04 Cam spacing). Below ~150 Hz the bands are finer than the FFT bins, so
those bands are interpolated rather than independent measurements.

1. Bin power is partitioned into band slices (exact frequency-overlap weights).
2. Outer/middle-ear weighting (PEAQ, ITU-R BS.1387 shape; HF roll-off capped at
   −15 dB) and an internal-noise floor from Terhardt's threshold in quiet
   (capped at 40 dB SPL above 4 kHz) with 0 dBFS sine = 92 dB SPL.
3. 20 ms temporal integration.
4. **Place level** `A[b]` = power within ±0.5 Cam (one ERB).
5. **Context** `C[b]` = masking spread of `A` from everything ≥ 1 Cam away
   (upward spread 9 dB/Cam, downward 16 dB/Cam; O(n) recursive).
6. **Prominence** `d[b] = 10·log10(A/C)` normalised so a flat spectrum gives 0 dB.
   `d > 0`: dominates its neighbourhood. `d < 0`: sits in a masking shadow.
7. Smooth cut = Smooth · min(9 dB, 0.5·(d − 2)). Reveal boost = Reveal ·
   min(8 dB, 0.6·(−d − 2)), faded out for d < −18 dB (empty gaps are not lifted) and
   gated by audibility above the internal noise.
8. Smoothed ±0.25 Cam across bands, then in time (falling gain: Response/3, rising: Response).
9. Heat need = 0.35 baseline, → 1 in shadowed bands, → 0 in dominant bands;
   multiplied by Focus (body↔air) and the Range gate.
10. Static Tilt (dB/oct around 1 kHz, ±9 dB cap) is added inside the Range.

## Spectral saturation

Peaks: local maxima (±2 bins) above −100 dBFS and within 70 dB of the frame peak.
Each peak owns a region of influence (≤ ±4 bins, split at the minimum between peaks).
For harmonic n the region is shifted by `round((n−1)·(k+δ))` bins (δ from parabolic
interpolation) and rotated by `e^{i(n−1)θ}` (θ = peak phase in a zero-phase frame), so
the harmonic is phase-locked to its fundamental, like a waveshaper's output.

Amplitude per harmonic: `sign_n · w_parity · tanh(u)^(n−1) / n`, with `u = a·drive / a_ref`,
`a_ref = −18 dBFS`, `drive = 10^((Heat·40 − 34)/20)`. Signs follow a soft clipper
(+, −, −, +, + for n = 2…6). Each write is scaled by the heat-need at the
*destination* bin.

Properties: no intermodulation products and no aliasing (writes above the top bin are
dropped). Expected weakness: like any STFT process, harmonics are smeared over the
frame (pre-echo up to ~N/2 before sharp onsets). Measured in the tests.

## Real-time contract

- All buffers, the FFT object and the band tables are allocated in `prepare()`.
- Audio callback: no allocation, locks, logging or I/O.
- Editor data: one `DisplayFrame` per STFT frame via a wait-free triple buffer;
  meters via atomics.

## Budgets

- CPU: < 3 % of one core per stereo instance at 48 kHz / 512 (measured on the build machine).
- Memory: < 2 MB per instance.

## Acceptance tests (`AnnealTests`)

| # | Test | Criterion |
|---|---|---|
| 1 | STFT identity, neutral params, fs 44.1/48/96 k, blocks 1…4096 + random | max error < 1e-4 vs delayed input |
| 2 | Latency | impulse arrives exactly `getLatencySamples()` later |
| 3 | Mix 0 null / Δ with neutral settings | bit-exact delayed dry / < −90 dBFS |
| 4 | Harmonics, even vs odd | even: 2nd ≥ −40 dBr, 3rd ≤ −60 dBr; odd: the reverse |
| 5 | No aliasing | 9 kHz sine @48 k, odd heat: nothing at 21 kHz / 3 kHz (≤ −90 dBr) |
| 6 | No IMD | 1000 + 1300 Hz: 300 Hz & 2300 Hz ≤ −80 dBr while harmonics present |
| 7 | Smooth / Reveal / Range | resonance cut ≤ −3 dB; shadowed band lifted ≥ +1 dB; zero outside range |
| 8 | Automation stress | random params & blocks for 30 s: finite, bounded |
| 9 | Re-prepare at new rates | latency follows N |
| 10 | State | round-trip + malformed data ignored |
| 11 | Bypass | delayed dry after the ramp |
| 12 | Benchmark | report real-time factor & worst block vs deadline |
| 13 | Pre-echo (informational) | report harmonic energy before an onset |

## Feature status

See README.md → "Status".
