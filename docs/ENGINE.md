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
| FFT size N | Precise: 4096 (fs ≤ 50 kHz), 8192 (≤ 100 kHz), 16384 above. Normal: N/2. Low: N/4 |
| Hop | sampleRate / 300 in every mode since 5.2.0 (160 at 48 kHz, 147 at 44.1 kHz; was N/8 and N/4 = 94 / 94 / 188 frames/s). Each position inside a hop gets its own overlap-add gain, 1 / Σ w² of the frames covering it, because 160 doesn't divide N |
| Window | periodic Hann analysis + synthesis, OLA gain 3 → scaled by 1/3 |
| Latency | N samples: Low 21.3 ms, Normal 42.7 ms, Precise 85.3 ms at 48 kHz. Reported to the host, dry path delayed by the same amount. One engine per mode is prepared up front; a switch happens on the message thread with processing suspended, then the new latency is reported |
| Spread load (5.2.1) | Off by default. A frame's work is four stages (0: the input spectra and Mid/Side; 1: the sidechain key and the model, plus the Mid model; 2: the curve on the spectra, the harmonics, the Side model and the blend, the display; 3: Listen, Width, the inverse FFTs into the overlap-add). Off, all four run in the block where the hop ends, as before. On, the frame is copied at the hop boundary and its stages run at 1/8, 3/8, 5/8 and 7/8 of the next hop; at the following boundary the overlap-add's next hop goes out. So a block of up to a quarter hop (40 samples at 48 kHz) carries at most one stage, and the output is the same one hop later, sample for sample (the tests compare them). Latency N + hop (+3.3 ms); the gate's aligned input and the dry path follow. Rests while analysis-only (Zero latency), where it would add latency for nothing |
| Tail | N samples (N + hop with Spread load) |
| Channels | mono or stereo, in = out; linked Stereo (default) or independently analysed Mid/Side; mono always uses linked mono |
| Parameter resolution | parameters are sampled once per host block, smoothed per frame (τ 40 ms) |
| FFT (4.0.2) | Anneal's own `RealFft`: the N real samples packed into N/2 complex points (even/odd split), a split-format radix-4 Stockham FFT written for the compiler's vectoriser, then the real/complex split. Same in-place layout and scaling as JUCE's `performRealOnly*` transforms (forward unscaled, inverse 1/N, DC and Nyquist imaginary parts ignored); within −133 dB of JUCE's and as close to an exact DFT (−136.5 dB vs −137.8 dB). About 4× faster than JUCE's portable fallback, which Windows and Linux builds get (no IPP or FFTW): 6.6 µs instead of 24.7 µs per 4096-point forward transform |
| Zero-latency design (5.2.5) | `ZeroLatencyFilter::design` takes its natural log through `powerToDb` (× ln 10 / 10), its exp through `dbToGain` (× 20 / ln 10) and its phases through `fastmath::sinCos` (reduced to ±π/4 around the nearest multiple of π/2, π/2 in three parts, Taylor to r⁹ / r¹⁰; largest error 8e-8 up to |x| = 10⁴), all in batches; the fade-out is a table. 41.5 → 32.5 µs per design, within 0.000006 dB of the per-value standard library (kept as `setExactMath`, for the tests) |
| Zero-latency design (5.2.6) | Steps 2 to 4 on transforms of half the design size M (= 4 × taps): the log magnitude L is real and even, so its cepstrum is a DCT-I, and the minimum phase needs only the imaginary part of the folded cepstrum's transform, a DST-I (the real part is L again). Each comes from one M/2-point real FFT of the sequence mixed with its mirror image (y = a − 2 sin (2πk/M) b, z = 2 sin (2πn/M) s + t), half the outputs directly and the other half by a running sum in double; the spectrum then takes the curve's own magnitude instead of exp (log). 33.3 → 26.0 µs per design, within 0.0008 dB of 5.2.5's above −40 dB (0.0023 dB down to −60 dB: the running sums round a little more); `setFullTransforms` keeps 5.2.5's for the tests, and exact math always uses them. A segment ends where the 1.33 ms crossfade ends, so the old filters run 64 samples per design instead of the rest of the hop (bit-identical output). A design transform of 2 × taps was measured and rejected: 0.04–0.08 dB off on music curves, up to 19 dB next to Mono bass |
| Punch at zero latency (5.2.7) | `TransientShaper`, after the zero-latency filters, when *Punch at Zero* is on (projects from before 5.2.7: off). What is inside Range is split off as b = LP1 (Range High) of HP1 (Range Low), one-pole TPT sections, and only b takes the gain: y = x + (g − 1) b. First-order edges keep b inside the circle Re (1/b) ≥ 1 relative to x, so \|y / x\| always lies between 1 and g: a boost never turns into a cut at the edges (steeper edges would, by up to 3 dB an octave out). Detector per component (Stereo: b_L² + b_R², one gain for both; Mid/Side: b_M² and b_S², Punch × the EQ routing): a fast envelope (attack 1 ms, release 30 ms) against a slow one of it (40 ms); onset = (10 log10 (fast / slow) − 4) / 8 clamped to 0..1, as in the spectral detector; weight = max (onset, weight × e^(−1/12 ms)); gain = Punch × 8 dB × weight, smoothed over 0.3 ms. The level primes over 2 ms after a reset or a Stereo ↔ Mid/Side switch. Punch 0 (or a routed-out component): the audio as it is, bit for bit; the detector keeps running. log10 only while a rise is between 4 and 12 dB, exp only while the gain isn't 0 |
| Loudness readout (5.3.1) | Two `LoudnessMeter`s, one on the latency-aligned dry block (the input Mix and Match use), one on what leaves the processor (after the Ceiling and Bypass). EBU R128 / BS.1770-4: K-weighting from B. De Man's analogue fit (the ITU's own coefficients at 48 kHz, +0.70 dB at 1 kHz; LoudnessMatch keeps its looser RBJ fit, +0.44 dB, which only sets a ratio), channel weights 1, sums of squares per 100 ms step. Short-term = the last 30 steps (fewer in the first 3 s). Integrated: every step, the 400 ms block that ends there (75 % overlap) goes into a histogram of 0.05 LU bins from −70 to +10 LUFS (count and summed mean square per bin, 1600 bins, allocated in the constructor) when it is over −70 LUFS; the relative gate (10 LU under the mean of those) then sums the bins whose middle is over it, within 0.025 LU of exact. A reset (a click on the readout, done at the next block) clears the histogram, and blocks start with the first whole step after it. `reset()` and `prepare()` forget the last 3 s, not the integrated value. Results reach the editor through four atomics. EBU Tech 3341 cases 1–5 and 9 within 0.02 LU |
| dB math (5.1.1) | The model's dB conversions (prominence, audibility, sidechain, Match, the display levels: 10·log10; the per-bin gains: 10^(dB/20)) run in batches through `Source/dsp/FastMath`: log2 from the float's exponent plus an atanh series on the mantissa (to t⁹), exp2 from a rounded exponent plus a Taylor series (to f⁷); SSE2 four at a time on x86, plain loops elsewhere. Largest errors 0.00003 dB / 0.000013 dB; the curve stays within 0.000004 dB of the per-value `std::log10` / `std::pow` (still there as the model's exact math, for the tests and the profile). Fully in Mid/Side the linked harmonic synth is skipped: `processMidSide` replaces its output |

### Listen modes (0.2.5)

Per frame, after the display data is published (so the display shows the full processing), the
engine can replace what it outputs with one part of it. With `X` the frame's spectrum before the
EQ and `A` the auto-EQ gain without Tilt (`PerceptualModel::computeAutoGain`, the same band→bin
steps as the full curve, so `binGain = A × tilt gain` exactly):

| Listen | Main path | Harmonic stream |
|---|---|---|
| Reveal only | `X·max(A−1, 0)`: what the lift adds | not written (its tail fades out) |
| Smooth only | `X·max(1−A, 0)`: what the cut takes away, in phase | not written |
| Heat only | 0 (its tail fades out) | as normal, gated as normal |

In Mid/Side each component uses its own model's `A` (an untargeted component has none), decoded to
L/R and blended with the stereo version exactly like the main path. With Tilt at 0:
normal − Reveal solo + Smooth solo = `X`, and normal − Heat solo = `X` (tested to < 1e-4). A
switch happens on a frame boundary; overlap-add crossfades it over one frame. The processor plays
the solo as `mix × match × wet × output` (fading 30 ms between it and the normal mix), Bypass
still wins, and the loudness-match measurement holds while a solo plays and for two frame lengths
after, so Match never chases the quieter solo.

## Perceptual model (heuristic, model-derived — not a calibrated loudness model)

### Mid/Side routing (0.2.4)

After the two L/R FFTs, encode `M=(L+R)/2`, `S=(L-R)/2`. Each component gets its own model and
harmonic synthesis, with separate EQ and Heat targets (Both / Mid / Side). EQ targeting includes
Reveal, Smooth, Tilt, Punch, the reference's match curve and (since 5.3.2; it was missed until then) the
sidechain's Amount, so an untargeted component has no auto-EQ move at all; Heat targeting leaves its
placement model active. Decode `L=M+S`,
`R=M-S` before inverse FFT and overlap-add, so every audio FIFO stays L/R. A 40 ms spectral
crossfade and smoothed routing amounts handle automation without reallocating or changing
latency. Pre-onset harmonic guards are measured separately for Mid and Side. The existing
latency-aligned harmonic envelope gate remains stereo-linked.

All M/S storage is prepared up front; the M/S analysis runs only while that mode is active or
fading. The linked analysis stays warm for return to Stereo. Stereo processing keeps the legacy
signal path. Missing new parameters in older sessions restore Stereo/Both/Both, even when
loading into a previously configured M/S instance. Factory presets do not change routing.

The display publishes separate Mid and Side gains, plus measured combined L/R spectrum and
harmonic energy. Tests compare M/S EQ against two independent mono engines, check neutral
nulls at every latency/rate, component isolation, mono fallback, state and automation.

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
10. Static Tilt (dB/oct around the pivot, ±9 dB cap) is added inside the Range: per band
    `clamp (tilt × (log2 (f / 1 kHz) − p), ±9 dB)`, with `p` the pivot's octaves from 1 kHz (5.3.3,
    200 Hz – 5 kHz; `Params.h tiltPivotOctaves`, exactly 0 within 0.5 Hz of 1 kHz, so the default is
    bit-identical to before). The engine glides `p` like the other settings (40 ms, per frame) and
    sets it exactly once within 0.0001 octave, so a pivot moved back to 1 kHz is exact again.
