# Distribution: platforms, DAWs, KVR, Plugin Alliance

Requested on 2026-10-13: "add it to kvr and plugin alliance for free and add support to all daws
and apple silicon".

## Platforms and formats (3.1.0)

| Platform | Formats | Built by | Tested |
|---|---|---|---|
| Windows x64 | VST3 + Standalone (in `Anneal-<v>-Setup.exe`) | `build.ps1` on the author's PC, every release | 93 acceptance + 17 host + 25 installer checks |
| Windows x64 | CLAP (`Anneal-<v>-Windows-CLAP.zip`) | the author's PC for 3.1.0 and 4.0.0; GitHub Actions once switched on | `tools/ClapCheck.py`: 10/10 (main + sidechain ports, 27 parameters since 4.0, latency 1091 samples at 48 kHz, 2 s of audio); not tried in a DAW |
| macOS 10.15+, **Apple Silicon and Intel** (one universal binary) | AU, VST3, CLAP, Standalone (`Anneal-<v>-macOS.zip`) | GitHub Actions, **not switched on yet** | the workflow runs: acceptance tests, host check on the VST3, ClapCheck, `auval -strict` on the AU, `lipo` check for arm64 + x86_64 |
| Linux x86_64 | VST3, LV2, CLAP, Standalone (`Anneal-<v>-Linux-x86_64.tar.gz`) | Dedicated `.github/workflows/linux.yml` (Ubuntu 24.04) | acceptance tests, built-VST3 host check, ClapCheck, LV2 metadata inspection, dependency checks and editor renders under Xvfb; first native run pending |

The parked combined workflow runs for each published x.y.0 release and by hand (Actions → Platforms → Run workflow,
optionally with a release tag to attach the files to). It skips patch releases: on a private
repository GitHub counts macOS minutes ten times.

### Linux builds (4.0.1)

The dedicated **Linux** workflow runs on main, the Linux development branch, pull requests,
manual dispatch and every published release, including patch releases. It builds the release's
exact tag before attaching its package. Ordinary builds have read-only repository access; only
the release upload job can write release assets. macOS jobs remain parked.

For a local Ubuntu 24.04 build, see README → Build (Linux). `bash build.sh --test --snapshot --package`
builds VST3, LV2, CLAP and Standalone, runs audio/host checks with Xvfb and Openbox when headless, and creates
the tar.gz and SHA-256 file in `dist/`. It never installs a live copy. The archive's `install.sh`
installs per user and stages replacements before touching the old plugin. A package-copy failure
preserves the old installation; 23 fixture packaging checks cover that regression and archive integrity.

The headless desktop includes a window manager and waits for its X11 properties before starting
JUCE. The VST3 test host also initialises its own Xlib before loading the plugin. The initial native
run compiled all formats and passed 112 acceptance checks, but bare Xvfb failed with `BadAtom` at
the editor attach check. That failure is kept visible until the full host check passes.

Native Linux build and runtime results must be checked in Actions before describing a release as
Linux-tested. LV2 inspection checks metadata/discovery, rather than claiming an LV2 DAW audio test.

### Turning on the parked macOS and Windows CLAP builds

The workflow is in the repository as `packaging/ci/platforms.yml`, but it isn't running yet. GitHub
only runs workflows from `.github/workflows/`, and refuses to accept a file there from a login
without the **workflow** permission. The `gh` login on the build PC doesn't have it. To grant it,
run this once and approve it in the browser:

    gh auth refresh -h github.com -s workflow

Then the file moves to `.github/workflows/platforms.yml` in one commit, and the first run checks
the macOS and Windows CLAP builds. Linux uses its dedicated workflow above; enabling it does not
require enabling the parked Mac jobs. macOS builds have not yet been verified.

### Which DAW uses what

| DAW | Windows | macOS | Linux |
|---|---|---|---|
| FL Studio | VST3 (CLAP in 2024+) | VST3, AU | — |
| Ableton Live | VST3 | VST3, AU | — |
| Logic Pro, GarageBand, MainStage | — | **AU** | — |
| Cubase, Nuendo | VST3 | VST3 | — |
| Studio One | VST3 | VST3, AU | — |
| Reaper | VST3, CLAP | VST3, AU, CLAP | VST3, CLAP |
| Bitwig Studio | VST3, CLAP | VST3, CLAP | VST3, CLAP |
| Reason | VST3 | VST3, AU | — |
| Cakewalk / Sonar | VST3 | — | — |
| Ardour, Mixbus | VST3 | VST3, AU | VST3, LV2 |
| **Pro Tools** | AAX: **not built** | AAX: **not built** | — |

### What needs your accounts (I can't do these for you)

- **Pro Tools (AAX).** Needs:
  - a free Avid developer account and the AAX SDK;
  - PACE's signing tools (wraptool) and an iLok account.

  Pro Tools only loads signed AAX plug-ins. Once you have the SDK, JUCE builds AAX with
  `FORMATS ... AAX` and `juce_set_aax_sdk_path(...)`.
- **macOS signing and notarization.** Without an Apple Developer Program membership (99 USD a year),
  macOS blocks downloaded plug-ins. The macOS zip includes `install.sh`, which removes the quarantine
  mark from the copies it installs. For a public release, notarized builds are the right thing:
  - sign with a Developer ID Application certificate (`codesign --options runtime --timestamp`);
  - `xcrun notarytool submit --wait`;
  - `xcrun stapler staple`.

  The workflow can do it once the certificate and an app-specific password are stored as
  repository secrets.

