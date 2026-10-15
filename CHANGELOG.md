# Changelog

Newest first. Each completed, tested update gets a versioned entry (see README → Minute updates).
The plugin version shown in the DAW and in the setup's name comes from `project(... VERSION)` in
CMakeLists.txt.

## 4.0.0 — 2026-10-14 17:46

The second major update. Anneal can now match the tone of a reference, and you can draw where it
works. With the new controls left alone, everything sounds exactly as in 3.1 (tested: bit-identical).

New
- **Reference match** (new REFERENCE group, teal). Give Anneal a reference and it steers its auto EQ
  toward that reference's long-term tone:
  - **Drop an audio file** (wav, aiff, flac, mp3, ogg) on the display, or use *Load a reference
    file…* in the Reference menu. It's analysed in the background, never in the audio thread, and
    Reference switches to Match. The display outlines itself in teal while you drag a file over it.
  - **Learn key**: learns the tone of whatever plays into the sidechain input (another mix, a
    bounce, a reference track on its own channel). The live line under Amount counts the seconds
    learned. Choosing it with nothing routed keeps the reference you had.
  - **Match** compares the reference with the last ~3 s of this track, both at the same overall
    level, and adds the smoothed difference (about 1.5 ERB either side, at most ±6 dB × Amount) to
    the auto EQ. Range, Freeze and Paint treat it like the rest of the curve. Measured: a mix with a
    +6 dB shelf at 4 kHz (5.5 dB brighter than a flat reference) comes out **5.3 dB closer** to it at
    Amount 100 % (8 kHz −3.6 dB, 500 Hz +2.0 dB), in Stereo and in Mid/Side. A reference learned from
    the key is within 0.28 dB of the same audio analysed as a file (200 Hz – 16 kHz).
  - **Amount** 0–100 % (default 50 %): how far Match steers. The live line says how far it is
    steering now ("matching −2.4 dB").
  - The reference is a teal line on the display at the level of the output, with a REF tag naming it.
    It's saved with the project. *Clear the reference* is in the menu.
  - Off is the default; projects from before 4.0 open Off with no reference. Off, Learn key and
    Amount 0 % are bit-identical to 3.1 (max difference 0).
- **Paint** (pill in the header): draw on the display how much of the auto EQ and of Heat acts in
  each region, beyond the two Range handles.
  - Drag in the EQ lane or the Heat lane: the top of the lane is 100 %, the bottom none. Alt-drag or
    right-drag erases back to 100 %, a double-click resets that lane, Escape leaves paint mode. Each
    stroke is one undo step.
  - The EQ amount covers everything the auto EQ does there (Reveal, Smooth, Tilt, sidechain and
    reference). Measured: painting the EQ to 0 % over 2–5 kHz takes a −9.0 dB Smooth cut at 3150 Hz to
    0.0 dB and leaves the −7.7 dB cut at 800 Hz alone. Painting Heat to 0 % there takes a 1.5 kHz
    tone's 2nd and 3rd harmonics from −6 / −10 dB to below −170 dB and leaves its 4th (6 kHz) as it was.
  - Painted-out regions stay dimmed, like outside the Range, and the hover read-out shows the amounts.
    Saved with the project (64 points per lane); 100 % everywhere is the default and bit-identical to 3.1.
- **Keyboard shortcuts**: Ctrl+Z undo, Ctrl+Shift+Z or Ctrl+Y redo (Cmd on macOS), once you've
  clicked in Anneal's window and if the DAW passes keys to plug-ins. Keys Anneal doesn't use go back
  to the DAW.

Changed
- **Wider window**: 1312 × 640 (was 1160 × 640) for the REFERENCE group, resizable from 984 × 480 to
  1968 × 960. Everything else is where it was.
- The project format is version 2: it adds the reference and the paint. 3.x projects open as they
  were; projects saved by 4.0 don't open their reference or paint in 3.x (the knobs do).
- The Windows CLAP is on the release again: `Anneal-4.0.0-Windows-CLAP.zip`, with the two new
  parameters (27 in all).

Fixes
- **Freeze** lost the held curve when the DAW stopped and started playback (the curve was re-learned
  from the first moment after the restart) or when the latency mode changed. A frozen curve now
  stays exactly where it was (−9.0 dB before and after both).
- Switching the **Ceiling** back on could pull quiet audio down for about 80 ms: its detector still
  remembered loud audio from the last time it was on. It now follows the audio while off.