11. **Transient guard** (0.2.2; Guard 0 = off = exactly the steps above). Per band, the one-ERB
    level of the frame without temporal integration is compared with its ~150 ms average: a rise
    of 4 dB starts an attack, 12 dB is a full one (`T` 0..1). `T` is held for one frame length
    (the attack is inside every frame of that span), during which the average does not climb,
    then decays with ~50 ms. With Guard `g`: the auto-EQ gain (not Tilt) is scaled by `1 − g·T`,
    its time smoothing is slowed by the same factor (the curve doesn't chase hits), and the heat
    need is scaled by `1 − g·T` (no harmonics smeared over attacks).
12. **Max cut / Max boost** (5.3.2; parameters `maxCut` / `maxBoost`, 0–24 dB, smoothed per frame like
    the others). The band-smoothed target (step 8, before the time smoothing) is clamped to
    [−Max cut, +Max boost], so the curve lets go as soon as the music does instead of first falling back
    from beyond a limit; the time-smoothed curve is clamped again where it is used (before Guard's relax,
    Tilt, Punch, Range and Paint), which limits a frozen curve, or one still beyond a limit just lowered,
    without changing what is held (raising the limit gives it back whole). It covers the whole auto-EQ
    target (Reveal, Smooth, the sidechain, the reference); Tilt and Punch are added after it. 24 dB is no
    limit: the largest possible move is Smooth 9 + Make room 9 + Match 6 = 24 dB, so a limit there could
    never act, and from 23.95 dB up (`isNoLimit`) neither clamp runs: bit-identical to 5.3.1. Mid/Side
    components each clamp their own curve; the zero-latency filters apply the clamped curve.
13. **Key range** (5.3.4; parameters `scKeyLo` 20 Hz – 10 kHz and `scKeyHi` 50 Hz – 20 kHz, crossed
    automation kept apart like Range: High at least 1.5 × Low). The sidechain's move per band (Make room's
    duck, Unmask's lift; step 7's target, before the smoothing) is multiplied by a window over the key range
    with edges like Range's: smoothstep over ±0.25 Cam around each end. So the key counts only there:
    outside it the sidechain moves nothing (a 300 Hz key with the range at 1–5 kHz: 0.000 dB), inside it
    exactly as before. An end at 20 Hz or 20 kHz (`keyLoIsOpen` ≤ 20.5 Hz, `keyHiIsOpen` ≥ 19.95 kHz) has no
    edge, and with both open (the default, and older projects) the window isn't computed: bit-identical to
    5.3.3. The engine glides both ends in log-frequency like Range (40 ms, per frame) and sets each exactly
    once within 0.01 %, so a range moved back to the whole key is exact again. It applies in every latency
    mode, Mid/Side (each component, by the EQ routing as before) and at zero latency (the analysis engine's
    curve); Learn key (Reference) always learns the whole key.