## Before anything goes public

The repository and its releases are **private**. People outside your GitHub account can't download
anything yet, so a KVR listing would have no working link. Nothing has been made public. That's
your decision. Options:

1. Make `zanovwest/Anneal` public. The source becomes public too.
2. A separate public repository (e.g. `zanovwest/anneal-releases`) that holds only the release files.
3. Host the files elsewhere: your own site, Gumroad / Payhip with a 0 price, or itch.io.

**Licences to sort out first** (not legal advice):
- **JUCE 8** is dual-licensed: AGPLv3 (you must publish your source under AGPLv3) or a JUCE
  licence. JUCE offers a free tier for small developers (check the current terms and revenue limit
  at juce.com). A free closed-source plug-in needs that licence; an open-source one can use AGPLv3.
- **VST3**: JUCE 8.0.4 includes Steinberg's VST3 SDK. Check which licence applies to the SDK version
  it includes: older versions are GPLv3 or Steinberg's free VST3 licence agreement, and Steinberg
  later moved the SDK to MIT. Also follow Steinberg's rules for the "VST" name and logo.
- CLAP and clap-juce-extensions: MIT. LV2: ISC. AU: Apple's SDK, no extra licence.

## KVR Audio listing kit

KVR lists products that developers submit. To list Anneal:

1. From your own KVR account, register as a developer ("Qulic" or your name).
2. Add a product, using the text below.
3. Attach the screenshots and a public download link.

You submit it; I can't sign in or create accounts for you.

- **Product name:** Anneal
- **Developer:** Qulic
- **Type / category:** Effect › EQ (also Distortion / Saturation, Spectral)
- **Formats:** VST3, AU, CLAP, LV2, Standalone
- **Platforms:** Windows x64; macOS 10.15+ (Apple Silicon and Intel, universal); Linux x86_64
- **Price:** Free
- **Version:** 3.1.0 (or the newest at the time)
- **Short description (one line):** An automatic perceptual EQ and spectral saturator in one:
  Reveal what's masked, Smooth what dominates, and add harmonics that never alias.
- **Description:**

  > Anneal listens the way you do. A hearing model splits the spectrum into 1024 bands on the
  > cochlea's own (ERB) scale and, about 94 times a second, works out which parts of the mix are
  > hidden and which are overbearing.
  >
  > **Reveal** lifts what's buried in the masking shadow of louder sounds, and **Smooth** tames
  > what dominates its neighbourhood: an automatic EQ that follows the music. **Heat** writes a
  > harmonic series from every spectral peak directly in the frequency domain, only where the
  > spectrum is thin. Because each partial is enriched on its own, there is no aliasing and no
  > intermodulation, and five **Flavours** (Clean, Tape, Tube, Transformer, Exciter) shape its
  > character.
  >
  > **Sidechain** makes room for a vocal or kick in the bands they need, or lifts what they would
  > mask. **Freeze** turns the curve into a static EQ. A true-peak **Ceiling** keeps the output safe.
  > Also: Mid/Side processing, a transient Guard, Listen solos for each part, loudness Match, Delta,
  > latency modes from 22.7 ms to 86.7 ms, undo/redo, factory presets, and a resizable, scalable
  > interface.

- **Features (bullets):**
  - Automatic perceptual EQ on 1024 ERB-spaced bands: Reveal (up to +8 dB) / Smooth (up to −9 dB), Tilt, Response, Range
  - Spectral saturation without aliasing or IMD; Color (even/odd), Focus (body/air), five Flavours
  - Sidechain: Make room / Unmask, with the key's spectrum on the display
  - Freeze the curve; true-peak Ceiling (0.0 / −0.3 / −1.0 dBTP)
  - Mid/Side with separate EQ and Heat routing; transient Guard
  - Listen solos (Reveal only / Smooth only / Heat only), Delta, loudness Match
  - Latency modes: Low 22.7 ms, Normal 44.1 ms, Precise 86.7 ms (48 kHz)
  - Undo/redo, factory presets, resizable UI (870 × 480 to 1740 × 960)
- **System requirements:**
  - Windows 10/11 x64;
  - macOS 10.15 or later (Apple Silicon or Intel);
  - Linux x86_64 (glibc 2.39 / Ubuntu 24.04 or newer);
  - a VST3, AU, CLAP or LV2 host.

  CPU: about 2.5 % of one core in Low latency at 48 kHz on a Core Ultra 9.
- **Screenshots:**
  - `snapshots/anneal_default_100.png` (main);
  - `anneal_sidechain_100.png`;
  - `anneal_midside_100.png`;
  - `anneal_hot_100.png`.

  All are 1160 × 640, and `_200` versions exist for high-DPI.
- **Links:** product page and download, once one is public (see above).

## Plugin Alliance

Plugin Alliance is a curated commercial store. It sells plug-ins from partner companies, mostly
paid, under its own licensing system. There is no self-service form for listing a free plug-in.
The only route is a partnership: contact them through the "Developers" or contact link on
plugin-alliance.com, introduce Anneal, and ask whether they'd carry a free title. Expect them to
want it to be:

- signed and notarized;
- AAX-capable;
- integrated with their licensing.

Expect them to possibly decline a free product. That conversation is yours to have; I can draft the
email if you'd like. Alternatives that list free plug-ins openly:

- KVR (above);
- Plugins4Free;
- Bedroom Producers Blog (they write about notable free releases; send them a press note);
- Audio Plugin Deals' free section.