Tests: 112/112 acceptance checks (19 new: the two fixes above, Reference match (identical when off,
the shelf moved 5.3 dB, Learn key, files analysed off the audio thread, Freeze holds the match), Paint
(identical at 100 %, EQ and Heat paint, a stroke is one undo step), reference and paint in saved projects
and older projects, the shortcuts, paint strokes and a double-click reset on the display, a dropped file,
the display following Learn key, and the REFERENCE group in the layout). 20/20 host checks on the built
VST3 (new: the Reference parameters, Learn key through the sidechain bus, and the reference and paint
travelling in the VST3 state into a new instance: Heat painted to 0 % there writes nothing, −300 dB,
against −15.3 dB unpainted). 25/25 isolated installer checks, ClapCheck 10/10 on the Windows CLAP, file
version 4.0.0 on the VST3 and the app. Editor rendered at 100/150/200 % and
at the new minimum and maximum sizes, plus Reference, Learn key, Paint, painted and file-drop snapshots.
CPU at 48 kHz on this machine: Stereo Low 2.0 %, Normal 1.8 %, Precise 2.7 % of one core; Mid/Side
Low 3.7 % (64), Normal 2.8 % (512), Precise 4.3 % (64), the same as 3.1.0. With Reference Match at
100 % and Paint on: Low 2.2 %, Normal 1.9 %, Precise 2.8 %. Slowest 0.1 % of 64-sample blocks: Stereo
Low 8 %, Precise 25 %; Mid/Side Low 19 %, Precise 36 % of the deadline.

## 3.1.0 — 2026-10-13 23:06

New
- **CLAP for Windows**: `Anneal-3.1.0-Windows-CLAP.zip` on the release, for Bitwig, Reaper,
  FL Studio 2024+ and other CLAP hosts. Same sound, controls, sidechain input and latency as the
  VST3. Copy `Anneal.clap` into `C:\Program Files\Common Files\CLAP` (or
  `%LOCALAPPDATA%\Programs\Common\CLAP` just for you). Checked with a small CLAP host
  (`tools/ClapCheck.py`, 10/10):
  - ports: Input and Sidechain in, stereo Output;
  - 25 parameters;
  - latency 1091 samples at 48 kHz, the same as the VST3;
  - 2 s of audio processed cleanly.

  Not tried inside a DAW yet.
- **macOS (Apple Silicon and Intel) and Linux, prepared, not built yet.** The project now targets:
  - on macOS, one universal binary (arm64 + x86_64) with AU (Logic Pro, GarageBand, MainStage),
    VST3, CLAP and the standalone app, for macOS 10.15 or later;
  - on Linux, VST3, LV2, CLAP and the standalone app.

  The interface uses Helvetica Neue on macOS and the system sans on Linux. A GitHub Actions
  workflow builds them, tests them (acceptance tests, host check, CLAP check, `auval -strict`,
  a both-architectures check) and packages them with an install script. It isn't running yet:
  GitHub needs one extra permission on the build PC's login first (see "Turning on the macOS and
  Linux builds" in docs/DISTRIBUTION.md). Until then there are no macOS or Linux downloads, and
  that code hasn't been compiled for those systems.
- **docs/DISTRIBUTION.md**:
  - which DAW uses which format;
  - what Pro Tools (AAX) and notarized macOS builds need;
  - a ready-to-paste KVR listing;
  - notes on Plugin Alliance and on licences before a public release.

The Windows VST3 is unchanged: same sound and look (all editor snapshots are byte-identical to
3.0.1's).

Tests: 93/93 acceptance checks, 17/17 host checks on the built VST3, 25/25 isolated installer
checks, ClapCheck 10/10 on the Windows CLAP, and file version 3.1.0 on the VST3 and the app.
CPU at 48 kHz on this machine: Stereo Low 2.0 %, Normal 1.8 %, Precise 2.7 % of one core; Mid/Side
Low 3.7 % (64), Normal 3.1 % (512), Precise 4.4 % (64). The DSP code is the same as 3.0.0's, so
3.0.0's higher figures (2.5 / 2.1 / 3.4 %) were the machine's load at the time, not a slowdown.
Slowest 0.1 % of 64-sample blocks: Stereo Low 10 %, Precise 24 %; Mid/Side Low 17 %, Precise 37 %
of the deadline.

## 3.0.1 — 2026-10-13 22:54

Fixes
- The plug-in file's Windows version (Explorer → Properties → Details, and what some plug-in
  managers show) said **0.2.1** in every release since 0.2.1, including 3.0.0. The plug-in itself
  always reported the right version to the DAW. JUCE only rewrote that version stamp when the icon
  changed. The build now refreshes it whenever the version changes, and `build.ps1` refuses to
  continue if the VST3's or the standalone app's file version differs from the release version.
  Sound, controls and projects are unchanged.

