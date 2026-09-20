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

## After the listening set: one family at a time, and glides

The strikes sounded like something, where the grade said crossfade. Two things
followed from that, both in the repo.

**One family alone.** `bake --family` fits four components to twelve models of
one family, with the family's middle model as the reference:

| family | variance in 4 | held-out worst | spread, pitch-norm. | variety (full / Λ only / G only) |
|---|---|---|---|---|
| bar | 95.2% | 102 cents | 6.6x | 0.81 / 1.01 / 0.39 |
| plate | 87.7% | 262 cents | 5.4x | 4.79 / 4.52 / 1.22 |
| bell | 94.3% | 82 cents | 21.1x | 4.06 / 4.15 / 0.51 |

Reconstruction is four to six times better than across families and the plate
and bell varieties land where Kyklophoria's mid-table worlds sit. The spread is
worse, and for a structural reason: each family was swept on *one* geometric
parameter, so its twelve models are a curve, and four components of a curve
are one real axis and three of its curvature. That is the corpus's limit, not
the space's — a four-dimensional family needs four independent parameters
(for the bar: taper, length, width, and a thickness profile), and this bake
swept one. The shape half is still a quarter of the frequency half within a
family, so the go/no-go does not change; what changes is that a family is
reconstructible and a corpus of families is not.

**The tine, added after the bake.** A Rhodes tine — a 2 mm steel rod clamped
at one end, the tuning spring as a thicker section near the tip, length swept
5 to 15 cm — is the first family where the shapes carry more than the
frequencies: G only 2.0 against Λ only 1.05, full 2.97, spread **2.89x**. A
cantilever's frequency ratios are scale-invariant (1 : 6.27 : 17.5 whatever the
length), so once pitch is normalised the frequency list barely moves along the
sweep, and what moves is where the spring sits relative to the length and what
that does to every mode's gain at the strike — which is exactly the half the
bake said was worth nothing. It was worth nothing in a corpus of bars, plates
and bells because those sweeps move frequencies; it is most of a tine. 2.89x is
within sight of Saw's 2.03x, the best any designed world in Kyklophoria's table
manages, and it is the first world of the instrument this is turning into. The
clamp is `--clamp` in modalfem, checked against the clamped-free rod at +8%
everywhere in the sweep; the four-family corpus reproduces the brief's three
headline rows to the last digit under `--family bar,plate,bell`.

**Glides.** `render --mode glide` is one oscillator bank that keeps ringing
while the point moves: each mode's phase accumulates at whatever its frequency
is now, its envelope decays at whatever its damping is now, its weight is
whatever the gain pattern says now, and a strike adds to the envelope rather
than resetting it. That is the sound Kyklophoria makes of a space — motion
while sounding — and it is what the first listening set did not have. `--walk`
wanders the cube on three slow sines an axis. Seven more files in `out/wav/`:
`glide-bar-to-bell`, `glide-plate-veering`, two walks of the full space (one
struck at position 5), and a walk of each family's own space.

Levelling: every strike is injected at equal ring energy, decay included,
because mass-normalised gains make a light object tens of times louder than a
heavy one and Rayleigh damping makes a 4 kHz bell die five times faster than a
1 kHz bar, and the first renders were one loud bell and fifteen seconds of
whisper. The file is then levelled to -20 dBFS RMS with a soft knee. None of
this touches the grade.

Nothing physical happens to an object whose shape changes while it rings. The
glides are the instrument the space would be, not a claim about metal.

## Recorded worlds, and the night the fitter was made honest

Five worlds are fitted from recordings rather than meshed: a Wurlitzer
(11 single notes, C2–C7), a tine electric piano (169 notes at two velocities,
`samples/EP`), and the Philharmonia's guitar (71), banjo (74) and mandolin
(39). Each note is one record — frequency, decay and amplitude per partial, the
twelve positions copies of one — and the packer places its partials by
harmonic number so that slot k is harmonic k across the keyboard, padding a
missing harmonic in place, silent and short. The world is then the diagonal
space over those records with decay carried (`make fitted`), rendered as a
keyboard of strikes and a walk in `out/wav/`.

The first fits whistled. Low and high Wurlitzer notes and most of the guitar
came back with partials well above anything the instrument makes — a guitar
with feedback. The night of 19 September was spent on that, in three passes,
each of which found a defect the previous one had hidden. The metric
throughout is **excess**: the resynthesis's mean log magnitude above the
recording's own floor, in the time–frequency cells where the recording is at
its floor, in dB. Zero is a model that makes no sound the instrument did not.

