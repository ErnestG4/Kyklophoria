# Kyklophoria

**An N-dimensional morph oscillator for the Hermetic Modular Alchemy Lab.**
The wavetable is not a line or a grid but a 4-dimensional space of spectra
(up to 6). Position CVs pass through an N-D rotation *before* they index it,
so one cable sweeps a diagonal slice no axis reaches, and slow rotation turns
a still hand into an orbit through the space.

**Status: beta.** It runs in the rack and makes sound. Twenty-two
worlds ship, the rotation and its orbit LFOs are real, ratio coupling works,
and presets persist. There is design work left: FM in is not read, most
payload lanes are not routed, and the panel layout is still a first draft.
J2 now clocks a loop of worlds.

## Two firmwares: Kyklophoria and Bongs

The module runs one of two firmwares, swapped from the SD card:

- **Kyklophoria**, the wavetable firmware, is everything below this section.
- **Bongs**, the modal firmware, plays physically modelled instruments. Each
  is a bank of resonances fitted to recordings (ModalBake), struck, plucked,
  bowed or blown by a modelled exciter.

### Bongs

**B1** steps through six pages. **B2** taps a strike, and held it is a gate.
**B3** flips the pitch lock. **J4** is the trigger, and held high it is a gate.
**J5-J8** move the four axes. **J1** is audio driven into the exciter as a
force. The worlds are `.kykm` files in `/kyklophoria/` on the card.

| | Play | Exciter | Resonator | Space | Motion | Kepler |
|---|---|---|---|---|---|---|
| P1 | Coarse pitch | Type | Voices (1/2/4) | Spread | Body-Velocity | Gravity |
| P2 | Fine pitch | Timbre | Release | Listen | Body-Decay | Eccentricity |
| P3 | Body | Position | Dig in | Ear orbit | Body-Coil | Plane |
| P4 | Velocity (the energy) | Noise | Family (switch/morph) | | Velocity-Decay | Damping |
| P5 | Decay | Mass | Pitch (lock/bend) | | Velocity-Coil | Radius |
| P6 | Coil | J1 in | Level | | Decay-Coil | Coupling |

**The exciter types** (Exciter P1):
- **Recorded:** the fitted world's recorded attack.
- **Hammer**, **Pluck:** a modelled felt hammer and plucking finger. The
  hammer is a mallet on the marimba and vibraphone.
- **Bow**, **Reed**, **Lips:** sustained. They drive the newest note while
  there is energy (Play P4, or its CV) and, when gated, while the gate is
  open. At no energy, or with the gate shut, the note rings free.
- **Trained** (the default): a hammer trained per note to the recordings,
  on the worlds that carry one. Elsewhere it plays the recorded attack.

Timbre, Position, Noise and Mass shape each type:
- **Timbre:** a felt's hardness, a bow's pressure, a reed's embouchure, or
  the lips' register.
- **Mass:** the hammer's mass, the reed's impedance, or the lip's Q.

**Gates:** J4 held high, B2 held, or a pad on the web page held (the axis
or a strike pad). A gate owns the articulation for ten seconds after it was
last held. A note whose gate has shut rings free until the next strike. With
nothing gating, energy alone plays the sustained types.

The web page for Bongs is `?page=modal`. Its play view shows the instrument,
its modes, and a row of controls for each panel page, which move the
module's pots (the pot under your hand catches the new value). The design is
in `docs/bongs-controls.md`, and the exciters in `docs/exciters.md`.

## Controls (Kyklophoria)

**B1** taps through seven pages. **B2 + B3** held opens Settings. The page has
three tabs: **play** is a readout of the instrument, **build** makes a world out
of imported waveforms, **worlds** is the library of the ones you have made.