Tests: 93/93 acceptance checks, 17/17 host checks on the built VST3, 25/25 isolated installer
checks, plus the new file-version check (failed on the 3.0.0 build: 0.2.1; passes on 3.0.1).

## 3.0.0 — 2026-10-13 19:05

The first major update. Anneal can now listen to another track, hold its curve, and keep its
output under a true-peak ceiling. Everything from 0.2.6 sounds the same with the new controls off.

New
- **Sidechain** (new SIDECHAIN group, violet). Route another track into Anneal's sidechain input
  and pick what it does:
  - **Make room**: dips the main only where the key is loud, in the key's own bands. A vocal key
    over a music bus pulls the bus down at the vocal's formants and leaves the rest. Measured with
    a 2 kHz key at Amount 100 %: −9.0 dB at 2 kHz, 0.0 dB at 300 Hz and 8 kHz.
  - **Unmask**: lifts the main where the key would mask it (+5.6 dB at 2 kHz in the test).
  - **Amount** 0–100 % (default 50 %). The ring's live line shows how far it is ducking or
    lifting right now. The key's spectrum is drawn on the display as a violet line, with a KEY tag.
  - With no key routed, or a silent one, the output is identical (max difference 0). The key's FFT
    runs only while Sidechain is on. Off is the default, and projects from earlier versions open Off.
  - Routing: in Cubase, enable the plug-in's side-chain input and send the key to it. In FL Studio,
    send the key's mixer track to Anneal's track with *Sidechain to this track*, then pick that
    track as Anneal's sidechain input in the wrapper's *Processing* tab. Both were tested here
    through JUCE's VST3 host, not inside the DAWs.
- **Freeze** (AUTO EQ title row): holds the auto-EQ curve where it is, so Anneal works as a static
  EQ shaped by the music it just heard. Heat, Tilt and Range stay live. The sidechain dip is part of
  the curve, so it holds too. The display shows "AUTO EQ FROZEN". Measured: a −9.0 dB cut stays at
  −9.0 dB after the music changes (without Freeze it returns to 0.0 dB).
- **Ceiling** Off / 0.0 / −0.3 / −1.0 dBTP (OUTPUT): a true-peak look-ahead limiter at the very end,
  after Mix and Output, so Heat and an Output boost can't clip. Peaks are found 4× between the
  samples (16-tap windowed sinc), with 1.25 ms look-ahead and 80 ms release. With a sine at fs/4 and
  music driven 12 dB over, the true peak (measured with an independent 8× meter) lands from
  0.01 dB under to 0.10 dB over the ceiling. The output meter shows the reduction in orange under
  the peak, holding the deepest value for 1.5 s. Off is a pure delay (bit-exact) and the default.
- **Undo / Redo**: two arrows in the header next to Latency. Each knob drag, typed value or menu
  choice is one step, and loading a preset is one step. Host automation isn't recorded. Undo
  history starts fresh when a project loads.
- **Wider layout**: 1160 × 640 (was 1000 × 620), resizable from 870 × 480 to 1740 × 960, with a
  SIDECHAIN group between HEAT and OUTPUT.

Changed
- **Latency is 1.4 ms longer** (67 samples at 48 kHz) in every mode: the Ceiling's look-ahead.
  It is reported whether the Ceiling is on or off, so turning it on never shifts timing. At 48 kHz:
  Low 22.7 ms, Normal 44.1 ms, Precise 86.7 ms. Hosts compensate automatically.
- The plug-in has a second, optional input bus (the sidechain). Hosts that route nothing to it
  see a stereo effect as before.

Fixes
- Hover read-outs with whole decibels could show four decimals ("heat −24.8106 dB"); they now round.

Compatibility: every parameter ID is unchanged. Four parameters are added (Freeze, Sidechain,
Sidechain Amount, Ceiling); projects from earlier versions open with all of them off.

