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

2. **A grade that hears decay.** Done in its first form: `grade --ring`
   weighs every mode by its energy over the whole decay, and the space can
   carry decay as a block (`bake --decay`, log zeta per mode) so that a fitted
   record's measured decay survives into a world. On the FEM corpus the ring
   grade moves nothing — Rayleigh decay is a function of frequency, so it adds
   no information — which is the point: it is there for the records that come
   from recordings. The pickup's nonlinearity is still to come.

3. **The Rhodes as the first world.** Half done, from the other end: the
   pickup is fitted (`fit_shaped`, findings), and it turned out to be most of
   the instrument — the tine is one sine, the harmonics and the velocity
   behaviour are the field's. What is left of this step is the tone bar as
   a second body and the strike point; the pickup is a `Shaper` with a
   voicing knob. Epi (`../epi`) is the bench for all of it: any note, any
   velocity, any voicing, eight metals, with the answer known. Four real parameters, not one: tine
   length, tuning-spring position, tonebar coupling, strike point. The tonebar
   is a second FEM body coupled through the clamp; the pickup is a memoryless
   nonlinearity at a position — a `Shaper` stage, which exists. Where a
   measurement is cheap it beats a model: a recording gives (f, T60,
   amplitude) per partial directly, and that is a better damping than any
   Rayleigh model, which is where the GPU comes in.

4. **Fitting to sources on the GPU.** Two uses, in order of how sure they are:
   - *Modal fitting.* Done in its first form: `tools/modalfit.py`, torch on
     the 4090 (the `fmexplorer` venv has it). Phase-vocoder initialisation,
     then a differentiable sum of decaying sines fitted by gradient against a
     multi-resolution STFT loss — spectral convergence plus log magnitude,
     because log magnitude alone let the loud modes ring four times too long:
     two loud bins in two thousand do not move a mean over the floor. Checked
     on synthetic strikes with known modes: the modes above -20 dB come back
     within a cent and within 2% in T60; modes that ring for 80 ms at -26 dB
     are dropped rather than misfitted, and the record says how many. A
     recording is one strike position, and the record says that too. What it
     needs now is recordings: a Rhodes, a Wurlitzer, the water drum.
   - *Geometry fitting.* Fit the FEM's *inputs* — dimensions, material, the
     spring's position — so that its modes match the recording. The FEM is not
     differentiable, so this is black-box optimisation over a handful of
     parameters, and the 4090 is for running many FEMs rather than for
     gradients. Also where the "crazy topologies" go: sweep a parametric family
     of shells, tubes with baffles, coupled cavities, and keep what sounds like
     something.
   Neither needs torch on the module; both need it on the desktop, and the
   `fmexplorer` venv has torch 2.11 with CUDA on the 4090.

5. **Condense.** kykeigen's recipe on the per-family corpus — log, PCA,
   whiten, bake to a lattice — which is what `tools/bake` already does with a
   manifold in the middle. Four components a family, the lattice in SDRAM.

6. **The runtime.** A `World::Kind::Modal` that evaluates to a modal set
   instead of a spectrum, and a resonator bank in the engine behind the same
   `dirty_`/render logic, then the world's `Shaper` — the bell field,
   differentiated, the coil — where the world carries one. Header-only, no
   heap, no exceptions; measured with `make armcost` before it is believed.
   The bank is small for an electric world (a few modes) and the stage is a
   table lookup, a difference and a biquad: cheaper than the 48 harmonics it
   replaces.

## What is not a modal body

- **Wind.** Air columns are modal (analytic — open, closed, conical), but a
  wind instrument is self-oscillating and the exciter is the whole problem.
  Breath noise into a pipe bank is honest and cheap; a reed model is not.
- **Water.** A drop is one Helmholtz bubble with a pitch chirp (van den Doel);
  a stream is a stochastic shower of them. Cheap, and an exciter question.
- **Fire.** Crackle: a stochastic impulse train into small bodies. Exciter
  again.
- **The water drum.** A glass jar half full of water, a metal lid, a thumb.
  The lid is a modal plate (FEM); the air above the water is a Helmholtz
  cavity whose volume — and so whose pitch — is the water level, which is the
  playing parameter; the "hyper-cavitation" of the thwok is the exciter, a
  short broadband burst, and the cavity's coupling to the lid is a mass-spring
  on the lid's centre. Two bodies coupled, one continuous parameter that is
  literally turned, and a family the FEM can sweep on water level. The first
  thing to fit from a recording, because it is the one instrument in this
  list that is in the room.

These are all "an exciter into the bank", which is the same runtime with a
different mallet — the wavetable, noise, a bubble train — and no new
representation.