**Pass 2 — verification and an honest loss** (`docs/lit-fitting.md` has the
sources). A candidate partial has to stand 8 dB over the median of the two
octaves around it, hold its phase advance to a third of a radian, and decay;
the log term of the loss is weighed a tenth of the linear one, both stand on
the recording's own floor, and a third term charges for energy above that
floor where the recording has none; after the fit a mode the recording does
not show at its frequency is dropped, and the survivors are fitted again.
Checked on the offending notes, then run over everything at 2000 steps.

**Three defects found by reading the records, not the loss.**

- *The guitar A2 had no fundamental.* The decay gate wanted a line, and the
  fundamental of a plucked string beats — two polarisations — so its log
  track had r² 0.41 while falling 14 dB. The gate now also accepts a clear
  drop, first quarter over last, and hum still fails it.
- *Three guitar notes had two, three and five modes.* The recordings open
  with a second and a half of hands on strings at 22 dB below the note; a
  threshold from the front fired on that, the window began in noise, every
  initial amplitude was 1e-4 and the fit starved. The onset is now found from
  the strike backwards: the envelope's peak, then the last 5 ms 20 dB below it.
- *Two notes converged to nothing (spectral convergence 0.94 and 1.00) with
  low excess.* The Philharmonia guitar carries a median quarter of its energy
  below 40 Hz — one note, 99% — and spectral convergence is a ratio over the
  whole spectrum. An eighth-order high-pass at 40 Hz before anything else
  halved the guitar's loss on identical files (1.89 → 0.91) and cut the
  banjo's 1.57 → 1.26; the Wurlitzer and EP have no energy there and did not
  move.

**Where it ended.** Mean excess over each set, first fit → final (after the
attack ramp and the clamp below), and the number of notes over 1 dB:

| set | notes | excess, dB | notes over 1 dB | modes per note |
|---|---|---|---|---|
| Wurlitzer | 11 | 1.61 → 0.06 | 9 → 0 | 29.7 → 27.1 |
| guitar | 71 | 2.97 → 0.06 | 70 → 0 | 27.8 → 19.7 |
| banjo | 74 | 2.06 → 0.06 | 68 → 0 | 30.7 → 34.9 |
| EP | 169 | 1.38 → 0.03 | 91 → 0 | 16.8 → 16.1 |
| mandolin | 39 | — → 0.12 | — → 0 | — → 36.3 |

The worst note in each set was the loudest complaint: Wurlitzer C7 3.08 →
0.14, guitar D5 forte 8.83 → 0.02, banjo E5 3.54 → 0.02, EP F6 med 3.69 →
0.03. The guitar lost a quarter of its modes and they were the whistle; the
banjo gained modes because the onset fix gave its fits a real first frame.
A/Bs — recording, 0.4 s, resynthesis — for the notes named here are in
`out/fit/*-overnight-AB.wav`, and the set-level A/Bs under `out/fit/<set>/`
were regenerated from the final records.

What the numbers do not say: a banjo D3 has no fundamental in the record
because the head has none at 147 Hz — the recording has it at −37 dB — and
that is right; the guitar D♯5 *piano* take is 99% rumble with the note buried
and fits five modes at a loss of 2.7 whatever the fitter does — that is the
take; and the EP's high notes fit three to nine modes because a tine at 1.7 kHz
has three to nine partials under 20 kHz — pass 1 had five of them, four of
which were one beating partial split by the phase vocoder.

**What the metric was still measuring, and more steps.** With the partials
right, the Wurlitzer's remaining excess (0.45 dB mean, C7 0.84) was looked at
cell by cell: 89% of it above 12 kHz, 94% of it in the first 200 ms. Not a
partial — the click of a sine that starts as a step. The recording's hammer
takes 2 ms to reach half its peak; the model now takes 3, a raised cosine, in
the fit, the resynthesis and the renderer (`render --attack`). Wurlitzer
excess 0.45 → 0.06 dB at the same loss, and the fast, broad pseudo-modes
(ζ ≈ 0.4) that the fit had been using to make the click are gone from the
top notes' records (C7 27 → 19 modes). Every set was refitted with it.
Separately, 6000 steps against 2000 on the Wurlitzer and twelve guitar notes
moved the loss +6% and −6%: noise, and one note (B6) doubled its loss on the
longer run, so the schedule is 2000 and the polish 1000, and steps are not
the lever.

**And what the ramp let in.** With the model's onset soft, the EP's hardest
strikes collapsed: the optimiser built the hammer's click out of one
overdamped sine — 14.5 kHz, ζ 1.9, amplitude four million times the loudest
partial — and the −60 dB threshold, judged at t = 0, threw every real partial
away under it (e5 13 modes → 1, A♯2 33 → 3). Nothing that dies inside 5 ms is
a mode: the decay is clamped there in the fit, and audibility is judged
10 ms in. Every set but the guitar (whose pass with the ramp had two
harmless sub-5 ms thumps) was refitted once more with the clamp; nothing
collapsed, and the mode counts came back to where the third pass had them.
Four passes in a night, then, each defect found by reading the records the
previous pass produced, none by the loss: the loss was fine every time.

