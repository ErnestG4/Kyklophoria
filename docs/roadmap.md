# From a bake to an instrument

What comes after the feasibility bake, in the order the dependencies run, and
what each step is for. The bake's verdict — a four-dimensional space of three
unrelated families does not hold, a family with real parameters does, and the
sound of a struck thing lives in its decay, its strike and its pickup, none of
which the grade heard — is the plan's premise.

**Where it stands, 21 September 2026.** The six steps below are done to the
line where the module is the only test left, and the module has now had two
evenings: it played, it overran, the overrun was three costs none of them the
voice, and then it "sounded amazing" (Combust, 20 Sept). Fourteen worlds ship
as `.kykm` files on the card; Kyklophoria's `modal` branch plays them behind
`World::Kind::Resonate` with a trigger, four CVs, a page readout, polyphony
and the spin. The second half of this document is what comes next, in order,
with what each item costs and what it would fix — the working list, since the
first six are history now.

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

The exciter was going to be the wavetable — Kyklophoria's oscillator into the
modal bank. What shipped first is a stored burst: the recording's own attack
minus the model, per note and per velocity layer, read at the played pitch and
filtered by velocity, which is commuted synthesis and the SY99's lineage in one
(`lit-runtime.md`). The oscillator into the bank, and the audio input into it
(Rings' external exciter), are still the right next exciters and are below.

## The six steps, done

1. **Clamped boundaries in the FEM.** Done. Every tine and reed is a
   cantilever; mesh2faust was free-free. Checked against the clamped-free rod:
   +8% at every length, the same everywhere in a sweep.

2. **A grade that hears decay.** Done: `grade --ring`, `bake --decay`, and
   then the metrics that mattered more — `decay_ratio` per note (fitted over
   measured T60), `excess_db` (whistle), the ringers check
   (`tools/ringers.py`), the sustain band error (`tools/washcheck.py`).

3. **The Rhodes as the first world.** Done, from both ends: the pickup fitted
   (`fit_shaped`) and found to be most of the instrument; Epi (`../epi`) as
   the bench with the answer known; the geometry fit putting a FEM tine on
   the recorded note. Then the Wurlitzer, the EP with velocity layers, the
   reed with an electrostatic gap, and the acoustic sets.

4. **Fitting to sources on the GPU.** Done: `tools/modalfit.py` and its
   drivers, with the gates, the beat pairs, the knees, the decay prior, the
   T60 cap, the phases (the STFT loss is sign-blind; the residual is not),
   `pitchman.py` for sets with no note names, `layer()` for sets with
   several dynamics a note. Fourteen worlds: wurli, ep, ep-vel, tine-vel,
   reed-vel, guitar, banjo, mandolin, violin, viola, bass, piano, perc (a
   row of nineteen bodies), tine-h045.

5. **Condense.** Done: `.kykm` v5 — five bytes a mode, the stage, the wash,
   the bursts, velocity layers, slots aligned by ratio to the note along the
   chain of points with ghosts so a mode fades rather than slides, each
   point pulled to its nominal note, a `kind` byte for a row of bodies.

6. **The runtime, in the engine, on the module.** Done past the line:
   `core/kyk_resonate.h` and the engine's resonate path, the card path for
   a `.kykm` (a card world, read into an SDRAM region and played where it
   lies), the strike, the spin, the readout, polyphony, J4 as the trigger
   and J5–J8 as the four CVs, the page's panes for a resonator. What the
   bench taught is in `Kyklophoria/docs/modal-mode.md` and `findings.md`:
   the render under a voice, `At()` on pot jitter, the wash gain on the
   audio thread, the pickup's rest flux, the slots by rank.

## What comes next, in order

Each with what it fixes and what it costs. *Measured*, *reasoned* and
*hunch* as in `lit-runtime.md`; the numbers are that document's and
`findings.md`'s.

1. **Hands on it, again.** The remap of J4–J8, the polyphony, the strum on
   a note jump, the in-tune export and the aligned slots have not been
   played. That is one evening and it decides the order of everything
   below. *Ask:* does a note between points now sound like the note; does a
   trigger with a velocity CV feel like an instrument; does 4 voices
   overrun.

2. **The bass notes' cliff** — the largest measured gap in the sound.
   `lit-runtime.md` items 3 and 5: first the band-wise burst crossfade (the
   burst's top fades on its own decay, its bottom hands to the modes;
   hides the cliff, a day, no format change), then B per note in the world
   (a float a point, costs nothing), then the waveguide above the modes for
   string worlds (the structural fix; a delay line a voice in AXI; a week
   and a flag in the format). *Reasoned*, the cliff *measured* at 60–77 dB
   above 2 kHz from the burst's end on a piano E1.

3. **Rings' external exciter.** J1's audio into the bank instead of a
   strike: one add a mode a sample, a mix pot, and the modal worlds become
   a resonant filter bank for anything patched in. *Reasoned*; the cheapest
   large feature on the list.

4. **The oscillator into the bank.** The plan's original exciter: the
   wavetable world as the mallet, so both halves of the firmware are one
   instrument. Needs a second world slot (the exciter's) and a way to pick
   it; the tour's ring of buffers is most of that. *Reasoned*.

5. **Bodies you can tune.** A flag on an index world so the pitch pot
   transposes the body — a tom tuned by v/oct. A few lines in the engine
   and a byte in the header; asked of Combust, not yet answered.

6. **More modes where they are needed.** The bank is 48 everywhere; a bass
   piano note wants 96 and a treble one 12. A per-point count is in the
   format already (`N` is the maximum; ghosts fill the rest); a per-point
   *cost* is the runtime skipping ghosts, which it could. Then refit the
   piano's bottom octave at 96. *Measured* that 48 stop at 1.7 kHz on an
   E1; *reasoned* that 96 is a quarter of the core at one voice.

7. **The UX.** The build tab's grid is fixed; the resonator has panes and
   named pots; what it does not have is a page of its own — the worlds
   tab is a library and the play tab is a readout, and neither is where a
   resonator's strike, spin, voices and burst live together. Also: the
   tune sliders should know when the module owns the spin; the Play page
   pot labels on the module's own rings.

8. **The refit, from the holistic pass** (`docs/holistic-math.md`, its
   items 9, 10 and 7 — the three the export rules only contain). Three
   changes to the fitter, then every set again, three hours on the GPU:
   - *A cluster penalty.* The fitter builds non-exponential attacks out of
     large antiphase pairs — the Wurlitzer C4's loudest mode, 7.7, is half
     of a pair summing to 1.1; `loudest` sits 14 dB (Wurlitzer median) to
     46 dB (piano worst) over what its own cluster sums to. Valid, and
     fragile: a byte, a slot or a lerp that moves one of the pair by a
     hair un-cancels it. A term on Σ|aᵢ| against |Σ aᵢ e^{jφᵢ}| over the
     modes within 1% of each other makes a record's amplitudes its audible
     amplitudes. Then the index world's glide stops paying the bump too.
   - *One coil per set.* `fit_shaped` fits fc and Q per note and uses the
     coil as a free equaliser (Q 0.02–55 across the EP; 13 of 84 records
     outside 0.3–10), and the runtime interpolates them linearly, 60 dB
     off the fit at h3 on those. A coil is one L, one C, one R: share fc
     and Q across a set's notes, or penalise their slope along the
     keyboard; and interpolate fc, Q, K geometrically in the runtime.
   - *A monotonic prior on the swings.* 39 of the EP's 84 notes had MAX
     under MED (every note from midi 81 up, where the tine barely moves
     and the swing is ill-determined). The export reorders them now; the
     fit should not produce them.
   Then the sets not fitted yet: the Philharmonia's guitar harmonics, the
   cello (no single-note pizz; the `phrase` files could be cut), Epi's
   other metals as a second tine-mh axis, and the water drum, which is
   still the one instrument in the room. ESPRIT on the gong's residual
   only if the wash is heard wrong there.

9. **Merge.** `modal` into `master` when 1 says the module is an
   instrument in this mode: the branch is 30-odd commits of engine, shell,
   page and tests, every one host-tested, and the suite is green on it.

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
different mallet — the wavetable, noise, a bubble train, the audio input — and
no new representation.
