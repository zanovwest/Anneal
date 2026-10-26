# Anneal roadmap

Every versioned update does two things:

1. **Bug pass.** It looks for real bugs (code review, the tests, the benchmark, the rendered UI) and
   fixes every one it can prove with a test.
2. **One item** from **Next up**: the first it can finish, tested, in one run. That item then moves
   to **Done** with the version.

Add ideas anywhere in the list and reorder freely. Never add anything listed under **Not wanted**.

## Next up

Linux support and the KVR listing were completed in 4.0.1 at the user's request on 2026-10-15.
The one-minute Codex schedule is paused. The remaining feature ideas below are unchanged.

Order set by the user on 2026-10-13: Transient guard (done in 0.2.2), Mid/Side (0.2.4), Listen
modes (0.2.5), Saturation flavours (0.2.6), then Sidechain, Freeze, Ceiling and Undo, which became
the 3.0 update. (The icon was done by hand that day, outside the hourly runs.)

17. **Loudness readout**: short-term and integrated loudness (LUFS, EBU R128 K-weighting) of the input
    and the output under the output meter, reset with a click, so you can see what Anneal does to the
    level as well as hear it (the Match gain shown next to it). Cheap on the audio thread (two biquads
    and 100 ms sums), shown through the display frame; tested against the EBU reference signals.
18. **Max cut / Max boost**: limits for how far the auto EQ may move (for example cut no more than 6 dB,
    boost no more than 3 dB), in AUTO EQ, defaulting to today's limits so projects sound the same.
    Also shown as a band on the display's EQ lane. Tests: a ring that Smooth cuts 9 dB stops at the
    limit, Reveal's lift likewise, defaults bit-identical.

## Waiting on the user

These need the user's own accounts or decisions. Daily updates don't take them.

- **Plugin Alliance and source/licence choices** (JUCE, VST3): see docs/DISTRIBUTION.md.
  KVR now hosts the free Windows/Linux downloads; the GitHub repository remains private.
- **Pro Tools (AAX)**: Avid developer account and AAX SDK, plus PACE signing (iLok).
- **Zero latency at 150 filter designs a second** (from 300): about 0.5 % of a core less in Stereo
  and 1 % in Mid/Side at zero latency, but the EQ there would then follow the music 150 times a second
  instead of the 300 you asked for in 5.2.0. Say if you want it, as the default or as a switch. (The
  other idea, a design transform half as long, was measured in 5.2.6 and dropped: 0.04-0.08 dB off on
  music curves, up to 19 dB next to Mono bass.)

## Not now

The user is on Windows and dropped the Mac plans on 2026-10-13 ("im on windows i thought mac
coding was free"). The setup stays in the repo, untouched, in case that changes. Keep the code
portable, but don't work on these:

- **Switch on the combined macOS / CLAP workflow**: it needs the gh login's `workflow` scope
  (`gh auth refresh -h github.com -s workflow`), then `packaging/ci/platforms.yml` moves to
  `.github/workflows/`.
  Linux now has its own `.github/workflows/linux.yml`; this does not enable the parked Mac jobs.
- **Notarized macOS builds**: Apple Developer Program membership (99 USD a year).

## Not wanted

The user said no to these on 2026-10-13. Don't add them.

- Auto Heat: the model setting Heat by itself.
- A/B compare.

## Done

- 5.3.0 (2026-10-25, asked for by the user that evening: "make the ui way less cluttered"): **a calmer
  interface**: SIDECHAIN over REFERENCE in one column with compact Amount rows shown only while their
  mode is on and their live line beside the title; the Mid/Side routing menus only in Mid/Side; the
  display's lane notes moved to its tooltip, fewer scale numbers, shorter status tags.
- 5.2.7 (2026-10-25, daily update): **Punch at zero latency** (item 16): a time-domain transient
  shaper after the zero-latency filters (a fast and a slow envelope, Stereo linked or Mid and Side by
  the EQ routing, inside Range with first-order edges that never turn a boost into a cut). 2 kHz
  plucks: attacks +6.2 / −6.0 dB, body 0.001 dB; Punch 0 bit-identical. A *Punch at Zero* switch in
  the Latency menu: on for new instances, off for projects from before 5.2.7 (they sound as made).
  Fixed: the Punch ring's inner arc was never drawn.
- 5.2.6 (2026-10-24, daily update): **Zero latency lighter, part 2** (item 15): the old filters stop
  when the 1.33 ms crossfade ends instead of running the whole hop (the same output, bit for bit), and
  the design's cepstrum and phase come from half-size transforms (a DCT and a DST through 2048-point
  FFTs) with the curve's own magnitude. One design 33.3 → 26.0 µs; Zero latency Stereo 3.7 → 3.5 %
  (3.3 % at 512 samples), Mid/Side 6.5 → 6.1 %; within 0.0008 dB of 5.2.5's above −40 dB. The
  2 × taps transform failed its 0.01 dB test; 150 designs a second waits on the user.
