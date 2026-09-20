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

3. **The Rhodes as the first world.** Done, from both ends. The pickup is
   fitted (`fit_shaped`, findings) and turned out to be most of the
   instrument — the tine is one sine, the harmonics and the velocity
   behaviour are the field's — and the voicing sweep says the fitted knob
   is the screw. The tone bar is in the records as the 1.4-1.7x partial the
   metal carries, measured rather than modelled; the strike point is the
   twelve gains of a FEM tine, and the geometry fit (4) puts that FEM tine
   on the recorded note. Epi (`../epi`) is the bench for all of it: any
   note, any velocity, any voicing, eight metals, with the answer known.
   As first written: Four real parameters, not one: tine
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
   - *Geometry fitting.* Done in its first form: `tools/geofit.py`,
     Nelder-Mead over the tine's length and spring position, a fresh mesh
     and a modalfem run per evaluation (a second each), against the two
     numbers a shaped record gives the metal — the fundamental and the
     second bending mode's ratio. Seven EP notes, C3 to E6: f1 to a tenth of
     a hertz and the ratio to a hundredth on every one, with lengths 106 to
     33 mm and springs 18 to 49 mm from the tip (`out/geofit-ep.tsv`). The
     FEM family is the instrument, note by note. Where the "crazy topologies"
     go is unchanged: sweep a parametric family and keep what sounds like
     something.
   Neither needs torch on the module; both need it on the desktop, and the
   `fmexplorer` venv has torch 2.11 with CUDA on the 4090.

5. **Condense.** Done in its first form: `tools/export.py` writes a world as
   a `.kykm` — four bytes a mode (cents, a log-decay byte, a quarter-dB
   byte), the stage per point, 48 harmonic slots from the aligned corpus for
   a pitched world or the metal's few modes for a shaped one. The EP is
   38 KB, the Wurlitzer 2.5 KB. `runtime/world.h` attaches the blob where
   it lies, decodes a point at note-on and interpolates between the two
   neighbouring points by slot — log frequency, log decay, dB — so eleven
   recorded Wurlitzer notes are a keyboard. Round trip within a cent. The
   bake's four-component manifold is the other road to the same lattice and
   stays for the FEM families; for a keyboard, the keyboard is the lattice.

6. **The runtime.** A `World::Kind::Modal` that evaluates to a modal set
   instead of a spectrum, and a resonator bank in the engine behind the same
   `dirty_`/render logic, then the world's `Shaper` — the bell field,
   differentiated, the coil — where the world carries one. Header-only, no
   heap, no exceptions; measured with `make armcost` before it is believed.
   Done to the line where hands are needed. `runtime/modal_bank.h` and
   `world.h` here are the prototype; `core/kyk_resonate.h` on Kyklophoria's
   `modal` branch is the same thing in that repo's conventions (nothing
   with a default member initialiser, so it can live in SDRAM), with
   `tests/resonate_check` in its suite — a mode within 0.01 cent and 0.1%
   in T60, the EP's C3 barking +8.5 → +20.2 dB where the fit said +7 → +19,
   the Wurlitzer interpolated between its eleven notes — and `make armcost`:
   ~15 M7 instructions a mode a sample, 48 modes under a tenth of the core.
   The check caught a real bug on its first run (the level scale applied
   after the bank took the gains). What is not done: wiring it behind
   `Engine::Process` as a stage on the oscillator's output under a new
   `World::Kind`, the exciter (gate, oscillator, both) and the pots. That
   is `docs/modal-mode.md` in Kyklophoria, written to be executed with the
   module on the bench and `switch_check` extended first, because that
   repo's history says what blind changes to world-derived state cost.

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