14. **Detail** (5.3.6; parameter `detail`, −100 Broad … +100 Fine, engine −1…+1). A width scale
    `w = 2.5^(−detail)` (`detailWidthScale`, exactly 1 at 0) sets the place window Reveal and Smooth read,
    ±0.5·w Cam (step 4; the context starts just outside it, step 5, with the spread over that distance), and the
    target smoothing, ±0.25·w Cam (step 8; it smooths the whole target, the sidechain and the match included).
    Both in whole bands, `max (1, round (half / dz))`, so Fine 100 is ±5 / ±3 bands (±0.2 / ±0.1 Cam) and
    Broad 100 ±31 / ±16 (±1.25 / ±0.63 Cam) at 48 kHz, against ±13 / ±6. A flat input gives 0 dB at every
    width: the normalisation for a width is made when the width changes (1024 log10 once, not per frame).
    Heat's placement (`promSlow`), audibility, the sidechain's key-against-main comparison and the transient
    detector keep the one-ERB measure. Wherever the place window comes out as the model's own (at 48 kHz from
    Broad 8 to Normal) the prominence is the usual one and the extra pass doesn't run; at Normal the smoothing
    is the usual one too: bit-identical to 5.3.5. The engine glides Detail like the others (40 ms) and lands it
    exactly within 0.0001. Cost while the window differs: one more place / context / dB pass per model per frame.

