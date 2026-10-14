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
  Updates never need approval.
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
| | Guard Off–100 % | Transient guard. On drum hits and plucks the auto EQ steps back toward 0 dB in the bands of the hit (Tilt stays) and Heat writes no harmonics over the attack, then both return. The curve also doesn't chase the hits, so nothing ducks after them. Sustained sounds are treated as before. Off (default) = as before; projects from before 0.2.2 open with it off. Type `off` or a number. |
| Heat | **Heat** 0–100 % | Spectral saturation drive. Level-dependent like a real saturator (calibrated at −18 dBFS). |
| | Color | Slider, even (2nd/4th/6th) ↔ odd (3rd/5th) harmonics; the bars under it show the harmonics Heat writes. |
| | Focus | Slider, body (harmonics concentrated low) ↔ air (high). Centre = follow the model only. |
| | **Flavour** Clean / Tape / Tube / Transformer / Exciter | Pill on the HEAT title row: the character of the harmonics. **Clean** = Anneal's own (as before). **Tape**: soft, mostly even, upper orders fall away, harmonics rolled off above 5 kHz. **Tube**: a strong 2nd, the rest pulled back. **Transformer**: odd harmonics driven by the low end (a 60 Hz note saturates 6 dB harder than 120 Hz, notes above ~1 kHz barely at all). **Exciter**: air only, no harmonics from partials below 1.5 kHz, full from 3 kHz, driven 6 dB harder. Color still moves even/odd on top; only the harmonics change (at Heat 0 all flavours are identical). The pill turns bronze when a flavour is on; the bars follow it. Clean (default) = as before; projects from before 0.2.6 open Clean. Also a host parameter (type `tape`, `tube`, `transformer`/`iron`, `exciter`/`air` or `clean`). |
| Output | Mix, Output | Latency-aligned dry blend, final gain. |
| | Match | Slow (~3 s) K-weighted loudness match, so you compare tone rather than level. The measured gain is shown. |
| | Delta | Listen to only what Anneal adds or removes. |
| | **Listen** Off / Reveal only / Smooth only / Heat only | Solo one part of what Anneal does: **Reveal only** plays just what Reveal lifts, **Smooth only** just what Smooth cuts away, **Heat only** just the harmonics Heat writes (Tilt is in none of them). The pill lights in that part's colour and the display says what you're hearing; the curves still show the full processing. Follows Mix and Output; Match holds its gain meanwhile; overrides Delta; Bypass still wins. Off (default) = the full result; projects from before 0.2.5 open with it off. Also a host parameter (type `reveal`, `smooth`, `heat` or `off`). |
| Display | Range handles | Drag the dashed handles to limit where Anneal acts (both EQ and heat). Double-click resets. If automation puts Range Low above Range High, Anneal works from Low to 1.5 × Low, and the display shows exactly that. |
| Header | Latency | Low 21 ms (default), Normal 43 ms, Precise 85 ms (finest bass detail). Projects from before 0.2.0 open in Precise. |
| Header | Stereo / Mid-Side | Stereo links the EQ across left and right. Mid/Side analyses the centre and sides independently; the display draws Mid solid and Side dashed. Routing changes fade without changing latency. Mono uses linked mono. Older sessions keep Stereo. |
| Auto EQ / Heat | EQ target / Heat target | In Mid/Side mode choose Both, Mid (centre) or Side independently for EQ (Reveal, Smooth, Tilt) and Heat. For centre warmth with wider detail, select Heat: Mid and EQ: Side. These choices are left alone by factory presets. |
| Header | Preset | Factory presets: click the name for the list, or step with the arrows. `*` = changed since loading it. Also in the DAW's preset menu. Every factory preset sets Flavour to Clean (they sound as before). Guard, Match, Delta and Bypass are left alone. |
| Header | Bypass | Latency-compensated, click-free. Also silences a Listen solo. |

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
(`tools\Install.ps1`). The all-users install asks for administrator approval; the approved step
installs the newest setup in `dist\`, only one approval is ever waiting, and the script returns
after 5 minutes ("waiting for approval") instead of hanging. `-Setup -Test` also runs `tools\TestSetup.ps1` on a copy of the
setup with its own AppId: it installs into a sandbox without admin rights, updates while another
process has the plugin DLL loaded (as a DAW would), and uninstalls, without touching a real install.

## Minute updates

The Codex task **Anneal feature updates** returns to the same chat every minute and continues
the next bug fix or roadmap feature. Each completed, tested update advances the patch version:
0.2.3, then 0.2.4, 0.2.5, and so on. The version in CMake, the changelog entry, and the roadmap
completion record agree. Work can span several scheduled runs while implementation and tests
finish; a version advances when the change is ready to publish. Auto Heat and A/B compare are
excluded. The task publishes tested source updates to GitHub; installing the new build is a
separate action.

## Hourly updates

A scheduled Claude task ("Anneal hourly update" in the Claude desktop app's scheduled tasks) works on
Anneal every hour, day and night. Each run fixes the bugs it finds and adds one new feature or
mode, usually the next item in [docs/ROADMAP.md](docs/ROADMAP.md) (never the ones under *Not
wanted*). It bumps the version and runs `.\build.ps1 -Test -Snapshot -Setup`. Only when everything
passes does it add a [CHANGELOG.md](CHANGELOG.md) entry, commit, push, publish the setup as a GitHub
release, and install it into **Program Files**. That needs your approval: Windows' administrator
prompt waits on screen until you click Yes. If you're away, the prompt keeps waiting, later updates
don't add more prompts, and the one Yes installs the newest build. The task never approves or works
around the prompt itself.

Parameter IDs, plugin codes and saved-state compatibility never change, so existing FL Studio
projects keep their settings.

## Status

- **Built (0.2.6):** Release VST3 + Standalone, MSVC 19.51 (VS 2026), x64, JUCE 8.0.4.
- **Saturation flavours (0.2.6):** Clean / Tape / Tube / Transformer / Exciter, measured on steady tones: Tube's 2nd +3.5 dB over Clean and 25 dB over its 3rd; Tape's 5th −17 dB, its 2nd at 9 kHz −6 dB but at 1 kHz unchanged; Transformer's 3rd 17 dB over its 2nd at 60 Hz, 37 dB below Clean at 2 kHz; Exciter writes nothing for a 300 Hz partial (−174 dB) and +6 dB at 8 kHz for a 4 kHz one, also in Mid/Side. Flavours leave the EQ path identical (< 1e-7) and switch click-free.
- **Listen modes (0.2.5):** Reveal only / Smooth only / Heat only. The solos split the processing exactly (normal − Reveal solo + Smooth solo = input; normal − Heat solo = input) in Stereo, Mid/Side, Low and Precise; switching while playing adds no clicks; Match holds; the display keeps showing the full result.
- **Mid/Side (0.2.4):** independent Mid and Side EQ/Heat with separate routing. Acceptance checks cover neutral accuracy, mono fallback, isolation, independent models, automation and old-session defaults.
- **Tested headless (`AnnealTests`, 83/83 pass):** STFT identity in every latency mode (44.1/48/96 kHz, blocks 1–4096 and random), latency per mode and mode switching, old projects load as Precise, bit-exact Mix 0 %, even/odd harmonics, no aliasing (≈ −160 dB vs −34 dB for a naive tanh), no IMD (≈ −170 dB), Smooth/Reveal/Range/Tilt behaviour, block-size invariance, 30 s automation stress, re-prepare at 44.1–192 kHz, state round trip and malformed state, bypass, reset (no old audio afterwards), mono, harmonic pre-echo −122 dB, factory presets (values, playback, edited-preset save/load, older projects), transient guard (attacks pass Smooth in Low and Precise, sustained ring cut unchanged, no harmonics over attacks, old projects load with Guard off), display range = engine range, range-handle drags (and a double-click reset as one host gesture), Mid/Side, Listen modes, saturation flavours, and the editor layout (no overlapping controls, routing and Flavour menus on the title row).
- **CPU (build machine, 0.2.6):** at 48 kHz / 512 samples, linked Stereo Low 1.9 %, Normal 1.7 %, Precise 2.7 % of one core. Mid/Side Low 3.6 %, Normal 2.9 %, Precise 4.3 % (Low/Precise at 64 samples, Normal at 512). Independent M/S analysis uses more CPU; the slowest 0.1 % of 64-sample blocks take 10 % / 22 % (Low / Precise Stereo) and 16 % / 35 % (Low / Precise M/S) of the deadline. Tape (the flavour with per-bin work) measures the same as Clean; Listen modes cost a little extra only while one is on. 0.2.5 measured 2.4 / 2.1 / 3.4 % an hour earlier on a busier machine; timings vary with load, host and machine.
- **Icon (0.2.1):** `Source/ui/Icon.h`, rendered with `AnnealSnapshot --icons resources/icon`, packed by `tools/make_ico.py` into `resources/icon/anneal.ico` (setup, Apps entry); JUCE builds the app and VST3-folder icons from `icon_512.png` / `icon_32.png`.
- **UI (v3, 0.2.0):** actual editor rendered to `snapshots/` at 100/150/200 % and at min/max size, with and without audio.
- **Host-checked (`AnnealHostCheck`, 16/16 pass on 0.2.6)** including Flavour through VST3 (names, typed values, Tube vs Exciter on a 220 Hz tone, swept while playing), Listen modes through VST3 (choices, typed values, a Heat-only solo at Heat 0 is silent, swept while playing), Mid/Side routing automation and saved routing state, latency switching through the host, Guard automated while playing, factory presets as host programs and an edited preset surviving save/load. The installed copy was checked separately before this update.
- **Setup-tested (`TestSetup.ps1`, 25/25 pass on 0.2.6):** sandbox install, custom VST3 destination metadata, update and uninstall while another process has the DLL loaded, and later cleanup after release.
- **Installed** for all users with `Anneal-0.2.0-Setup.exe` on 2026-10-13: VST3 in `C:\Program Files\Common Files\VST3` (FL Studio's default folder), standalone in `C:\Program Files\Anneal`, listed under Settings → Apps. Host-checked 12/12 there.
- **Not done:** pluginval (not installed here), testing inside FL Studio / Cubase, listening tests. The perceptual model is a heuristic, not a calibrated loudness model. Low latency (21 ms) suits tracking; Precise (85 ms) is for mixing/mastering.