- 5.2.5 (2026-10-24, daily update): **Zero latency lighter, part 1** (item 15): the filter design's log,
  exp, sin and cos batched through `fastmath` (a new SSE2 sin / cos), the fade as a table. One design
  41.5 → 32.5 µs; Zero latency Stereo 4.0 → 3.7 %, Mid/Side 7.1 → 6.5 %; within 0.000006 dB of the
  exact math. (The roadmap's "cos / sin from a recurrence" didn't fit: the phases aren't evenly spaced.)
- 5.2.4 (2026-10-24, daily update, catch-up for 2026-10-23): **Import and export presets** (item 14):
  `.annealpreset` files dropped on the preset bar are checked and copied into your presets folder (a
  single one is loaded; no duplicates; a clash becomes "Name 2"), and the menu copies the folder's path.
- 5.2.3 (2026-10-22, daily update, done by hand that evening: "do todays entry"): **the stereo image
  on the display** (item 13): the WIDTH lane shades behind its curve by how much of the sound heard is
  in the sides at each frequency (the input's Mid and Side through the EQ curves, Width and Mono
  bass, 37.5 times a second), and the hover read-out says "sides −8 dB" or "mono".
- 5.2.2 (2026-10-21, daily update, asked for by the user that evening: "do 2.2 cause that should be
  todays update"): **Paint the width** (item 12): a WIDTH lane on the display between EQ and Heat that
  shows the stereo width heard per frequency (Width, its paint, Mono bass) and takes Width's paint in
  paint mode. Width acts by its painted share in every latency mode and at zero latency; saved in the
  project. The EQ lane is 28 px shorter for it.
- 5.2.1 (2026-10-21, daily update): **Spread load** (item 10), a switch in the Latency menu: each
  frame's work in four stages run through the next hop instead of in one block, for one hop more
  latency (3.3 ms). The output is the same one hop later, sample for sample; the slowest 0.1 % of
  64-sample blocks in Precise Mid/Side 21 → 11 % of the deadline (32 samples: 42 → 16 %).
- 5.2.0 (2026-10-20, the user: "dont change anneal 5.1.1 but make it 300/s instead of 94/s"):
  **300 EQ updates a second** in every latency mode (hop = sampleRate / 300, was N/8 and N/4), with a
  per-position overlap-add gain so hops that don't divide N stay exact. CPU about 3.4× in Normal and
  Precise (Stereo Precise 4.5 %, Mid/Side Precise 7.8 %); the CPU item can win some of it back.
- 5.1.1 (2026-10-19, daily update): **CPU under the 3 % budget in Mid/Side Precise too** (item 8,
  part 2 of the CPU item): the model's dB conversions batched through Anneal's own vectorised log /
  exp (`Source/dsp/FastMath`), within 0.000004 dB of 5.1.0's curve; the stereo harmonic generator
  skipped in full Mid/Side, where its output was always replaced. Mid/Side Low 2.2 %, Normal 1.5 %,
  Precise 2.2 %; Stereo Low 1.1 %, Normal 0.8 %, Precise 1.3 %.
- 5.1.0 (2026-10-18, the user asked for it right after 5.0.0): **Zero latency**: the Low engine
  analyses, the curve is applied through minimum-phase FIR filters (cepstral design every frame,
  crossfaded), Width / Mono bass through the Side filter; Heat, Punch and Listen rest; Ceiling
  without look-ahead.
- 5.0.0 (2026-10-18, a major update the user asked for): **SHAPE** group with **Punch** (spectral
  transient shaper, ±8 dB on attacks only), **Width** (0–200 %, per bin, linear phase) and **Mono
  bass** (Off / 20–400 Hz); **attack marks** on the display (item 11, "show the guard"); **your own
  presets** (save, delete, folder, remembered by projects). Window 1464 × 640.
- 4.0.2 (2026-10-18): **Faster engine**, part 1 of the CPU item: Anneal's own real FFT (N/2-point
  split radix-4 Stockham, JUCE's layout and scaling) instead of JUCE's portable fallback, ~4×
  faster per transform. Stereo Low 2.0 → 1.7 %, Normal 1.8 → 1.2 %, Precise 2.7 → 1.8 %; Mid/Side
  Precise 4.3 → 3.6 %. Written in-house rather than adding PFFFT: the daily runs add no libraries,
  and the KVR binaries would need its licence notice. Fixed: a frozen curve and the loudness-match
  gain were lost when the host prepared Anneal again (new buffer size or rate, many exports).
- 4.0.1 (2026-10-15): **Linux support**: native Ubuntu 24.04 VST3, CLAP, LV2 and Standalone
  builds, local build/package scripts, checksum archives and correct VST3 copy defaults.
  Fixed the Linux installer's deletion of the previous plugin on a copy failure. The headless
  desktop includes Openbox; the test host initialises Xlib and detaches the editor before
  destroying its parent window. Passed 112 acceptance, 20 VST3 host, 10 CLAP and 23 packaging
  checks, plus LV2 metadata/dependency inspection and Linux editor renders. Windows regression:
  112 acceptance, 20 host and 25 setup checks. **KVR listing** with free Windows/Linux downloads
  and current screenshots: https://www.kvraudio.com/product/anneal-by-qulic.

- 4.0.0 (2026-10-14), the major update the user asked for on 2026-10-13/14:
  - **Reference match**: learn a reference's long-term ERB spectrum from the sidechain key (Learn
    key) or from an audio file dropped on the display (or loaded from the Reference menu), and steer
    the auto EQ toward it (Match, with Amount; ±6 dB, smoothed ±1.5 Cam). New REFERENCE group, teal
    line on the display, saved in the project.
  - **Paint where it acts**: in paint mode, draw how much of the auto EQ and of Heat acts in each
    region, directly on the display's two lanes (64 points each, saved in the project; each stroke
    one undo step).
  - **Keyboard shortcuts**: Ctrl+Z undo, Ctrl+Shift+Z / Ctrl+Y redo (Cmd on macOS), Escape leaves
    paint mode.
  - Fixed: a frozen curve was lost when the host stopped and started or the latency mode changed;
    switching the Ceiling on again could dip quiet audio for ~80 ms.