### Reference match (4.0)

A reference is a **long-term one-ERB spectrum** (the physical power within ±0.5 Cam, no ear
weighting or internal noise: a recording has neither), stored as 1024 dB values on a fixed grid,
uniform in Cam from 20 Hz to 20 kHz, so a saved project means the same at every sample rate.
Every reference is **level-normalised** (`anneal::normaliseLevels`): minus the mean level over
50 Hz – 16 kHz, where each band counts as at least 40 dB under that window's power mean (an empty
region, such as above an MP3's cut-off, doesn't pull the mean down).

- **From a file** (wav, aif/aiff, flac, mp3, ogg; JUCE's own decoders on every platform): decoded
  and analysed on its own thread, in 65 536-sample chunks, up to 20 minutes, channels averaged in
  power. Hann frames of 8192 (16384 / 32768 above 50 / 100 kHz), hop ½; the power mean of every
  frame, then the model's own one-ERB measure. A silent or unreadable file changes nothing and the
  reason is kept for the display. The result is handed over through a wait-free triple buffer
  (`refToAudio`, preallocated) that the audio thread reads at the top of `processBlock` and copies into
  each engine's model (resampled onto that model's bands, no allocation). A dropped file switches
  Reference to Match (one undo step).
- **Learn key**: while Reference is Learn key and the sidechain key plays (> −90 dBFS), each frame's
  key one-ERB power is summed (a power mean over time); about four times a second the normalised
  curve is published back to the message thread through a second triple buffer and becomes the
  reference ("sidechain key"). A new learn starts with the first playing frame after Learn key is
  chosen, so choosing it with nothing routed keeps the reference there was. The key's FFT runs only
  while Sidechain or Learn key is on.
- **Match**: the main's physical one-ERB power is averaged over ~3 s (a plain running mean until 3 s
  have been heard), normalised the same way, and subtracted from the reference. The difference is
  weighted where both have content (each fades out from −30 to −45 dB under its mean, so nothing is
  lifted into an empty region of the main or pulled down where the reference is empty), averaged
  over ±1.5 Cam, clamped to ±6 dB, × Amount, and added to the model's target where the main is
  audible, before the curve's time smoothing. So Range, Guard, Freeze and Paint treat it like the rest
  of the curve. In Mid/Side the linked model's match curve is added to each routed component.
