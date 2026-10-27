# Changelog

Newest first. Each completed, tested update gets a versioned entry (see docs/ROADMAP.md).
The plugin version shown in the DAW and in the setup's name comes from `project(... VERSION)` in
CMakeLists.txt.

## 5.3.5 — 2026-10-27 10:15

New
- **Peak hold on the display**: a faint dotted line that keeps the output spectrum's highest level at each
  frequency, so you can see where a mix builds up over a whole song. Play the song through with it on, then
  look for the bumps the line leaves: a low-mid build-up in the chorus or a harsh 3 kHz peak in one section
  shows up even after that part has passed.
  - Turn it on with the small **HOLD** pill in the display's top right corner (above the dB scale). Off is
    the default, and then nothing is drawn.
  - The line only rises. **Click the spectrum** (the EQ lane) to clear it and start again; clicking a range
    handle or another lane doesn't clear it. Clicking HOLD again turns it off.
  - Hovering over the display adds "peak −37.3 dB" to the read-out, the level held there.
  - It collects while Anneal's window is open, and the line is still there when you close and reopen the
    window. It isn't saved in the project.
  - Display only: the sound doesn't change.

Fixes
- **Long tooltips were cut off at the edge of the window.** Tooltips were drawn on one line, so a long one
  ran past the window and was cut mid-word: the display's own tooltip (what each lane shows) lost about half
  its text, and at the smallest window size so did those of Latency, Flavour, Limits, Punch and the harmonic
  bars (11 of 112 tooltips in all). Long tooltips now wrap onto several lines, at most 520 px wide.

