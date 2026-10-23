# Changelog

Newest first. Each completed, tested update gets a versioned entry (see docs/ROADMAP.md).
The plugin version shown in the DAW and in the setup's name comes from `project(... VERSION)` in
CMakeLists.txt.

## 5.2.3 — 2026-10-22 17:57

New
- **See the stereo image on the display.** The WIDTH lane now has a soft shade behind its line that
  shows how stereo the sound you hear is at each frequency. It's dark where the sound is mono and
  brighter the more of it is in the sides, at its brightest where the sides are as loud as the centre.
  So you can see Width and Mono bass working on the actual music: turn Mono bass up and the shade
  goes dark below it; push Width and it brightens. Hovering adds "sides −8 dB" (the sides' level
  against the centre there) or "sides mono".
  - It is what you hear from the main signal: the input's centre and sides through the auto EQ (in
    Mid/Side, its Mid and Side curves), Width with its paint, and Mono bass. It works the same at Zero
    latency and with Spread load; the Heat harmonics aren't in it, and a Listen solo doesn't change it.
  - Updated 37.5 times a second, in three small steps on separate frames, so it never adds to a
    single frame's peak. Mono tracks show no shade.
  - Measured on a test mix: sides 6 dB under the centre read −6.02 dB; Width 200 % takes them to
    0.00 dB and Width 0 % to mono; Mono bass at 400 Hz makes 200 Hz mono and leaves 1.5 kHz at −6.02 dB.

Fixes
- None found. The bug pass looked at yesterday's Width paint code (the per-bin share, the Side filter
  design at zero latency, the saved paint, the lanes' hit-testing) and today's build logs.

Tests: 173/173 acceptance checks, 3 new:
- the image per band through Width (−6.02 dB; 200 %: 0.00 dB; 0 %: mono), centre-only and sides-only bands;
- it follows Mono bass, Zero latency and Mid/Side, and mono tracks have none;
- the display shows it (1.5 kHz −6.02 dB, 500 Hz mono).

24/24 host checks on the built VST3 and 25/25 setup checks pass.

CPU at 48 kHz: Stereo Low 2.0 %, Normal 2.8 %, Precise 4.7 %; Mid/Side Low 4.0 %, Normal 5.3 %,
Precise 8.0 % (5.2.2: 2.0 / 2.8 / 4.5 and 3.9 / 5.2 / 7.8; the image costs a little, about 0.1 % of
a core in Precise, part of the difference is run-to-run variation). The slowest 0.1 % of 64-sample
blocks are where they were: Stereo Low 5 %, Precise 13 %; Mid/Side Precise 22 % of the deadline.

## 5.2.2 — 2026-10-21 21:06

New
- **Paint the width.** Width (SHAPE) used to act on the whole spectrum. Now you can paint where it
  acts, like the EQ and Heat paint: turn on **Paint** and drag in the new **WIDTH lane**. Top = all of
  Width acts there, bottom = none, so that region keeps its own stereo width. For example, Width 150 %
  with the lows painted down widens only the top end and leaves the bass and low mids as they were.
  Alt- or right-drag erases, a double-click resets the lane, and each stroke is one undo step.
  - Works in every latency mode, with Spread load and at Zero latency (there it shapes the Side filter).
    Mono bass still works on top.
  - Saved with the project. Projects from earlier versions open with Width acting everywhere, as they
    were made; their EQ and Heat paint stay as saved.
  - Measured: Width 0 % painted out below 1 kHz keeps 300 Hz sides at 0.00 dB and takes 3 kHz sides
    to −161 dB (zero latency: −78 dB); Width 200 % at a 50 % share above 1 kHz lifts the sides there
    by 3.52 dB and below by 6.02 dB; the centre doesn't move (0.00 dB).
- **WIDTH lane on the display** (rose, between the EQ and Heat lanes): the stereo width you hear at
  each frequency, mono at the bottom, as is in the middle ("100 %"), 200 % at the top: Width by its
  painted share, then Mono bass. You can now see Mono bass and the painted Width working. The hover
  read-out adds "width 150 %" where it isn't 100 %. The EQ lane is 28 px shorter to make room.

Fixes
- None found in the plug-in. The bug pass looked at today's Spread load code, the processor's host
  paths and today's build logs. Silenced a compiler warning in the host check (a loop counter hid
  another one).

