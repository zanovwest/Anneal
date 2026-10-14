# Anneal roadmap

Every versioned update does two things:

1. **Bug pass.** It looks for real bugs (code review, the tests, the benchmark, the rendered UI) and
   fixes every one it can prove with a test.
2. **One item** from **Next up**: the first it can finish, tested, in one run. That item then moves
   to **Done** with the version.

Add ideas anywhere in the list and reorder freely. Never add anything listed under **Not wanted**.

## Next up

Order set by the user on 2026-10-13: Transient guard (done in 0.2.2), Mid/Side (0.2.4), Listen
modes (0.2.5), Saturation flavours, Sidechain unmask, then the rest. (The icon was done by hand that
day, outside the hourly runs.)

3. **Saturation flavours**: Tape (soft, mostly even, gentle HF roll-off), Tube (2nd-heavy),
   Transformer (odd, low end), Exciter (air only). A flavour menu next to Color.
4. **Sidechain unmask mode**: lift what a sidechain key masks, or dip the bands the key needs.
6. **Freeze curve**: hold the current auto-EQ curve as a static EQ.
7. **True-peak output ceiling** (a look-ahead limiter, as in Morphfall) so Heat can't clip.
8. **CPU under the 3 % budget in Precise** (now 3.4 %; Low is 2.4 %): SIMD FFT (PFFFT) and
   batched log/exp, as in Morphfall (8.8 % → 1.5 %).
9. **Undo/redo** for parameter changes, in the header.
10. **Spread the frame work across blocks** (optional): each STFT frame lands in one host block.
    That's fine for Anneal alone (Low: 10 % of a 64-sample block), but spreading it over the hop
    would leave more headroom for other plugins at tiny buffers. Costs one hop of latency, so only
    as a switch.
11. **Show the guard on the display**: a faint mark in the bands where Guard is holding an attack,
    and a live line under the Guard knob, so you can see it working.

## Not wanted

The user said no to these on 2026-10-13. Don't add them.

- Auto Heat: the model setting Heat by itself.
- A/B compare.

## Done

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
