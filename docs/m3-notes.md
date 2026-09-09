# M3 notes — worlds, coupling and gravity (2026-09-09)

An overnight, planned in `docs/overnight-2026-09-09.md` and worked in four
passes. The rule set before starting was: **if I cannot measure it here, I do
not claim it**, because the last two sessions both ended with something the
desktop said was fine and the module said was not.

Everything below is a desktop measurement or a test-suite result. Nothing here
has been on the bench.

## 1. Gravity on a torus

Under `Wrap` the space has no edges, so the attractor has an image in every
direction and the body should feel the nearest one. Folding the displacement
into ±half a turn does that. It changes the character rather than the numbers:
at high eccentricity the body no longer swings out to a far apoapsis and back,
it leaves one side and arrives from the other.

| space | ecc | r min | r max | v max/min | laps in 200 s |
|---|---|---|---|---|---|
| flat | 0.50 | 0.2007 | 0.3500 | 1.74 | 121 |
| flat | 1.00 | 0.3500 | 1.3594 | 3.88 | 23 |
| torus | 0.50 | 0.2007 | 0.3500 | 1.74 | 121 |
| torus | 1.00 | 0.0020 | 0.7065 | 3.81 | 166 |

Whether it wraps is **derived** from the world's topology on the orbit plane's
two axes, not switched. A toroidal potential in a clamped space would pull
toward an image that is not there, and there is no reading of the panel under
which a player would want that.

A ninth world, **Torus**, wraps on all four axes so the behaviour is reachable.
It also turned out to be the second most isotropic space in the instrument
(1.26x) and the third most varied.

## 2. Kuramoto, but on the ratio

The design note specified textbook Kuramoto, `dθᵢ/dt = ωᵢ + (K/N)Σ sin(θⱼ−θᵢ)`.
That is the wrong equation for this instrument: it drags every oscillator to
one frequency *and one phase*, collapsing six planes into a single rotation.

What is wanted is the ratio to lock. For each pair the nearest simple rational
p/q to their rate ratio is found — once, when a rate knob moves — and the pair
is coupled through ψ = q·θᵢ − p·θⱼ, stationary exactly at that ratio:

    dψ/dt = (q·ωᵢ − p·ωⱼ) − K·sin(2πψ)

A fixed point exists whenever the ratio detuning is within K, which is an
Arnold tongue. Complex ratios get narrower tongues for nothing, because their
detuning term carries the larger integers, so sweeping a rate steps through a
staircase rather than gliding.

Measured over 1200 rate settings from 0.4x to 3x, 300 s each, counting how
many land within 0.2% of a coprime p/q with both under six:

| coupling | 0.00 | 0.01 | 0.02 | 0.05 | 0.10 | 0.15 | 0.25 | 0.40 | 0.80 |
|---|---|---|---|---|---|---|---|---|---|
| captured | 2.7% | 3.6% | 4.9% | 9.8% | 18.9% | 28.1% | 46.2% | 65.3% | 87.9% |

2.7% is chance. The whole knob is useful travel. Cost is one table sine per
active pair per block, fifteen at N=4: **+0.117 µs per block on x86**, against
a 500 µs block period.

**Reach** caps the integers, 1 to 5. At 1 the only lock is unison; at 5 the
full staircase is open.

The **lock readout** is the smoothed mean of cos(2πψ), not |cos|. Stability
puts a real lock on the branch where the cosine is positive, so this reads
near 1 locked and 0 drifting. The absolute value would read 1 at the unstable
anti-phase and 0.64 for a freely drifting pair, which is a readout that never
says no.

## 3. Two new worlds, and why the old ones were lopsided

Every legible family in the instrument weights the harmonic series
multiplicatively, and multiplicative weights add in log magnitude. A sum of
per-axis terms is separable; a separable space has natural axes by
construction; and rotating a space whose axes are already natural finds
nothing. That is the structural reason those worlds measured 2.1x to 6.1x
direction spread, and no tuning was going to fix it.

**FM** escapes it. J_k(I) does not factor into a function of k times a
function of I, and the sideband *positions* move with the ratio, so one axis
relocates what another put down. 12.8 variety — twice the next — 1.83x spread,
and no near-duplicates anywhere.

**Vowel** escapes it by chaining: the second resonance is a multiple of the
first and the third a multiple of the second, so the first axis moves all
three. 2.87x, and the closest world to real material in the instrument: the
best median distance to the Braids bank of anything here, including the
eigenspace baked from that bank.

Full table in `docs/worlds.md`.

### Three things measurement caught that listening would have missed

(The FM row was measured twice. The first table, and the commit that
introduced the world, carry 13.317 variety and 1.81x spread — those are the
numbers *before* the DC-crossing fix below, when a full-strength sideband
vanishing in one render was inflating the measured variety with a
discontinuity. The corrected figures, 12.758 and 1.83x, are what the docs
carry now.)

**A hard cutoff, worth a factor of thirty.** FM sidebands crossing between DC
and the fundamental were dropped outright. A full-strength first sideband
vanishing in one render is a step of 0.25 in normalised spectrum distance,
where every other world's worst over the same sweep was 0.008. Flooring rather
than cutting off lets the partial fade continuously into the discarded DC bin.
0.2464 → 0.0202, which is what its variety predicts and no more.

**A world twelve times the cost of any other.** The vowel world ran 256 series
exponentials per spectrum: a natural log and a power per harmonic, plus three
Gaussians. The logs and reciprocals of the harmonic numbers are the same 64
constants every time, so they now sit in `kyk_tables.h` beside the sine table,
and each Gaussian is skipped when its exponent is under −12, where it is 100 dB
down. 9.99 µs → 0.98 µs, with variety and spread unchanged to three figures.

**The telemetry was clipping the most important bar.** The magnitude byte
topped out at 0 dB, but the engine normalises a spectrum to a sum of squares
of 2, so one partial can legitimately reach +3 dB — and a peaky world's
tallest one does. Measured 1.4 dB of error against a quantisation step of 0.4.
The byte now spans −96 to +6 dB, which costs 0.024 dB of resolution.

All three were found by cross-checks, not by ear: the first two by grading
every world on one table, the third by a new web selftest that switches the
module to each formula world in turn, parks it at a position and compares the
page's own evaluation against the module's spectrum partial by partial in
decibels. Worst mismatch after the fix is 0.23 dB for FM and 0.27 for the
vowel world, both inside one quantisation step.

## 4. Tooling

- `tools/kykworlds` grades every registered world on one table. `kykspace`
  could only grade a *file*, which reaches half the worlds.
- `tools/renderpack.sh` writes sixteen wavs — one diagonal through each world,
  and one of each way the position can move on its own — at the same pitch and
  level so they can be compared by ear before anything is flashed.
- Goldens added for coupling, FM and the vowel world. The coupling golden is
  the one that would catch a regression of the orbit bug from two builds ago in
  its coupled form: if pot writes ever wipe the accumulated phase again, the
  lock cannot form and the bytes change.

## What this does not know

- **Nothing here has been played.** Every number is a desktop measurement or a
  proxy for sounding good.
- **No CPU number from the module.** The x86 figures say the new work is small
  next to a transform, but the M7 has surprised this project twice — most
  recently with float division costing fourteen cycles in a loop that was
  invisible on x86.
- **Whether the FM world's variety is too high to play.** 12.8 is twice
  anything else. The worst one-render step is proportionate to that and shows
  no discontinuity, so it is not a defect, but a space that changes that fast
  may simply be hard to steer.
