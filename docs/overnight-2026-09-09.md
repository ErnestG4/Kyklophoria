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

Filled in as it goes; the morning report is the last section.