Tests: 93/93 acceptance checks (10 new: a silent key changes nothing, Make room ducks only the
key's band, Unmask lifts, the processor's sidechain bus, Freeze holds, true peak under each
ceiling, Ceiling Off is a pure delay, undo/redo with a preset as one step, older projects open with
the new controls off, and the 3.0 layout with every control inside and none overlapping). 17/17 host
checks on the built VST3 (new: the sidechain bus through VST3, a routed key ducking the main −9.0 dB;
latency checks now include the look-ahead). 25/25 isolated installer checks. Editor rendered at
100/150/200 % and at minimum/maximum size, plus new Sidechain and Freeze snapshots.
CPU at 48 kHz on this machine (Ceiling and Sidechain off): Stereo Low 2.5 %, Normal 2.1 %, Precise
3.4 % of one core; Mid/Side Low 4.6 % (64), Normal 3.5 % (512), Precise 5.3 % (64). That is above
0.2.6's run (1.9 / 1.7 / 2.7 %) and close to 0.2.5's (2.4 / 2.1 / 3.4 %); the runs weren't side by
side and the machine's load varies, so treat it as unconfirmed. Slowest 0.1 % of 64-sample blocks:
Stereo Low 10 %, Precise 28 %; Mid/Side Low 20 %, Precise 43 % of the deadline. Ceiling-on and
Sidechain-on CPU weren't benchmarked.

## 0.2.6 — 2026-10-13 18:23