- 3.1.0 (2026-10-13): **CLAP for Windows** (zip on the release, ClapCheck 10/10); macOS universal
  (AU / VST3 / CLAP) and Linux (VST3 / LV2 / CLAP) build setup, workflow and install scripts (not
  running yet, see above); platform fonts; docs/DISTRIBUTION.md (DAW matrix, KVR kit).
- 3.0.1 (2026-10-13): fixed the DLL's Windows file version (said 0.2.1 since 0.2.1); `build.ps1`
  now checks it.
- 3.0.0 (2026-10-13): **Sidechain** Make room / Unmask with Amount and a violet key line;
  **Freeze**; true-peak **Ceiling** (Off / 0.0 / −0.3 / −1.0 dBTP, 4× detection, 1.25 ms look-ahead,
  always in the latency); **Undo/Redo** in the header; 1160 × 640 layout with a SIDECHAIN group;
  ceiling reduction on the output meter. Fixed: whole-dB hover read-outs showed four decimals.
- 0.2.6 (2026-10-13): **Saturation flavours** (Flavour menu on the HEAT title row): Clean (as
  before), Tape (soft, mostly even, harmonics rolled off above 5 kHz), Tube (strong 2nd),
  Transformer (odd, driven by the low end), Exciter (air only: nothing from partials below
  1.5 kHz). Only the harmonics change; the bars under Color follow the flavour; older projects and
  factory presets stay Clean. Fixed: double-clicking a Range handle sent the host a nested
  begin-edit for the same parameter.
- 0.2.5 (2026-10-13): **Listen modes** (Listen menu in OUTPUT): Reveal only (what Reveal lifts),
  Smooth only (what Smooth cuts), Heat only (just the harmonics). Exact split, tested: normal −
  Reveal solo + Smooth solo = the input, normal − Heat solo = the input, in Stereo, Mid/Side and
  every latency mode. Match holds while soloing; old projects open with Listen off. Fixed: the
  Heat routing menu sat 4 px above the EQ routing menu.
- 0.2.4 (2026-10-13): **Mid/Side mode.** Independent centre/sides analysis, separate EQ and
  Heat routing (Both / Mid / Side), solid Mid and dashed Side curves. Routing automation keeps
  latency, old projects keep linked Stereo, and mono falls back to linked mono. Tested with
  57 acceptance checks, 14 built-VST3 host checks and 25 isolated installer checks.
- 0.2.3 (2026-10-13): **Uninstall while a DAW has Anneal loaded.** Move the held DLL aside
  using the recorded install destination; a later install cleans it after release. Installer
  regression covers the held-DLL uninstall and custom folder (25/25 sandbox checks).
- 0.2.3 (2026-10-13): **Range handles after crossed automation.** Dragging Low starts from
  the displayed range and preserves the displayed High limit; a click without dragging leaves
  the parameters alone. Five interaction regression checks added (45/45 acceptance checks).
- 0.2.2 (2026-10-13): **Transient guard** (Guard, in AUTO EQ): attacks pass the auto EQ and get no
  smeared harmonics; the curve doesn't chase hits. Fixed: with crossed Range automation the
  display showed a different range from the one the engine used.
- 0.2.1 (2026-10-13): icon for the setup, the standalone app and the VST3 bundle (done by hand).
- 0.2.0 (2026-10-13): new interface (cool graphite, ring knobs with live activity, one large
  display with a smooth spectrum and glowing EQ curve, harmonic bars, pill buttons); latency
  modes Low 21 ms (new default) / Normal 43 ms / Precise 85 ms. Fixed: Smooth stopped cutting
  during development (name clash), caught by the tests before release.
- 0.1.1 (2026-10-13): factory presets (header menu + host programs). Fixed: old audio after a
  host reset. Checked: the "64-sample overrun" was the benchmark measuring Windows interruptions,
  not Anneal (slowest 0.1 % of blocks: 28 % of the deadline).
- 0.1.0 (2026-10-13): first GitHub release; Windows setup that installs just for you (no admin)
  or for all users; updates install while a DAW has Anneal loaded.
