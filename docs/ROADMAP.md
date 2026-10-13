# Anneal roadmap

The daily update takes the first item under **Next up** it can finish, tested, in one run (or a bug
it finds, which comes first), then moves the item to **Done** with the version. Add ideas anywhere
in the list; reorder freely.

Each item is one shippable change: a new feature, a new mode, or a fix.

## Next up

1. **Fix: overruns at small buffer sizes.** At 48 kHz / 64-sample buffers the worst block takes
   2.2 ms of a 1.33 ms deadline (AnnealTests benchmark), because a whole STFT frame lands in one
   block. Spread the frame work across the hop, or move the model to every other frame, so 64 and
   128-sample buffers never overrun. Add a test that fails above 70 % of the deadline.
2. **Factory presets** (Mix bus glue, Master polish, Vocal air, 808 weight, Drum bus snap,
   Lo-fi warmth, Gentle) in a preset menu in the header, plus host programs.
3. **Low-latency mode**: a "Latency: Normal / Low" switch (N 4096 → 1024 at 48 kHz, ~21 ms) for
   tracking. Reports the new latency to the host.
4. **Mid/Side mode**: Stereo (linked) / Mid-Side, with Heat on Mid only and Reveal on Side, or both.
5. **Saturation flavours**: Tape (soft, mostly even, gentle HF roll-off), Tube (2nd-heavy),
   Transformer (odd, low end), Exciter (air only). A flavour menu next to Color.
6. **Auto Heat**: an "Auto" toggle where the model sets Heat from how thin the spectrum is.
7. **A/B compare and undo/redo** in the header.
8. **Transient guard**: protect attacks from the smoothing and the harmonic smear (pre-echo).
9. **Freeze curve**: hold the current auto-EQ curve as a static EQ.
10. **Listen modes**: solo what Reveal adds, what Smooth removes, or only the harmonics.
11. **Sidechain unmask mode**: lift what a sidechain key masks, or dip the bands the key needs.
12. **True-peak output ceiling** (look-ahead limiter, as in Morphfall) so Heat can't clip.
13. **CPU under the 3 % budget** (now 3.3 %): SIMD FFT (PFFFT), batched log/exp, as in Morphfall
    (8.8 % → 1.5 %).
14. **Icon** for the plug-in bundle, the standalone app and the setup.

## Done

- 0.1.0 (2026-10-13): first GitHub release, Windows setup, update-safe installer.