New
- **Saturation flavours**: a **Flavour** menu on the HEAT title row picks the character of the
  harmonics Heat writes:
  - **Clean**: Anneal's own saturation, exactly as before.
  - **Tape**: soft and mostly even. The 2nd stays, the 3rd is halved, the 5th and 6th fall
    away (about −17 dB against Clean), and harmonics that land high are rolled off gently
    (−3 dB at 5 kHz, −6 dB at 9 kHz). Warm without fizz.
  - **Tube**: a strong 2nd. About +3.5 dB more 2nd than Clean, with everything above it pulled
    back (the 2nd sits 25 dB over the 3rd instead of 11 dB).
  - **Transformer**: odd harmonics, driven by the low end, the way an iron core saturates. A
    60 Hz note gets a 3rd 17 dB above its 2nd (13 dB more 3rd than Clean); the lower the note, the
    harder it drives (+12 dB at 30 Hz), and from about 1 kHz up it does almost nothing (a 2 kHz
    note's 3rd is 37 dB lower than Clean).
  - **Exciter**: air only. Partials below 1.5 kHz get no harmonics at all; from 3 kHz up they
    get bright even and odd harmonics, driven 6 dB harder than Clean (a 4 kHz note's 2nd at
    8 kHz: +6 dB).

  Color still moves the even/odd balance on top of the flavour, and Focus and the model still
  decide where the harmonics are written. The harmonic bars under Color follow the flavour
  (Transformer is drawn for a 120 Hz note, Exciter for a 4 kHz one). The pill turns bronze when a
  flavour is on. A flavour changes only the harmonics: the auto EQ is untouched, and at Heat 0
  every flavour sounds the same. Switching while playing is click-free, in Stereo and Mid/Side.
  Flavour is a host parameter too (type `tape`, `tube`, `transformer`/`iron`, `exciter`/`air`
  or `clean`). It defaults to Clean, and projects from earlier versions open in Clean, so they
  sound the same. Factory presets set Clean, so they also sound as before; choosing another
  flavour after a preset marks it `*`.

Fixes
- Double-clicking a Range handle (to reset it) sent the host a second "begin edit" for the same
  parameter inside the first, so FL Studio and other hosts recording touch automation saw
  begin, begin, end, end. The reset now happens inside the click's own edit: one begin, one end.
- The Heat ring moved 8 px down, so its top lines up with Reveal and Smooth and the new Flavour
  pill above it has room.

Tests: 83/83 acceptance checks (14 new: Tube, Tape (two), Transformer, Exciter in Stereo and
Mid/Side, flavours change only the harmonics, click-free switching, bars = synth law with Clean
identical to 0.2.5's formula, older projects open Clean and typed values, presets keep Clean, the
Flavour menu on the HEAT title row, and the Range-handle double-click as one gesture for Low and
High, which failed before the fix). 16/16 host checks on the built VST3 (new: Flavour through
VST3, with all five names, typed values, a 220 Hz tone that Tube enriches and Exciter leaves
alone, and a sweep during playback). 25/25 isolated installer checks. Editor rendered at
100/150/200 % and at minimum/maximum size, plus a new Transformer snapshot.
CPU at 48 kHz on this machine: Stereo Low 1.9 %, Normal 1.7 %, Precise 2.7 % of one core;
Mid/Side Low 3.6 % (64), Normal 2.9 % (512), Precise 4.3 % (64). Tape, the only flavour with extra
per-bin work, measures the same as Clean in the same run (Low 1.94 % vs 1.95 % at 64 samples,
Precise 2.69 % vs 2.72 % at 512). Slowest 0.1 % of 64-sample blocks: Stereo Low 10 %, Precise 22 %;
Mid/Side Low 16 %, Precise 35 % of the deadline. 0.2.5's figures (2.4 / 2.1 / 3.4 %) were taken an
hour earlier and not re-run side by side; the drop is the machine being less busy, not a
speed-up. Clean runs the same code as before plus one multiply by 1 per harmonic.

## 0.2.5 — 2026-10-13 18:01

New
- **Listen modes**: a new **Listen** menu at the top of OUTPUT lets you hear one part of what
  Anneal does on its own:
  - **Reveal only**: just what Reveal lifts out of the masking shadow.
  - **Smooth only**: just what Smooth takes away from the bands that poke out.
  - **Heat only**: just the harmonics Heat writes, with no dry signal.

  The menu lights up in that part's colour, the display says what you're hearing, and Delta dims
  while a solo is on (the solo overrides it). The EQ curve and the knobs' live read-outs keep
  showing the full processing. The solos are exact: the normal output minus the Reveal solo plus
  the Smooth solo gives back your input, and so does the normal output minus the Heat solo. Tilt
  is a static slope, so it is in none of them. Solos follow Mix and Output, Bypass still wins, and
  Match holds its gain while you listen, so turning a solo off doesn't jump in level. Switching
  while playing is click-free in every latency mode, in Stereo and Mid/Side. Listen is a host
  parameter too (you can type `reveal`, `smooth`, `heat` or `off`). It defaults to Off, presets
  leave it alone, and projects from earlier versions open with it off, so they sound the same.

Fixes
- The "Heat:" routing menu (Mid/Side) sat 4 px higher than the "EQ:" menu. Both now line up with
  the group titles. A new layout check also makes sure no two controls overlap.

Tests: 69/69 acceptance checks (12 new: the exact Reveal/Smooth split in Low, Precise and Mid/Side;
Heat only = just the harmonics; Mix/Bypass with a solo; Match holding; click-free switching; the
display while soloing; older projects and typed values; no overlapping controls; routing menus on
the title row; the Listen menu following the parameter). 15/15 host checks on the built VST3 (new:
Listen through VST3). 25/25 isolated installer checks. Editor rendered at 100/150/200 % and at
minimum/maximum size, plus two new Listen snapshots.
CPU at 48 kHz on this machine, unchanged from 0.2.4 measured side by side: Stereo Low 2.4 %, Normal
2.1 %, Precise 3.4 % of one core (512 samples); Mid/Side Low 4.6 % (64), Normal 3.4 % (512),
Precise 5.3 % (64). Slowest 0.1 % of 64-sample blocks: Stereo Low 10 %, Precise 29 %; Mid/Side
Low 19 %, Precise 43 % of the deadline. 0.2.4's own figures were measured on a different machine.
A solo costs a little extra only while it is on.

## 0.2.4 — 2026-10-13

New
- **Mid/Side mode**: choose Stereo or Mid/Side in the header. In Mid/Side, the centre and sides
  have independent perceptual models. Route Reveal, Smooth and Tilt with **EQ: Both / Mid / Side**
  and route saturation separately with **Heat: Both / Mid / Side**. For example, put Heat on Mid
  and Reveal on Side. The display shows Mid as a solid curve and Side as a dashed curve.
- Routing changes fade over 40 ms without changing latency. Mono buses keep linked mono processing.
  Older sessions reopen in linked Stereo, and factory tone presets preserve routing choices.

Tests: 57/57 acceptance checks, including 12 new Mid/Side checks; 14/14 built-VST3 host checks,
including routing automation and saved routing state. 25/25 isolated installer checks. The real
editor rendered in Stereo and Mid/Side at 100/150/200 % and minimum/maximum sizes.
Measured at 48 kHz on this machine: Mid/Side Low 3.71 %, Normal 2.88 %, Precise 4.59 % of one core
(Low/Precise at 64 samples, Normal at 512). Stereo remains 1.99 % Low / 1.78 % Normal / 2.98 %
Precise at 512 samples. Independent analysis costs extra CPU; timings vary with host and machine.

## 0.2.3 — 2026-10-13

Fixes
- Uninstalling while a DAW has Anneal loaded now moves the locked DLL aside before removing the
  bundle. Setup records the actual VST3 destination so custom install folders work at uninstall
  time. A later install cleans the moved-aside DLL after the DAW releases it.
- Dragging Range Low after automation crosses the range limits now starts from the displayed
  range instead of jumping below the stored Range High. The displayed high limit stays in place
  while dragging Low. Clicking without dragging leaves the stored values unchanged.

Tests: 45/45 acceptance checks, including five range-handle interaction checks; 13/13 host checks
on the patched VST3. The installed 0.2.2 copy also passed 13/13 host checks. Editor snapshots
rendered at 100/150/200 % and at the minimum and maximum window sizes.
Installer regression: 25/25 sandbox checks, including custom VST3 destination metadata,
uninstall while the replacement DLL is loaded, and cleanup after release on a later install.

## 0.2.2 — 2026-10-13 14:50

New
- **Guard (transient guard)**, a new knob in AUTO EQ under Response. Turn it up and drum hits,
  plucks and other attacks come through with their punch intact: in the bands where a hit lands,
  the auto EQ steps back toward 0 dB for the attack and Heat doesn't smear harmonics over it, then
  both return. The EQ curve also stops chasing the hits, so nothing ducks right after them.
  Sustained sounds are treated exactly as before. Works in every latency mode. Measured on a
  plucked 2 kHz tone with Smooth at 100 %: the attack was cut by 6.2 dB (Low) / 8.6 dB (Precise),
  with Guard at 100 % by 0.0 / 0.2 dB; harmonics written over the attack drop by about 10 dB.
  Guard is Off by default, presets leave it alone, and projects saved with earlier versions open
  with it off, so they sound exactly as before.

Fixes
- If automation pushed Range Low above Range High, Anneal worked from Low to 1.5 × Low (as
  intended), but the display drew the handles at the raw values and dimmed the wrong part. It now
  shows the range Anneal actually uses.

Tests: DSP 40/40 (new: Guard lets attacks through in Low and Precise, a sustained ring is cut the
same, no harmonics over attacks while a steady tone keeps them, old projects load with Guard off,
display range = engine range), host 13/13 (new: Guard automated while playing), setup 16/16
(0.2.1 said 17/17; it was 16 then too).
CPU at 48 kHz stereo: Low 2.4 %, Normal 2.1 %, Precise 3.4 % of one core (Precise +0.1 %: the
attack detector runs on every frame, so Guard can be turned up mid-song without a warm-up).
Slowest 0.1 % of blocks at 64 samples: Low 10 %, Precise 28 % of the deadline (unchanged).

## 0.2.1 — 2026-10-13

New
- **Icon.** Anneal's own ring knob as its mark: the arc runs through the temper colours of heated
  steel (blue, purple, bronze, straw) to a white thumb, on a dark tile. It's on the setup, the
  standalone app, the Start menu and Apps entries, and the plug-in's folder in Explorer. Drawn as
  vectors and rendered separately at each Windows size (16 to 256 px), with a bolder version for
  the smallest ones.

Tests: DSP 34/34, host 12/12, setup 17/17 (new: the installed plug-in folder shows its icon in Explorer).

## 0.2.0 — 2026-10-13

New
- **Redesigned interface.** Cool near-black graphite instead of brown, so the gold (lift), blue
  (cut) and orange (heat) accents stand out. One large display: the live auto-EQ curve in front
  with a soft glow, the spectrum behind it as smooth flowing lines (no more blocky plateaus),
  and a smooth heat lane underneath. Ring knobs with the value inside (click to type); under
  Reveal, Smooth and Heat a live line says how hard each is working right now ("lifting
  +2.9 dB", "cutting −3.6 dB", "harmonics −44 dB"), also shown as a thin inner arc. Color and
  Focus are sliders with named ends, and next to them bars show the harmonics Heat writes for
  the current Color. Match, Delta and Bypass are in the header; Match shows its gain.
- **Lower latency.** A Latency menu in the header: **Low 21 ms** (default for new instances),
  Normal 43 ms, Precise 85 ms (the original engine, finest bass detail). FL Studio re-aligns
  tracks automatically when it changes. Projects saved with earlier versions open in Precise,
  so they sound exactly as before. Low is also lighter on CPU (2.4 % vs 3.3 %).

Fixes
- During this update Smooth briefly stopped cutting (a new display value had the same name as
  the cut limit); the tests caught it before release, and a Low-mode test now guards it too.

Tests: 34/34 DSP checks (identity, latency and Smooth/Heat in every mode, old projects load as
Precise), 12/12 host checks (latency switching through the host), 16/16 setup checks.
CPU at 48 kHz stereo: Low 2.4 %, Normal 2.1 %, Precise 3.3 % of one core.

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
