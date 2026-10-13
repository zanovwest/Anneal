# Anneal — design brief

## Musical task

A mix-bus / instrument-bus tone shaper that does two things at once, both steered
automatically by a model of hearing:

1. **Auto EQ** — a dynamic, per-band EQ (1024 ERB-spaced bands, gains updated
   ~94×/s at 48 kHz) that lifts material sitting in the masking shadow of louder
   neighbours (**Reveal**) and pulls down bands that dominate their auditory
   neighbourhood (**Smooth**).
2. **Spectral saturation** — instead of a waveshaper, every spectral peak grows its
   own harmonic series (2nd–6th) directly in the FFT domain. Harmonics are placed
   where the model says the spectrum is thin, and withheld where it is already
   dominant. Because each partial is enriched on its own, there is **no
   intermodulation** and **no aliasing** (harmonics above the top of the range are
   simply never written).

References: Musik Hack SweetEQ (saturating EQ with macro regions — low end juice,
high end sparkle, band-pass range) and Soundtheory Gullfoss (perceptual automatic
EQ, Recover/Tame). Anneal borrows the *ideas* (saturation as EQ, masking-driven
EQ), not their algorithms, names or panel layouts.

**Main gesture:** while listening, the user turns **Heat** (how much harmonic
enrichment) and balances **Reveal** against **Smooth**. Everything else is
secondary.

## Signal path and control groups

```
in ─┬─ STFT ─► perceptual model ─► auto-EQ gain curve ─┐
    │            │                                     ├─► ×gain + harmonics ─► iSTFT ─► match ─► mix/Δ ─► output ─► bypass xfade ─► out
    │            └─► heat-need map ─► harmonic synth ──┘
    └─ latency-matched dry ─────────────────────────────────────────────────────────┘
```

| Group (left → right = signal flow) | Primary | Secondary |
|---|---|---|
| AUTO EQ | Reveal, Smooth | Tilt, Response |
| HEAT | Heat | Color (even↔odd), Focus (body↔air) |
| OUTPUT | — | Mix, Output, Match (loudness), Δ (delta listen) |
| On the display | Range low / high handles (limits where Anneal acts) | |

## Visual direction: "temper colours on a measurement screen"

Annealed steel changes colour with heat — pale straw, dark straw, bronze, purple,
blue. Those **temper colours are the palette's semantic roles**, not decoration:

| Role | Token | Meaning |
|---|---|---|
| Reveal / boost | straw `#E3C072` | lifted bands |
| Heat / harmonics | bronze `#D9823F` | harmonic energy written |
| Smooth / cut | temper blue `#7898CC` | reduced bands |
| Panel | warm graphite `#1A1917` | |
| Screen | `#100F0E`, grid `#26231F` | |
| Text / muted | `#ECE6DA` / `#8F897E` | |
| Divider | `#2F2C28` | |
| Warning | `#E2553D` | output over 0 dBFS only |

Boost vs. cut is also encoded by **position** (above/below the 0 dB line) and
fill pattern, so it does not rely on hue.

- **Type:** Bahnschrift (DIN-derived, ships with Windows 10/11) for everything;
  stable-width digits for values. Fallback: the default system sans. Product name
  in Bahnschrift Light with wide tracking; control labels plain caps at 13 px.
- **Surface:** flat ink. No gradients-as-decoration, no glow, no fake hardware.
  Hairline dividers between groups instead of cards.
- **Controls:** one knob family. 270° arc, value arc in the role colour, a short
  pointer, bipolar knobs fill from the centre tick. Primary knobs 88 px, secondary
  54 px. Double-click resets, Ctrl/Alt-drag is fine adjust, the value field under
  each knob is typeable (units parsed by the parameter).
- **Spacing:** 8 px grid; header 40 px; display ≈ 55 % of height.

## Distinctive functional feature: the ERB-scale "perception screen"

The display's x-axis is the **ERB-rate (Cam) scale**, not a log-frequency axis:
horizontal space is proportional to how much of the cochlea a frequency range
occupies, which is exactly the grid the model works on. On it:

- input spectrum (thin line) and output spectrum (filled),
- the live auto-EQ curve (straw above 0 dB, blue below, ±12 dB scale on the right),
- a **heat strip** along the bottom showing how much harmonic energy is being
  written into each band (bronze intensity + height),
- draggable **Range** handles.

Everything drawn comes from the processor's real per-frame data (published through
a lock-free triple buffer). The curve is labelled "AUTO EQ (live)".
