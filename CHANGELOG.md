# Changelog

Newest first. One entry per version; the daily update adds one (see README → Daily updates).
The plugin version shown in the DAW and in the setup's name comes from `project(... VERSION)` in
CMakeLists.txt.

## 0.1.1 — 2026-10-13

New
- **Factory presets.** A preset menu in the header (click the name for the list, or step with the
  arrows): Default, Gentle, Mix bus glue, Master polish, Vocal air, 808 weight, Drum bus snap and
  Lo-fi warmth. They also appear in FL Studio's own preset menu for the plugin. When you change a
  knob after loading one, the name gets a `*`. Presets leave Match, Delta and Bypass alone. Projects
  remember which preset they came from, and loading a project never overwrites your knobs with the
  preset.

Fixes
- After the DAW paused or stopped Anneal, the next playback began with about 85 ms of old audio
  from where it had stopped. Anneal now clears its buffers when the host resets it (old audio after
  a reset: −16 dB before, silence now).
- Benchmark: the "overrun at 64-sample buffers" in 0.1.0 was Windows briefly interrupting the
  test, not Anneal. The benchmark now runs at high priority, like a DAW's audio thread, and
  reports percentiles. Anneal's slowest 0.1 % of blocks take 0.37 ms of the 1.33 ms available at
  64 samples (28 %), so small buffers are fine.

Tests: 31/31 DSP checks, 11/11 host checks, 16/16 setup checks. CPU 3.4 % of one core at 48 kHz
stereo (unchanged).

## 0.1.0 — 2026-10-13

First release on GitHub, with a Windows setup (`Anneal-0.1.0-Setup.exe`).

- Automatic perceptual EQ on 1024 ERB bands (~94 updates/s at 48 kHz): **Reveal** lifts masked
  bands, **Smooth** cuts dominant ones; Tilt, Response and a draggable Range.
- Spectral saturation (**Heat**, Color even↔odd, Focus body↔air): harmonics 2nd–6th written per
  spectral peak, so no aliasing and no intermodulation.
- Output: Mix, Output, loudness Match, Delta listen, click-free bypass.
- Setup: "just for me" (no admin; VST3 in `%LOCALAPPDATA%\Programs\Common\VST3`) or "all users"
  (`C:\Program Files\Common Files\VST3`), plus the standalone app and an uninstaller. Updates
  install while a DAW has Anneal loaded.
- Tests: 26/26 DSP checks, 9/9 host checks on the built and the installed VST3, 16/16 setup
  checks. CPU 3.3 % of one core at 48 kHz stereo.
