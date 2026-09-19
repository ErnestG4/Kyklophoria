# Modal operator space — what was measured

A feasibility bake, 2026-09-18. The question was whether interpolating modal
operators across instrument models produces a space worth navigating, graded
the way Kyklophoria grades its worlds. The answer, by the brief's own rule, is
no: the eigenvector half of the operator adds about a tenth of the spectral
motion that the frequency half adds, and a space built from both is no more
varied and no more even than a space built from the frequencies alone. The
manifold machinery is not decorative — without it the space contains negative
frequencies and total-flip cliffs — but it does not make the premise true.

Everything below is a number that came out of `make all-stages`, and every
choice that could have been made otherwise is a flag with its default stated.
Nothing in the corpus was curated to move a score.

## The three numbers

Spread is the 95th percentile of variety per unit of travel over the 5th, across
200 random directions through the centre of the four-dimensional cube; 1.0 is a
space where every direction is as interesting as every other. Variety is the
mean. Both are ported unchanged from `tools/kykworlds`.

The brief's reference numbers, "FM scores 12.76 Spread, Saw 2.03", are two
different columns of Kyklophoria's table: FM's *variety* is 12.76 and its
*spread* 1.83x; Saw's variety is 1.44 and its spread 2.03x. Both pairs are used
below.

The brief's spectrum — strike at position 0, listen there, uniform damping, the
magnitude on sixth-octave bands — counts pitch motion as variety, and in this
corpus pitch motion is most of what there is: the bars sit an octave below the
bells. So every number is given twice, as the brief defines it and with the
lowest audible mode of every point moved to 440 Hz first, which leaves the
ratio structure and the gain pattern, i.e. what Kyklophoria's variety measures.

| variant | what it tests | spread | variety | spread, pitch-normalised | variety, pitch-normalised |
|---|---|---|---|---|---|
| (a) full — Λ + G, tangent-space PCA | the proposal | **6.60x** | 19.0 | **5.07x** | 12.5 |
| (b) Λ only, diagonal operators | does G add anything | **5.97x** | 19.5 | **9.08x** | 17.8 |
| (c) linear PCA on raw (Λ, G) | does the geometry earn its keep | **20.27x** | 38.0 | **25.06x** | 29.1 |
| (d) G only, Λ frozen | the other half of (b), added here | 18.73x | **1.6** | 14.17x | **1.7** |
| Kyklophoria FM | | 1.83x | 12.76 | | |
| Kyklophoria Saw | | 2.03x | 1.44 | | |
| Kyklophoria Shapes | its most lopsided | 8.47x | 0.99 | | |

Reference model bar05, blocks weighted to equal total variance, four
components, extent 2.2, 48 modes, 12 positions. `out/grade-*.txt` has the rest
of each report.

**(a) against (b).** Close, in both variety and spread, and which of the two has
the better spread depends on the reference model (see the ablations: with bell05
as reference, (b) is the more even space). Variant (d) says why: with the
frequencies frozen and only the shapes moving, variety is 1.6 against 19 — the
eigenvector half is a tenth of the motion. By the brief's rule, "if they're
close, the whole premise collapses to a crossfade and we stop." It collapses.

**(a) against (c).** Not close. Linear PCA on raw (Λ, G) is off the manifold in
exactly the way predicted: 0.14 modes per evaluation have a non-positive
frequency, and on two of the four axes the rendered spectrum flips completely
between adjacent steps (continuity ratio 1.00 at a step of √2, the largest
possible distance between unit vectors) as a mode's frequency crosses zero and
its 1/ω amplitude takes over the spectrum on the way. Its spread of 20x and
variety of 38 are those artefacts, not a richer space. The log/exp sandwich is
load-bearing for validity: the tangent-space variants have positive frequencies
everywhere, orthonormal frames to 10⁻¹² and continuity ratios of 3.9 to 4.0 on
every axis. It is not load-bearing for variety.

**Against Kyklophoria.** At 5 to 7x the full space is more lopsided than every
legible world in that table except Shapes, whose 8.47x is an artefact of the
grader not seeing two of its axes. FM at 1.83x is the target that space was
built to; nothing here comes within a factor of two of it.

## The corpus (Stage 1)

Three families, twelve models each, 48 modes, 12 canonical strike positions.

| family | swept | real modes below 20 kHz | lowest mode |
|---|---|---|---|
| tapered bar, 120 × 8 × 4 mm steel | taper 0.2 to 1.0 | 9 to 11 | 989 to 1512 Hz |
| plate, 150 mm × 150/aspect × 4 mm | aspect 1.0 to 3.0 | 16 to 40 | 898 to 1326 Hz |
| bell, 150 mm tall, r₀ 30 mm, 4 mm wall | flare 0 to 1.2 | 24 to 46 | 4177 to 1920 Hz |

