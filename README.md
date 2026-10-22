# Anneal

An audio effect (VST3, AU, CLAP, LV2) for Windows, macOS (Apple Silicon and Intel) and Linux:
an **automatic perceptual EQ** combined with a **spectral saturator**. Think "saturating EQ" (SweetEQ-style colour) steered by a hearing model
(Gullfoss-style Recover/Tame behaviour), with the saturation itself done in the FFT
domain so it never aliases and never intermodulates.

- 1024 analysis bands spaced on the ERB-rate (cochlear) scale, gains updated 300×/s in every latency mode (since 5.2.0; was ~94×/s).
- **Reveal** lifts bands sitting in the masking shadow of louder neighbours.
- **Smooth** pulls down bands that dominate their auditory neighbourhood.
- **Heat** grows a 2nd–6th harmonic series from every spectral peak, phase-locked to its
  fundamental, and writes it only where the model says the spectrum is thin.
- **Sidechain** (3.0) dips the main where another track needs room, or lifts what it masks;
  **Freeze** holds the curve as a static EQ; **Ceiling** keeps the output under a true-peak limit.
- **Reference match** (4.0) learns the tone of a reference (a file dropped on the display, or the
  sidechain track) and steers the auto EQ toward it; **Paint** (4.0) lets you draw on the display
  how much of the EQ and of Heat acts in each region.
- **SHAPE** (5.0): **Punch** makes attacks louder or softer band by band, **Width** sets the stereo
  width and **Mono bass** keeps the low end mono; plus your own **presets**.

Design brief: [docs/DESIGN.md](docs/DESIGN.md) · Engine plan and test criteria: [docs/ENGINE.md](docs/ENGINE.md) ·
What's next: [docs/ROADMAP.md](docs/ROADMAP.md) · What changed: [CHANGELOG.md](CHANGELOG.md) ·
Platforms, DAWs, KVR: [docs/DISTRIBUTION.md](docs/DISTRIBUTION.md)

## Install