Tests: 170/170 acceptance checks, 7 new:
- Width paint at 100 % everywhere (also after painting and erasing) is bit-identical;
- Width 0 % painted out below 1 kHz: the lows stay stereo, the highs go mono, in Low and Precise and
  with Spread load;
- Width 200 % at a 50 % share: +3.52 dB where painted, +6.02 dB elsewhere;
- at Zero latency through the Side filter;
- saved with the project; older projects and malformed lists load at 100 %;
- the display's WIDTH lane sits between EQ and Heat and shows the width heard (Width, paint, Mono bass);
- paint strokes in the WIDTH lane change only Width's paint, one undo step, double-click resets.

24/24 host checks on the built VST3; the reference-and-paint check now also carries Width's paint
into a new instance (at Width 0 % its sides stay at −14.6 dBFS; unpainted −121 dBFS). 25/25 setup
checks pass.

CPU at 48 kHz unchanged (Stereo Low 2.0 %, Normal 2.8 %, Precise 4.5 %; Mid/Side Low 3.9 %, Normal
5.2 %, Precise 7.8 %; with Punch, Width and Mono bass on: Low 2.0 %, Precise 4.7 %).

## 5.2.1 — 2026-10-21 18:28

New
- **Spread load** (Latency menu → *Spread load +3.3 ms*, off by default). Anneal updates its EQ 300
  times a second, and until now each update was done all at once, inside whichever audio block it fell
  in. At small buffers (64 samples and less) that block had to carry a whole update while the blocks
  around it had almost nothing to do. Spread load splits each update into four steps and runs them
  through the next 3.3 ms, so every block carries about the same small share. Total CPU stays the
  same; the peaks the audio thread has to survive get lower, which leaves more room for your other
  plug-ins before anything crackles.
  - Costs one update step of latency, 3.3 ms at any sample rate: at 48 kHz Low 22.7 → 26.1 ms,
    Normal 44.1 → 47.4 ms, Precise 86.7 → 90.1 ms. The host re-aligns tracks, as for the latency
    modes. Not automatable.
  - **The sound is exactly the same**, one step later: the tests compare the output with and without
    it sample for sample in Low, Normal and Precise, with Reveal, Smooth, Heat, Mid/Side, Guard, Punch,
    Width, Mono bass and a sidechain key all working (largest difference 0.000000000).
  - Rests at Zero latency (the menu says so), where it would add latency for nothing.
  - Slowest 0.1 % of blocks, at 48 kHz, as a share of the time the block may take:
    - Low Stereo at 64 samples: 5 → 3 %;
    - Precise Stereo at 64: 12 → 7 %;
    - Precise Mid/Side at 64: 21 → 11 %;
    - Precise Mid/Side at 32: 42 → 16 % (the slowest 1 %: 40 → 15 %).
  - Saved with the project ("Spread Load"); projects from earlier versions open with it off.

Fixes
- None found. The bug pass looked at the engine and 5.2.0's 300 updates a second (the per-position
  overlap-add gain, every time constant now in seconds), the zero-latency filters, the processor's
  host paths (prepare, reset, latency and Zero switches, state), yesterday's build log and benchmark,
  and the interface snapshots.
- Removed four outdated snapshots of the window size before 5.0 that the snapshot tool no longer makes.

Tests: 163/163 acceptance checks, 7 new:
- with Spread, the output equals the output without it one hop later in every mode (neutral, EQ and
  Heat, Mid/Side with Guard, Punch, Width, Mono bass and a key), in random blocks of 1 to 700 samples;
- a 32-sample block runs at most one of a frame's four steps (64: two; without Spread: all four);
- the engine reports N + hop and an impulse comes out there;
- through the processor, with the default sound in each mode: one hop more, the same output;
- the wet (Mix 100 %) and dry (Mix 0 %) paths arrive at the reported latency; at Zero latency it rests;
- switching Spread, the modes and Zero while playing (Mid/Side in one step): the right latency, clean;
- saved with the project, older projects open with it off, not automatable.

24/24 host checks on the built VST3, 1 new: Spread load through the host, with the latency reported
(1251 samples in Low) and the impulse in place at Mix 0 % and 100 %, then back to 1091. 25/25 setup
checks pass.

