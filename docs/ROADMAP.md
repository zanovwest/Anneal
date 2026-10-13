# Anneal roadmap

Every daily update does two things:

1. **Bug pass.** It looks for real bugs (code review, the tests, the benchmark, the rendered UI) and
   fixes every one it can prove with a test.
2. **One item** from **Next up**: the first it can finish, tested, in one run. That item then moves
   to **Done** with the version.

Add ideas anywhere in the list and reorder freely. Never add anything listed under **Not wanted**.

## Next up

1. **Fix: overruns at small buffer sizes.** At 48 kHz with 64-sample buffers, the worst block takes
   2.2 ms of a 1.33 ms deadline (AnnealTests benchmark), because a whole STFT frame lands in one
   block. Spread the frame work across the hop, or run the model every other frame, so 64- and
   128-sample buffers never overrun. Add a test that fails above 70 % of the deadline.
2. **Factory presets** (Mix bus glue, Master polish, Vocal air, 808 weight, Drum bus snap,
   Lo-fi warmth, Gentle) in a preset menu in the header, plus host programs.
3. **Low-latency mode**: a "Latency: Normal / Low" switch (N 4096 → 1024 at 48 kHz, ~21 ms) for
   tracking. It reports the new latency to the host.
4. **Mid/Side mode**: Stereo (linked) / Mid-Side. Heat on Mid only and Reveal on Side, or both.
5. **Saturation flavours**: Tape (soft, mostly even, gentle HF roll-off), Tube (2nd-heavy),
   Transformer (odd, low end), Exciter (air only). A flavour menu next to Color.
6. **Transient guard**: protect attacks from the smoothing and from the harmonic smear (pre-echo).
7. **Freeze curve**: hold the current auto-EQ curve as a static EQ.
8. **Listen modes**: solo what Reveal adds, what Smooth removes, or only the harmonics.
9. **Sidechain unmask mode**: lift what a sidechain key masks, or dip the bands the key needs.
10. **True-peak output ceiling** (a look-ahead limiter, as in Morphfall) so Heat can't clip.
11. **CPU under the 3 % budget** (now 3.3 %): SIMD FFT (PFFFT) and batched log/exp, as in
    Morphfall (8.8 % → 1.5 %).
12. **Undo/redo** for parameter changes, in the header.
13. **Icon** for the plug-in bundle, the standalone app and the setup.

## Not wanted

The user said no to these on 2026-10-13. Don't add them.

- Auto Heat: the model setting Heat by itself.
- A/B compare.

## Done

- 0.1.0 (2026-10-13): first GitHub release; Windows setup that installs just for you (no admin)
  or for all users; updates install while a DAW has Anneal loaded.
