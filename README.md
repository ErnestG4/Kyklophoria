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

**B1** taps through three pages. **B2 + B3** held opens Settings.

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

**Jacks.** J3 v/oct · J4 to J7 position 0 to 3 · J8 CV out A · J9/J10 stereo
out. J1 and J2 are reserved for FM and sync and are not read yet.

**Stereo is not a chorus.** The two channels read the space at rotation angles
either side of centre, so width is an *angular* spread, and an orbit moves
your ears through the space slightly out of step. At zero spread the output is
mono, bit for bit.

## Spaces

A space is a lattice of single-cycle spectra plus a payload vector per node,
in one `*.kyk` file (`docs/space-format.md`). Three ways to fill one:

- **Field** — a correlated random field, which by construction has no
  privileged directions. This is what the module boots.
- **Eigen** — a corpus of real waves, analysed to log magnitudes and reduced
  by PCA to four whitened axes. Braids' 256-wave bank is the first corpus.
  Whitening is the load-bearing step: raw PCA is maximally lopsided, and
  lopsided is the one thing rotation cannot survive.
- **Harmonic** — one legible synthesis parameter per axis. Easiest to reason
  about, worst to rotate through, kept for that contrast.

```sh
build/host/kykspace gen space.kyk --family field --side 8 --seed 7
build/host/kykeigen space.kyk --braids ../../Mutable/Streams/eurorack/braids/resources.cc
build/host/kykspace info space.kyk        # variety, level, isotropy
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

To be confirmed; Audiothurgist is AGPL-3.0. Third-party material and its terms
are listed as it lands.