The fitted worlds' spreads (pitch-normalised, ring-weighted, four components)
are what a keyboard is: one long axis. Wurlitzer 1.95× (was 55× with the
spurious partials in), banjo 6.2×, mandolin 4.4×, guitar 31×, EP 292× — the
EP's third and fourth components carry nothing (axis variety 0.03) because
two velocities of one tine are two curves, and a spread over a curve is not a
number that means anything. What the EP world needs is a second parameter,
which is the tonebar, which is the roadmap.

## The pickup is the sound: metal into a field, fitted across velocities

Two things were heard in the overnight's worlds: the mandolin and the like
sounded damped, and the electric pianos had one timbre at every velocity —
what a Rhodes does when you dig in was missing, and a linear modal world can
never have it. Both were measured before they were fixed.

**Damped.** A mandolin course is two strings a few Hz apart, and on the
initialiser's 5 Hz grid they were one peak: the C4's second harmonic is
520.2 and 523.5 Hz, the D♯5's third is three peaks. One sine can only make a
beating envelope by dying fast, and the fits did — D♯5's second-harmonic
cluster at T60 0.10 / 0.49 / 0.07 s against 1.8 s measured off the track. A
course's fundamental also *swells* for a quarter second after the pluck (the
two strings start near antiphase at the pickup), and the STFT loss finds a
loud, short compromise through a swell every time. The fitter now splits
pairs on a 65536-point spectrum, reads unresolvable pairs off the beat
period of the bin's envelope, detects a double decay by the knee in the
falling track, and — the thing that moved the number — holds log decay to
the track's own measured slope within a ×1.5 band. `decay_ratio`, fitted T60
over measured for the ten loudest modes, sits beside `excess_db` in every
manifest. Mandolin, 39 notes: ×0.56 → ×0.72, notes more than twice too dead
14 → 3, loss unchanged. What remains is the body's dense low-Q resonances and
the sympathetic courses — on a D♯5 the records now show doublets at G3, D4
and E5, the other strings ringing — which is a residual layer, not more
partials.

**The pickup.** Epi's sources (Muenster & Pfeifle, ISMA 2014, a tine tracked
at 38 kfps) settle the architecture: after its first ten milliseconds the
tine moves as a *pure sine*. Every harmonic at the jack is manufactured by
the pickup, because the tine samples a strongly non-uniform field and the
coil reads the rate of flux change. So one velocity of recording cannot
separate the metal from the transducer — the harmonics look like modes and
were fitted as modes — but several velocities of the same note can: the
metal's partials are shared, only the strike amplitude differs, and what
grows with it is the field's. `modalfit.fit_shaped` fits exactly that:

    u_k(t) = g_k · Σ a_i e^{-r_i t} sin(2π f_i t + φ_i)     the metal, shared
    Φ(u)   = 1 / (1 + ((u − h) / w)²)                        the field
    y_k(t) = K · coil( d/dt Φ(u_k(t)) )                      Faraday, then the coil's LR resonance

Five numbers — h/w the voicing, K, the coil's f and Q, and one swing g_k per
take — with the same multi-scale STFT loss summed over the takes. Two lessons
from making it converge: started deep in the field the model is noise and
the loss's answer to noise is silence (the field opens wide against the
softest swing); and without the coil's low-pass the derivative tilts
everything +6 dB/octave and nothing matches.

*On the bench.* Epi (`tools/epigen`, ../epi) renders any note at any
velocity with the pickup physics in the loop, so the truth is known. E2 at
six velocities, initialised from the softest take with sixteen partials: the
fit came back with the metal as **one sine** — 82 Hz, T60 28 s, the next
component at 4%, the rest at zero — and the bark reproduced across the range:

| take | target h2 / h3 / h4, dB re h1 | model |
|---|---|---|
| v0.15 | −9 / −37 / −58 | −12 / −27 / −51 |
| v0.50 | +7 / −6 / −13 | +7 / −3 / −13 |
| v1.00 | +22 / +14 / +21 | +25 / +14 / +23 |

