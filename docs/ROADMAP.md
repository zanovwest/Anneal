# Anneal roadmap

Every hourly update does two things:

1. **Bug pass.** It looks for real bugs (code review, the tests, the benchmark, the rendered UI) and
   fixes every one it can prove with a test.
2. **One item** from **Next up**: the first it can finish, tested, in one run. That item then moves
   to **Done** with the version.

Add ideas anywhere in the list and reorder freely. Never add anything listed under **Not wanted**.

## Next up

Order set by the user on 2026-10-13: Transient guard, Mid/Side, Listen modes, Saturation flavours,
Sidechain unmask, then the rest. (The icon was done by hand that day, outside the hourly runs.)

1. **Transient guard**: protect attacks from the smoothing and from the harmonic smear (pre-echo).
2. **Mid/Side mode**: Stereo (linked) / Mid-Side. Heat on Mid only and Reveal on Side, or both.
3. **Listen modes**: solo what Reveal adds, what Smooth removes, or only the harmonics.
4. **Saturation flavours**: Tape (soft, mostly even, gentle HF roll-off), Tube (2nd-heavy),
   Transformer (odd, low end), Exciter (air only). A flavour menu next to Color.
5. **Sidechain unmask mode**: lift what a sidechain key masks, or dip the bands the key needs.
6. **Fix (setup): uninstall while a DAW has Anneal loaded.** Install moves a loaded DLL aside, but
   uninstall doesn't, so the locked DLL and its folders stay behind. Do the same move-aside in
   `CurUninstallStepChanged (usUninstall)` and extend TestSetup to uninstall while the DLL is held.
7. **Freeze curve**: hold the current auto-EQ curve as a static EQ.
8. **True-peak output ceiling** (a look-ahead limiter, as in Morphfall) so Heat can't clip.
9. **CPU under the 3 % budget in Precise** (now 3.3 %; Low is 2.4 %): SIMD FFT (PFFFT) and
   batched log/exp, as in Morphfall (8.8 % → 1.5 %).
10. **Undo/redo** for parameter changes, in the header.
11. **Spread the frame work across blocks** (optional): each STFT frame lands in one host block.
    That's fine for Anneal alone (Low: 10 % of a 64-sample block), but spreading it over the hop
    would leave more headroom for other plugins at tiny buffers. Costs one hop of latency, so only
    as a switch.

## Not wanted

The user said no to these on 2026-10-13. Don't add them.

- Auto Heat: the model setting Heat by itself.
- A/B compare.

## Done

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