Tests: 220/220 acceptance checks, 6 new:
- the tooltips: every one of the 112 tooltips, laid out the way it is drawn, fits its box at the full and the
  smallest window (5.3.4: 11 didn't);
- the held line never falls while playing: 36 frames, loud with a bump moving 200 Hz → 2 kHz → 8 kHz, then
  40 dB quieter: no band fell, 1 kHz stays at the loud −37.81 dB while the output is at −77.81 dB, and all
  three bumps are kept (within 0.000 dB);
- a click on the spectrum clears it (a click on a range handle or in the Heat lane doesn't), and it then
  holds from the quiet level;
- off draws nothing: with it off the display is pixel for pixel the same as one that never held (on, 2424
  pixels differ, all inside the EQ lane and the pill);
- the HOLD pill sits in the display's top right corner, clear of the 20 kHz range grip and above the EQ lane,
  its text inside it;
- through the plug-in: closing and reopening the window keeps the line and HOLD (1 kHz −28.13 dB before and
  after, and after 0.6 s 40 dB quieter); the output with it on is bit-identical to it off.

28/28 host checks on the built VST3 and 25/25 setup checks pass (nothing host-facing changed: no new
parameter, nothing new in the saved state). CPU: no change to the audio path; the held line is a max over the
1024 display bands 30 times a second in the editor. At 48 kHz / 64 samples Stereo Low 2.0 %, Precise 4.7 %,
Mid/Side Precise 7.9 % (5.3.4: 2.1 / 4.6 / 7.9 %); the slowest 0.1 % of blocks 5 / 14 / 22 % of the deadline,
within the benchmark's usual spread.

## 5.3.4 — 2026-10-26 09:55

New
- **Key range for the sidechain**: you can now choose which part of the sidechain key counts. Until now the
  whole key did: a vocal routed in to *Make room* on a bus ducked the bus wherever the vocal had energy,
  its low end included. A **Key** pill beside the Sidechain menu sets the range.
  - Example: a vocal key with the key range at 1–5 kHz ducks the bus only around the vocal's presence; its
    body at 300 Hz no longer pulls the bus's low mids down. Set it "up to 150 Hz" and only a kick or bass in
    the key does anything (*Unmask* likewise lifts only where that part of the key masks the track).
  - The menu has *Whole key*, three ready ranges (Low end up to 150 Hz, Vocal presence 1–5 kHz, Highs above
    5 kHz) and *From* / *Up to* steps. Choosing a From above the Up to (or the other way round) opens the
    other end, so the range never crosses. The pill reads "Key: all", or "Key 1k–5k" with a range set.
  - On the display, the key's violet line stays bright inside the range and goes faint outside it, so you see
    which part of the key is acting.
  - The pill only shows while a sidechain mode is on, next to the mode (the menu got narrower), so the deck
    gains no row.
  - It works everywhere the sidechain does: every latency mode, Zero latency and Mid/Side (with the EQ
    routing). Learn key (Reference) still learns the whole key.
  - Also two host parameters, "Sidechain Key Low" and "Sidechain Key High", automatable (type `1k`, `5 kHz`
    or `all`). The whole key, 20 Hz – 20 kHz, is the default and what older projects open with, so they
    sound exactly as before. Factory presets leave the key range as it is (like the routing).

Fixes
- None found this run. The bug pass looked at the sidechain path (processBlock's key bus, the model's key
  term, Mid/Side routing), the dry path across latency switches, reset and prepare, the newest build log,
  the benchmark and the rendered interface, and found nothing to fix.

Tests: 214/214 acceptance checks, 5 new:
- the model: with the key range at 1–5 kHz a 300 Hz key ducks the main 0.000 dB there (the whole key:
  −9.00 dB) and 2 kHz −9.00 dB as before; *Unmask* with the key up to 150 Hz lifts a masked 2 kHz tone
  0.000 dB (the whole key: +5.64 dB);
- the whole key is bit-identical to before: the defaults are the open ends, and a key range moved to
  1–5 kHz and back plays exactly like one that never moved (music with Reveal, Smooth and Make room on,
  difference 0.000000000000);
- through the plug-in's sidechain bus in Low, Mid/Side and Zero latency: with 1–5 kHz the main's 300 Hz is
  untouched (0.00 dB) and 2 kHz ducks −9.0 dB; with the whole key both duck (−8.7 / −9.0 dB);
- the parameters: "20 Hz" / "20.0 kHz" by default, typed values (`1k`, `all`), older projects open with the
  whole key whatever the instance had, a saved range comes back, factory presets leave it alone;
- the Key pill: hidden with the sidechain off, on the Sidechain menu's row while a mode is on, nothing
  overlapping with everything shown, its menu items and texts, and the display drawing the range the
  engine uses.

28/28 host checks on the built VST3 (new: Sidechain Key Low / High through VST3: "20 Hz" / "20.0 kHz" by
default, typed values; a key at 300 Hz and 2 kHz routed into the sidechain bus ducks the main −9.00 /
−9.00 dB with the whole key and −0.00 / −9.00 dB with the key range at 1–5 kHz) and 25/25 setup checks
pass. CPU: nothing with the sidechain off or the whole key (the window isn't computed); with a range set,
one multiply per band while a sidechain mode is on. At 48 kHz / 64 samples Stereo Low 2.1 %, Precise
4.6 %, Mid/Side Precise 7.9 % (5.3.3: 2.0 / 4.6 / 6.9 %, 5.3.2: 2.0 / 4.6 / 8.0 %: the machine was busier
this time, Zero latency measured higher by the same amount, and none of these cases run new code); the
slowest 0.1 % of blocks 8 / 13 / 21 % of the deadline, within the benchmark's usual spread.

## 5.3.3 — 2026-10-25 20:26

New
- **Tilt pivot**: you can now choose where Tilt turns. Until now Tilt always pivoted at 1 kHz; now a
  **Pivot** row (200 Hz – 5 kHz) sets the frequency that stays where it is while everything above it gets
  brighter (or darker) and everything below it the opposite.
  - Example: Tilt +1 dB/oct around 300 Hz leaves 300 Hz untouched and lifts 1.2 kHz by 2 dB. Around 1 kHz
    the same Tilt would also pull 300 Hz down 1.7 dB. A low pivot brightens a bus without thinning its body;
    a high pivot (say 3 kHz) darkens the top without touching the mids.
  - The Pivot row only appears while Tilt isn't 0, so the deck stays as calm as before. To make room for it
    without anything jumping around, the stack beside Reveal and Smooth is now Response, Guard, Tilt, then
    Pivot (Tilt used to be on top).
  - It works everywhere Tilt does: every latency mode, Zero latency, Mid/Side (with the EQ routing) and with
    Paint and Range.
  - Also a host parameter, "Tilt Pivot", automatable (type `300`, `2k` or `2.5 kHz`). 1 kHz is the default and
    what older projects open with, so they sound exactly as before. Factory presets leave the pivot as it is
    (like the Limits).

Fixes
- None found this run. The bug pass looked at the newest code (the loudness meters, the Limits, the audio
  callback, reset and prepare), the benchmark and the rendered interface, and found nothing to fix.

Tests: 209/209 acceptance checks, 5 new:
- the model: +1 dB/oct around 300 Hz is −0.004 dB at 300 Hz and +2.003 dB at 1.2 kHz (around 1 kHz: −1.74 /
  +0.27 dB);
- 1 kHz is bit-identical to before: a project that moved the pivot to 300 Hz and back plays exactly like one
  that never did (music with Reveal, Smooth, Tilt and Heat on, difference 0.000000000000), and the default is
  exactly 0 octaves;
- through the plug-in in Low, Mid/Side and Zero latency: 300 Hz −0.03 dB, 1.2 kHz +2.00 dB;
- the parameter: "1.00 kHz" by default, typed values (`2k`, `300 Hz`, out of range clamped), older projects
  open at 1 kHz whatever the instance had, a saved pivot comes back, factory presets leave it alone;
- the Pivot row: hidden at Tilt 0, directly under Tilt inside AUTO EQ while Tilt is on, hidden again at 0,
  nothing overlapping with everything shown.
The automation stress test now moves the pivot too.

27/27 host checks on the built VST3 (new: Tilt Pivot through VST3: "1.00 kHz" by default, typed values;
Tilt +1 dB/oct moves 300 Hz / 1.2 kHz by −1.77 / +0.26 dB around 1 kHz and −0.03 / +2.00 dB around 300 Hz)
and 25/25 setup checks pass. CPU: one subtraction per band, nothing measurable. At 48 kHz / 64 samples
Stereo Low 2.0 %, Precise 4.6 %, Mid/Side Precise 6.9 % (5.3.2: 2.0 / 4.6 / 8.0 %, a quieter machine this
time); the slowest 0.1 % of blocks 5 / 16 / 19 % of the deadline, within the benchmark's usual spread.

## 5.3.2 — 2026-10-25 20:15

New
- **Max cut and Max boost**: limits for how far the auto EQ may move. A new pill on the AUTO EQ title row,
  "Limits: none", opens a menu with *Max boost* (no limit, +1 … +12 dB) and *Max cut* (no limit, −1 … −12 dB).
  - Set Max cut to 6 dB and nothing is ever pulled down more than 6 dB, however much Smooth (or Make room,
    or the reference) would like to; set Max boost to 3 dB and Reveal never lifts more than 3 dB. Everything
    that moves less than the limit stays exactly as it was. Useful on a master or a vocal bus, where you
    want Anneal's tone but a hard rule on how far it goes.
  - The limits cover the whole auto EQ: Reveal, Smooth, the sidechain and the reference together. Tilt
    and Punch are your own moves and aren't limited.
  - With a limit set the pill reads "Max +3 / −6 dB" (lift in straw, cut in blue), and the display draws
    the limits as dashed lines across the Range, so you see the curve flatten against them. Reveal's and
    Smooth's live lines show the limited amounts.
  - It works everywhere: every latency mode, Zero latency, Mid/Side (each part limited), Freeze (a frozen
    curve is limited too, and comes back whole when you raise the limit), the Listen solos.
  - Both are also host parameters, automatable, with any value from 0 to 24 dB (type `6`, `-6 dB` or
    `none`); 24 dB reads "No limit". No limit is the default and what older projects open with, and no
    move of the auto EQ can reach 24 dB, so projects sound exactly as before. Factory presets leave the
    limits as they are (like the Ceiling).

Fixes
- **Mid/Side: the sidechain ignored the EQ routing.** With EQ: Mid (or Side) the sidechain still ducked or
  lifted the other part: in Make room, a vocal key dipped the sides of a bus by up to 9 dB even though the
  EQ was routed to the centre only. Now the sidechain follows the EQ routing like Reveal, Smooth, Tilt, the
  reference and Punch already did. Only projects in Mid/Side with EQ: Mid or Side and a sidechain mode on
  sound different: the part the EQ isn't routed to is left alone, as the routing promised.

Tests: 204/204 acceptance checks, 9 new:
- the sidechain follows the EQ routing (it failed before the fix: the untargeted part ducked −9.0 dB at
  2 kHz; now untouched, error 0.0000000, and EQ: Both still ducks −9.0 dB);
- Max cut stops a 9.0 dB Smooth cut at 4.5 dB exactly (bands cut less move 0.000 dB); Max boost stops a
  7.39 dB Reveal lift at 3.70 dB; no limit with Reveal, Smooth and Make room at full is bit-identical to a
  clamp just under 24 dB (largest move 18 dB); a frozen −9 dB cut reads −3.000 dB with Max cut 3 and −9.00
  again without; Mid/Side and zero-latency curves stay inside Max cut 3 / Max boost 1 dB; at zero latency
  through the plug-in a 3150 Hz ring cut 8.9 dB is cut 3.0 dB with Max cut 3; texts, typed values, older
  projects (no limits, whatever the instance had), saved projects and presets; the Limits pill (placed
  before Freeze, its menu, its text, the display's lines).

26/26 host checks on the built VST3 (new: Max Cut / Max Boost through VST3: "No limit" by default, typed
values, a 3150 Hz ring cut 8.9 dB is cut 3.0 dB with Max Cut 3 dB) and 25/25 setup checks pass. CPU: with no
limit the code doesn't run; with limits, two comparisons per band. At 48 kHz / 64 samples Stereo Low 2.0 %,
Precise 4.6 %, Mid/Side Precise 8.0 % (5.3.1: 2.0 / 4.6 / 8.1 %); the slowest 0.1 % of blocks 6 / 12 / 21 % of
the deadline, within the benchmark's usual spread.

## 5.3.1 — 2026-10-25 19:50

New
- **Loudness readout**, in the header between PAINT and MATCH: how loud your track is going in and coming
  out, in LUFS (the EBU R128 / streaming loudness unit), so you can see what Anneal does to the level as
  well as hear it.
  - Two small rows, each "in → out": **S** is short-term loudness (the last 3 seconds), **I** is
    integrated loudness (everything played since you last clicked the readout). For a mastering target
    like −14 LUFS, watch the I output number over a whole play-through.
  - **Click it** to start I over. A stop in FL Studio keeps I, so you can play a song in parts.
  - It measures like a proper loudness meter: silence and very quiet passages (more than 10 LU under the
    rest) don't pull I down, and the numbers match the EBU's own test signals within 0.02 LU.
  - The input is the signal Mix and Match compare with; the output is what leaves Anneal, after the
    Ceiling and Bypass. With Bypass on the two read the same.
  - It's just numbers (no new knob, the sound doesn't change) and what they mean is in its tooltip, so
    the calmer header stays calm. It costs about 0.05 % of a CPU core.

Fixes
- **Garbled text after dropping presets on the preset bar.** Since 5.2.4 the bar's notes ("Added “Bright
  Mix”", "… isn’t an Anneal preset", "· 1 not added") showed broken characters in place of the quotes,
  the apostrophe and the dot, because those characters had been saved wrongly in the source.

Tests: 195/195 acceptance checks, 6 new:
- the preset bar's notes read correctly (it failed before the fix: "Added âBright Mixâ");
- loudness: EBU Tech 3341 cases 1–5 (integrated: −22.99, −32.99, −23.01, −23.01, −22.98 LUFS, each
  within 0.1 LU) and case 9 (short-term −22.99 throughout), 44.1 / 48 / 96 kHz and mono; through the
  plug-in in Low and at zero latency (a −23 dBFS tone with Output −6 dB reads −23.0 in, −29.0 out) and
  with Bypass; a click starts I over and a host stop keeps it; the readout in the header.

25/25 host checks on the built VST3 and 25/25 setup checks pass. CPU: the two meters 0.04 % of one core;
at 48 kHz / 64 samples Stereo Low 2.0 %, Precise 4.6 %, Mid/Side Precise 8.1 % (5.2.7: 2.0 / 4.6 / 8.0 %);
the slowest 0.1 % of blocks 5 / 13 / 24 % of the deadline, within the benchmark's usual spread.

## 5.3.0 — 2026-10-25 19:30

New
- **A calmer, less cluttered interface** (asked for by you: "make the ui way less cluttered"). Nothing
  was taken away: every control is still there when it does something. What changed:
  - **Sidechain and Reference share one column.** Each is its title and its menu. Their Amount knob
    is now a small row that only appears while that mode is on, and what they are doing ("ducking
    −7.2 dB", "matching −6.0 dB", "no key connected") reads right beside their title. At rest the
    column is just two menus, instead of two big rings saying "off". SHAPE and OUTPUT get the room
    that frees up.
  - **The Mid/Side routing menus** (EQ: Both / Heat: Both) only show in Mid/Side mode, where they do
    something. In Stereo they used to sit there greyed out.
  - **The display says less.** The lanes are just named (AUTO EQ, WIDTH, HEAT) without the sentences
    after them; those explanations are now in the display's tooltip. The scale keeps +12, 0 and
    −12 dB (the ±6 lines stay, unlabelled), and the heat lane lost its numbers. Status tags are
    shorter: "ZERO LATENCY · Heat and Listen rest", "AUTO EQ FROZEN", "KEY", and the reference shows
    just its name. Paint mode still explains its gestures.
  - The window size, the colours and every knob, menu and parameter are as before, so projects open
    and sound exactly the same.

Fixes
- None in this update: it is the interface change you asked for, made right away. The daily updates
  keep doing their bug pass.

Tests: 189/189 acceptance checks, 1 new:
- the calmer deck: the routing menus show in Mid/Side only, the Sidechain and Reference Amount rows
  only while their mode is on (and go again when it is switched off), and with everything showing
  nothing overlaps and the rows stay inside their column.
The layout checks follow the new columns (SIDECHAIN over REFERENCE between 832 and 1032, SHAPE between
1032 and 1232).

25/25 host checks on the built VST3 and 25/25 setup checks pass. CPU unchanged (the audio code
didn't change).

## 5.2.7 — 2026-10-25 10:21

New
- **Punch works at zero latency.** Up to now Punch rested whenever Latency was on Zero, because it
  shapes attacks inside the FFT frames that zero latency doesn't have. Now a separate transient shaper
  takes over there, with no delay:
  - It listens to the sound inside your Range for attacks (a quick level against a slower one, the same
    "4 dB starts, 12 dB is a full hit" scale as the normal Punch) and makes them up to 8 dB louder (+) or
    softer (−) as they happen, then lets go within a few tens of milliseconds. The body of the sound
    stays as it is.
  - Measured on 2 kHz plucks: attacks +6.2 dB at +100 % and −6.0 dB at −100 % (the normal Punch in Low:
    ±7.9 dB), the body within 0.001 dB.
  - How it differs from the normal Punch: it shapes each attack across the whole Range at once, not band
    by band, and Paint doesn't shape it. Range's edges are gentle (a pluck two octaves outside Range
    still moves 0.6 dB, against 6 dB inside), but never the wrong way: a boost never turns into a cut.
    In Mid/Side it follows the EQ routing, like the normal Punch. The Punch knob's tooltip and the
    display's ZERO LATENCY tag say so.
  - Punch at 0 % leaves the audio exactly as it was, bit for bit, and a steady sound with Punch on is
    untouched once its start has passed.
- **Punch at Zero**, a switch at the bottom of the Latency menu. It is on for new instances. Projects
  saved before 5.2.7 open with it off, so a project made with Zero latency and Punch sounds exactly as
  it did (Punch rested there). Tick it to use Punch at zero latency in such a project.

Fixes
- **The Punch knob's activity arc now shows.** Since 5.0, the small Punch ring was meant to show how hard
  the latest attack is being shaped with a thin arc inside it, but it was never drawn (small knobs threw
  that information away). It now shows, in the latency modes and at zero latency.

Tests: 188/188 acceptance checks, 8 new:
- the Punch ring's arc (it failed before the fix: activity −1, 0 pixels of arc; now 450 pixels);
- zero-latency Punch: attacks ±6 dB with the body within 0.1 dB; Punch 0 bit for bit in Stereo, Mid/Side
  and mono at random block sizes; steady tones untouched after their start; Range (4 kHz plucks, Range
  up to 1 kHz: +0.6 / −0.2 dB, never the wrong way); Mid/Side routing (a pluck in the sides with EQ: Mid
  untouched); the same output at any block size and clean under automation of Punch, Range and
  Mid/Side every block; projects from before 5.2.7 open with Punch at Zero off; the Punch ring at zero
  latency.

25/25 host checks on the built VST3 (1 new: Punch at zero latency through the host) and 25/25 setup checks
pass. CPU at 48 kHz / 64 samples: Zero latency Stereo 3.6 %, Mid/Side 6.1 % of one core with Punch off
(5.2.6: 3.5 / 6.1 %; the shaper's detector, which always runs at zero latency, costs under 0.1 %), and
4.3 % with Punch, Width and Mono bass all working; the slowest 0.1 % of blocks 9 % of the deadline.
The latency modes measure as in 5.2.6 (Stereo Low 2.0 %, Precise 4.6 %; Mid/Side Precise 8.0 %).

## 5.2.6 — 2026-10-24 23:40

New
- **Zero latency is lighter again** (part 2 of the zero-latency CPU item). Two changes, and neither
  changes the sound:
  - **Crossfades only as long as they last.** At zero latency Anneal fades over to each new pair of
    filters in 1.33 ms. With bigger buffers (like FL Studio's usual ones) the old filters kept running
    for the whole 3.3 ms until the next update anyway. Now they stop when the fade ends. The output is
    the same, bit for bit.
  - **Smaller transforms in the filter design.** Each of the 300 filter designs a second needed three
    4096-point FFTs. Two of them are now 2048-point ones (a cosine and a sine transform worked out
    through a half-size FFT), and the filter takes the curve's own level instead of going through a
    log and back. One design takes 26.0 µs instead of 33.3 µs. The filters stay within 0.0008 dB of
    5.2.5's wherever the response is above −40 dB (0.002 dB down to −60 dB), far below anything you
    could hear.
  - Zero latency at 48 kHz: Stereo 3.7 → 3.5 % of one core with 64-sample buffers and 3.7 → 3.3 % with
    512; Mid/Side 6.5 → 6.1 %. The slowest 0.1 % of blocks stay where they were: Stereo 8 %, Mid/Side
    17 % of the deadline.
  - Not done: the roadmap's two other ideas. A design transform half as long was tried and measured.
    It moved the filters by 0.04–0.08 dB on real music curves and by up to 19 dB next to Mono bass, so
    it isn't in. Designing the filters 150 times a second instead of 300 would save more (about 0.5 %
    of a core in Stereo, 1 % in Mid/Side). But the EQ at zero latency would then follow the music half
    as often as the 300 a second you asked for in 5.2.0, so that one is your call (roadmap, *Waiting
    on the user*).

Fixes
- None found. The bug pass looked at 5.2.5's batched filter design, the zero-latency path through host
  resets, latency switches and changing buffer sizes, today's build logs, the benchmark and the
  zero-latency editor snapshot.

Tests: 180/180 acceptance checks, 2 new:
- the design on half-size transforms against 5.2.5's on nine curves: the engine's own Mid and Side
  curves on music, a −9 dB dip, a curve moving ±12 dB, Width 200 % with Mono bass at 20 and 400 Hz,
  painted Width, Width 0 % painted out. Within 0.0008 dB above −40 dB; taps within 0.000003 of the peak;
- the old filters now run 63.9 samples per design: the 64-sample crossfade. (Up to 5.2.5, with big
  buffers, they ran for the whole 160-sample hop.) The output is identical with 512-sample, 64-sample
  and random buffers.

24/24 host checks on the built VST3 and 25/25 setup checks pass. Everything but zero latency measures
as in 5.2.5, within run-to-run variation.

## 5.2.5 — 2026-10-24 14:20

New
- **Zero latency is lighter on the CPU** (part 1 of the zero-latency CPU item). At zero latency Anneal
  designs a new pair of minimum-phase filters 300 times a second, and each design needed about 2,000
  logarithms, 2,000 exponentials and 4,000 sines and cosines, one at a time. They now go through
  Anneal's own fast math in batches, four at a time (a new fast sine / cosine joins the log and exp
  from 5.1.1), and the filter's fade-out is a table instead of 128 cosines per design.
  - One filter design: 41.5 → 32.5 µs.
  - Zero latency at 48 kHz / 64 samples: Stereo 4.0 → 3.7 %, Mid/Side 7.1 → 6.5 % of one core; the
    slowest 0.1 % of Mid/Side blocks 18 → 16 % of the deadline.
  - The sound is the same: the filters match the exact math's within 0.000006 dB (and their taps
    within 0.000005 % of the peak), and every zero-latency check from 5.1 still measures the same
    (Smooth −8.9 dB as in Low, Width, Mono bass, Width paint).
  - The 3 % budget in Mid/Side isn't reachable this way: since 5.2.0's 300 updates a second, the
    analysis alone costs about 4 % there. The roadmap keeps the next ideas (fewer designs a second,
    a shorter design transform).

Fixes
- None found. The bug pass looked at 5.2.4's preset import (names, duplicates, broken files) and its
  build logs.

Tests: 178/178 acceptance checks, 2 new:
- the fast sine / cosine against the standard library: largest error 0.00000008 up to |x| = 10,000,
  with SIMD and as plain loops; NaN, infinity and 1e30 stay finite;
- the fast filter design against the exact one on three curves (a −9 dB dip, a curve moving ±12 dB, a
  Side curve with Width painted and Mono bass): within 0.000006 dB.

24/24 host checks on the built VST3 and 25/25 setup checks pass. Everything but zero latency
measures as in 5.2.3.

## 5.2.4 — 2026-10-24 14:13 (catch-up for a missed day)

New
- **Import presets by dropping them on the preset bar.** Drag one or more `.annealpreset` files (from
  a friend, another computer, a backup) onto the preset name in the header: it outlines itself ("DROP
  TO ADD THE PRESET"), and each file is checked and copied into your presets folder. A single preset is
  loaded straight away (one undo step), and the bar briefly says what happened ("Added “Bright Mix”").
  - A preset you already have isn't added twice; a different one with the same name comes in as
    "Bright Mix 2".
  - Anything that isn't an Anneal preset (another file type, a broken file, something that only
    looks like one) is left alone, and the bar says which file and why.
  - The file is copied as it is, so a preset from an older Anneal keeps meaning what it meant.
- **Copy the presets folder path** in the preset menu (under *Open the presets folder*): handy for
  sharing presets, finding them from a file dialog, or backing them up.

Fixes
- None found. The bug pass looked at the stereo image code from 5.2.3 (the three-step update, the
  display's easing), its build logs and benchmark.

Tests: 176/176 acceptance checks, 3 new:
- a preset dropped on the preset bar (through the bar's own drag-and-drop) is copied into your folder,
  loaded as one undo step, and the bar says so; only `.annealpreset` files are taken;
- the same preset isn't added twice, a clash becomes "Bright Mix 2", and a broken preset, a `.wav` and
  a missing file are refused with the reason shown;
- the preset menu has *Copy the presets folder path* (not run in the tests, so they never touch your
  clipboard).

24/24 host checks on the built VST3 and 25/25 setup checks pass. CPU unchanged.

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