| | Play | Rotate | Stereo | World |
|---|---|---|---|---|
| P1 | Coarse pitch, ±3 oct | plane (0,1) angle | Spread | Position 4 offset |
| P2 | Fine, ±1 semitone | plane (0,2) | Stereo plane | Position 5 offset |
| P3 | Position 0 offset | plane (0,3) | **Morph**: smooth blend to hard steps | Tour division |
| P4 | Position 1 offset | plane (1,2) | Render divider | Tour glide |
| P5 | Position 2 offset | plane (1,3) | Level | Tour free-run |
| P6 | Position 3 offset | plane (2,3) | CV out A depth | |

Four dimensions give six rotation planes, and the panel has six pots. There are
four position CVs and up to six axes, so axes 4 and 5 are pot-only — the World
page is where they are, and a world you wrote can put an **effect** on either of
them. 

Three more pages move the position without you.

| | Orbit | Kepler | Couple |
|---|---|---|---|
| P1 | plane (0,1) rate | Gravity, off at the bottom | **Coupling** |
| P2 | plane (0,2) | Eccentricity | Reach, 1 to 5 |
| P3 | plane (0,3) | Orbit plane | Rate ×, detent at 1 |
| P4 | plane (1,2) | Softening | **Bodies**, 1 to 8 |
| P5 | plane (1,3) | Damping | **Company**: their mass |
| P6 | plane (2,3) | Radius | **World morph** |

**Orbit** turns the rotation planes at rates you set, which never close into a
repeating figure. **Kepler** Designed to introduce orbit between channels. For
now it's an orbiting modulator. 

**Couple** Turn it up and each pair of orbits pulls the other toward the nearest
simple ratio. 