Public Windows and Linux downloads: [Anneal on KVR](https://www.kvraudio.com/product/anneal-by-qulic).
KVR hosts the free files; the source repository remains private.

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

**macOS** (10.15 or later; one universal build for Apple Silicon and Intel): download
`Anneal-<version>-macOS.zip`. It has the AU (Logic Pro, GarageBand), VST3, CLAP and the standalone
app. Run `bash install.sh` from the unzipped folder: it copies them into
`~/Library/Audio/Plug-Ins` and `~/Applications`. These builds aren't notarized by Apple yet, so the
script also removes macOS's download quarantine from the installed copies; `INSTALL.txt` explains
how to do it by hand.

**Linux** (x86_64): `Anneal-<version>-Linux-x86_64.tar.gz` has the VST3, LV2, CLAP and the
standalone app; `bash install.sh` puts them in `~/.vst3`, `~/.lv2`, `~/.clap` and `~/.local/bin`.

**CLAP on Windows**: `Anneal-<version>-Windows-CLAP.zip`, for Bitwig, Reaper and FL Studio 2024+
(the VST3 from the setup works there too).

Linux packages come from the dedicated **Linux** GitHub Actions workflow, including patch releases.
Windows CLAP packages are built separately. The combined macOS workflow remains parked in
`packaging/ci/platforms.yml`; macOS downloads are not verified or available yet.
See [docs/DISTRIBUTION.md](docs/DISTRIBUTION.md) for verification details and the formats each DAW uses.
Pro Tools (AAX) isn't supported yet.

## Controls

| Group | Control | What it does |
|---|---|---|
| Auto EQ | **Reveal** 0–100 % | Lift for shadowed (masked) bands, up to +8 dB. Gaps deeper than −18 dB below their surroundings are left alone, so silence between partials is not pumped up. |
| | **Smooth** 0–100 % | Cut for dominant bands, up to −9 dB. |
| | Tilt ±1.5 dB/oct | Static slope around 1 kHz inside the range (darker ↔ brighter). |
| | Response 20–500 ms | How fast the curve follows the music (release; attack is 3× faster). |
| | **Freeze** | Pill on the AUTO EQ title row: hold the auto-EQ curve where it is (a static EQ shaped by what it just heard). Heat, Tilt and Range stay live; a sidechain dip holds too. The display says "AUTO EQ FROZEN". Off (default) = as before; projects from before 3.0 open with it off. |
| | Guard Off–100 % | Transient guard. On drum hits and plucks the auto EQ steps back toward 0 dB in the bands of the hit (Tilt stays) and Heat writes no harmonics over the attack, then both return. The curve also doesn't chase the hits, so nothing ducks after them. Sustained sounds are treated as before. Off (default) = as before; projects from before 0.2.2 open with it off. Type `off` or a number. |
| Heat | **Heat** 0–100 % | Spectral saturation drive. Level-dependent like a real saturator (calibrated at −18 dBFS). |
| | Color | Slider, even (2nd/4th/6th) ↔ odd (3rd/5th) harmonics; the bars under it show the harmonics Heat writes. |
| | Focus | Slider, body (harmonics concentrated low) ↔ air (high). Centre = follow the model only. |
| | **Flavour** Clean / Tape / Tube / Transformer / Exciter | Pill on the HEAT title row: the character of the harmonics. **Clean** = Anneal's own (as before). **Tape**: soft, mostly even, upper orders fall away, harmonics rolled off above 5 kHz. **Tube**: a strong 2nd, the rest pulled back. **Transformer**: odd harmonics driven by the low end (a 60 Hz note saturates 6 dB harder than 120 Hz, notes above ~1 kHz barely at all). **Exciter**: air only, no harmonics from partials below 1.5 kHz, full from 3 kHz, driven 6 dB harder. Color still moves even/odd on top; only the harmonics change (at Heat 0 all flavours are identical). The pill turns bronze when a flavour is on; the bars follow it. Clean (default) = as before; projects from before 0.2.6 open Clean. Also a host parameter (type `tape`, `tube`, `transformer`/`iron`, `exciter`/`air` or `clean`). |
| Sidechain | **Sidechain** Off / Make room / Unmask | What a key routed into Anneal's sidechain input does. **Make room**: dip the main where the key is loud, only in the key's bands (a vocal over a bus). **Unmask**: lift the main where the key would mask it. The key's spectrum is the violet line on the display. With nothing routed, or a silent key, the output is identical. Off (default) = as before; projects from before 3.0 open Off. Cubase: enable the plug-in's side-chain input and send the key to it. FL Studio: *Sidechain to this track* from the key's mixer track, then choose it as Anneal's sidechain input in the wrapper's *Processing* tab. |
| | Amount 0–100 % | How far Make room dips (up to −9 dB) or Unmask lifts (up to +6 dB). The live line under the ring shows the current amount. |
| Reference | **Reference** Off / Learn key / Match | Tone matching (4.0). **Drop an audio file** (wav, aiff, flac, mp3, ogg) on the display, or use *Load a reference file…* in this menu: Anneal analyses its long-term tone in the background and switches to Match. **Learn key** learns the tone of whatever plays into the sidechain input instead (the live line counts the seconds learned; choosing it with nothing routed keeps the reference you had). **Match** steers the auto EQ toward the reference: it compares the reference with the last ~3 s of this track, both at the same overall level, and adds the smoothed difference (about 1.5 ERB either side, at most ±6 dB) to the curve, so Range, Freeze and Paint apply to it too. The reference is the teal line on the display, drawn at the level of the output; the REF tag names it. It's saved with the project. *Clear the reference* is in the menu. Off (default) = exactly as before; projects from before 4.0 open Off with no reference. Also a host parameter (type `off`, `learn` or `match`). |
| | Amount 0–100 % | How far Match steers (100 % = up to ±6 dB). The live line shows the strongest move right now ("matching −2.4 dB"), or what Learn key has learned. Default 50 %. |
| Shape | **Punch** −100 … +100 % | Spectral transient shaper (5.0). In the bands where an attack happens it makes the attack louder (+) or softer (−), up to 8 dB, and leaves the body alone (measured on 2 kHz plucks: attacks ±7.9 dB, body 0.01 dB). Same per-band detector as Guard; held while the attack is, gone ~15 ms after. Inside Range and Paint; in Mid/Side it follows the EQ routing. Sharpest in Low latency. With Punch or Guard on, rose bars at the foot of the EQ lane show where attacks are happening. Off (default) = as before. Type `-40`, `+25` or `off`. |
| | **Width** 0–200 % | Stereo width of Anneal's output, per frequency bin (linear phase): 0 % mono, 200 % doubles the sides. The centre is unchanged; mono tracks are left alone. Applies to everything heard (harmonics and Listen solos too); Mix blends it with the dry signal. 100 % (default) = as before. Type `mono` or a number. |
| | **Mono bass** Off / 20–400 Hz | Makes everything below this frequency mono, fading in over a quarter octave either side, so the low end stays centred while the rest keeps its width. Off (default) = as before. Type `off` or a frequency (`120`, `150 Hz`). |
| Output | Mix, Output | Latency-aligned dry blend, final gain. |
| | Match | Slow (~3 s) K-weighted loudness match, so you compare tone rather than level. The measured gain is shown. |
| | Delta | Listen to only what Anneal adds or removes. |
| | **Ceiling** Off / 0.0 / −0.3 / −1.0 dBTP | True-peak look-ahead limiter at the very end (after Mix and Output): peaks between samples are found 4× oversampled, 1.25 ms look-ahead, 80 ms release. The meter shows the reduction in orange. Its look-ahead is always in the latency, so switching it never shifts timing. Off (default) = a plain delay. |
| | **Listen** Off / Reveal only / Smooth only / Heat only | Solo one part of what Anneal does: **Reveal only** plays just what Reveal lifts, **Smooth only** just what Smooth cuts away, **Heat only** just the harmonics Heat writes (Tilt is in none of them). The pill lights in that part's colour and the display says what you're hearing; the curves still show the full processing. Follows Mix and Output; Match holds its gain meanwhile; overrides Delta; Bypass still wins. Off (default) = the full result; projects from before 0.2.5 open with it off. Also a host parameter (type `reveal`, `smooth`, `heat` or `off`). |
| Display | Range handles | Drag the dashed handles to limit where Anneal acts (both EQ and heat). Double-click resets. If automation puts Range Low above Range High, Anneal works from Low to 1.5 × Low, and the display shows exactly that. |
| Display | **Paint** (pill in the header) | Draw how much of the auto EQ and of Heat acts in each region (4.0). In paint mode each lane shows its amount as a white line (top = 100 %, bottom = none): drag in the EQ lane to paint the EQ (Reveal, Smooth, Tilt, sidechain and reference moves alike), in the Heat lane to paint Heat. **Alt-drag** or **right-drag** erases back to 100 %, **double-click** resets that lane, each stroke is one undo step, **Escape** leaves paint mode. Painted-out regions stay dimmed like outside the Range, and the hover read-out shows the amounts. Works on top of the Range handles. Saved with the project; 100 % everywhere (default) = exactly as before. |
| Display | Drop a file | Drop an audio file on the display to use it as the reference (see Reference). |
| Header | Latency: **Zero** | Zero latency (5.1): no delay at all, for tracking and monitoring. The model analyses in Low-latency frames and its curve is applied through minimum-phase filters rebuilt every ~5 ms (the curve follows ~5 ms later than in Low). Reveal, Smooth, Tilt, Range, Guard, Freeze, Sidechain, Reference, Paint, Match, Width and Mono bass work; **Heat, Punch and the Listen solos rest** (they need the FFT frames), and the display says so. The Ceiling holds sample peaks only (no look-ahead). At Mix below 100 % the minimum-phase filters' phase can colour the dry blend a little. A separate setting ("Zero Latency"), off by default; older projects open with it off. |
| Header | Latency | Low 22.7 ms (default), Normal 44.1 ms, Precise 86.7 ms at 48 kHz (finest bass detail). Since 3.0 each includes the Ceiling's 1.4 ms look-ahead. Projects from before 0.2.0 open in Precise. |
| Header | Latency: **Spread load** | A switch at the bottom of the Latency menu (5.2.1): each of the 300 EQ updates a second is worked out over the next 3.3 ms instead of inside the one audio block it falls in, so at small buffers (64 samples and less) no block carries a whole update and the CPU per block is even (Precise Mid/Side at 32 samples: slowest blocks 42 → 16 % of their time). Total CPU is the same, and so is the sound, sample for sample, 3.3 ms later: Low 26.1, Normal 47.4, Precise 90.1 ms at 48 kHz. Rests at Zero. Not automatable (the host re-aligns tracks); off by default, and older projects open with it off. |
| Header | Undo / Redo | The arrows next to Latency, or **Ctrl+Z** / **Ctrl+Shift+Z** or **Ctrl+Y** (Cmd on macOS) once you've clicked in Anneal's window, if the DAW passes keys to plug-ins. Each knob drag, typed value, menu choice or paint stroke is one step; a preset load is one step; host automation isn't recorded. History starts fresh when a project loads. |
| Header | Stereo / Mid-Side | Stereo links the EQ across left and right. Mid/Side analyses the centre and sides independently; the display draws Mid solid and Side dashed. Routing changes fade without changing latency. Mono uses linked mono. Older sessions keep Stereo. |
| Auto EQ / Heat | EQ target / Heat target | In Mid/Side mode choose Both, Mid (centre) or Side independently for EQ (Reveal, Smooth, Tilt) and Heat. For centre warmth with wider detail, select Heat: Mid and EQ: Side. These choices are left alone by factory presets. |
| Header | Preset | Click the name for the list, or step with the arrows (factory presets, then yours). `*` = changed since loading it. Every factory preset sets Flavour to Clean, Punch off, Width 100 % and Mono bass off (they sound as before); Guard, Match, Delta, Bypass and the routing are left alone. **Your presets** (5.0): *Save current settings as…* in the list stores the same settings a factory preset sets in `%APPDATA%\Qulic\Anneal\Presets\<name>.annealpreset` (an existing name is replaced), *Delete “…”* moves the chosen one to the Recycle Bin, *Open the presets folder* shows the files. Loading one is one undo step; a project remembers which one it was set from. The DAW's preset menu lists the factory presets. |
| Header | Bypass | Latency-compensated, click-free. Also silences a Listen solo. |

Every knob: drag, **Ctrl-drag** for fine adjustment, **double-click** to reset, **click
the value** to type one (units such as `2.5k`, `Body 40`, `odd` are understood).

## Build (Linux, x86_64)

Ubuntu 24.04, GCC/Clang with C++20, CMake 3.28+ (for CLAP), Git and Python 3:

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake ninja-build git python3 \
  libasound2-dev libjack-jackd2-dev libfreetype-dev libfontconfig1-dev \
  libx11-dev libxcomposite-dev libxcursor-dev libxext-dev libxinerama-dev libxrandr-dev libxrender-dev \
  xvfb xauth openbox x11-utils lilv-utils
bash build.sh --test --snapshot --package
```

This builds VST3, LV2, CLAP and Standalone, tests the DSP and built VST3/CLAP, and creates
`dist/Anneal-<version>-Linux-x86_64.tar.gz` plus its SHA-256 file. Build products are in
`build-linux/Anneal_artefacts/Release/`. Use `--juce-dir /path/to/JUCE` for an existing JUCE 8.0.4
checkout; otherwise it is fetched. `--no-clap` skips the CLAP dependency download (not available
with `--package`). `--build-dir` and `--jobs` control the build directory and parallelism.

Nothing installs automatically. To install, extract the archive and run its `bash install.sh`.
Updates stage each replacement so a copy failure preserves the previous plugin. Linux builds use
X11/XWayland; on a headless machine the script starts Xvfb with Openbox for JUCE's build helpers
and test tools. It waits for the window manager to be ready and stops it after each command.
Bare Xvfb lacks the window-manager properties JUCE 8.0.4 needs to open a native editor.
The dedicated **Linux** GitHub Actions workflow checks these same commands, inspects LV2 metadata
and shared-library dependencies, and stores the archive and editor snapshots as artifacts. It
attaches tested Linux packages on release events, including patch releases. Version 4.0.1 passed
112 acceptance, 20 built-VST3 host, 10 CLAP and 23 packaging checks on Ubuntu 24.04, including
three editor attach/detach cycles. LV2 discovery/metadata and runtime dependencies also passed.
The editor was rendered on Linux at 100/150/200 % and minimum/maximum sizes.

## Build (Windows, x64)

(macOS: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Release` and
`cmake --build build --parallel`; add `"-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"` for a universal Mac
build and `-DANNEAL_CLAP=ON` for CLAP. The parked combined-platform steps are in
`packaging/ci/platforms.yml`.)

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

