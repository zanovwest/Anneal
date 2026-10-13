# Changelog

Newest first. One entry per version; the daily update adds one (see README → Daily updates).
The plugin version shown in the DAW and in the setup's name comes from `project(... VERSION)` in
CMakeLists.txt.

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
