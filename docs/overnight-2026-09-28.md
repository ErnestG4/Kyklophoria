# Overnight, 28 September 2026

Combust, 01:15: "Please set a timer for every 30 minutes and work on this as
well as continuing to advance through the roadmap as you can. I'll give you any
feedback you need in the AM" — until 8am. "This" is the strike overruns:
"Getting closer, but try it with Piano loaded on 4 voices", "again I think it's
the strikes", "can they overlap and overload?", and it is overruns (the
module's counter), not a sound in the maths (desktop renders click-free).

Rules as the last overnight: every item a test that fails without it, `make
test` green, a commit; nothing pushed; nothing outside Kyklophoria and
ModalBake.

## Where it stood at 01:15

Firmware 9965634 has, since the 27th: the tail held to the voice's share
(4fcf97a), the late-CV retune once a strike and one rebuild a block at most
(9965634), the model sliders following the pots (a635fa3). Desktop stress
(four voices, a strike every 4 ms, audio-rate v/oct): one rebuild a block at
most, and the worst strike block is that rebuild on top of a normal block,
1.7x the median. Piano measures no worse than salamander on the desktop, so
what overruns on the module is the M7's cost of the strike, which the desktop
does not show: the rebuild calls libm log2f twice a mode (Body), sinf/cosf/
exp2f/powf, and 37 divides in Build alone (M7 disassembly census).

Can strikes overlap and overload? Not in the same block — one strike a block,
each held 4 ms — but their aftermath stacks: a strike's modes run twice while
they fade in (the Piano's fades are up to 154 ms in the bass, its leads up to
308 ms; salamander's 32 and 43), a lead ending pays an Advance (sinf and cosf a
mode at large arguments), and the tails ring on.

## The list, in order

1. **The rebuild off the audio thread.** A strike waits at least 4 ms for the
   pitch; the main loop is idle. The shell asks for the voice the pending
   strike will need, the main loop builds it fresh into a staging voice, and
   the strike, if the staged one matches (world, member, note, cap, tune),
   swaps its coefficients in and releases the old ring to the tail — no At()
   in the audio callback. Anything that does not match, a pickup world's
   coil crossfade, or a family morph builds inline as now. Test: a strike
   through the staged path is bit-identical to one built inline, and a stale
   plan is refused.
2. **The strike's cost on the CPU line.** The cycles of the strike itself
   (Engine::Strike in the callback) as its own maximum in the stats, so the
   morning's test reads it off the page rather than inferring it from max and
   engine.
3. **The M7's transcendentals on the strike path.** Body()'s log2f to
   fastmath::Log2; Advance's sinf/cosf at large arguments range-reduced onto
   fastmath::SinCos; the divides in Build. Each against the goldens and the
   ear check.
4. **The fade doubling.** While a strike fades in its voice runs its modes
   twice; on the Piano's bass that is 154 ms of it, four voices at once in a
   roll. Measure what folding the strike bank into the main state early (once
   the ramp is nearly full) or running the ramp at the main state's cost would
   save, and whether it can be done without a step.
