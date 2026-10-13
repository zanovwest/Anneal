# Anneal roadmap

Every daily update does two things:

1. **Bug pass.** It looks for real bugs (code review, the tests, the benchmark, the rendered UI) and
   fixes every one it can prove with a test.
2. **One item** from **Next up**: the first it can finish, tested, in one run. That item then moves
   to **Done** with the version.

Add ideas anywhere in the list and reorder freely. Never add anything listed under **Not wanted**.

## Next up

1. **Low-latency mode**: a "Latency: Normal / Low" switch (N 4096 → 1024 at 48 kHz, ~21 ms) for
   tracking. It reports the new latency to the host.
2. **Mid/Side mode**: Stereo (linked) / Mid-Side. Heat on Mid only and Reveal on Side, or both.
3. **Saturation flavours**: Tape (soft, mostly even, gentle HF roll-off), Tube (2nd-heavy),
   Transformer (odd, low end), Exciter (air only). A flavour menu next to Color.
4. **Transient guard**: protect attacks from the smoothing and from the harmonic smear (pre-echo).
5. **Freeze curve**: hold the current auto-EQ curve as a static EQ.
6. **Listen modes**: solo what Reveal adds, what Smooth removes, or only the harmonics.
7. **Sidechain unmask mode**: lift what a sidechain key masks, or dip the bands the key needs.
8. **True-peak output ceiling** (a look-ahead limiter, as in Morphfall) so Heat can't clip.
9. **CPU under the 3 % budget** (now 3.3 %): SIMD FFT (PFFFT) and batched log/exp, as in
    Morphfall (8.8 % → 1.5 %).
10. **Undo/redo** for parameter changes, in the header.
11. **Icon** for the plug-in bundle, the standalone app and the setup.
12. **Spread the frame work across blocks** (optional): each STFT frame (~0.35 ms) lands in one host
   block. That's fine for Anneal alone (28 % of a 64-sample block), but spreading it over the hop
   would leave more headroom for other plugins at tiny buffers. Costs one hop of latency, so only
   do it as a switch.

## Not wanted

The user said no to these on 2026-10-13. Don't add them.

- Auto Heat: the model setting Heat by itself.
- A/B compare.

## Done

- 0.1.1 (2026-10-13): factory presets (header menu + host programs). Fixed: old audio after a
  host reset. Checked: the "64-sample overrun" was the benchmark measuring Windows interruptions,
  not Anneal (slowest 0.1 % of blocks: 28 % of the deadline).
- 0.1.0 (2026-10-13): first GitHub release; Windows setup that installs just for you (no admin)
  or for all users; updates install while a DAW has Anneal loaded.
