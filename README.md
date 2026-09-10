# Kyklophoria

**An N-dimensional morph oscillator for the Hermetic Modular Alchemy Lab.**
The wavetable is not a line or a grid but a 4-dimensional space of spectra
(up to 6). Position CVs pass through an N-D rotation *before* they index it,
so one cable sweeps a diagonal slice no axis reaches, and slow rotation turns
a still hand into an orbit through the space.

**Status: alpha, and it plays.** It runs in the rack and makes sound. It also
has a lot of design work left: the modes are sketched rather than built, most
of the payload routing is not wired, and the panel layout is a first guess.
`docs/spec.md` is the source of truth, `docs/worlds.md` is where the argument
about what comes next lives, and `docs/m*-notes.md` carry the measurements,
including the ones that came out the wrong way round.

## Why the extra dimensions

A 2-D wavetable grid has exactly one rotation plane, so "rotate the map" is a
single knob and mostly a novelty. Four dimensions have six planes, each with
its own angle, and two of them turning at unrelated rates trace a path that
never repeats. That is the point of the dimensions: not more waves, but more
*ways through* the waves.

It only pays off if the space has no favourite directions, and a grid of
independent synthesis parameters has them badly. Measured, its best direction
gave about ten times the timbral movement of its worst, so most rotations
landed a CV on a nearly dead axis. Fixing that drove the content work below.
`kykspace info` grades any space on that number.

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

Four dimensions give six rotation planes, and the panel has six pots. That is
a coincidence, and a lucky one.

Three more pages move the position without you.

| | Orbit | Kepler | Couple |
|---|---|---|---|
| P1 | plane (0,1) rate | Gravity, off at the bottom | **Coupling** |
| P2 | plane (0,2) | Eccentricity | Reach, 1 to 5 |
| P3 | plane (0,3) | Orbit plane | Rate ×, detent at 1 |
| P4 | plane (1,2) | Softening | — |
| P5 | plane (1,3) | Damping | — |
| P6 | plane (2,3) | Radius | — |

**Orbit** turns the rotation planes at rates you set, which never close into a
repeating figure. **Kepler** drops the position into a softened central
potential and reads the space wherever it falls, so the second law does the
work: it rushes through periapsis and lingers at apoapsis, and the timbre
dwells unevenly rather than gliding. On a world whose axes wrap, gravity wraps
with them, and the body leaves one side of the space to arrive from the other.

**Couple** is the one that changes what the instrument is. Turn it up and each
pair of orbits pulls the other toward the nearest simple ratio, the figure
closes, and the waveform snaps into shape. Over a sweep of rate settings, 2.7%
land on a simple ratio at zero coupling — chance — and 88% at the top of the
knob. Reach says how exotic a ratio it will settle for.

**Jacks.** J3 v/oct · J4 to J7 position 0 to 3 · J8 CV out A · J9/J10 stereo
out. J1 and J2 are reserved for FM and sync and are not read yet.

**Stereo is not a chorus.** The two channels read the space at rotation angles
either side of centre, so width is an *angular* spread, and an orbit moves
your ears through the space slightly out of step. At zero spread the output is
mono, bit for bit.

## Worlds

Eleven ship. A world is either a **formula**, evaluated wherever you happen to
be standing, or a **lattice** of sampled spectra, interpolated between. Which
one a world is is a property of the world, not a storage decision: a formula
that exists is 1.3 KB and exact everywhere, and baking it out to a grid would
cost 1.18 MB to approximate what it just threw away.

| world | what it is | variety | spread | covers Braids |
|---|---|---|---|---|
| Braids | eigenspace of Emilie Gillet's 256-wave bank | 1.47 | 2.14x | 32% |
| 24-cell | a waveform on each vertex of the 4-D solid | 0.66 | 3.64x | 16% |
| 16-cell | eight vertices, on the axes: mostly mire | 0.38 | 3.14x | 14% |
| Tesseract | sixteen, on the cube corners | 0.47 | 4.22x | 16% |
| Stack | one waveform idea per axis | 3.43 | 3.69x | 27% |
| Field | correlated noise, even in every direction | 2.81 | 1.31x | 9% |
| Field II | the same, rougher | 5.87 | 1.39x | 10% |
| Torus | a field with no edges; gravity wraps with it | 4.33 | 1.26x | 11% |
| Harmonic | one parameter per axis, legible but lopsided | 0.20 | 6.13x | 15% |
| **FM** | index, ratio, carrier, and a second carrier that interferes | **12.76** | **1.83x** | 11% |
| **Vowel** | three chained resonances over a falling source | 1.50 | 2.87x | **29%** |
| **Shapes** | a 4×4 grid of *real* waveforms, plus fold and phase modulation | — | — | — |
| **Shapes R** | the same grid, with fold and ring modulation | — | — | — |
| **Lock** | real waveforms on the 24-cell, a family per rotation plane | 0.82 | 4.72x | 18% |
| **Unison** | one wave stacked on itself; interval, detune | 3.95 | 4.39x | 2% |
| **Plate** | a struck plate; strike, shape, time and damping | 1.31 | 4.78x | 20% |
| **Bar** | a struck bar, tuned onto the harmonic grid | 1.15 | 3.31x | 19% |
| **Drum** | a struck membrane, tuned; dense and low-ordered | 0.88 | 3.52x | 24% |
| **Saw** | nothing but a saw: tilt, parity, comb, fold point | 1.44 | **2.03x** | — |
| **Pulse** | nothing but a pulse: duty, tilt, comb, fold point | 2.79 | 3.36x | — |
| **Edge** | saw against pulse, only those two, bent four ways | 1.86 | 2.57x | — |

