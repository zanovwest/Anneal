# Anneal roadmap

Every versioned update does two things:

1. **Bug pass.** It looks for real bugs (code review, the tests, the benchmark, the rendered UI) and
   fixes every one it can prove with a test.
2. **One item** from **Next up**: the first it can finish, tested, in one run. That item then moves
   to **Done** with the version.

Add ideas anywhere in the list and reorder freely. Never add anything listed under **Not wanted**.

## Next up

Order set by the user on 2026-10-13: Transient guard (done in 0.2.2), Mid/Side (0.2.4), Listen
modes (0.2.5), Saturation flavours (0.2.6), then Sidechain, Freeze, Ceiling and Undo, which became
the 3.0 update. (The icon was done by hand that day, outside the hourly runs.)

8. **CPU under the 3 % budget in Precise** (3.4 % in 3.0; Low is 2.5 %): SIMD FFT (PFFFT) and
   batched log/exp, as in Morphfall (8.8 % → 1.5 %).
10. **Spread the frame work across blocks** (optional): each STFT frame lands in one host block.
    That's fine for Anneal alone (Low: 10 % of a 64-sample block), but spreading it over the hop
    would leave more headroom for other plugins at tiny buffers. Costs one hop of latency, so only
    as a switch.
11. **Show the guard on the display**: a faint mark in the bands where Guard is holding an attack,
    and a live line under the Guard knob, so you can see it working.

## Waiting on the user

These need the user's own accounts or decisions. Daily updates don't take them.

- **Public downloads, KVR listing, Plugin Alliance**: making releases public, and licence choices
  (JUCE, VST3). See docs/DISTRIBUTION.md. The user will do KVR later (2026-10-13).
- **Pro Tools (AAX)**: Avid developer account and AAX SDK, plus PACE signing (iLok).

## Not now

The user is on Windows and dropped the Mac plans on 2026-10-13 ("im on windows i thought mac
coding was free"). The setup stays in the repo, untouched, in case that changes. Keep the code
portable, but don't work on these:

- **Switch on the macOS / Linux / CLAP workflow**: it needs the gh login's `workflow` scope
  (`gh auth refresh -h github.com -s workflow`), then `packaging/ci/platforms.yml` moves to
  `.github/workflows/`.
- **Notarized macOS builds**: Apple Developer Program membership (99 USD a year).

## Not wanted

The user said no to these on 2026-10-13. Don't add them.

- Auto Heat: the model setting Heat by itself.
- A/B compare.

## Done

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