CPU at 48 kHz, as 5.2.0 with Spread off (Stereo Low 2.0 %, Normal 2.8 %, Precise 4.5 %; Mid/Side
Low 3.9 %, Normal 5.2 %, Precise 7.9 %) and the same with it on.

## 5.2.0 — 2026-10-20 14:34

New
- **The auto EQ now updates 300 times a second** in every latency mode (you asked: "make it 300/s
  instead of 94/s"). Before, Normal and Precise updated 94 times a second and Low 188. The EQ follows
  the music in finer steps; how fast it reacts is set in seconds, so its timing and character are
  the same, only smoother in time. 5.1.1 is untouched; this is a new version.
- At every sample rate the step is the sample rate / 300 (160 samples at 48 kHz, 147 at 44.1 kHz).
  160 doesn't divide the frame sizes, so the overlap-add now scales each position in a step on its
  own; with neutral settings the output is still the input exactly (within 0.0000002), in every
  mode and block size.
- Latency is unchanged in every mode (it depends on the frame size, not the step).
- CPU: more updates cost more. At 48 kHz, against 5.1.1:
  - Stereo: Low 1.1 → 2.0 %, Normal 0.8 → 2.8 %, Precise 1.3 → 4.5 % of one core;
  - Mid/Side: Low 2.2 → 3.9 %, Normal 1.5 → 5.2 %, Precise 2.2 → 7.8 %;
  - Zero latency: Stereo 2.4 → 4.1 %, Mid/Side 3.9 → 7.1 %.

  The slowest 0.1 % of 64-sample blocks take at most 21 % of the deadline (Mid/Side Precise), so it
  stays safe in real time even at tiny buffers.

Fixes
- None (a direct request, not a daily run).

Tests: 156/156 acceptance checks, 3 new: 300 updates/s in Low, Normal and Precise at 44.1 / 48 /
88.2 / 96 / 192 kHz; neutral output equal to the input with a 160-sample step in every mode; Precise
at 48 kHz running 300 frames/s (was 93.75). 23/23 host checks on the built VST3, setup checks pass.

## 5.1.1 — 2026-10-19 19:37

New
- **Faster engine, part 2** (the CPU item on the roadmap): the hearing model now converts its levels
  to and from dB in batches, four values at a time, with Anneal's own fast log and exp (no tables, no
  new libraries). Before, it called the standard log10 / pow one value at a time, about 5,000 times
  per frame. The sound is unchanged: over 5,031 frames of music in Low and Precise, with Reveal,
  Smooth, Tilt, Sidechain (Make room and Unmask), Reference match, Guard and Punch all on, the curve
  stays within 0.000004 dB of 5.1.0. The display levels stay within 0.00001 dB.
- In full Mid/Side the stereo harmonic generator no longer runs. Its harmonics were always replaced
  by the Mid and Side ones, so it was wasted work; the sound is the same.
- Measured in the same session on this machine (`AnnealTests profile`, per STFT frame):
  - the model: Low 30.9 → 21.6 µs, Normal 33.3 → 22.0 µs, Precise 38.5 → 24.4 µs;
  - the display levels: 4.7 → 2.4 µs (three per frame);
  - the whole stereo engine: Low 73 → 57 µs, Precise 163 → 141 µs.
- **Mid/Side is now under the 3 % budget in every latency mode.** At 48 kHz, 64 samples (Normal at
  512), against 5.1.0's run:
  - Mid/Side: Low 3.7 → 2.2 %, Normal 2.5 → 1.5 %, Precise 3.6 → 2.2 %;
  - Stereo: Low 1.7 → 1.1 %, Normal 1.2 → 0.8 %, Precise 1.8 → 1.3 %;
  - Zero latency: Stereo 3.3 → 2.4 %, Mid/Side 5.6 → 3.9 %.

  The slowest 0.1 % of 64-sample blocks: Stereo Low 5 %, Precise 14 %; Mid/Side Low 10 %, Precise
  20 % of the deadline (5.1.0: 7 / 15 / 15 / 29 %).

