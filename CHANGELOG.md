# Changelog

Newest first. Each completed, tested update gets a versioned entry (see README → Minute updates).
The plugin version shown in the DAW and in the setup's name comes from `project(... VERSION)` in
CMakeLists.txt.

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
