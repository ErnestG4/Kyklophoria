# From a bake to an instrument

What comes after the feasibility bake, in the order the dependencies run, and
what each step is for. The bake's verdict — a four-dimensional space of three
unrelated families does not hold, a family with real parameters does, and the
sound of a struck thing lives in its decay, its strike and its pickup, none of
which the grade heard — is the plan's premise.

## The shape of the thing

One **world per family**, exactly as Kyklophoria has one world per formula: a
Rhodes is a world, a Wurlitzer is a world, a shell of revolution with a flare is
a world. Morph *within* a world through its baked space; morph *between*
worlds with the tour and the morph target, which already exist and do not care
whether a node holds 64 harmonics or 48 modes. The bake stays offline. The
module gets, per world, a lattice of modal sets — log frequency, log decay,
gains — and a bank of second-order resonators driven by an exciter. Forty-eight
resonators is about three hundred operations a sample, a few per cent of the
M7; the morph is coefficient updates per block, which is what the engine
already does with the frame. Nothing needs a manifold at runtime.

The exciter is the wavetable. Kyklophoria's oscillator into the modal bank —
the existing worlds become the mallet, and the modelling synth is a second mode
of the same firmware rather than a fork. If it fails to be one, it is a
separate instrument with the same tools behind it.

## Steps

1. **Clamped boundaries in the FEM.** Done. Every tine and reed is a
   cantilever; mesh2faust was free-free. Checked against the clamped-free rod:
   +8% at every length, the same everywhere in a sweep.

2. **A grade that hears decay.** The bake's spectrum was uniform-damped at
   200 ms because the brief said so, and that is the meter that cannot hear
   why the listening set sounds like something. The next grade renders a
   strike over its whole ring — per-mode decay, the pickup's nonlinearity —
   and measures variety on that. Pitch normalised, so that a Rhodes an octave
   apart is the same thing and a Rhodes against a Wurlitzer is not.

3. **The Rhodes as the first world.** Four real parameters, not one: tine
   length, tuning-spring position, tonebar coupling, strike point. The tonebar
   is a second FEM body coupled through the clamp; the pickup is a memoryless
   nonlinearity at a position — a `Shaper` stage, which exists. Where a
   measurement is cheap it beats a model: a recording gives (f, T60,
   amplitude) per partial directly, and that is a better damping than any
   Rayleigh model, which is where the GPU comes in.

4. **Fitting to sources on the GPU.** Two uses, in order of how sure they are:
   - *Modal fitting.* Partial tracking on a recording gives a modal set; a
     differentiable renderer (sum of decaying sines, all parameters
     continuous) fits frequency, decay and gain by gradient to the recording
     and gives the corpus a row that is a *measured* Rhodes beside the FEM
     ones. This is the part that makes a model "absolutely kick ass" rather
     than plausible: the FEM supplies the parametric freedom, the recording
     supplies the truth, and the space is built from both.
   - *Geometry fitting.* Fit the FEM's *inputs* — dimensions, material, the
     spring's position — so that its modes match the recording. The FEM is not
     differentiable, so this is black-box optimisation over a handful of
     parameters, and the 4090 is for running many FEMs rather than for
     gradients. Also where the "crazy topologies" go: sweep a parametric family
     of shells, tubes with baffles, coupled cavities, and keep what sounds like
     something.
   Neither needs torch on the module; both need it on the desktop, and the
   machine has a 4090 and no torch. Installing it user-side is a `pip --user`.

5. **Condense.** kykeigen's recipe on the per-family corpus — log, PCA,
   whiten, bake to a lattice — which is what `tools/bake` already does with a
   manifold in the middle. Four components a family, the lattice in SDRAM.

6. **The runtime.** A `World::Kind::Modal` that evaluates to a modal set
   instead of a spectrum, and a resonator bank in the engine behind the same
   `dirty_`/render logic. Header-only, no heap, no exceptions; measured with
   `make armcost` before it is believed.

## What is not a modal body

- **Wind.** Air columns are modal (analytic — open, closed, conical), but a
  wind instrument is self-oscillating and the exciter is the whole problem.
  Breath noise into a pipe bank is honest and cheap; a reed model is not.
- **Water.** A drop is one Helmholtz bubble with a pitch chirp (van den Doel);
  a stream is a stochastic shower of them. Cheap, and an exciter question.
- **Fire.** Crackle: a stochastic impulse train into small bodies. Exciter
  again.

These are all "an exciter into the bank", which is the same runtime with a
different mallet — the wavetable, noise, a bubble train — and no new
representation.
