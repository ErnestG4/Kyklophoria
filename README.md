# Kyklophoria

**An N-dimensional morph oscillator for the Hermetic Modular Alchemy Lab.**
The wavetable is not a line or a grid but a 4-dimensional space of spectra
(up to 6). Position CVs pass through an N-D rotation *before* they index it,
so one cable sweeps a diagonal slice no axis reaches, and slow rotation turns
a still hand into an orbit through the space.

**Status: beta.** It runs in the rack and makes sound. Twenty-one
worlds ship, the rotation and its orbit LFOs are real, ratio coupling works,
and presets persist. There is design work left: FM and sync are not read, most
payload lanes are not routed, and the panel layout is still a first draft.

## Controls

**B1** taps through six pages. **B2 + B3** held opens Settings.

| | Play | Rotate | Stereo |
|---|---|---|---|
| P1 | Coarse pitch, ±3 oct | plane (0,1) angle | Spread |
| P2 | Fine, ±1 semitone | plane (0,2) | Stereo plane |
| P3 | Position 0 offset | plane (0,3) | **Morph**: smooth blend to hard steps |
| P4 | Position 1 offset | plane (1,2) | Render divider |
| P5 | Position 2 offset | plane (1,3) | Level |
| P6 | Position 3 offset | plane (2,3) | CV out A depth |

Four dimensions give six rotation planes, and the panel has six pots. 

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

**Jacks.** J3 v/oct · J4 to J7 position 0 to 3 · J8 CV out A · J9/J10 stereo
out. J1 and J2 are reserved for FM and sync and are not read yet.

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
whatever sits in the cosine half — measured over 401 AKWF waveforms, after
searching for the best rotation of the cycle, the mean keeps 94.1% and the worst
54.6%. The page shows the worst fit of a set by name rather than hiding it.
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

Twenty-one ship. A world is either a **formula**, evaluated wherever you happen
to be standing, or a **lattice** of sampled spectra, interpolated between.
Which one a world is is a property of the world, not a storage decision: if a
formula is 1.3 KB and exact everywhere, baking it out to a grid
would cost 1.18 MB for no gain.

| world | what it is | variety | spread |
|---|---|---|---|
| Braids | eigenspace of Émilie Gillet's 256-wave bank | 1.47 | 2.14x |
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

`build/host/kykworlds` prints this table, and with `--braids <resources.cc>` adds
a column for how much of the Braids bank each world can reach.

† The two Shapes worlds are measured on their first two axes only. The other
two drive shapers that act on the rendered cycle rather than the spectrum, so
the tool cannot see them and scores them dead, which is most of that 8.47x.

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
Braids included. A world that already knows what it is has no axis fighting
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

## The morph does not click

Every cell shares one phase spectrum, so the rendered frame is a linear
function of the magnitude vector and partials cannot cancel, whatever the
interpolator does. The usual wavetable complaint, a comb-filtered dip or a
tick as you cross between waves, is unreachable by construction rather than
tuned away. `tests/morph_check` holds that down, along with the level across a
cell and the behaviour at an instant position jump — which measures 1.4x the
99.99th-percentile curvature of the same render, where a click is 50x or more.
That is the licence to drive the position with anything at all.

A world is free to put a step in the coefficients even so, and twice in one day
one did. `tests/cont_check` sweeps every axis of every world at two step sizes:
halve the step and a continuous function halves its largest change, a
discontinuity does not, so the ratio is about 4 for something smooth and about
1 for a cliff. No threshold on step size alone can tell them apart, which is
why the first wavefolder passed every other test.

Band-limiting is spectral truncation, verified at −88 dBFS worst non-harmonic
content over a five-octave sweep (`tests/alias_check`). As far as the survey
in `docs/lit/` found, nobody else in the field publishes such a figure.

## Building and flashing

Siblings expected beside this repo, as for Audiothurgist:
[`alchemy-sdk`](https://github.com/hermetic-modular/alchemy-sdk) v0.11 with
its one-line 480 MHz patch, and `DaisySP`.

```sh
make host                 # build/host/{kykdesk,kykspace,kykeigen,kykworlds}
make test                 # unit, continuity, aliasing, morph, golden WAVs, link
KYK_NODE=1 make test      # the same plus the node web selftest
make armcost              # M7 instruction counts for the audio-path inner loops
cd shell/alchemy && make  # build/kyklophoria.bin
make program-dfu          # with the module parked in the bootloader
```

Nothing here needs npm. The tooling is C++ and the Python standard library;
node is optional and used only by the web selftest.

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

**What has been played and what has not.** The twenty-one built-in worlds are
what has had real time on the bench. Morphing, user worlds, import and the card
are tested by the suite — 420 world-pair switches, 99 link checks, 105 link-level
and 119 page-level web checks, and the continuity and aliasing sweeps — but not by ear at length. Treat
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
**Émilie Gillet / Mutable Instruments** — the Braids wave bank, the first
corpus the eigenspace is baked from. **Luke / Hermetic Modular** — the
Alchemy Lab and its SDK. The shell, the HostLink transport and the web wire
layer come from [Audiothurgist](https://codeberg.org/combust/Audiothurgist).

## License

**AGPL-3.0** (`LICENSE`). Third-party terms in `THIRD_PARTY.md`: the Alchemy
SDK and libDaisy are MIT and are build-time siblings rather than vendored
here, and the Braids wave bank the eigenspace is baked from is MIT too.
