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

(written at the end)

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