- Off, Learn key and Amount 0 % leave the target untouched: the output is bit-identical to 3.x.

### Paint (4.0)

Two curves of 64 points on the same fixed Cam grid, 0..1: one for the EQ, one for Heat. Each model
resamples them onto its bands. Per band, after Range: the auto-EQ change (Reveal, Smooth, Tilt,
sidechain, reference; also what the Listen solos play) × the EQ weight, and the heat need × the Heat
weight. A curve that is 1 everywhere switches its multiply off, so no paint is bit-identical to 3.x.

5.2.2 adds a third curve, Width's share. Each engine resamples it per FFT bin (each bin's Cam is kept
from `prepare()`, so a paint change costs one table lookup per bin, no logs) and Width then acts by that
share: w = 1 + share × (Width − 1), before Mono bass. At zero latency the Side filter's design does the
same per analysis bin, and the Side filter is only muted at Width 0 % when nothing is painted. Saved as
a third list, "width", in the Paint child; projects without it get 100 %.

5.2.3, the stereo image for the display, in three light steps on the first three frames of every
eight (37.5 a second, more than the display's 30): stage 0 keeps the input's Mid and Side power per bin
and stage 2 multiplies them by what Anneal applies to each (the auto-EQ curve, or in Mid/Side its Mid
and Side curves; Width by its painted share; Mono bass); the next frame takes Mid to ERB band levels,
the one after Side, and publishes Side − Mid per band (−200 where neither is above −100 dB). Split so
no frame carries more than one band-level pass. The same at zero latency. The harmonics aren't in it,
and the Listen solos don't change it (the display shows the full processing).
The curves reach the audio thread through a third triple buffer; a stroke (begin, changes, end) is
one undo step in the processor's history.

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

### Flavours (0.2.6)

A flavour reshapes that law and nothing else (the main path and the model are untouched). For a
partial at `f0` and harmonic `n` landing at bin frequency `f`:

`amp = parity(Color) · w_flavour[n] · gate(f0) · sign_n · tanh(u · s(f0))^(n−1) / n`, written
scaled by `need[dst] · d(f)`.

| Flavour | w for n = 2…6 | s(f0) (drive) | gate(f0) | d(f) (destination) |
|---|---|---|---|---|
| Clean | 1, 1, 1, 1, 1 | 1 | 1 | 1 |
| Tape | 1, 0.5, 0.35, 0.15, 0.08 | 1 | 1 | 1/√(1+(f/5 kHz)²) |
| Tube | 1.5, 0.3, 0.45, 0.08, 0.1 | 1 | 1 | 1 |
| Transformer | 0.05, 1.4, 0.04, 0.8, 0.03 | clamp(120 Hz / f0, 0.1, 4) (core flux ∝ 1/f) | 1 | 1 |
| Exciter | 1.1, 1, 0.6, 0.45, 0.3 | 2 | smoothstep(log2(f0 / 1.5 kHz)): 0 below 1.5 kHz, 1 from 3 kHz | 1 |

Clean multiplies by exactly 1, so it is bit-identical to the law before flavours. `f0` is the
interpolated peak position. Tape's `d` is a per-bin table built in `prepare()`; the rest is one
multiply per peak. The flavour switches per frame and the harmonic stream's overlap-add crossfades it.
`HarmonicSynth::harmonicLevel` evaluates the same law for the editor's bars.

Properties: no intermodulation products and no aliasing (writes above the top bin are
dropped). Expected weakness: like any STFT process, harmonics are smeared over the
frame (pre-echo up to ~N/2 before sharp onsets). Measured in the tests.

## Real-time contract

- All buffers, the FFT's tables and scratch, and the band tables are allocated in `prepare()`.
- 4.0.2: preparing again (hosts do it for a new buffer size or rate and often for an export; JUCE's
  VST3 wrapper on every `setActive (true)`) keeps a frozen curve when the bands stay the same (every
  rate from 40.8 kHz up), and keeps the loudness-match measurement and gain; only audio history
  starts over, as in `reset()`.
- Audio callback: no allocation, locks, logging or I/O.
- Editor data: one `DisplayFrame` per STFT frame via a wait-free triple buffer;
  meters via atomics.
- 4.0: references and paint arrive through preallocated triple buffers (read at the top of
  `processBlock`, copied, no allocation); file decoding and analysis run on their own thread.

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