**Jacks.** J3 v/oct · J4 CV out A (the strike trigger under a resonator) ·
J5 to J8 position 0 to 3 (a resonator's body, velocity, decay and coil) ·
J9/J10 stereo out · J1 audio in (FM is planned; under a resonator it is the
exciter, driven into the bank by Stereo P6) · J2 clock, which steps the world
tour. `docs/io-map.md` has the whole map.

**Stereo is not a chorus.** The two channels read the space at rotation angles
either side of centre, so width is an angular spread, and an orbit moves
your ears through the space slightly out of step. At zero spread the output is
mono.

## Phase is part of the representation

Every world used to render against a fixed random phase spectrum. That keeps
the morph click-free, but it throws the *waveform* away: a saw's spectrum at
random phase correlates 0.79 with a saw, so the instrument could never lock
onto one, and it never did — the complaint that started this was never seeing
anything settle into a square or a saw, only stacks of sines rolling through
triangles.

A world now declares its phase convention. At **sine phase**, with **signed
coefficients**, sine, triangle, saw and square all come out at 1.0000 against
the textbook series, at a slightly better crest factor than random phase was
costing. A negative coefficient is a half turn, so blending stays linear and
click-freedom survives intact. Everything from Shapes down the table below is
built in that basis.

## Worlds you write yourself

A world is 24 spectra at 24 positions, blended by distance, with Morph
narrowing the basins until arriving at a vertex arrives at that exact waveform.
That is what `Lock` is; the only thing that made it uncurated was that its
spectra came from a switch statement. So a user world is that written down —
6560 bytes for a four-dimensional one — and it loads into the same code path,
which is why click-freedom and the continuity guarantee come with it unchanged.

Single-cycle WAVs import from the page: WaveEdit and the AKWF corpus already
make and shape them better than anything here would, so this only imports and
places. A node is signed coefficients on the sine basis, so an import loses
whatever sits in the cosine half. Measured over all 48,007 AKWF waveforms
(`docs/importfit-2026-09-13.txt`), as a fraction of each file's own energy:
import keeps a mean 90.3%, and the wave the module renders correlates 0.947 with
the file on average and 0.865 at the tenth percentile. Storing phase would put
those at 0.986 and 0.980 — the band limit costs little and the missing cosine
half costs the rest. The page shows the worst fit of a set by name rather than
hiding it, and will play you any node three ways so you can judge it by ear.
Per-harmonic phase would make import exact and is not available: click-freedom
rests on every cell sharing one phase spectrum, and giving each node its own
would let partials cancel, which is the comb-filtered dip the whole design
exists to make unreachable.

Name a set and **save** it and the page writes a `.kykw` — the module's own
format, the same bytes **send** puts on the wire, so what you heard is what you
kept. Copy it into `/kyklophoria` on the card and it is in the module's list at
the next scan, or hand the file to somebody else. The coefficients are f32
rather than the log encoding telemetry uses precisely so this round-trips: an
export of an import is the file you started from. Saving needs no module
attached; curating a folder of worlds offline is a reasonable way to use the
page.

Building one is its own view — the **build** tab. Playing is a readout of the
module and building is an editor for something it is not holding yet, and they
were sharing a picture, which made the placement rings just more marks on a
live display.

The build view is the set, where each waveform sits, and what the module will
make of it. Every import gets a card with its waveform drawn and the fraction
of it that survived projection, so a poor fit names itself instead of hiding in
a summary. The inspector draws the cycle you imported against the cycle the
engine will render — to the same scale, so what the projection dropped is
visible rather than merely reported — over the 64 signed coefficients the node
actually is.

Nodes arrive on the 24-cell's vertices and are **dragged** where you want them.
A square shows two axes of four, so a drag sets those two and leaves the rest;
the inset in the corner draws the other two and is live, so with four
dimensions a node is finished without leaving the view. That is not a
concession to the screen — the instrument is played one plane at a time, and
placing a node the same way puts it somewhere you can describe. A node far away
in the axes a square cannot show is drawn faint, because a projection that drew
it bright would be lying about the extra directions the whole design exists
for. Once the set has been sent, every drop re-sends it, so you hear where you
just put something; **re-place** is the way back to the arrangement you were
given.

**Effects** put a frame shaper on axis 4 or 5 — wavefold, ring modulate, phase
distort, bit reduce, rate reduce. They are the same five the Shapes and Grit
worlds use, running in the same stage on the rendered cycle, so they cost the
world nothing it was not already paying and inherit the band-limit headroom rule
for free. A resonator is not on the list and cannot be: a world is one cycle read
cyclically, and a resonator has state and is not periodic at f0. Clip the output
into Rings through an ADSR instead — that is settled, with the argument in
`docs/checklist.md`.

They go on axes 4 and 5 rather than on a placement axis, and that is the whole
design of the feature. Put an effect on axis 2 and its depth is wherever the
blend has got to, so "open the folder" and "cross the space" become one gesture
and neither is available alone. Axes 4 and 5 exist because there are six axes
and four position CVs; the module reaches them on the World page, every node
sits at 0.5 on them so they are exactly neutral in the blend — a term equal for
every node cancels out of the softmax — and placement keeps all four of its
dimensions. A world that declares one is written as `.kykw` **version 2**, and
version 1 otherwise, so a module that predates effects still loads every file it
can render truthfully and refuses precisely the ones it would get wrong.

**Add to worlds** keeps what you have built, in the first free slot, and says
which. It is on the build tab because that is where you made the thing: having to
change tabs, find a slot by number and press a button referring to "the build
set" was a flow that only made sense to whoever wrote it. **Play it now** puts it
on the module without keeping it, which is the one you want while you are still
moving nodes around.

**New from Lock** is where you start when you have no wavs in mind. Lock is
twenty-four real waveforms on the 24-cell, one family per rotation plane, which
is a far better thing to edit than an empty set — and better than a slot
pre-filled with silence, which is why there are no blank slots: a slot either
holds a world or holds nothing and says so. A blank world that plays is silence,
and silence that looks deliberate is the worst state a synth can show you. It is
sampled by name rather than by being made live, so pressing it does not interrupt
whatever you are listening to.

You can also **hear** it. Pick a node and the build view plays it three ways —
as imported, band-limited to 64 harmonics, and as the module will render it —
looping the same buffer length at rate 1.0, started together so they stay
sample-aligned, switched by a 4 ms gain ramp so the switch itself does not
click. Levels are matched and the gap is printed, because the quieter of two
sounds is reliably judged the worse one. Imported against band-limited is what
the band limit costs; band-limited against rendered is what the missing cosine
half costs, on its own. The **shape** / **spectrum** toggle beside *add wavs*
decides what an import even is: shape searches for the rotation of the cycle
that keeps the most on the sine basis, spectrum keeps the magnitudes exactly
and lets the waveform be redrawn.

## A library of your own worlds

The **worlds** tab is every world there is: the twenty-two that ship, then the
ones you made. One grid, one selection, and three verbs that work on either kind
— **play** it, **morph towards** it, **open to edit** it. Only the ones that are
yours can also be forgotten or written to the card, because the others are in the
firmware.

Opening one to edit puts it in the build tab. One of yours comes back exactly,
the bytes it was given. One that ships is sampled at the 24-cell vertices and
says so, because a formula is not twenty-four spectra — it borrows a free slot to
be read back through and gives it straight back, so looking at a world costs you
nothing.

Drag a `.kykw` from the desktop onto a slot to load it — the file is parsed and
refused by the page before any of it goes on the wire, so a wrong file costs a
message rather than a transfer — and **drag one slot onto another to exchange
them**.

Exchange rather than insert, because with a fixed grid of numbered slots
shifting the rest along renumbers everything the module is holding, including
whatever is playing. A swap touches two. Dropping onto an empty slot is
therefore a move, which is what it looks like. The module does the swapping: a
host does not have the blob for a slot it did not put there, only the name.

That last one is the point. Until slots existed a world you imported could be
played and never *returned to*: a transfer replaced whatever was live, and the
morph target could only ever be one of the built-ins, so nothing you
made could be one end of a blend. Now it can be both ends.

The slots are blobs in SDRAM, expanded only when one is played — a world is 7 KB
of the fast memory there is little of, and a blob is 6.7 KB of the 64 MB there
is plenty of. All thirty-two cost 216 KB of it.

**The card is writable now**, which is what makes any of this survive a power
cycle. Pick a slot, press *save to card*, and the module writes
`/kyklophoria/<name>.kykw` — named after the world, through a temp file and a
rename so a write that dies half way cannot leave something the scanner will
list and the parser will accept. If that name is already there it **refuses**,
and the page asks whether to replace it or save as `name-2.kykw` instead. The
refusal is in the module, not only in the dialog, so a host that forgets to ask
cannot quietly overwrite somebody's collection.

The other direction too: pick a card file and *into the slot* loads it, so the
card is a library you keep rather than a one-shot load. That closes the loop —
build, slot, card, power cycle, slot, play.

The slots themselves are still RAM: what survives is what you put on the card.

One honest limit: the module says which slots are filled and what they are
called, not what is *in* them. So the play view can draw the rings of a slot
this page put there and not of one stored in an earlier session — that one
plays, and its rings are left undrawn rather than guessed at.

The play view keeps the rings, and they are **what the module is holding** —
not the set on the other tab. Those are the same thing only between a
successful send and the next edit, and drawing the draft there had the page
claiming the instrument contained something it did not. There is no way to ask
the module what is in its world, so the page draws its own record of what it
sent and throws it away the moment anything could have replaced it: a built-in
selected, a card world loaded, the link dropped, a send that failed, or a world
list that comes back saying a built-in is live. Emptying the builder does not
throw it away, because the module is still playing what it was given.

**Where this stops, for now.** The card is still read-only from the module, so
a world sent over the link lives in RAM and is gone at the next boot — save it
and copy it across to keep it. None of this is needed to play the instrument;
it is needed to keep what you make.

## Worlds

Twenty-two ship. A world is either a **formula**, evaluated wherever you happen
to be standing, or a **lattice** of sampled spectra, interpolated between.
Which one a world is is a property of the world, not a storage decision: if a
formula is 1.3 KB and exact everywhere, baking it out to a grid
would cost 1.18 MB for no gain.

| world | what it is | variety | spread |
|---|---|---|---|
| Crop | the first four principal components of a 256-wave bank (Braids, Émilie Gillet) | 1.47 | 2.14x |
| 24-cell | a waveform on each vertex of the 4-D solid | 0.66 | 3.64x |
| 16-cell | eight vertices, on the axes: mostly mire | 0.38 | 3.14x |
| Tesseract | sixteen, on the cube corners | 0.47 | 4.22x |
| Stack | one waveform idea per axis | 3.43 | 3.69x |
| Field | correlated noise, even in every direction | 2.81 | 1.31x |
| Field II | the same, rougher | 5.87 | 1.39x |
| Torus | a field with no edges; gravity wraps with it | 4.33 | 1.26x |
| Harmonic | one parameter per axis, legible but lopsided | 0.20 | 6.13x |
| **FM** | index, ratio, carrier, and a second carrier that interferes | **12.76** | 1.83x |
| **Vowel** | three chained resonances over a falling source | 1.50 | 2.87x |
| **Shapes** | a 4×4 grid of real waveforms, plus fold and phase modulation | 0.99 | 8.47x † |
| **Shapes R** | the same grid, with fold and ring modulation | 0.99 | 8.47x † |
| **Lock** | real waveforms on the 24-cell, a family per rotation plane | 0.82 | 4.72x |
| **Unison** | one wave stacked on itself; interval, detune, wave | 3.95 | 4.39x |
| **Plate** | a struck plate; strike, geometry, time, damping | 1.31 | 4.78x |
| **Bar** | a struck bar, tuned onto the harmonic grid | 1.15 | 3.31x |
| **Drum** | a struck membrane, tuned; dense and low-ordered | 0.88 | 3.52x |
| **Saw** | nothing but a saw: tilt, parity, comb, fold point | 1.44 | **2.03x** |
| **Pulse** | nothing but a pulse: duty, tilt, comb, fold point | 2.79 | 3.36x |
| **Edge** | saw against pulse, only those two, bent four ways | 1.86 | 2.57x |
| **Grit** | the same grid as Shapes, bit- and rate-reduced; aliases on purpose | 0.99 | 8.47x † |

`build/host/kykworlds` prints this table, and with `--braids <resources.cc>` adds
a column for how much of the Braids bank each world can reach.

† The three grid worlds — Shapes, Shapes R and Grit — are measured on their
first two axes only. The other two drive shapers that act on the rendered cycle
rather than the spectrum, so the tool cannot see them and scores them dead,
which is most of that 8.47x.

**Spread** is the number that decides whether rotating the control frame was
worth building: the most varied direction through the space over the least. At
1.0 every direction is as interesting as every other. Most legible worlds
measure badly at it, and the reason is structural rather than a failure of
taste — they weight the harmonic series multiplicatively, multiplicative
weights add in log magnitude, and a sum of per-axis terms is separable. A
separable space already has natural axes, so a rotation finds nothing new.

FM is the exception, and the k'th sideband being a Bessel function of the
modulation index is why: it does not factor, and the sidebands *move* with the
ratio, so one axis relocates energy another axis put down. Vowel escapes the
same way, by chaining its three resonances so the first axis moves all three.

**Narrow worlds beat broad ones.** Saw does one thing and spends all four axes
bending it, and at 2.03x it is second only to FM among the designed worlds —
ahead of every world that tried to span a wide range with four balanced axes,
Crop included. A world that already knows what it is has no axis fighting
another for the same job. Saw, Pulse and Edge exist to test that, and it held.

**The modal worlds needed a time axis.** A struck thing is defined by its
decay, so Plate and Bar and Drum put strike, geometry, **time** and damping on
the four axes: the axis is how far the decay has got, frozen, not a decay that
runs. Sweeping it back and forth is not time running backwards — it is moving
through a family of spectra that a decay generated.

```sh
build/host/kykworlds --braids ../../Mutable/Streams/eurorack/braids/resources.cc
tools/renderpack.sh                       # a wav per world plus the motion renders
build/host/kykspace info space.kyk        # grade a baked lattice file
build/host/kykeigen space.kyk --braids ../../Mutable/Streams/eurorack/braids/resources.cc
```

## A loop of worlds, on a clock

The loop is built on the **worlds** tab, under the library: pick a world, **add
stop**, repeat. The row shows the stops in the order the clock walks them, marks
the one that is live and the one the sound is travelling towards, and says how
far along it is. It is a readout of the module and not a memory of what the page
sent — the clock moves the loop on, and choosing a world by hand stops it, so the
row is re-read whenever telemetry disagrees with it.

Up to eight stops, each a built-in or one of yours, advanced by **J2** through a
division, with the blend travelling between them: the sound is always somewhere
between two worlds and always moving. The list comes from the page; how fast,
how far and how smoothly it travels are knobs — **division** counts edges of J2,
**glide** is how much of each interval the blend spends moving (at the top it
never stops, at the bottom it arrives in a few milliseconds and waits, which is
a sequencer with a crossfade), and **free-run** is for a rack with no clock in
it. J2 has said Sync on the panel since the I/O map was agreed and was read by
nothing until now.

While a tour runs the clock owns the blend, so the Morph knob is inert — what
the clock has taken over is exactly what that knob does — and choosing a world
by hand stops the tour.

**Three buffers, and that is the design.** The arithmetic of a step is free: at
full blend the sound *is* the target's spectrum, so making that the live world
and resetting the blend renders the same thing. Loading is not free — expanding
a lattice world is about 9 ms on a laptop and the M7 is an order of magnitude
slower on that kind of loop. With two buffers, whichever one a step overwrites
is either audible now or becomes audible as the blend travels, and a load
landing halfway through the interval moves the rendered cycle by 1.85 where the
frame spans about ±2. With three in a ring the world a step needs next goes into
the one nobody is listening to and has a whole interval to arrive in: measured
at 0.0475, which is the blend's own per-block travel. The module already had
three — the live pair and the morph target — so the ring cost no memory. Two
stops is the one case that needs no loading at all, and uses two.

It takes **two clock edges to start**: the first says when, the second says how
long. Only then can the blend be paced, and a step taken before the blend has
travelled is a switch rather than an arrival.

**A step is a morph inside a family and a switch across families.** Magnitudes
are exact across a step; the phase spectrum and the frame shapers are not,
because the engine takes both from the live world. Measured over all 462 ordered
pairs, 128 step with no seam at all, and they are exactly two groups: the
random-phase worlds that share a seed (Crop, the vertex three, Stack, Field,
Harmonic, FM, Vowel) and the sine-phase worlds with no shaper (Lock, Unison,
Plate, Bar, Drum, Saw, Pulse, Edge). Field II and Torus have seeds of their own,
and Shapes, Shapes R and Grit shape the cycle, so those five seam against
everything — 0.4 to 5.4. A tour across families is a sequencer, which is worth
having on purpose; the table is in `docs/checklist.md` along with what the
honest fix would cost.

## The morph does not click

Every cell shares one phase spectrum, so the rendered frame is a linear
function of the magnitude vector and partials cannot cancel, whatever the
interpolator does. The usual wavetable complaint, a comb-filtered dip or a
tick as you cross between waves, is unreachable by construction rather than
tuned away. `tests/morph_check` holds that down, along with the level across a
cell and the behaviour at an instant position jump — which measures 0.84x the
99.99th-percentile curvature of the same render, where a click is 50x or more.
That is the licence to drive the position with anything at all.

A world is free to put a step in the coefficients even so, and twice in one day
one did. `tests/cont_check` sweeps every axis of every world at two step sizes:
halve the step and a continuous function halves its largest change, a
discontinuity does not, so the ratio is about 4 for something smooth and about
1 for a cliff. No threshold on step size alone can tell them apart, which is
why the first wavefolder passed every other test.

Band-limiting is spectral truncation, verified at −88 dBFS worst non-harmonic
content over a five-octave sweep (`tests/alias_check`) — on one bright lattice
cell, which is a narrower claim than it reads as. Scanning every world at every
corner and midpoint of its space, 81 cells each, the honest worst is **−64.6
dBFS**, on Field at (1, 1, 0.5, 0); the field worlds are the loudest because
their spectra are the least tilted, so the frame reader's images have the most
to work with. `build/host/alias_check --scan` prints the table and takes 95 s.
Both numbers are the *spectral path*, which is what nineteen of the twenty-two
worlds use. The three
grid worlds run their shapers on the rendered cycle instead, where there is no
closed form in the harmonics to truncate, so they alias — two of them as a cost
and one of them as the point. Measured the same way, worst non-harmonic bin over
the same sweep, each shaper at full depth and scanned over 25 cells of its grid:

| shaper at full depth | quietest cell | loudest cell | band limit |
|---|---|---|---|
| none (dry grid) | −98.2 | −90.0 | 64 |
| Shapes, wavefold | −16.0 | **−9.0** | 21 |
| Shapes, phase modulation | −92.8 | −52.9 | 21 |
| Shapes R, ring modulation | −101.6 | −96.1 | 33 |
| Grit, bit reduction | −29.4 | −19.8 | 64 |
| Grit, rate reduction | −28.0 | −17.1 | 64 |
| Grit, both | −27.3 | **−15.3** | 64 |

```sh
build/host/alias_check --world 21 --pos 0.75 1 1 1   # the loudest cell of Grit
```

It prints `FAIL` against the suite's −80 dBFS limit, and that limit is about the
spectral path: `make test` runs the sweep on a lattice world, where anything
above −80 is a bug. On Grit the number is the specification.

The **band limit** column is the point of the comparison. The folder and the
phase modulator buy their way down by throwing two thirds of the harmonics away
before they start — 21 of 64, so what you hear at full fold is a dull waveform
made bright again by a nonlinearity, and it is *still* the dirtiest thing here at
−9 dBFS. Grit does not buy anything: it keeps all 64 and reduces them, because
quantisation error is broadband whatever you feed it and a hold's images sit at
multiples of the hold rate, so pulling the band in would cost brightness and
clean up nothing. Every figure here is ours and every one is published, which is
the point: as far as the survey in `docs/lit/` found, nobody else in the field
publishes any of them.

## Building and flashing

Siblings expected beside this repo, as for Audiothurgist:
[`alchemy-sdk`](https://github.com/hermetic-modular/alchemy-sdk) v0.11 with
its one-line 480 MHz patch, and `DaisySP`.

```sh
make host                 # build/host/{kykdesk,kykspace,kykeigen,kykworlds}
make test                 # unit, continuity, aliasing, morph, golden WAVs, link
KYK_NODE=1 make test      # the same plus the node web selftest
make armcost              # M7 instruction counts for the audio-path inner loops
cd shell/alchemy && make MODE=modal      # Bongs: build-modal/bongs.bin (the default MODE)
cd shell/alchemy && make MODE=wavetable  # Kyklophoria: build-wavetable/kyklophoria-wavetable.bin
make program-dfu          # with the module parked in the bootloader
```

Nothing here needs npm. The tooling is C++ and the Python standard library;
node is optional, and used only by the web selftest, the page harness and the
corpus tool — never npm, and never for the page itself.

Two rules the build will not catch for you. Nothing placed in `.sdram_bss` may
have default member initialisers — `.init_array` would write SDRAM before
`hw.Init()` has configured the FMC, and the module hard-faults on boot; there
is a `static_assert` at each such declaration. And desktop timings understate
the M7 by an order of magnitude on serial dependency chains, so `make armcost`
counts instructions rather than trusting a stopwatch on a laptop.

## The web page

`web/index.html` is one static page over Web Serial: where you are in the
space, over a shaded map of the world you are standing in, with the live frame,
its spectrum and the band limit, the rotation planes, the Kepler orbit and a
CPU readout. It also mirrors the panel — which page you are on, what its six
knobs do, what each is currently worth and where the pot itself is sitting,
which are two different numbers because the panel catches — and carries the
things that are setup
rather than playing: the morph target and whether it is aimed, which motions are
muted, and which world to load from the card. Importing, placing, sending and
saving a world of your own has a view of its own, behind the **build** tab.

It is a readout first. Everything it shows comes *from* the module, including
the state of its own controls, so two pages open at once agree and a preset load
is not something it has to be told about.

Because a world is a formula, the page evaluates the whole projection plane
itself and shades it, rather than drawing dots where the module happened to
sample. The shading is on an **absolute** domain — centroid, flatness or high
end, each logged against its own bound — so two slices of a world, and two
different worlds, can be compared. It does not autoscale to whatever range a
slice happens to occupy; a slice that really is flat says "flat field" instead
of being stretched to fill the ramp. The dot takes its colour from the same
function that painted the ground under it.

The trail is paced by the slowest rotation that is actually moving the picture,
so it holds about one turn of it rather than a fixed ten seconds, and each
sample keeps the timbre it was drawn at. A clamped axis pushed past the edge
of the space gets a mark, because that is the one thing here that is invisible
and audible at once: the position stops moving while the knob keeps going.

```sh
python3 -m http.server 8080 -d web        # then press Serial
# the desktop shell can pretend to have an SD card, which is how the card
# path is tested without hardware:
build/host/kykdesk --serve --gen --seed 1 --card /tmp/mycard
python3 tools/bridge/bridge.py -- --gen --family field --side 8 --seed 1 \
    --script tests/scripts/m2_field.txt --loop     # no module needed
```

Published at https://combust.codeberg.page/Kyklophoria/, which is https and
therefore a secure context, so Serial works there from any machine. The page
and the firmware share a descriptor version (`ext`, currently 5); publish and
flash together, or the page will parse an older shape than the module sends.
The telemetry frame is the exception and grows without a bump — everything
after the Kepler block was added later, each field optional on the way in — so
a newer page against older firmware loses those readouts and nothing else.
Absent is not zero there: a page that read a missing live-world byte as 0xFF
would conclude every older module was playing a user world.

## Not built yet

Writing to the card from the module, so keeping a world does not mean moving a
file by hand. FM and sync
inputs. The filter, drive and FM-index payload lanes — only CV out A is routed.
Wrap and sphere topologies on the panel. Per-axis LFO shapes. Scattered
(non-lattice) spaces.

**What has been played and what has not.** Twenty-one of the twenty-two
built-in worlds are what has had real time on the bench; Grit is measured and
not yet played. Morphing, user worlds, import and the card
are tested by the suite — 420 world-pair switches, 102 link checks, 165
link-level and 140 page-level web checks, and the continuity and aliasing sweeps — but not by ear at length. Treat
them as the new half of a beta.

## Layout

`core/` header-only engine, no hardware and no allocation · `shell/desktop`
renders a param script to WAV and serves HostLink on stdio · `shell/alchemy`
the module · `shell/common` the HostLink extension both compile · `tools/`
space generation, the eigenspace bake, the world grader, the web bridge ·
`tests/` the suite · `docs/` spec, io-map, formats, protocol, milestone notes,
the survey, and `sdk-quirks.md` — the libDaisy and Alchemy SDK traps that cost
us time, including the two that hard-fault on boot.

## Credits

Combust — design and direction. Built with Claude Code.
**Émilie Gillet / Mutable Instruments** — the Braids wave bank that the world
named **Crop** is four principal components of, the first
corpus the eigenspace is baked from. **Luke / Hermetic Modular** — the
Alchemy Lab and its SDK. The shell, the HostLink transport and the web wire
layer come from [Audiothurgist](https://codeberg.org/combust/Audiothurgist).

## License

**AGPL-3.0** (`LICENSE`). Third-party terms in `THIRD_PARTY.md`: the Alchemy
SDK and libDaisy are MIT and are build-time siblings rather than vendored
here, and the Braids wave bank the eigenspace is baked from is MIT too.