**Spread** is the number that decides whether rotating the control frame was
worth building: the most varied direction through the space over the least. At
1.0 every direction is as interesting as every other. Most legible worlds
measure badly at it, and the reason is structural rather than a failure of
taste — they weight the harmonic series multiplicatively, multiplicative
weights add in log magnitude, and a sum of per-axis terms is separable. A
separable space already has natural axes, so a rotation finds nothing new.

FM is the exception. The k'th sideband is a Bessel function of the modulation
index, which does not factor, and the sidebands *move* with the ratio, so one
axis relocates energy another axis put down. The vowel world escapes the same
way, by chaining its three resonances so the first axis moves all three.

The two **Shapes** worlds are not playing that game and have no spread number,
because they are after something else. Every world above renders against a
random phase spectrum, which keeps the morph click-free but throws the
*waveform* away: a saw's spectrum at random phase correlates 0.79 with a saw,
so the instrument could never lock onto one. Rendering at sine phase with
signed coefficients gives the real thing — sine, triangle, saw and square all
at 1.0000 against the textbook series, at a slightly better crest factor than
the random phase cost. Those four sit on the top row of a 4×4 grid you scan
with two axes, the Erica GraphicVCO's wavetable matrix, and the Morph knob
decides how hard you land on a node. The other two axes fold, phase-modulate
or ring-modulate the cycle. It is the world for knowing what you are hearing,
where the rest are for not knowing.

```sh
build/host/kykworlds --braids ../../Mutable/Streams/eurorack/braids/resources.cc
tools/renderpack.sh                       # 16 wavs to listen to before flashing
build/host/kykspace info space.kyk        # grade a baked lattice file
build/host/kykeigen space.kyk --braids ../../Mutable/Streams/eurorack/braids/resources.cc
```

## The morph does not click

Every cell shares one phase spectrum, so the rendered frame is a linear
function of the magnitude vector and partials cannot cancel, whatever the
interpolator does. The usual wavetable complaint, a comb-filtered dip or a
tick as you cross between waves, is unreachable by construction rather than
tuned away. `tests/morph_check` holds that down, along with the level across a
cell and the behaviour at an instant position jump.

Band-limiting is spectral truncation, verified at −88 dBFS worst non-harmonic
content over a five-octave sweep (`tests/alias_check`). As far as the survey
in `docs/lit/` found, nobody else in the field publishes such a figure.

## Building and flashing

Siblings expected beside this repo, as for Audiothurgist:
[`alchemy-sdk`](https://github.com/hermetic-modular/alchemy-sdk) v0.11 with
its one-line 480 MHz patch, and `DaisySP`.

```sh
make host                 # build/host/{kykdesk,kykspace,kykeigen}
make test                 # unit, aliasing, morph, golden WAVs, link check
cd shell/alchemy && make  # build/kyklophoria.bin
make program-dfu          # with the module parked in the bootloader
```

Nothing here needs node or npm. The tooling is C++ and the Python standard
library; the node files are optional twins.

## The web page

`web/index.html` is one static page over Web Serial: your position in the
space with a fading trail, the contributing cells, the live frame and its
spectrum with the band limit marked, and a CPU readout. It is a readout, not
a remote control, and almost everything on it comes *from* the module.

```sh
python3 -m http.server 8080 -d web        # then press Serial
python3 tools/bridge/bridge.py -- --gen --family field --side 8 --seed 1 \
    --script tests/scripts/m2_field.txt --loop     # no module needed
```

Published at https://combust.codeberg.page/Kyklophoria/, which is https and
therefore a secure context, so Serial works there from any machine.

## Not built yet

FM and sync inputs; the filter, drive and FM-index payload lanes; orbit LFOs
and ratio lock; wrap and sphere topologies on the panel; loading spaces from
the card; presets; scattered (non-lattice) spaces. The rotation and the stereo
pair are real. Much of the rest of `docs/spec.md` is still a plan.

## Layout

`core/` header-only engine, no hardware and no allocation · `shell/desktop`
renders a param script to WAV and serves HostLink on stdio · `shell/alchemy`
the module · `shell/common` the HostLink extension both compile · `tools/`
space generation, the eigenspace bake, the web bridge · `tests/` the suite ·
`docs/` spec, io-map, formats, protocol, milestone notes and the survey.

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
