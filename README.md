# Anneal

A VST3 effect for Windows: an **automatic perceptual EQ** combined with a **spectral
saturator**. Think "saturating EQ" (SweetEQ-style colour) steered by a hearing model
(Gullfoss-style Recover/Tame behaviour), with the saturation itself done in the FFT
domain so it never aliases and never intermodulates.

- 1024 analysis bands spaced on the ERB-rate (cochlear) scale, gains updated ~94×/s.
- **Reveal** lifts bands sitting in the masking shadow of louder neighbours.
- **Smooth** pulls down bands that dominate their auditory neighbourhood.
- **Heat** grows a 2nd–6th harmonic series from every spectral peak, phase-locked to its
  fundamental, and writes it only where the model says the spectrum is thin.

Design brief: [docs/DESIGN.md](docs/DESIGN.md) · Engine plan and test criteria: [docs/ENGINE.md](docs/ENGINE.md) ·
What's next: [docs/ROADMAP.md](docs/ROADMAP.md) · What changed: [CHANGELOG.md](CHANGELOG.md)

## Install

Download `Anneal-<version>-Setup.exe` from the repository's **Releases** and run it. It asks how to
install:

- **Just for me** (recommended, no administrator rights): the VST3 goes into
  `%LOCALAPPDATA%\Programs\Common\VST3`, the standalone app into `%LOCALAPPDATA%\Programs\Anneal`.
  Updates never need approval, so the daily update installs them by itself, even at night.
  FL Studio doesn't look in that folder by default: once, open *Options → Manage plugins*, add
  `C:\Users\<you>\AppData\Local\Programs\Common\VST3` under *Plugin search paths*, then click
  *Find installed plugins*. Cubase scans it already.
- **All users** (administrator approval): the VST3 goes into `C:\Program Files\Common Files\VST3`,
  which FL Studio scans by default, the app into Program Files. Every later update asks for approval
  again.

Both add the standalone app (optional, Start menu → Anneal) and an uninstaller (Settings → Apps →
Anneal). Updates can be installed while FL Studio is open, even with Anneal loaded; FL uses the new
version the next time it loads the plugin. Don't keep both installs: FL would list Anneal twice.

Unattended update: `Anneal-<version>-Setup.exe /CURRENTUSER /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`.

## Controls

| Group | Control | What it does |
|---|---|---|
| Auto EQ | **Reveal** 0–100 % | Lift for shadowed (masked) bands, up to +8 dB. Gaps deeper than −18 dB below their surroundings are left alone, so silence between partials is not pumped up. |
| | **Smooth** 0–100 % | Cut for dominant bands, up to −9 dB. |
| | Tilt ±1.5 dB/oct | Static slope around 1 kHz inside the range (darker ↔ brighter). |
| | Response 20–500 ms | How fast the curve follows the music (release; attack is 3× faster). |
| Heat | **Heat** 0–100 % | Spectral saturation drive. Level-dependent like a real saturator (calibrated at −18 dBFS). |
| | Color | Even (2nd/4th/6th) ↔ odd (3rd/5th) harmonics; the centre gives both. |
| | Focus | Body (harmonics concentrated low) ↔ Air (high). Centre = follow the model only. |
| Output | Mix, Output | Latency-aligned dry blend, final gain. |
| | Match | Slow (~3 s) K-weighted loudness match, so you compare tone rather than level. The measured gain is shown. |
| | Delta | Listen to only what Anneal adds or removes. |
| Display | Range handles | Drag the dashed handles to limit where Anneal acts (both EQ and heat). Double-click resets. |
| Header | Preset | Factory presets: click the name for the list, or step with the arrows. `*` = changed since loading it. Also in the DAW's preset menu. Match, Delta and Bypass are left alone. |
| Header | Bypass | Latency-compensated, click-free. |

Every knob: drag, **Ctrl-drag** for fine adjustment, **double-click** to reset, **click
the value** to type one (units such as `2.5k`, `Body 40`, `odd` are understood).

## Build (Windows, x64)

Requirements: Visual Studio 2022 or 2026 with the C++ workload, CMake ≥ 3.22.
JUCE 8.0.4 is taken from a local git checkout of exactly that tag when one is found (on this
machine `%USERPROFILE%\Downloads\HarshVision\HarshVision\build\_deps\juce-src`; pass `-JuceDir` to
point elsewhere); otherwise CMake fetches tag `8.0.4` from GitHub. The setup needs Inno Setup 6
(`winget install JRSoftware.InnoSetup`).

```powershell
.\build.ps1                     # Release: VST3 + Standalone + AnnealTests + AnnealSnapshot
.\build.ps1 -Test -Snapshot     # also run the acceptance tests and render editor PNGs
.\build.ps1 -Setup              # also build dist\Anneal-<version>-Setup.exe (with -Test: and test it)
.\build.ps1 -Install            # after the tests, run the setup silently just for you (no admin prompt)
.\build.ps1 -InstallSystem      # after the tests, run the setup silently for all users (admin prompt)
```