Two departures from the brief's letter. OpenSCAD is not on the machine and
cannot be installed without a password, so the families are a Python generator
(`tools/meshgen.py`) — the same parametric geometry, written in a language that
was present. And it emits structured tetrahedral volumes rather than surfaces,
because Vega's tet mesher, which is what mesh2faust uses, refines for element
quality and not for size: a 4 mm bar came out one layer of linear tets thick,
and one layer is twice too stiff in bending — 2942 Hz for the uniform bar's
first mode against the Euler–Bernoulli 1441. With ten layers the same mode is
1512 Hz (+5%), with fourteen 1473 (+2%); ten is where a model costs ten seconds
instead of a minute, and every family is refined the same way at every
parameter value so that the sweep is a sweep of one thing.

`tools/modalfem` is mesh2faust's FEM path — Vega's StVK assembly, Spectra's
shift-invert generalised eigensolver — with one change at the end: mesh2faust
reports a mode's gain at a vertex as the *norm* of the eigenvector there, which
throws away the sign, and the alignment stage lives on signs. Here a gain is the
eigenvector at the vertex nearest a canonical position, projected on the strike
direction. Damping is Rayleigh with mesh2faust's steel coefficients, stored
because the format has a slot for it; the grade does not use it.

Padding: a model with fewer than 48 real modes is padded with modes above
hearing (22 kHz up), damped to nothing, with a gain row that is a small fixed
pseudo-random pattern, different for every padded slot and identical across
models. The first corpus used one pattern for every padded slot and the bake's
round-trip check caught it: a bar with ten real modes and thirty-eight copies of
one row has a G of rank eleven, which is not a point on the Grassmann manifold
at all. Bars are three quarters padding at N = 48; that is the brief's rule
(dimension constant, never truncate to the smallest model) and it is what a bar
is.

## Alignment (Stage 2)

Mean matched MAC over the modes real in both models, Hungarian on 1 − MAC:

|  | bar | plate | bell |
|---|---|---|---|
| bar | 0.800 | 0.447 | 0.457 |
| plate | 0.447 | 0.744 | 0.394 |
| bell | 0.457 | 0.394 | 0.804 |

Within-family 0.783, cross-family 0.433. Along each sweep, neighbour to
neighbour: bar 0.967 (worst link 0.89), plate 0.837 (worst 0.63, at the square
end), bell 0.918 (worst 0.79).

Cross-family MAC is not uniformly low, but it is a statement about the corpus as
presented rather than about the physics: a bar's twelve positions are a line
along its top, a plate's are a 4 × 3 grid, a bell's are four heights by three
azimuths, so a cross-family MAC compares gain patterns on unrelated layouts.
The number says a plate mode and a bar mode can be paired at 0.45 by pattern
alone; it does not say they are the same kind of mode.

The correspondence to the reference is chained, not direct — each model to its
neighbour towards the middle of its own sweep, each family's middle to the
reference — because a plate aligned straight to the bar reference has its modes
assigned by a coin toss that lands differently at every parameter value.
Measured: the direct alignment's veering map showed 93 slots of the plate sweep
jumping by more than 300 cents between neighbours, the worst by seven octaves,
and none of it was veering. Chained: 42, the worst 4300 cents, concentrated
where the physics puts them. `--direct` reproduces the other.

## The bake (Stage 3)

Log map Λ → log Λ, so frequencies glide geometrically and stay positive;
G → its column frame's Grassmann log map at the reference (principal-angle
form, no inverse of cos Θ) plus its coefficients in the exp-mapped frame, so
G = Q̃ R̃ exactly at every corpus point. PCA on the M × M Gram matrix, four
components, whitened. Both maps round-trip to 2 × 10⁻⁹.

Held out two models per family (sweep positions 3 and 8), fitted on the thirty,
reconstructed each from its own coordinates:

| variant | variance in 4 components | held-out worst, cents RMS | held-out worst, gain error | non-positive frequencies |
|---|---|---|---|---|
| full | 73.6% | 485 | 0.69 | 0 |
| lambda | 95.9% | 261 | — (G is frozen) | 0 |
| linear | 87.3% | 518 | 0.57 | 0 at corpus points; 0.14 per evaluation off them |
| gonly | 66.3% | — (Λ is frozen) | 0.75 | 0 |