*On the recording.* The EP set has two velocities, MED and MAX, and the
library levelled each sample (MAX's fundamental is 4 dB *quieter* than
MED's), so the takes carry no level and the fit has a linear escape route:
harmonics in the metal, field flat — which is what it took on G3, the two
swings coming out equal. Two priors close it, both physics. A tine is a
clamped bar whose own partials sit at 1 : 6.27 : 17.5, so anything at an
integer multiple of the fundamental is the pickup's (`bar_metal`); and a
levelled take gets its own output gain after the coil, so that the swing
into the field is decided by the harmonics alone (`normalised`). With those:

| note | MAX target h2 / h3 / h4 | model | swing MED → MAX |
|---|---|---|---|
| C3 | +13 / +2 / +1 | +12 / +2 / 0 | 1.43 → 3.54 |
| G3 | +5 / −2 / −14 | +3 / −4 / −11 | 1.35 → 3.37 |
| C4 | −3 / −11 / −39 | −3 / −12 / −32 | 0.91 → 3.36 |

The metal on every note: one sine and a thump. The swing ratio between the
layers 2.5× on every note, as two fixed velocity layers should give.

*Over the whole sets* (`fitvel`, the bark judged over the harmonics the
target has above −40 dB re h1): Epi's tine, 37 notes at six velocities, one
metal mode the median, error 2.4 dB mean and 5.0 at the 90th percentile;
the hardest take's h2 runs from +2 to +32 dB across the keyboard and the
model has it within 2.1 dB. The recorded EP, 84 notes at two levelled
velocities, six metal modes the median (the thump and the tone bar), 7.4 dB
mean and 22 at the 90th — the top octave, where a levelled take at MED and
one at MAX barely differ and there is little to fit. Epi's reed at 7.0 dB
mean is the form being wrong rather than the fit: an electrostatic pickup
is a gap, 1/(g − u)², not a bell, and it wants its own stage.

*The voicing screw.* The one test that says whether a fitted number is a
physical one: Epi's tine at six pickup heights, one velocity, nineteen
notes each. Fitted h/w against Epi's `pickupPos`: 0.27 at −0.85, 0.21 at
−0.65, 0.18 at −0.45, 0.08 at −0.25, **0.01 at −0.05** — the centreline,
where Epi's own note says the tine crosses the field's peak twice a cycle
and the output comes out an octave up, and where the fit reproduces it
(h2 +2 dB over h1, model +2) — then 0.04 on the far side, the bell being
symmetric. Monotonic in |h|, zero at centre: the fitted number is the
screw, and a world can have it as an axis.

*The geometry, from the other end.* The shaped records give the metal two
numbers a note: the fundamental, and the second bending mode's ratio to it
(5.4-7.5 across the EP's keyboard; a uniform clamped rod's is 6.27 and the
tuning spring moves it). `tools/geofit.py` drives the FEM tine's length and
spring position onto them — Nelder-Mead, a mesh and a modalfem run per
evaluation, a second each — and lands both on every one of seven notes:
C3 106 mm with the spring 41 mm from the tip, C4 77 / 39, C5 53 / 26, E6
33 / 18. Real tines run about that. The FEM family is not a stand-in for
the instrument any more; it is the instrument, note by note, and a world
can be baked from it with the pickup on top.

*Decay, measured honestly.* `decay_ratio` is now energy-weighted, because a
thump at a three-hundredth of the fundamental's energy judged against a
track that is the room's had a vote equal to the fundamental's and read the
EP as ×0.30 when its fundamental was ×1.12. After the sixth pass:
Wurlitzer ×0.96, EP ×0.96, guitar ×1.01, banjo ×0.87, mandolin ×0.79 (was
×0.56 by the old unweighted metric on the same records' predecessors; the
two numbers are not comparable, and the mandolin is still the deadest,
which is body and sympathetic courses, not partials). This is
a `Shaper` stage after the modal bank, per world, off for the acoustic
worlds; the record carries it as `shaper bell h w K fc Q`, which the packer
ignores and the runtime will read. What the world then needs at runtime is
not 48 harmonics a note but a handful of metal modes and five numbers, and
velocity comes out of the physics instead of out of a layer.

## Reproducing it

    make -j8 all-stages                 # ~10 minutes: 36 FEMs in parallel, then seconds
    make -B grade GRADEFLAGS="--strike 6 --listen 6"
    make -B align REF=bell05 && make bake grade    # a different reference
    tools/overnight.sh                  # every recorded set through the fitter, then the fitted worlds (GPU, ~3 h)
    make build/epigen && build/epigen out/gen/tine --instrument 0      # Epi's tine piano, 37 notes x 6 velocities
    tools/fitvel.py tine out/gen/tine out/fit/tine-vel --bar             # metal + pickup per note

Dependencies beside the repo, unmodified: `../faust` (mesh2faust's Vega and
Spectra), `../eigen`. Host C++ and Python stdlib.
