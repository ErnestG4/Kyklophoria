# Overnight, 2026-09-09

Will asked how I would structure a night on this, then to go and do it. This
is the plan, written before starting so the morning can be read against it.

## Constraints that shape it

No bench. Everything tonight is desktop measurement, the headless page, and
the test suite — so anything whose truth depends on hardware is off the list,
including further CPU work beyond what a desktop profile can honestly show.
The last two sessions both ended with me shipping something the desktop said
was fine and the module said was not, so the rule tonight is: **if I cannot
measure it here, I do not claim it.**

Second rule: **leave it flashable.** Every pass ends green and committed. If a
pass goes wrong it gets reverted rather than left half-applied, because the
worst outcome is waking up to a repo that needs archaeology before it needs a
decision.

## Passes

1. **Finish Kepler.** Gravity on the torus, which is Will's own
   topology-meets-gravity idea and is cheap now the mode exists. Then get the
   body into the telemetry and onto the page, since an orbit you cannot see is
   hard to tune.
2. **Kuramoto.** Couple the six orbit-plane oscillators so ratio lock becomes
   something that happens rather than something you choose. Measurable: does
   coupling actually entrain them, and at what strength.
3. **More worlds**, which is the standing request. Priority to an **FM world**,
   because it is the one identified route to genuine non-separability — every
   legible family so far is a product of per-axis factors and measures
   lopsided, and FM is not. Then a formant world, which should sound good
   whether or not it measures well.
4. **Consolidate.** All worlds and modes on one measurement table, a pack of
   renders to listen to before flashing anything, docs, and one firmware
   built from a clean tree.

## What I am deliberately not doing

- Reading v/oct at audio rate. Will has a use for the aliasing as it stands
  and said not to get distracted.
- Anything that needs the bench to confirm.
- The world manifest and the big corpora. Both are real work, neither is a
  night's work, and both would land unverified.

## Result

All four passes landed. Every commit is green and the tree builds a firmware.
Full write-up in `docs/m3-notes.md`; the table of worlds is in
`docs/worlds.md`.

**1. Kepler finished.** Gravity on the torus, derived from the world's own
topology rather than switched, plus a Torus world so it is reachable. The body
and its rush go out at the end of the telemetry frame behind a new flags bit,
and the page draws the orbit — the attractor in the main view when the plane
is on screen, and always an inset of the conic in its own unrotated plane,
because once the rotation is doing anything the main view shows a shadow of
the orbit rather than the orbit.

**2. Kuramoto, on the ratio rather than the phase.** The design note's
equation was wrong for this instrument: plain Kuramoto locks phase as well as
frequency and collapses six planes into one. Coupling on q·θᵢ − p·θⱼ instead
makes ratio lock what emerges. Measured, over 1200 rate settings: 2.7%
landing on a simple ratio at zero coupling, which is chance, rising to 87.9%
at 0.80. It needed a sixth panel page, exactly as the design note predicted
the panel would be the constraint.

**3. Two worlds.** FM was the identified route to genuine non-separability and
it delivered: 1.81x direction spread against 2.1x to 6.1x for every other
legible world, twice the variety of anything at any price, no near-duplicates.
The vowel world was the one expected to sound good rather than measure well,
and it did both — 2.87x, and the closest world to real material in the
instrument.

**4. Consolidated.** `tools/kykworlds` grades every world on one table,
`tools/renderpack.sh` writes sixteen wavs to listen to, three goldens added,
docs updated, firmware built from a clean tree.

### What the rules bought

The measure-or-do-not-claim rule paid three times, and in each case listening
would have found the symptom without finding the cause.

- FM sidebands crossing between DC and the fundamental were being dropped
  outright: a 0.25 step in one render, thirty times any other world's worst.
- The vowel world cost 9.99 µs an evaluation, twelve times anything else,
  from 256 series exponentials per spectrum. Down to 0.98 with the numbers
  measured unchanged to three figures.
- The telemetry magnitude byte was clipping the tallest partial of any peaky
  world by 1.4 dB against a 0.4 dB quantisation step.

The leave-it-flashable rule cost nothing. Nothing had to be reverted.

### What I got wrong, and caught

The first render pack put all five motion renders at the exact centre of the
cube, which is the rotation pivot and therefore a fixed point of every
rotation. Five files came out bit-identical. It is now a known wart, because
it is reachable from the page and looks exactly like a broken orbit.

### Still not known

Nothing here has been played, and there is no CPU number from the module. The
x86 figures say all of tonight's work is small next to one transform, but the
M7 has surprised this project twice.