**Which folder your DAWs see:** FL Studio 2024/2026 scan `C:\Program Files\Common Files\VST3`
and the folders you add under *Plugin search paths* (stored in the registry under
`Image-Line\Shared\Plugin search paths`). Cubase 15 scans the system and the per-user folder.

`-Test` also runs `AnnealHostCheck`, which loads the **built `.vst3`** through JUCE's VST3 host
(scan, parameters, latency, processing at 44.1/48/96 kHz, state, editor attach ×3, 4 instances).
You can point it at an installed copy too:

```powershell
& "$env:LOCALAPPDATA\AnnealBuild\build\AnnealHostCheck_artefacts\Release\AnnealHostCheck.exe" "C:\Program Files\Common Files\VST3\Anneal.vst3"
```

If the project folder's path is long (over ~90 characters), MSBuild's file tracker fails
with `FTK1011`. `build.ps1` then builds out-of-tree in `%LOCALAPPDATA%\AnnealBuild` and
prints where the artefacts are. Override with `-BuildDir`.

Plain CMake equivalent:

```powershell
cmake -S . -B C:\path\to\short\build -A x64 -DANNEAL_JUCE_DIR=C:\path\to\JUCE
cmake --build C:\path\to\short\build --config Release --parallel
```

Outputs (under the build folder): `Anneal_artefacts\Release\VST3\Anneal.vst3`,
`Anneal_artefacts\Release\Standalone\Anneal.exe`.

Nothing is installed unless you pass `-Install` / `-InstallSystem`; both go through the setup
(`tools\Install.ps1`). The all-users install waits at most 5 minutes for the administrator approval,
then reports instead of hanging. `-Setup -Test` also runs `tools\TestSetup.ps1` on a copy of the
setup with its own AppId: it installs into a sandbox without admin rights, updates while another
process has the plugin DLL loaded (as a DAW would), and uninstalls, without touching a real install.

## Daily updates

A scheduled Claude task ("Anneal daily update" in the Claude desktop app's scheduled tasks) works on
Anneal every day at 7:00 (Hawaii time). Each run fixes the bugs it finds and adds one new feature or
mode, usually the next item in [docs/ROADMAP.md](docs/ROADMAP.md) (never the ones under *Not
wanted*). It bumps the version and runs `.\build.ps1 -Test -Snapshot -Setup`. Only when everything
passes does it add a [CHANGELOG.md](CHANGELOG.md) entry, commit, push, publish the setup as a GitHub
release, and install it **just for you**. That needs no administrator approval, so the update is in
place when you wake up. The task never asks for, approves or works around administrator rights.

Parameter IDs, plugin codes and saved-state compatibility never change, so existing FL Studio
projects keep their settings.

## Status

- **Built:** Release VST3 + Standalone, MSVC 19.51 (VS 2026), x64, JUCE 8.0.4.
- **Tested headless (`AnnealTests`, 31/31 pass):** STFT identity (44.1/48/96 kHz, blocks 1–4096 and random), latency = 4096 @48k, bit-exact Mix 0 %, even/odd harmonics, no aliasing (≈ −160 dB vs −34 dB for a naive tanh), no IMD (≈ −170 dB), Smooth/Reveal/Range/Tilt behaviour, block-size invariance, 30 s automation stress, re-prepare at 44.1–192 kHz, state round trip and malformed state, bypass, reset (no old audio afterwards), mono, harmonic pre-echo −122 dB, factory presets (values, playback, edited-preset save/load, older projects).
- **CPU (this machine):** ≈ 3.4 % of one core at 48 kHz stereo (budget 3 %; the gated harmonic stream costs one extra iFFT). Measured at high priority like a DAW audio thread: the slowest 0.1 % of blocks take 0.37 ms of 1.33 ms at 64 samples (28 %), 0.38 ms of 2.67 ms at 128.
- **UI:** actual editor rendered to `snapshots/` at 100/150/200 % and at min/max size.
- **Host-checked (`AnnealHostCheck`, 11/11 pass)** including factory presets as host programs and an edited preset surviving save/load, on the built bundle and on the installed copy.
- **Setup-tested (`TestSetup.ps1`, 16/16 pass):** sandbox install, update while another process has the DLL loaded, uninstall.
- **Installed** just for the user (no admin) with `Anneal-0.1.1-Setup.exe` on 2026-10-13: VST3 in `%LOCALAPPDATA%\Programs\Common\VST3`, standalone in `%LOCALAPPDATA%\Programs\Anneal`, listed under Settings → Apps. The earlier all-users copy was removed.
- **Not done:** pluginval (not installed here), testing inside FL Studio / Cubase, listening tests. The perceptual model is a heuristic, not a calibrated loudness model. Latency is 85 ms at 48 kHz, so it suits mixing/mastering, not tracking.