Fixes
- None found. The bug pass looked at the engine, the zero-latency filters (5.1.0's newest code), the
  processor's host paths (prepare, reset, latency and Zero switches, state), yesterday's build log
  and benchmark, and the interface snapshots.

Tests: 153/153 acceptance checks, 3 new:
- the fast dB conversion against 10·log10 from 1e-20 to 1e30 (largest error 0.00003 dB), with SIMD
  and as plain loops (what non-x86 builds run), including 0, negative, NaN and infinite inputs;
- the fast gain conversion against 10^(dB/20) from −200 to +200 dB (largest error 0.000013 dB);
- the model's curve within 0.001 dB of 5.1.0's math (measured: 0.000004 dB).

23/23 host checks on the built VST3 and 25/25 setup checks pass.

## 5.1.0 — 2026-10-18 20:34

New
- **Zero latency** (Latency menu → *Zero 0.0 ms*): Anneal with no delay at all, for tracking,
  monitoring and live playing. The hearing model still analyses the sound in Low-latency frames, but
  its curve is applied through **minimum-phase filters**, so nothing waits for a frame. A new filter
  is designed from the curve every ~5 ms and crossfaded in over ~1.3 ms.
  - Works at zero latency: Reveal, Smooth, Tilt, Response, Range, Guard, Freeze, Sidechain,
    Reference, Paint, Match, Mix, Delta, Width and Mono bass. Width and Mono bass are built into the
    Side filter.
  - Rests at zero latency: Heat writes its harmonics into the FFT frames, Punch has to boost an attack
    as it happens, and the Listen solos are made from the frames, so these three wait for Low or
    more. The Heat knob says "off at zero latency", and the display shows a ZERO LATENCY tag.
  - The curve follows the music about one frame (~5 ms) later than in the latency modes. A
    minimum-phase EQ also shifts phase where it boosts or cuts, so at Mix below 100 % the dry blend
    can colour the sound a little.
  - The Ceiling has no look-ahead here: it holds sample peaks exactly but can't catch peaks between
    samples.
  - Measured:
    - reported and applied latency 0 (also through the built VST3);
    - neutral settings pass the input within 0.0000001;
    - Smooth cuts a sustained 3150 Hz ring by 8.9 dB, the same as Low;
    - Heat writes nothing (2nd harmonic −191 dB);
    - Width 0 % is mono (sides −183 dB), Width 200 % lifts the sides by 6.02 dB, and Mono bass at
      200 Hz takes 60 Hz sides to −34 dB while 1 kHz stays within 0.01 dB;
    - the filter design matches its target curve within 0.01 dB, with all its energy in the first
      quarter of the response, as a minimum-phase filter should;
    - the Ceiling at −1 dB holds a signal 6 dB over full scale at −1.00 dB.
  - Off by default. It's a separate setting, "Zero Latency", so the Latency menu's other choices and
    every saved project are unchanged; projects from earlier versions open with it off. Not
    automatable: the host re-aligns tracks when latency changes.

Tests: 150/150 acceptance checks, 8 new:
- the filter design and minimum phase;
- latency 0 with the input passed through;
- Smooth cuts as in Low;
- Heat rests;
- Width and Mono bass;
- switching Zero and the latency modes while playing, including Mid/Side;
- the Ceiling without look-ahead;
- older projects open with it off.

23/23 host checks on the built VST3, 1 new: Zero latency through the host, with latency 0 and the
impulse in place at Mix 0 % and 100 %, then back to 1091 samples. Setup checks pass.

CPU at 48 kHz / 64 samples: Zero latency Stereo 3.2 %, Mid/Side 5.6 % of one core, against Low
1.7 % / 3.7 %. The filters cost about 1.5 % of a core. With Zero latency off, everything is as in 5.0.0.

## 5.0.0 — 2026-10-18 20:10

The third major update: a SHAPE group for the punch and the stereo image of the sound, and your own
presets. With the new controls left alone, everything sounds as in 4.0.2.

New
- **SHAPE** (new group, rose, between REFERENCE and OUTPUT):
  - **Punch** −100 … +100 % (Off by default): a spectral transient shaper. In the bands where an
    attack happens it makes the attack louder (+) or softer (−), up to 8 dB, and leaves the body of
    the sound alone. It uses Guard's per-band attack detector but follows only the attack itself:
    held while the detector holds the attack, gone within about 15 ms after. It works inside Range and
    Paint like the rest of the auto EQ, and in Mid/Side it follows the EQ routing. It's sharpest in Low
    latency; in Precise, the 85 ms frames hold an attack longer. Measured on 2 kHz plucks in Low:
    - attacks (first 15 ms): +7.9 dB at +100 %, −7.9 dB at −100 %;
    - the body (80–160 ms): moves 0.01 dB;
    - with Range at 5–20 kHz the 2 kHz plucks are untouched.
  - **Width** 0–200 % (100 % by default): the stereo width of what Anneal outputs, set per frequency
    bin, so it's linear phase with no crossover filters. 0 % is mono; 200 % doubles the sides
    (+6.0 dB). The centre stays where it was (within 0.01 dB), and mono tracks are left alone.
  - **Mono bass** Off / 20–400 Hz (Off by default): everything below the frequency becomes mono,
    fading in over a quarter octave either side, so the low end stays centred and mono-safe while the
    rest keeps its width. Measured:
    - sides at 60 Hz: −85 dB with Mono bass at 200 Hz (Low), −122 dB at 120 Hz (Precise);
    - sides at 1 kHz: unchanged (0.00 dB).
  - Width and Mono bass shape everything you hear, harmonics and Listen solos included. Mix blends
    them with the dry signal like the rest.
- **Attack marks on the display** (roadmap item 11): with Guard or Punch on, short rose bars at the
  foot of the EQ lane show the bands where an attack is happening right now (taller = stronger).
- **Your own presets.** The preset menu lists them under YOUR PRESETS, after the factory ones (the
  arrows step through both), and has three new commands: *Save current settings as…*,
  *Delete “…”* and *Open the presets folder*.
  - A preset holds what the factory presets set: Reveal, Smooth, Tilt, Response, Range, Heat, Color,
    Focus, Mix, Output, Flavour, Punch, Width and Mono bass.
  - Each is a small XML file, `%APPDATA%\Qulic\Anneal\Presets\<name>.annealpreset`, shared by every
    project and every instance. Saving under an existing name replaces that preset; deleting sends
    the file to the Recycle Bin. Characters Windows doesn't allow in names (`\ / : * ? " < > |`)
    become spaces.
  - The bar shows the preset's name, with `*` once you change something. Loading one is one undo
    step. A project remembers which of your presets it was set from, as long as the file is still there.
  - The DAW's own preset list (host programs) still holds the 8 factory presets.

Changed
- **Wider window**: 1464 × 640 (was 1312 × 640) for the SHAPE group, resizable from 1098 × 480 to
  2196 × 960.
- Factory presets also set Punch off, Width 100 % and Mono bass off, so each sounds as before.
  Projects from earlier versions open with those settings too.

Tests: 142/142 acceptance checks, 22 of them new:
- Punch: louder and softer, body untouched, inside Range only;
- Width: 100 %, 0 % and 200 %, with Heat working, through the whole processor, and mono tracks
  untouched;
- Mono bass in Low and Precise;
- typed values and read-outs, older projects, factory presets;
- user presets: save, load and undo, remembered by a project, broken and older files, safe names,
  the preset bar, delete;
- the SHAPE layout.

22/22 host checks on the built VST3, 1 new: Punch, Width and Mono bass through VST3. Width 0 %
leaves the sides 107 dB under the signal, and all three were swept while playing. 25/25 setup
checks.

CPU at 48 kHz with the new controls off, as in 4.0.2:
- Stereo: Low 1.7 %, Normal 1.2 %, Precise 1.8 %;
- Mid/Side: Low 3.7 %, Normal 2.5 %, Precise 3.6 %.

With Punch, Width and Mono bass on, Low and Precise at 64 samples measured 1.4 % and 1.6 %, against
1.8 % and 1.6 % without them in the same run: no measurable cost. Linux was not rebuilt on this PC;
the Linux workflow runs on GitHub for the push.

## 4.0.2 — 2026-10-18 19:39

Lighter on the CPU: Anneal now runs its spectral transforms on its own FFT, about four times faster
than the one it used, so every mode costs less. The sound, the latency and your projects are
unchanged.

New
- **Faster engine** (the first part of the CPU item on the roadmap). Anneal's own FFT replaces
  JUCE's portable one, which Windows and Linux builds get: 6.6 µs instead of 24.7 µs per 4096-point
  transform, and as accurate (within −133 dB of the old one, as close to an exact calculation).
  - CPU at 48 kHz on the build machine, of one core: Stereo Low 2.0 → 1.7 %, Normal 1.8 → 1.2 %,
    Precise 2.7 → 1.8 %. Mid/Side Low 3.7 → 3.6 %, Normal 2.8 → 2.4 %, Precise 4.3 → 3.6 %. With
    Reference Match and Paint on: Low 2.2 → 2.0 %, Normal 1.9 → 1.3 %, Precise 2.8 → 2.0 %.
  - Slowest 0.1 % of 64-sample blocks: Stereo Precise 25 → 14 % of the deadline, Mid/Side Precise
    36 → 29 %, Mid/Side Low 19 → 15 %; Stereo Low stays at 8 %.
  - Reference files are analysed faster too.
  - Mid/Side Precise is still above 3 %: the perceptual model is now the largest cost there (three
    models per frame). That is the next part of the item.
  - For development: `AnnealTests profile` shows where one frame's time goes.

Fixes
- **Freeze survives the host preparing Anneal again.** Hosts do this when you change the buffer
  size or the sample rate, and many when an export starts (a VST3 plug-in is prepared every time
  it's activated). Until now a frozen curve was thrown away there and re-made from the first moment
  of audio afterwards, so an export could sound different from what you had frozen. Now it holds,
  also when the rate changes (44.1 kHz and up). Measured: a −9.0 dB cut stays −9.0 dB after a new
  buffer size and after 48 → 44.1 kHz; through the built VST3, −8.9 dB before and after the host
  deactivates and activates it (4.0.0: back to 0.0 dB).
- **Loudness Match keeps its gain when the host prepares again.** Before, it started over at 0 dB
  and swelled to the matched level over half a second, for example at the start of an export. Now it
  carries on from what it had measured (−0.65 dB stays −0.65 dB).

Tests: 120/120 acceptance checks (new: the FFT against JUCE's and an exact DFT, and both fixes,
which failed before them), 21/21 built-VST3 host checks (new: Freeze through deactivate/activate,
which fails on 4.0.0), 25/25 setup checks. The Linux build was not re-run for this update; the new
code is plain portable C++ with no new libraries.

## 4.0.1 — 2026-10-15

Linux support
- A dedicated Linux workflow builds VST3, LV2, CLAP and Standalone on Ubuntu 24.04, runs
  acceptance and built-plugin host checks under Xvfb, inspects LV2 metadata and runtime
  dependencies, and packages the binaries with SHA-256 checksums. It runs on pull requests,
  main pushes, pull requests, manual dispatch and published releases, including patch releases.
- `bash build.sh --test --snapshot --package` provides the same local build path, including a
  display for JUCE's bundle metadata helpers. Building does not install a live plugin.
- Linux and macOS get their own correct default VST3 copy folders rather than a Windows path.
- Headless checks use Xvfb with a ready Openbox window manager, and the VST3 test host
  initialises Xlib before loading a plugin. Bare Xvfb previously crashed at editor attachment
  with `BadAtom`; editor creation and repeated attachment remain required host checks.
- The VST3 test host detaches and destroys the editor before closing its parent window.
  Reversing that order destroyed the native child first and raised `BadWindow` on Linux.

Fixes
- A failed Linux update copy no longer deletes the previous plugin first. Replacements are
  staged on the destination filesystem and the old copy can be restored if replacement fails.
  Running install.sh without any extracted binaries now reports an error.

Distribution
- [Anneal is listed on KVR](https://www.kvraudio.com/product/anneal-by-qulic) with free Windows
  and Linux downloads and three current editor screenshots. KVR hosts the public binaries;
  source visibility is unchanged. The one-minute Codex schedule is paused at the user's request.

Verification: Ubuntu 24.04/GCC 13.3: 112/112 acceptance, 20/20 built-VST3 host (including three
editor attach/detach cycles) and 10/10 CLAP checks. LV2 discovery/metadata and runtime dependency
inspection passed. 24 Linux editor snapshots rendered; default, reference and minimum-size views
inspected. 23/23 packaging fixture checks cover paths with spaces, copy failures and archive
integrity (two failed before the installer fix). Windows regression: 112/112 acceptance,
20/20 built-VST3 host and 25/25 isolated setup checks. KVR's Linux ZIP preserves all 16 entries and
Unix permissions from the tested tar archive; downloaded Windows and Linux files match local SHA-256.
LV2 audio in a DAW and macOS builds remain unverified.

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
