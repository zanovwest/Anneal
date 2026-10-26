# Anneal — design brief

## Musical task

A mix-bus / instrument-bus tone shaper that does two things at once, both steered
automatically by a model of hearing:

1. **Auto EQ** — a dynamic, per-band EQ (1024 ERB-spaced bands, gains updated
   300×/s since 5.2.0, ~94×/s before) that lifts material sitting in the masking shadow of louder
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
file ─► (background thread) long-term spectrum ─┐ (4.0 reference, or learned from the key)
key ── STFT ─► key spectrum ─┬──────────────────┤ (3.0, only while Sidechain or Learn key is on)
in ─┬─ STFT ─► perceptual model (+ Match toward the reference) ─► auto-EQ gain curve (Freeze holds it) × Paint ─┐
    │            │                                                      ├─► ×gain + harmonics ─► iSTFT ─► match ─► mix/Δ ─► output ─► ceiling ─► bypass xfade ─► out
    │            └─► heat-need map ─► harmonic synth ───────────────────┘
    └─ latency-matched dry (STFT + ceiling look-ahead) ──────────────────────────────────────────────────────────────┘
```

| Group (left → right = signal flow) | Primary | Secondary |
|---|---|---|
| AUTO EQ | Reveal, Smooth | Freeze (3.0, title row), Tilt, Response, Guard (transients) |
| HEAT | Heat | Flavour (0.2.6: Clean / Tape / Tube / Transformer / Exciter, on the title row), Color (even↔odd), Focus (body↔air) |
| SIDECHAIN (3.0) | Mode (Off / Make room / Unmask) | Amount (5.3: a compact row, shown while a mode is on) |
| REFERENCE (4.0; 5.3: under SIDECHAIN, same column) | Reference menu (Off / Learn key / Match, plus Load a reference file… and Clear) | Amount (5.3: a compact row, shown while on) |
| SHAPE (5.0) | — | Punch, Width, Mono bass (compact rows, like Tilt / Response / Guard) |
| OUTPUT | — | Listen (0.2.5: Off / Reveal only / Smooth only / Heat only), Ceiling (3.0), Mix, Output |
| Header | Preset, Latency (Low / Normal / Precise) | Undo / Redo (3.0; Ctrl+Z, Ctrl+Shift+Z / Ctrl+Y since 4.0), Paint (4.0), Match (loudness), Δ (delta listen), Bypass |
| Routing (0.2.4) | Stereo / Mid-Side in the header | EQ target and Heat target (Both / Mid / Side) in their group title rows |
| On the display | Range low / high handles (limits where Anneal acts) | Drop an audio file (4.0 reference); in paint mode, draw the EQ and Heat weights (4.0) and Width's (5.2.2) |

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
| Sidechain / key | violet `#A57BE0` (3.0) | key spectrum line, SIDECHAIN title dot, Amount ring. Annealed steel goes violet just before blue, so the key sits in the temper family |
| Reference / target | teal `#5FCDBB` (4.0) | the reference's spectrum line, REF legend tag, REFERENCE title dot, Amount ring, the Reference menu when on, the drop outline. The last temper colour, the grey-teal steel takes past light blue: the tone you are tempering toward |
| Shape (punch and stereo image) | rose `#E8849A` (5.0) | SHAPE title dot, the Punch / Width / Mono bass rings, the attack marks at the foot of the EQ lane. Outside the temper family on purpose: it shapes the sound's movement and space, not its tone |
| Paint (the user's hand) | text `#ECEEF1` (4.0) | the Paint pill when on, the weight lines and their points in paint mode. Neutral on purpose: it is not one of the processes, it is where you let them act |
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
- **Layout (5.0: 1464 × 640, resizable 1098 × 480 to 2196 × 960, fixed aspect):** header 48 px
  (wordmark, latency, undo/redo, presets centred, Stereo/Mid-Side and the Paint pill, compare cluster
  right); the display takes 56 % of the height; the deck follows the signal: AUTO EQ → HEAT →
  SIDECHAIN → REFERENCE → SHAPE → OUTPUT (dividers at x 490 / 832 / 984 / 1136 / 1288), hairlines
  between groups, no cards. The output meter is a tall bar at the right edge of the deck. (3.0 was
  1160 × 640; REFERENCE (4.0) and SHAPE (5.0) each added 152 px, the width of SIDECHAIN.)

**A calmer deck (5.3).** The user asked for a far less cluttered interface. The rule since then: a
control that does nothing in the current setting isn't shown, and text that explains rather than
informs moves to a tooltip. SIDECHAIN and REFERENCE share one column (dividers at x 490 / 832 / 1032 /
1232): each is its title, its menu and, only while its mode is on, a compact Amount row (like Tilt);
their live line ("ducking −7.2 dB", "matching −6.0 dB", "no key connected") sits beside the title in
the role colour, faint while idle. SHAPE and OUTPUT take the width this frees. The EQ / Heat routing
menus show in Mid/Side only. On the display the lane tags are bare names (AUTO EQ, WIDTH, HEAT; "Mid
solid / Side dashed" stays in Mid/Side because it's needed to read the curves), the gutter keeps
+12 / 0 dB / −12 and WIDTH's 100 %, the heat lane has no numbers, and status tags are short (ZERO
LATENCY · Heat and Listen rest; AUTO EQ FROZEN; KEY; REF with the reference's name). The display's
tooltip carries what each lane shows. Paint mode keeps its gesture line: it is needed while painting.

## Distinctive functional feature: the ERB-scale "perception screen"

The display's x-axis is the **ERB-rate (Cam) scale**, not a log-frequency axis:
horizontal space is proportional to how much of the cochlea a frequency range
occupies, which is exactly the grid the model works on. On it:

- the live auto-EQ curve in front (straw above 0 dB, blue below, ±12 dB on the right),
- input and output spectrum behind it as smooth lines over a soft wash (smoothed across
  ±0.25 Cam for display only, so tonal music reads as a flowing line, not plateaus),
- a slim **width lane** under it (5.2.2, rose): the stereo width heard at each frequency, 0 % (mono)
  at the bottom, 100 % (as is) in the middle, 200 % at the top, with the area between the middle and
  the line filled; flat and faint while Width is 100 % and Mono bass off. Behind it (5.2.3) a neutral
  shade, like the spectrum's wash, shows the stereo image heard: none where the sound is mono, brighter
  the more of it is in the sides (full where the sides are as loud as the centre),
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

**Flavour (0.2.6).** A pill menu on the HEAT title row, between the group title and the Heat
routing menu (the Heat ring sits at y 432, level with Reveal and Smooth, to leave it room). Clean is a
plain raised pill ("Flavour: clean"); any other flavour shows its name tinted bronze, the Heat role
colour, like Listen's solos. The harmonic bars under Color are drawn with the synth's own flavour law,
for a 120 Hz partial (Transformer's drive depends on the note) or a 4 kHz one for Exciter, and the
tooltip says so.

**Sidechain, Freeze, Ceiling (3.0).** The key's spectrum is a thin violet line on the EQ lane, drawn
only while a key is arriving, with a KEY legend under AUTO EQ. Freeze is a pill on the AUTO EQ title
row, tinted straw when on, and the display then shows an "AUTO EQ FROZEN" tag so a held curve is never
mistaken for a live one. The Ceiling's reduction is a short bronze bar and number under the meter's
peak, holding its deepest value for 1.5 s so a brief catch is readable.

**Zero latency (5.1).** The Latency menu gets a first entry, *Zero 0.0 ms*, with what rests in
brackets. When it is on, the pill reads 0.0 ms, the display adds a ZERO LATENCY tag under AUTO EQ
("minimum-phase filters, no delay; Punch on whole attacks; Heat and Listen need Low or more" since 5.2.7,
or "Heat, Punch and Listen need Low or more" with *Punch at Zero* off), the heat lane stays flat
and the Heat ring's live line says "off at zero latency". Nothing is hidden or greyed: the knobs keep
their settings for when a latency mode comes back.

**Spread load (5.2.1).** The last entry of the Latency menu, under a separator, is a tickable switch:
*Spread load +3.3 ms (even CPU at small buffers)*, with "; rests at Zero" while Zero is on. It works on
top of the mode, so the pill simply reads the longer latency (Low 26.1 ms instead of 22.7 ms at 48 kHz) and the
modes in the menu list theirs with it. Nothing else on screen changes: the sound is the same.

**Paint the width (5.2.2).** The display has three lanes: EQ (208 px), WIDTH (28 px) and HEAT (46 px),
8 px apart (the EQ lane was 236 px and HEAT 52 px). WIDTH is a rose line of the width actually heard:
Width by its painted share, then Mono bass, the same formula as the engine, with "100 %" in the right
gutter at its middle. Its tag reads "WIDTH · stereo width as is" while nothing changes the width, and
just "WIDTH" otherwise (a grey note over the rose fill wouldn't read). In paint mode it takes Width's
paint like the other two lanes take theirs (the tag then reads "how much of Width acts", on a backing),
and painted-out regions dim there. The Range doesn't dim it:
Width acts at every frequency. The hover read-out adds "width 150 %" wherever the width heard isn't
100 %, and "paint EQ / heat / width" shares as before. Since 5.2.3 it also says how stereo the sound
is there: "sides −8 dB" (the sides' level against the centre) or "sides mono" (40 dB or more under it),
and the tag at rest reads "as is; shading = how much is in the sides".

**SHAPE and presets (5.0).** SHAPE is three compact rows in rose: Punch (bipolar ring, its inner
arc shows how hard the latest attack is shaped; drawn since 5.2.7, also at zero latency), Width and Mono bass. With Punch or Guard on, the
display draws a short rose bar at the foot of the EQ lane in each band where an attack is happening
(height and opacity grow with its strength, fading over ~0.1 s), so you see what they act on without
another lane. (5.2.4: a preset file held over the preset bar outlines it in a dashed line with "DROP TO
ADD THE PRESET"; afterwards the bar shows "Added “…”", or why a file wasn't taken, for 4 s.) The preset
menu keeps the factory list first; your presets follow under YOUR PRESETS,
then *Save current settings as…*, *Delete “…”* (only when one of yours is chosen) and *Open the
presets folder*. Saving asks for a name in a small dialog; deleting asks first.

**Reference, Paint, shortcuts (4.0).** The REFERENCE group mirrors SIDECHAIN: a pill menu (Off is a
plain raised pill, "Reference: off"; Learn key and Match are tinted teal) over a teal Amount ring
whose live line says what is happening ("learned 12 s", "matching −2.4 dB", "no reference yet",
"analysing the file"). The menu also has *Load a reference file…* and *Clear the reference*. On the
display the reference is a teal line with a soft halo, drawn on the spectrum scale where the output
would sit at the same normalised level, only while Reference is Learn key or Match; a REF tag in the
legend names it (the file, or "learning the sidechain key — 12 s"), or says what to do, or why a
dropped file wasn't used (in the warning red, for 6 s). Dragging a file over the display outlines it
in dashed teal with "DROP TO USE AS THE REFERENCE". The legend's tags stack (AUTO EQ, KEY, REF, PAINT)
and the hover read-out sits under them.

Paint is a header pill (neutral white when on). In paint mode each lane shows its weight as a white
line through 64 points (top of the lane = 100 %, bottom = none) and a PAINT tag explains the gestures:
drag to paint, Alt- or right-drag to erase, double-click to reset the lane; the range handles are then
taken by their grips above the plot only. With paint mode off the lines go, but painted-out regions
stay dimmed exactly like outside the Range (the same screen colour at 72 % × (1 − weight)), so a
painted project never hides it. The hover read-out adds "paint EQ 40 % / heat 100 % / width 100 %" wherever a
weight is below 100 %. Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y (Cmd on macOS) undo and redo once the editor
has focus (click it), and Escape leaves paint mode.

In Mid/Side mode the centre and sides are analysed independently. The EQ curve is solid for Mid
and dashed for Side, with the same straw-above / blue-below colour convention. Hover reports
both gains. Input/output spectra and the heat lane keep showing the combined stereo energy.
