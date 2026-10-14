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
| AUTO EQ | Reveal, Smooth | Tilt, Response, Guard (transients) |
| HEAT | Heat | Color (even↔odd), Focus (body↔air) |
| OUTPUT | — | Listen (0.2.5: Off / Reveal only / Smooth only / Heat only), Mix, Output |
| Header | Preset, Latency (Low / Normal / Precise) | Match (loudness), Δ (delta listen), Bypass |
| Routing (0.2.4) | Stereo / Mid-Side in the header | EQ target and Heat target (Both / Mid / Side) in their group title rows |
| On the display | Range low / high handles (limits where Anneal acts) | |

## Visual direction (v3, 0.2.0): temper accents on cool graphite

Annealed steel changes colour with heat: pale straw, bronze, blue. Those **temper colours
are the palette's semantic roles**, not decoration. v1 put them on warm brown graphite, where
gold-on-brown read as muddy and nothing stood out; v3 keeps the roles and moves everything else
to a cool, near-black neutral so the accents carry the information.

| Role | Token | Meaning |
|---|---|---|
| Reveal / lift | straw `#F1C36B` | lifted bands, Reveal ring |
| Heat / harmonics | bronze `#FF8B3D` | harmonic energy written, Heat ring, harmonic bars |
| Smooth / cut | temper blue `#6FA7FF` | reduced bands, Smooth ring |
| Deck | `#111215` | |
| Screen | `#0A0B0D`, grid `#1B1E23`, divider `#24272D` | |
| Raised (buttons, menus) | `#191B1F` | |
| Text / muted / faint | `#ECEEF1` / `#8C939D` / `#575D67` | |
| Warning | `#FF5A4E` | output over 0 dBFS only |

Lift vs. cut is also encoded by **position** (above/below the 0 dB line), so it never relies
on hue alone; even vs. odd harmonics are solid vs. lighter bars.

- **Type:** Segoe UI (every Windows; tabular figures) for labels and values: caps 11–12 px
  semibold for titles, 14 px values, 21–25 px semibold inside the rings. The wordmark alone
  is Bahnschrift Light with wide tracking.
- **Surface:** flat, with two exceptions that carry meaning: the EQ curve has a soft halo
  and its fill grows stronger away from 0 dB (bigger moves read as stronger); the spectrum
  and heat lanes are soft washes so they stay behind the curve.
- **Controls:** one ring family. Hero (Heat, 132 px) and primary (Reveal, Smooth, 112 px)
  rings show the value inside (click it to type), the title under the ring, and a live line
  under the title ("lifting +2.9 dB", "cutting −3.6 dB", "harmonics −44 dB") that comes from
  the engine; the same amount is a thin inner arc. Secondary controls are compact rows: a
  42 px ring with title and value beside it. Color and Focus are faders with their two
  ends named (even/odd, body/air). Buttons are pills; on = tinted in the role colour.
- **Layout:** header 48 px (wordmark, latency, presets centred, compare cluster right);
  the display takes 56 % of the height; the deck follows the signal: AUTO EQ → HEAT → OUTPUT,
  hairlines between groups, no cards.

## Distinctive functional feature: the ERB-scale "perception screen"

The display's x-axis is the **ERB-rate (Cam) scale**, not a log-frequency axis:
horizontal space is proportional to how much of the cochlea a frequency range
occupies, which is exactly the grid the model works on. On it:

- the live auto-EQ curve in front (straw above 0 dB, blue below, ±12 dB on the right),
- input and output spectrum behind it as smooth lines over a soft wash (smoothed across
  ±0.25 Cam for display only, so tonal music reads as a flowing line, not plateaus),
- a **heat lane** below: how much harmonic energy is written into each band, relative to
  the output there,
- draggable **Range** handles; outside the range is dimmed.

Everything drawn comes from the processor's real per-frame data (published through
a lock-free triple buffer). The harmonic bars next to Color are model-derived (the synth's
own formula at −18 dBFS) and say so.

**Listen (0.2.5).** A pill menu at the top of OUTPUT solos one part of the processing. Off it is a
plain raised pill ("Listen: off"); on, it is tinted in that part's role colour (straw = Reveal only,
blue = Smooth only, bronze = Heat only), Delta dims (the solo overrides it), and the display shows
a matching pill at the top right of the EQ lane ("REVEAL ONLY · hearing what Reveal lifts"). The
curves and the knobs' live lines keep showing the full processing, so you can see the whole
picture while hearing one part of it.

In Mid/Side mode the centre and sides are analysed independently. The EQ curve is solid for Mid
and dashed for Side, with the same straw-above / blue-below colour convention. Hover reports
both gains. Input/output spectra and the heat lane keep showing the combined stereo energy.