The Codex task **Anneal feature updates** was paused at the user's request on 2026-10-15.
The one-minute schedule is stopped. Requested manual work continues separately, including the
4.0.1 Linux support and KVR listing. Tested updates keep the CMake version, changelog and roadmap
completion record in agreement. Auto Heat and A/B compare remain excluded. Installing a new build
is a separate action.

## Daily updates

A scheduled Claude task ("Anneal daily update" in the Claude desktop app's scheduled tasks) works on
Anneal once a day at 7:01 PM HST. It ran hourly until 2026-10-13, when the user switched it to daily
starting after 4.0.0; it was paused from 2026-10-14 and restarted on 2026-10-18. Each run fixes the
bugs it finds and adds one new feature or mode, usually the next item in [docs/ROADMAP.md](docs/ROADMAP.md) (never the ones under *Not wanted* or *Not now*).
Missed days are caught up together, up to five. It bumps the version and runs
`.\build.ps1 -Test -Snapshot -Setup`. Only when everything passes does it:
- add a [CHANGELOG.md](CHANGELOG.md) entry;
- commit and push;
- publish the setup as a GitHub release;
- install it into **Program Files**.

The install needs your approval: Windows' administrator prompt waits on screen until you click Yes.
If you're away, the prompt keeps waiting, later updates don't add more prompts, and the one Yes
installs the newest build. The task never approves or works around the prompt itself.

The major 4.0.0 came from its own task ("Anneal 4.0 update") on 2026-10-14; the daily updates
continue from it.

Parameter IDs, plugin codes and saved-state compatibility never change, so existing FL Studio
projects keep their settings.

## Status

- **Built (5.2.1):** Windows x64 VST3 + Standalone (MSVC 19.51, JUCE 8.0.4). Linux x86_64 VST3, LV2, CLAP + Standalone last built at 4.0.1 (Ubuntu 24.04, GCC 13.3, JUCE 8.0.4); there Linux passed 112 acceptance, 20 VST3 host, 10 CLAP and 23 packaging checks, plus LV2 metadata and dependency inspection. Windows CLAP remains verified at 4.0.0 (10/10 checks). macOS builds remain parked and unverified.
- **Spread load (5.2.1):** with Spread the output equals the output without it one hop later, sample for sample (largest difference 0.000000000) in every mode, with the auto EQ, Heat, Mid/Side, Guard, Punch, Width, Mono bass and a sidechain key, in random blocks of 1–700 samples; a 32-sample block runs at most one of a frame's four stages (without Spread: all four); the latency is N + hop through the engine, the processor and the built VST3, with the wet and dry paths aligned; it rests at Zero latency; switching it while playing stays clean. Slowest 0.1 % of 64-sample blocks at 48 kHz: Stereo Low 5 → 3 %, Precise 12 → 7 %; Mid/Side Precise 21 → 11 % of the deadline, and 42 → 16 % at 32 samples. Total CPU unchanged.
- **Zero latency (5.1):** latency 0 reported and applied (also through the built VST3), neutral settings pass the input within 1e-7, Smooth cuts a 3150 Hz ring 8.9 dB as in Low, Heat writes nothing, Width / Mono bass work through the Side filter, the filter design matches its target within 0.01 dB and is minimum phase, switching Zero and the latency modes while playing stays clean. CPU: Stereo 3.2 %, Mid/Side 5.6 % at 64 samples (Low: 1.7 / 3.7 %); 2.4 / 3.9 % since 5.1.1.
- **SHAPE and user presets (5.0):** Punch ±100 % moves 2 kHz pluck attacks by ±7.9 dB in Low with the body within 0.01 dB, and does nothing outside Range. Width 0 % is mono (sides −300 dB in the engine, −148 dB through the processor, −107 dB through the built VST3), 200 % lifts the sides by 6.02 dB, the centre stays within 0.01 dB, and mono tracks are bit-identical. Mono bass removes 60 Hz sides to −85 dB (200 Hz, Low) or −122 dB (120 Hz, Precise) and keeps 1 kHz sides at 0.00 dB. User presets save, load (one undo step), survive a project reload, refuse broken files and unsafe names, and delete cleanly.
- **Reference match, Paint, shortcuts (4.0):** Match at 100 % moves a mix with a +6 dB shelf at 4 kHz 5.3 dB toward a flat reference (Stereo and Mid/Side); a reference learned from the sidechain key is within 0.28 dB of the same audio analysed as a file; WAV, FLAC and AIFF files analyse exactly like the audio itself (0.000 dB) on their own thread while the audio thread keeps running; Freeze holds the match. EQ paint at 0 % over 2–5 kHz takes a −9.0 dB cut at 3150 Hz to 0.0 dB (800 Hz untouched); Heat paint there takes the harmonics below −170 dB. Reference Off / Learn key / Amount 0 and paint at 100 % are bit-identical to 3.1. Reference and paint round-trip in the project (also through the built VST3 into a new instance), older projects open with neither. On the display: paint strokes are one undo step each, Alt erases, a double-click reset is one step; a dropped file becomes the reference; Ctrl+Z / Ctrl+Shift+Z / Ctrl+Y undo and redo.
- **Sidechain, Freeze, Ceiling, Undo (3.0):** Make room ducks a 2 kHz key's band −9.0 dB and leaves 300 Hz / 8 kHz at 0.0 dB (also through the built VST3's sidechain bus); Unmask lifts +5.6 dB; a silent key changes nothing. Freeze keeps a −9.0 dB cut after the music changes. The Ceiling holds the true peak (independent 8× meter) between −0.01 and +0.10 dB of 0.0 / −0.3 / −1.0 dBTP on a sine at fs/4 and on music 12 dB over; Off is bit-exact. Undo/redo restores edits and treats a preset as one step.
- **Saturation flavours (0.2.6):** Clean / Tape / Tube / Transformer / Exciter, measured on steady tones: Tube's 2nd +3.5 dB over Clean and 25 dB over its 3rd; Tape's 5th −17 dB, its 2nd at 9 kHz −6 dB but at 1 kHz unchanged; Transformer's 3rd 17 dB over its 2nd at 60 Hz, 37 dB below Clean at 2 kHz; Exciter writes nothing for a 300 Hz partial (−174 dB) and +6 dB at 8 kHz for a 4 kHz one, also in Mid/Side. Flavours leave the EQ path identical (< 1e-7) and switch click-free.
- **Listen modes (0.2.5):** Reveal only / Smooth only / Heat only. The solos split the processing exactly (normal − Reveal solo + Smooth solo = input; normal − Heat solo = input) in Stereo, Mid/Side, Low and Precise; switching while playing adds no clicks; Match holds; the display keeps showing the full result.
- **Mid/Side (0.2.4):** independent Mid and Side EQ/Heat with separate routing. Acceptance checks cover neutral accuracy, mono fallback, isolation, independent models, automation and old-session defaults.
- **Tested headless (`AnnealTests`, 163/163 pass on 5.2.1 on Windows; 112/112 on Linux at 4.0.1):** STFT identity in every latency mode (44.1/48/96 kHz, blocks 1–4096 and random), latency per mode and mode switching, old projects load as Precise, bit-exact Mix 0 %, even/odd harmonics, no aliasing (≈ −160 dB vs −34 dB for a naive tanh), no IMD (≈ −170 dB), Smooth/Reveal/Range/Tilt behaviour, block-size invariance, 30 s automation stress, re-prepare at 44.1–192 kHz, state round trip and malformed state, bypass, reset (no old audio afterwards), mono, harmonic pre-echo −122 dB, factory presets (values, playback, edited-preset save/load, older projects), transient guard (attacks pass Smooth in Low and Precise, sustained ring cut unchanged, no harmonics over attacks, old projects load with Guard off), display range = engine range, range-handle drags (and a double-click reset as one host gesture), Mid/Side, Listen modes, saturation flavours, sidechain (Make room, Unmask, silent key, the processor's bus), Freeze, true-peak Ceiling, undo/redo, older projects with the 3.0 controls off, the editor layout (no overlapping controls, routing and Flavour menus on the title row, the REFERENCE group and the Paint pill), and since 4.0: Freeze across a host stop and a latency change, the Ceiling switched on again, Reference match (identity when off, the +6 dB shelf, Learn key, dropped files, Freeze), Paint (identity, EQ and Heat paint, one undo step), reference and paint in saved projects, keyboard shortcuts, paint strokes and file drops on the display, and since 4.0.2: Anneal's own FFT against JUCE's (−133 dB, sizes 8–32768) and an exact DFT, Freeze and the loudness-Match gain kept when the host prepares again (new buffer size, 48 → 44.1 kHz), and since 5.1.1: the fast dB and gain conversions against log10 / pow over their whole range (SIMD and plain loops) and the model's curve within 0.001 dB of 5.1.0's math on music with every feature on (measured 0.000004 dB), and since 5.2.0: 300 EQ updates/s in every mode at 44.1 / 48 / 88.2 / 96 / 192 kHz and an exact overlap-add with hops that don't divide the frame (160 at 48 kHz), and since 5.2.1: Spread load (the same output one hop later, at most one frame stage per 32-sample block, latency and alignment, Zero, switching, saved state).
- **CPU (build machine, 5.2.0, 300 EQ updates/s):** the EQ updates 3.2× as often in Normal and Precise and 1.6× in Low, and the CPU rose with it: at 48 kHz, Stereo Low 2.0 %, Normal 2.8 %, Precise 4.5 % of one core; Mid/Side Low 3.9 % (64), Normal 5.2 % (512), Precise 7.8 % (64); Zero latency Stereo 4.1 %, Mid/Side 7.1 %. Slowest 0.1 % of 64-sample blocks: Stereo Low 6 %, Precise 12 %; Mid/Side Low 10 %, Precise 21 % of the deadline.
- **CPU (build machine, 5.1.1, batched fast log / exp in the model):** at 48 kHz, Stereo Low 1.1 %, Normal 0.8 %, Precise 1.3 % of one core; Mid/Side Low 2.2 % (64), Normal 1.5 % (512), Precise 2.2 % (64), now all under the 3 % budget; Zero latency Stereo 2.4 %, Mid/Side 3.9 %. Slowest 0.1 % of 64-sample blocks: Stereo Low 5 %, Precise 14 %; Mid/Side Low 10 %, Precise 20 % of the deadline. Per frame, measured in the same session: the model Low 30.9 → 21.6 µs, Precise 38.5 → 24.4 µs (`AnnealTests profile`).
- **CPU (build machine, 5.0.0):** as 4.0.2 with the new controls off (Stereo Low 1.7 %, Normal 1.2 %, Precise 1.8 %; Mid/Side Low 3.7 %, Normal 2.5 %, Precise 3.6 %). Punch, Width and Mono bass on: Low 1.4 %, Precise 1.6 % at 64 samples, no measurable cost.
- **CPU (build machine, 4.0.2, Anneal's own FFT):** at 48 kHz, Stereo Low 1.7 %, Normal 1.2 %, Precise 1.8 % of one core; Mid/Side Low 3.6 % (64), Normal 2.4 % (512), Precise 3.6 % (64). With Reference Match at 100 % and Paint on: Low 2.0 %, Normal 1.3 %, Precise 2.0 %. Slowest 0.1 % of 64-sample blocks: Stereo Low 8 %, Precise 14 %; Mid/Side Low 15 %, Precise 29 % of the deadline. Per Precise frame the FFTs now take ~38 µs (were ~160 µs); the perceptual model (~44 µs, three of them in Mid/Side) is the largest cost left (`AnnealTests profile`).
- **CPU (build machine, 4.0.0):** at 48 kHz, Stereo Low 2.0 %, Normal 1.8 %, Precise 2.7 % of one core; Mid/Side Low 3.7 % (64), Normal 2.8 % (512), Precise 4.3 % (64), the same as 3.1.0. With Reference Match at 100 % and Paint on: Low 2.2 %, Normal 1.9 %, Precise 2.8 %. Slowest 0.1 % of 64-sample blocks: Stereo Low 8 %, Precise 25 %; Mid/Side Low 19 %, Precise 36 % of the deadline.
- **CPU (build machine, 3.1.0, Ceiling and Sidechain off):** at 48 kHz, Stereo Low 2.0 %, Normal 1.8 %, Precise 2.7 % of one core; Mid/Side Low 3.7 % (64), Normal 3.1 % (512), Precise 4.4 % (64). Same DSP as 3.0.0, whose 2.5 / 2.1 / 3.4 % were taken on a busier machine. Ceiling-on and Sidechain-on CPU not benchmarked yet.
- **CPU (build machine, 0.2.6):** at 48 kHz / 512 samples, linked Stereo Low 1.9 %, Normal 1.7 %, Precise 2.7 % of one core. Mid/Side Low 3.6 %, Normal 2.9 %, Precise 4.3 % (Low/Precise at 64 samples, Normal at 512). Independent M/S analysis uses more CPU; the slowest 0.1 % of 64-sample blocks take 10 % / 22 % (Low / Precise Stereo) and 16 % / 35 % (Low / Precise M/S) of the deadline. Tape (the flavour with per-bin work) measures the same as Clean; Listen modes cost a little extra only while one is on. 0.2.5 measured 2.4 / 2.1 / 3.4 % an hour earlier on a busier machine; timings vary with load, host and machine.
- **Icon (0.2.1):** `Source/ui/Icon.h`, rendered with `AnnealSnapshot --icons resources/icon`, packed by `tools/make_ico.py` into `resources/icon/anneal.ico` (setup, Apps entry); JUCE builds the app and VST3-folder icons from `icon_512.png` / `icon_32.png`.
- **UI (v3, 0.2.0; 1464 × 640 since 5.0):** actual editor rendered to `snapshots/` at 100/150/200 % and at min/max size, with and without audio, with a sidechain key, frozen, matching a reference, learning from the key, in paint mode, painted, and with a file dragged over the display.
- **Host-checked (`AnnealHostCheck`, 24/24 pass on 5.2.1 on Windows; 20/20 on Linux at 4.0.1)** including Spread load through the host (5.2.1: latency and the impulse in place at Mix 0 % and 100 %), a frozen curve through the host deactivating and activating Anneal (4.0.2; 4.0.0 lost it), the Reference parameters, Learn key through the sidechain bus, and the reference and paint carried in the VST3 state into a new instance and acting there (Heat painted to 0 %: −300 dB vs −15.3 dB unpainted; Match moves the output), the sidechain bus (a routed key ducks the main −9.0 dB), latency with the Ceiling's look-ahead, Flavour through VST3 (names, typed values, Tube vs Exciter on a 220 Hz tone, swept while playing), Listen modes through VST3 (choices, typed values, a Heat-only solo at Heat 0 is silent, swept while playing), Mid/Side routing automation and saved routing state, latency switching through the host, Guard automated while playing, factory presets as host programs and an edited preset surviving save/load. The installed copy was checked separately before this update.
- **Setup-tested (`TestSetup.ps1`, 25/25 pass on 5.2.1):** sandbox install, custom VST3 destination metadata, update and uninstall while another process has the DLL loaded, and later cleanup after release.
- **Installed** for all users: each release's setup installs the VST3 into `C:\Program Files\Common Files\VST3` (FL Studio's default folder) and the standalone into `C:\Program Files\Anneal`, after you approve the Windows prompt. Listed under Settings → Apps.
- **Not done:** pluginval (not installed here), testing inside FL Studio / Cubase (including sidechain routing there), listening tests, macOS builds. The perceptual model is a heuristic, not a calibrated loudness model. Low latency (21 ms) suits tracking; Precise (85 ms) is for mixing/mastering.