5. **Roadmap, no hardware:** the coupled exciters' cost on the M7 (`make
   armcost`), the contact noise tied to the exciter (the "snare chain"), then
   the Exciter page's design against the module's pots.

## For the morning (needs Combust)

**Flash** `shell/alchemy/build/kyklophoria.bin` (stamp in the footer and on
the module: the commit this section is in). The page changed too (the CPU
line's new fields): push `pages` and hard-refresh.

**Test:** the Piano (and piano-salamander, the Wurlitzer) at 4 voices —
smashed chords, fast rolls, audio-rate v/oct with triggers. Watch the CPU line:

    last · avg · max · engine · strike · At/s · staged/s · overruns

- overruns: the one that matters — do they still climb on strikes?
- strike: what a strike itself costs. A staged strike is small; if it reads
  high, look at staged/s.
- staged/s against At/s: most strikes should be staged. If staged/s is near
  zero while you are playing, the control loop is not serving the plans in
  time, and that is the next thing to fix.
- engine: the voices themselves. If max is high with engine high, it is the
  ringing and not the strike.

**Also on the card, unplayed:** everything from the 27th (stereo spin, 24
worlds, family morph, release slider). The old variants in the build folder
(-fullintro, -morph, -release200) are from the 27th and have none of this.

**After that, if wanted:** `kyklophoria-room.bin` — the side branch
`modal-room`, the resonator's voices lent so the right engine holds none (17
KB of AXI SRAM back for the exciters). Same sound, all tests green.

**Decisions only Combust can make:**
1. The Exciter page (docs/exciters.md, "Stage 4, proposed"): eighth page or
   folded in; the six pots; J4 as a gate under a bow/breath; recorded attack
   as default until the synthesised one has been heard — for which the A/B/C
   renders are in ModalBake out/wav/exciter-ab/.
2. Merge `modal-room` into `modal` (the room the exciters need).
3. ep-vel's bass: refit seeded off-centre (ModalBake docs/ep-bass-2026-09-28.md,
   minutes of GPU) — or leave it.
4. The rough remainder (ModalBake docs/rough-remainder-2026-09-27.md) and the
   uncommitted refit outputs in ModalBake, still waiting.
5. Pushes: Kyklophoria `modal`, `pages` (-f), `modalbake` (and `modal-room`
   if you want it on Codeberg).

## Log

- 01:16 — list written; check-ins at :13 and :43, wrap-up 07:52.
- 01:40 — item 2 done (cf5204c): the CPU line says "strike N%", the peak of
  Engine::Strike in the callback, a fourth appended stats word.
- 01:45 — item 1 done (87b7b84): the staged strike. 114 of 150 strikes in
  the test take a voice built on the control loop, output bit for bit the
  inline build. Its test found the late-CV rule dragging a roll's ringing note
  to the next note (under 30 ms apart): now not while a strike waits, and not
  until the jump has stood 2 ms. The code put AXI SRAM 1.8 KB over; the world
  receiver moved to SDRAM (98.99 % now). Firmware 87b7b84.
- 01:49 — the staged strike on pickup worlds too (fef561e): the coil's
  crossfade split out of Build (SetPickup) and done on the audio thread after
  taking the staged voice; the tine fixture takes 114 of 150, bit for bit.
- 01:55 — item 3 rethought: with the rebuild off the audio thread, what a
  strike still does there is a percent or two (a few libm calls, Release's
  divides) — not worth chasing blind. Item 4 turned into something better
  (feced5f): a new note's main state is exact zeros until its strike folds in,
  and the main loop ran every mode over them through the lead and the fade (up
  to 460 ms on the Piano's bass). Skipped while quiet, bit for bit: Piano roll
  median block 1.89 -> 1.31 us, p99 2.91 -> 2.32. Stack checked with
  -fstack-usage: the plan's chain on the control loop (~6.5 KB) is under the
  loop's existing deepest path (Basis, 8.3 KB frame), and a staged strike on
  the audio thread uses less than the inline build. Firmware feced5f.
- 02:00 — an unsettled strike (the module's 30 ms hold ran out, the pitch
  never stable: audio-rate v/oct) strikes at the staged voice's note (9db9a39;
  43 of 50 take the plan in the test). Desktop, module-like harness (pitch
  first, strike after the hold, the plan built untimed as the control loop
  would): the strike block on the Piano and the Wurlitzer 2.20 -> 1.58 us
  against a plain block's 1.34 — what a strike adds, 0.88 -> 0.24. Not taken:
  decay/coil CVs at audio rate (the tune differs every block; those rebuilds
  are one per 2 ms, bounded) — a plan could accept a millisecond-old tune, not
  done. Firmware 9db9a39.
- 02:06 — in progress, uncommitted: a load governor (Engine::SetLoad; over
  0.85 of the budget the tails end within 2 ms, ResonatorBank::Hurry; the
  module passes the last block's share), test 22i. Found while writing it:
  the tests' Run() overwrites its buffer, so 22e (the staged strike's
  bit-identity) compared only each performance's last block — RunOn()
  appends, 22e and 22i use it now. Not yet run: the shell's permission check
  stopped answering (a transient server fault), so paused for the 02:13
  check-in. Next: make test, then commit, then rebuild.
- 02:13 — check-in: the shell's permission check still gives no verdict (six
  in a row; ten ends the session), so nothing run. Reviewed the uncommitted
  governor and the RunOn change by reading; they stand as written. Waiting
  for the 02:43 check-in to run the suite.
- 02:25 — the shell answers again. The governor committed (67ced0c): all
  green, 22e bit for bit over whole performances, 22i fails without the
  hurry. Firmware 67ced0c. The strike list (items 1-4) is done as far as the
  desktop can take it; the rest is the morning's numbers off the CPU line.
  Pages rebuild left for the wrap-up (the strike readout is a page change).
  Next: item 5, the roadmap.
- 02:40 — item 5. (a) `make armcost` lists the coupled exciters' loops
  (tools/armloops.py, 07eab17): about 21 instructions a mode a sample against
  the free bank's 10 — one coupled voice 2.5 % of the block at 12 modes, 10 %
  at 48; in docs/exciters.md. (b) ContactNoise (6871dc9): the contact's noise
  follows the contact and cannot stack (the snare chain), exciter_check 9;
  a noise with its own decay fails it. (c) The Exciter page proposed in
  docs/exciters.md with four questions (882e6f9) — and the room it needs: AXI
  SRAM is 99.3 % full, so the exciters on the module want the right engine's
  unused resonator voices (22 KB; on a resonator only the left engine plays)
  reclaimed first. Not done tonight: it reshapes how the engine holds voices,
  and that is a daytime change. (d) Known data issues, checked: ep-vel's top
  is in tune now (0 off, E4-C6). Its bass reading an octave up turned out,
  once notecheck could find a layered world's recordings (ModalBake 190946b,
  also --vel), to be the fits: the recordings read their notes, ours the
  octave, with the 3rd and 4th harmonics 20-40 dB under the recording's — the
  pickup not driven hard enough, a refit question. An audit of every layered
  card world at velocity 0.3 and 0.9 is running (scratchpad velaudit.log).
- 02:57 — check-in. The audit ran; notecheck now finds every layered
  world's recordings (ModalBake 32a32c9: Epi's v050 takes, bells' rows a
  take, piano-iowa-vel fitted as piano-iowa3). Graded on the recordings:
  marimba and vibraphone clean; piano-iowa-vel's "off" bass is its
  recordings' own (16 -> 1-2 left, all in the top octave where the detector
  is unreliable); tine-vel's octave at hard velocity is its recordings' bark;
  what remains is transposition outside the recorded ranges. The fault in
  range: ep-vel's bass fits (3rd, 4th harmonics 20-40 dB short) — a refit,
  for Combust to call. Also (3cbb80f): the staged strike builds nothing from a
  world changed since the request (it read a world that may be gone; tested).
- 02:59 — why ep-vel's bass fits miss the 3rd and 4th harmonics (ModalBake
  docs/ep-bass-2026-09-28.md, 8b95d94): the fits sit with the tine dead
  centre on the pickup (h/w 0.014-0.03), where a symmetric field makes the
  2nd harmonic and little else at any swing; the runtime's own pickup set a
  tenth to a third of a width off centre reaches the recordings within 3-6 dB
  over h2-h4. A local optimum; the refit seeded off centre is minutes of GPU
  on ~20 records — written up, not run (the models were called good enough).
- 03:25 — check-in; validation of tonight's code on real worlds. The three
  resonator tests clean under ASan + UBSan; the module-like stress (staged,
  roll and audio-rate v/oct with the axes moving) clean under both on piano,
  wurli, ep-vel, piano-iowa-vel and guitar-str-vel. And the staged strike is
  bit for bit the inline build on seven card worlds — plain, both pickup
  forms (wurli's gap, ep's bell), a string family, a layered grand, the bells —
  over 6000 blocks with body, decay and coil moved (278 of 375 strikes staged).
- 03:30 — docs/modal-mode.md has a "Fast playing" section: the staged strike,
  the rebuild limits, the tail share, the governor, how to read the CPU line
  (98a5d83). And the CPU line now says "N staged/s" (9956cc6): if the strike
  number stays high in the morning, it tells whether the module took the
  voices built for its strikes at all. Firmware 9956cc6.
- 03:58 — the room for the exciters, tried on a side branch (`modal-room`,
  worktree .wt/room inside the project; `modal` untouched). The engine as a
  template on its voice count, the right engine one voice: green, goldens
  identical, the engine 17.5 KB smaller — but the second instantiation
  compiles the engine's code twice and SRAM falls only 4.8 KB (99.3 -> 98.3
  %). Measured and committed there (1796376) as the reason not to. The way,
  for the day: one EngineCore on lent voices (as the morph scratch and the
  strike plan are lent), Engine a thin subclass owning four so every caller
  is unchanged, the stereo pair lending L four and R none — about 23 KB, no
  code twice; 53 voice-array references and 25 kPoly uses to walk, which is
  a daylight job with the Exciter page decided.
- 04:30 — done anyway, on the side branch, since it risks nothing on `modal`
  (7bbfe05 on modal-room): EngineCore on lent voices, Engine owning four,
  the stereo pair lending L four and R none. AXI SRAM 99.3 -> 95.8 % (17 KB
  back; about 21 KB of headroom where there was 3.5), goldens identical, all
  green, clean under ASan/UBSan, test 22j. Its firmware is
  shell/alchemy/build/kyklophoria-room.bin — for after kyklophoria.bin has
  been played, not instead of it. Merge into `modal` is Combust's call.
- 04:55 — for decision 4 of docs/exciters.md (keep the recorded attack as
  the default until the synthesised strike is heard against it): the A/B/C
  rendered, ModalBake out/wav/exciter-ab/ (ignored folder; the source beside
  them as exciterab.cpp.txt). piano-salamander and wurli, C2 C4 C6, velocity
  0.3 0.6 0.9: A the recorded attack and modes as today, B the same modes
  struck by the coupled hammer (Hunt-Crossley felt at an eighth of the
  string, no recording), C B with the contact noise; B and C matched to A's
  loudness over the first second, each triplet scaled together under 0.9.
  The Wurlitzer's hammer versions peak well over the recording's at the same
  loudness — the felt's impulse through the pickup — so listen for the click.
- 05:15 — check-in. Nothing left on the list that needs neither hardware
  nor a decision; the project memory carries tonight's findings for the next
  session. Holding until the 07:52 wrap-up (pages rebuild, clean firmware,
  the morning summary); check-ins continue.
- 05:43 — check-in, holding. Considered and left: the lead-end Advance's
  libm sin/cos (a percent or two on the M7, unmeasured) and plans that accept
  a millisecond-old tune (only for decay/coil CVs at audio rate). Neither is
  worth changing tested firmware for before it has been played.
- 06:13 — check-in, holding for the wrap-up.
- 06:43 — check-in, holding for the wrap-up.
- 07:13 — check-in, holding for the wrap-up.
- 07:52 — wrap-up: check-ins stopped, suite green (link_check 155/155,
  selftest 230/230), firmware and pages rebuilt at this commit.