The first component separates the families; the bar sweep runs along the
third. Four dimensions hold three families of modal operators at 485 cents RMS
on the held-out models, which is to say they do not hold them: the reconstructed
plate is a plate-like object with its modes in roughly the right octave. That is
what four components of a 768-dimensional tangent space can do with thirty-six
points in three clusters, and it is the honest size of the compression the
brief asked for.

## Continuity and the veering map (Stage 4)

Every axis of the tangent-space variants measures a continuity ratio between
3.9 and 4.0 on the rendered spectrum and 4.00 on the log-frequency vector, at
step/500 against step/2000. That is what a smooth interpolant evaluated through smooth maps has
to measure, and it is reported because it is true rather than because it is
informative: the brief's second sweep is a veering detector only where the
interpolant can have a kink, and this one cannot. (The first version of the
grader cut modes off at 20 kHz and measured that cut as a cliff on every axis —
the grid's cliff, not the space's; the kernel's tail handles it now.)

The veering that exists is in the corpus, between samples of a sweep, and the
map measures it there: the second difference of each aligned slot's log
frequency along each family's sweep, in cents.

- **bar**: 2 slots over 300 cents, both at taper 0.93 (slots 2 and 4, about
  1130 cents) — near the uniform bar, where the two bending planes' modes cross.
- **plate**: 42 slots over 300 cents, worst 4300. Twenty-nine of them at
  aspect 1.18 to 1.55: the square plate's degenerate mode pairs splitting as it
  becomes a rectangle, which is textbook veering, and the rest at aspect 1.7 to
  2.5, mostly in slots 9, 12, 18 and 20.
- **bell**: 10 slots over 300 cents, worst 782 at flare 0.76.

`out/wav/plate-veering.wav` walks aspect 1.0 to 1.55 in 32 strikes.

## Ablations

`out/ablations.txt`, spread / variety, pitch-normalised, four components:

| reference | full | lambda | gonly |
|---|---|---|---|
| bar05 | 5.07x / 12.5 | 9.08x / 17.8 | 14.17x / 1.7 |
| plate05 | 6.94x / 12.1 | 3.75x / 18.3 | 2.29x / 2.1 |
| bell05 | 6.32x / 11.3 | 3.32x / 19.1 | 2.47x / 2.3 |

Three things are robust across the reference, the block weighting and the
strike position (0, 3, 6 and 11 were graded): the shapes alone carry a variety
of 1.3 to 2.3 against 17 to 19 for the frequencies alone; the full space's
variety is the frequency space's; and the full space's spread sits between 5x
and 7x wherever the chart is centred. What is *not* robust is the spread of the
frequency-only space, 3.3x to 9.1x depending on the reference — a reminder that
spread measures the chart as much as the space, and that a single spread number
should not be over-read in either direction.

The `gonly` spread with a bar reference (14 to 19x) against a plate or bell
reference (2.3 to 2.8x) is the bar's ten real modes: its frame is three quarters
padding, and a tangent space at a frame that is mostly padding is mostly
padding. The brief's "start with a mid-sweep bar" is the worst reference in the
corpus for the shape half, and the numbers are given at all three so that this
is visible rather than chosen.

## What it rules out, and what it does not

It rules out the proposal as posed: a four-dimensional space of modal operators
across these three families, graded at one strike position, gets its variety
from the frequency list and almost none from the mode shapes. Interpolating a
frequency list needs no Grassmann manifold, and a frequency list is a crossfade.

It does not rule out the shapes mattering *across strike positions*. The grade
hears one position, as the brief specified, and the gain pattern's whole
content is how the sound changes with where you strike. Position 3, 6 and 11
were graded as well and the shape-only variety stayed at 1.3 to 2.2, so a fixed
position anywhere gives the same answer — but a space whose fifth axis was the
strike position is a different proposal, and this bake did not measure it.

It does not rule out a within-family space. Every within-family number is
better than every cross-family one — MAC 0.78 against 0.43, neighbour links at
0.84 to 0.97 — and the plate sweep's veering region is the one genuinely
interesting thing in the corpus. A four-dimensional space of one family, with
the frequencies pitch-normalised, was not measured.

It does not say anything about e-piano calibration, real instruments or
real-time cost, none of which were goals.

## Reproducing it

    make -j8 all-stages                 # ~10 minutes: 36 FEMs in parallel, then seconds
    make -B grade GRADEFLAGS="--strike 6 --listen 6"
    make -B align REF=bell05 && make bake grade    # a different reference

Dependencies beside the repo, unmodified: `../faust` (mesh2faust's Vega and
Spectra), `../eigen`. Host C++ and Python stdlib.
