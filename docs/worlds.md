# Worlds and modes — design note (Will, 2026-09-08; not yet built)

Two orthogonal ideas came out of the first bench session. Recording them
before they drift, with my read on what each costs.

## Worlds

Will's framing: **a world is an extrapolation technique plus a corpus plus a
representation, and the spaces you rotate through inherit its properties.**

This is the right abstraction and it already almost exists. A `*.kyk` file is
a *baked world*. What is missing is the recipe that produced it, so a world
can be re-baked, varied by seed, or shipped as a name rather than a blob.

A world is therefore three choices:

| | what it picks | what we have |
|---|---|---|
| **corpus** | where the waves come from | Braids' 256-wave bank; WAV directories; nothing (purely generative) |
| **representation** | what a point in the space *is* | K log-magnitudes with a shared phase spectrum |
| **extrapolation** | how the lattice is filled between and beyond corpus points | PCA reconstruction (`kykeigen`), correlated field (`Family::Field`), parametric (`Family::Harmonic`) |

The four Will named, and what each needs:

1. **A Plaits world** and **a Braids world.** Both are corpus swaps into
   `kykeigen`, which already reads the Braids bank. Plaits generates its
   tables from `wavetables.py` rather than shipping an array, so it needs a
   small extractor. Cheap, do first.
2. **A General MIDI world.** Every GM instrument, analysed. This is a
   different kind of corpus: sampled instruments are not single cycles, so it
   needs a pitch-tracked, cycle-extracting front end before analysis. The
   payoff is a space whose axes are the directions real instruments differ
   in, which is the classic timbre-space result (Wessel 1979) with a modern
   corpus. Biggest of the four.
3. **An Erica-like world.** Stacked saws and stacked sines, where the
   relationships Will named — n−1, n/2, n+1, n×2 stacks — should all be
   *linear* from a given point. That constraint has a clean answer: **make
   the axis logarithmic in the stack count.** On a log axis, ×2 and ÷2 are
   equal and opposite steps of the same size, and n±1 becomes a smaller step
   near large n. Nothing else gives you both multiplicative and additive
   neighbours on one straight line. This is a parametric world, no corpus.

### Analytic or tabulated, and why it matters

Settled 2026-09-08 after Will asked why a world could not simply be swapped in
the way weights are. It can, and the first cut of this had it wrong.

An eigenspace *is* a matrix: a mean log-spectrum plus N component vectors,
about 1.3 KB for the whole Braids bank. Given a coordinate the spectrum is
`mean + Σ coord·comp`, exponentiated. The first implementation took that
formula, evaluated it at 4096 grid points, discarded the formula, and
interpolated between the samples — approximating the thing it had just thrown
away.

Measured on that exact world:

| | formula | its own side-8 sampling |
|---|---|---|
| memory | 1.3 KB | 1.18 MB (922x) |
| cost per render | 1.58 µs | 0.84 µs |
| at a grid point | — | exact |
| between grid points | — | up to 5.6% of peak magnitude |
| switching worlds | a pointer write | expand a megabyte |

Both costs are noise beside the transform, which is several microseconds in
the same units. So `core/kyk_world.h` has two backends and a world declares
which one answers for it:

- **Analytic** where a formula exists — the eigenspace, and the parametric
  families. Continuous everywhere, no tabulation error, no expansion pause.
- **Lattice** where one does not — a correlated random field is *defined* by
  its samples, and so is any imported corpus. Multilinear as before.

This also answers the "closed-form axes" argument from the parametric
proposal: our eigenspace was always a closed-form function of its coordinates,
and the lattice was hiding that rather than being required by it. A field
could join the analytic side too if it were rebuilt as gradient noise, which
is a closed-form function of position rather than a smoothed grid.

**What to build**: a world manifest, one small text file per world, naming
corpus, representation, extrapolation and parameters, that `kykspace` and
`kykeigen` can execute to produce a `.kyk`. Then the module browses worlds by
name and the web page shows which one is loaded. The runtime does not change
at all, which is the point: the format is already world-agnostic.

## Vertex worlds, and a hypothesis that failed

Will's description of the target: "if you align on any given plane you catch
that waveform locking into a familiar shape, and as quickly as you cross it
you're pulled back into the mire." That is a specification, and it has a
direct construction: put a recognisable waveform on each vertex of a regular
4-D polytope and weight them by distance. `core/kyk_solids.h`, shipped as the
**24-cell** world — 24 vertices, self-dual, a shape with no 3-D analogue,
which happens to exist exactly at our default N.

**The hypothesis was that distance would buy isotropy.** Every other legible
family weights the harmonic series multiplicatively, and multiplicative
weightings add in log magnitude, which is separable by construction, and
separable is what measures at 6 to 11x direction spread. Distance is not
separable, since `|p − v|²` couples every axis at once, so this ought to have
been the first legible family that was also even.

It is not. Measured spread is 4.5 to 5.9x depending on the solid — better
than Stack at 6.2x and Harmonic at 11x, nowhere near the random field at 1.5x.
Non-separability is evidently necessary and not sufficient: 24 points in four
dimensions still leave the space structured *by the polytope*, and directions
toward vertices differ from directions between them. Anisotropy of a different
shape, not its absence.

**What it does deliver is the thing that was actually asked for.** The lock is
real and tunable, and the Morph knob drives it, meaning the same control now
says "how discrete is this space" to both backends. On the 24-cell at K=64,
the fraction of the space sitting within 0.05 of a vertex waveform:

| sigma | locked | within 0.15 | worst spectral step | reads as |
|---|---|---|---|---|
| 0.06 | 81% | 92% | 0.058 | switching |
| 0.10 | 51% | 79% | 0.020 | locks and blends |
| 0.15 | 14% | 56% | 0.008 | locks and blends |
| 0.20 | 0.3% | 34% | 0.004 | always blended |

Morph runs sigma from the table's value down to 30% of it, so the low end is
mire everywhere and the high end lands on named waveforms most of the time,
with the interesting settings in between. Below about 0.10 the space stops
blending and becomes a Voronoi switch, which is the other failure.

## Rotating at audio speed

Will asked for worlds designed to be rotated through at audio rate. There is
a hard ceiling on that and it is worth stating before designing for it.

The frame is rendered once per block, so the position is sampled at 2 kHz at
48 kHz and 24 samples. Rendering the same orbit with the position updated
every 4 samples and comparing against every 24 isolates what the block rate
costs:

| orbit rate | error against a 12 kHz reference |
|---|---|
| 2 to 200 Hz | −32 dB, flat |
| 400 Hz | −28 dB |
| 800 Hz | −20 dB |

The flat part is not the orbit at all — it is the two renders having
different crossfade lengths, a fixed offset that does not grow with rate. The
rate-dependent damage starts around 200 Hz, which is 10 position updates per
orbit cycle, and by 800 Hz there are barely two.

**So the usable ceiling is roughly 200 Hz**, which is the bottom of the audio
band rather than the middle of it. Enough for growl, sidebands and rhythmic
grain; not enough for true audio-rate scanning. Going higher needs the
position evaluated per sample, which for an analytic world is not absurd —
it is one basis evaluation, not an inverse transform — but it is a different
render architecture and a much larger CPU bill.

**A world designed for this should tie its orbit rate to the pitch.** At
these rates the modulation puts sidebands at multiples of the orbit rate
around every harmonic. At an arbitrary rate those land between harmonics and
the result is clangorous, which is ring modulation rather than timbre. At a
rational ratio of f0 they land *on* harmonics and it reads as a waveform
instead. That argues for an orbit mode whose rate follows v/oct with a
rational multiplier, which is the same ratio-lock machinery already wanted
for the Ptolemaic orbits.

## Modes

Also Will's, and a separate axis from worlds. Three named:

1. **Self-controlled.** Knobs and CVs drive rotation, spread, position
   directly. This is what M1 ships.
2. **Orbit.** The plane angles advance on their own and the player controls
   the *rules* rather than the values. This is M2 as specced: a rate per
   plane, incommensurate rates giving quasi-periodic paths, ratio lock for
   closed figures.
3. **Kuramoto.** The strongest of the three and worth doing properly. Give
   each rotation plane a phase oscillator with its own natural frequency and
   couple them: `dθᵢ/dt = ωᵢ + (K/N)·Σⱼ sin(θⱼ − θᵢ)`. At zero coupling the
   planes drift independently and the orbit fills the space. As coupling
   rises they entrain, and the figure closes on its own. That makes **ratio
   lock emergent rather than imposed** — one knob from free drift to
   spontaneous synchronisation, with the interesting behaviour in the middle
   where some planes lock and others do not. It replaces the Brocot-tree
   ratio picker in the spec with something that is a physical process rather
   than a menu, which suits an instrument better.

   Cost is trivial: six oscillators updated once per block, one sine per pair.
   The design work is choosing the natural-frequency spread and how coupling
   maps to a knob, both of which want listening rather than theory.

4. **Shaped orbits.** Today a plane's angle advances at a constant rate,
   which is a ramp: uniform circular motion. Put any LFO shape in place of
   that ramp and the character changes completely — a sine makes the angle
   swing back and forth, a pendulum rather than an orbit; a triangle sweeps
   linearly and reverses; sample-and-hold jumps between orientations. Cheap:
   the phase accumulator per plane already exists, this only reshapes the map
   from phase to angle. The catch is that a shape needs a depth as well as a
   rate, so six planes become eighteen controls.

5. **Gravitational orbit.** A different thing from the rotation orbits, and
   arguably more direct: let the *position* fall through a softened central
   potential rather than the frame rotate. `a = −G·r/(|r|²+ε²)^{3/2}`,
   integrated per block. Kepler's second law then does the musical work, since
   the position rushes through periapsis and lingers at apoapsis, so the
   timbre dwells unevenly instead of sweeping at constant speed. Softening
   breaks the inverse-square closure, so ellipses precess and the path is
   quasi-periodic without needing two incommensurate rates.

   Wants care rather than compute: a bare 1/r² potential has a singularity at
   the centre and unbounded escape, so it needs the softening, a speed clamp
   and probably light damping to stay playable. Under `Wrap` the attractor is
   periodic, which makes the trajectory genuinely strange — that is the
   topology-meets-gravity idea and it is worth trying once the plain version
   is stable.

**Compute is not the constraint.** Gravity is about ten operations per axis
per block against a budget of 240,000 cycles; the Kuramoto coupling is six
sines. Every modulation idea here is free next to one inverse transform. What
is scarce is the panel: six pots already vanish into the six rotation planes,
and rate-plus-shape-plus-depth per plane is eighteen controls. So the
bottleneck is the mode idea below, not the arithmetic.

Modes are a page-level idea, not an engine one: they all drive the same
angles or the same position, so the engine does not need to know which is
running. That is also what makes them affordable — a mode reinterprets the
same six pots rather than asking for more.

---

# What got built, and what the design note got wrong (2026-09-09)

Both halves above are now built, and the table below is the first time every
world has been measured the same way. `tools/kykworlds` walks the registry and
asks each one the question the engine asks it — given a folded coordinate,
what is the spectrum there — so a formula and a lattice are graded on equal
terms.

| world | backend | variety | spread | twins | covers Braids | median |
|---|---|---|---|---|---|---|
| Braids | analytic | 1.466 | 2.14x | 0.4% | 32% | 0.0505 |
| 24-cell | vertices | 0.658 | 3.64x | 0.4% | 16% | 0.1766 |
| 16-cell | vertices | 0.382 | 3.14x | 2.3% | 14% | 0.2148 |
| Tesseract | vertices | 0.469 | 4.22x | 1.2% | 16% | 0.1667 |
| Stack | lattice | 3.433 | 3.69x | 0.1% | 27% | 0.0492 |
| Field | lattice | 2.813 | 1.31x | 0.0% | 9% | 0.0907 |
| Field II | lattice | 5.869 | 1.39x | 0.0% | 10% | 0.0838 |
| Torus | lattice | 4.325 | 1.26x | 0.0% | 11% | 0.0935 |
| Harmonic | lattice | 0.199 | 6.13x | 12.8% | 15% | 0.2401 |
| FM | fm | 12.758 | 1.83x | 0.0% | 11% | 0.0695 |
| Vowel | formant | 1.503 | 2.87x | 0.0% | 29% | 0.0358 |

**Variety** is timbral change per unit of travel, averaged over 200 random
directions. **Spread** is the most varied direction over the least, which is
the number that says whether rotating the control frame is worth having: at
1.0 every direction is as interesting as every other. **Twins** is the
fraction of random point pairs whose spectra sit within 5% of each other, so
it counts the places a sweep stalls. **Covers** is how much of Emilie
Gillet's 256-wave bank the world lands recognisably close to, and **median**
the typical distance to the nearest thing in it.

## Separability was the whole story

The design note above guessed that the legible parametric families would be
lopsided and it was right, but the reason is sharper than "they were designed
by hand". Every one of them weights the harmonic series *multiplicatively* —
a tilt, a width, a formant bump — and multiplicative weights **add in log
magnitude**. A sum of per-axis terms is separable, and a separable space has
natural axes by construction, so there is nothing for a rotation to find. That
is why Harmonic measures 6.1x and why no amount of tuning was going to save
it.

Two worlds escape it, and both do so the same way: by making one axis change
*where* another axis put its energy.

- **FM.** The k'th sideband is J_k(I), and a Bessel function is not a product
  of a function of k with a function of I. Better still, the sideband
  positions move with the ratio. Measured 1.83x, the least lopsided world with
  any legible structure in it; only the featureless correlated-noise fields
  beat it, and they have nothing to recognise.
- **Vowel.** Three resonances would be three multiplicative filters and
  therefore separable. Chaining them — the second peak a multiple of the
  first, the third a multiple of the second — means the first axis moves all
  three. 2.87x.

The vertex worlds were supposed to escape it too, since a squared distance
couples every axis at once. They did not: 3.1x to 4.2x. Distance couples the
axes but the *waveforms at the vertices* still differ mostly along one
direction, and that dominates.

## Ranges were swept, not chosen

Both new worlds had their reaches picked by measurement. FM's first setting
measured 3.6x; every setting under 1.9x turned out to have the ratio axis
capped near 2.5, because the ratio moves every sideband at once and left wide
it does five times the work of any other axis. The vowel world with textbook
proportions measured 6.0x, with the third formant's axis moving the spectrum
a thirty-fifth as far as the first's — a knob doing nothing at all. Levelling
the peaks fixed both numbers at once.

## Modes, as built

**Kuramoto is in, but not the equation in the design note.** Plain Kuramoto
drags every oscillator to one frequency *and one phase*, which collapses the
six planes into a single rotation and throws away the reason for having more
than one. What is wanted is for the ratio to lock. So the coupling is on a
harmonic combination: for each pair the nearest simple rational p/q to their
rate ratio is found, and they are coupled through ψ = q·θᵢ − p·θⱼ, which is
stationary exactly at that ratio. The pair reduces to

    dψ/dt = (q·ωᵢ − p·ωⱼ) − K·sin(2πψ)

so a lock exists whenever the ratio detuning is within K. That is an Arnold
tongue, and complex ratios get narrower tongues for free because their
detuning term carries the larger integers.

| coupling | 0.00 | 0.05 | 0.10 | 0.25 | 0.40 | 0.80 |
|---|---|---|---|---|---|---|
| settings landing on a simple ratio | 2.7% | 9.8% | 18.9% | 46.2% | 65.3% | 87.9% |

2.7% at zero coupling is chance. Measured over 1200 rate settings between
0.4x and 3x, 300 s each.

**Gravity on a torus works and is stranger than the flat version**, as the
note guessed. Whether it wraps is derived from the world's own topology on the
orbit plane's two axes rather than being a switch, because a toroidal
potential in a clamped space would pull toward an image that is not there.

| space | ecc | r min | r max | v max/min | laps in 200 s |
|---|---|---|---|---|---|
| flat | 0.50 | 0.2007 | 0.3500 | 1.74 | 121 |
| flat | 1.00 | 0.3500 | 1.3594 | 3.88 | 23 |
| torus | 0.50 | 0.2007 | 0.3500 | 1.74 | 121 |
| torus | 1.00 | 0.0020 | 0.7065 | 3.81 | 166 |

**Shaped orbits are not built**, and the note's own objection stands: a shape
needs a depth as well as a rate, so six planes become eighteen controls.

**The panel was the constraint, exactly as predicted.** Coupling needed a
sixth page rather than a spare pot, and that page carries three knobs rather
than being padded out to six.

## One thing to know before playing it

The rotation pivots about 0.5 on every axis, so a control frame sitting
exactly at the centre of the cube is a fixed point of *every* rotation. The
angles turn, the orbit accumulates, and the position does not move. Gravity is
the exception, because it adds an offset rather than turning the frame. In
practice a pot never sits exactly at 0.5, but it is reachable from the page
and from a script, and it produces a convincing impression that the orbit is
broken.

---

# Hopf fibrations (Will asked, 2026-09-09)

Will's question: are we trying known Hopf fibrations. We had not been, not by
name — but the machinery turns out to already contain them, and the coupling
built the same night already seeks them without either of us noticing.

## The condition

SO(4) is special: it factors, and a rotation of four-space turns two invariant
planes by two angles. When those angles are **equal in magnitude** the rotation
is *isoclinic*, and the orbits of an isoclinic one-parameter group on S³ are
exactly the fibres of a Hopf fibration. Every orbit is a great circle, every
pair of orbits is linked once, and the orbit space is S².

In this instrument's plane ordering — lexicographic, so 0=(0,1), 1=(0,2),
2=(0,3), 3=(1,2), 4=(1,3), 5=(2,3) — two planes are complementary (they share
no axis, and together they span all four) exactly when their indices **sum to
five**. Three pairs: (0,5), (1,4), (2,3). They are the quaternion units i, j,
k, and each with either relative sign gives one of the six one-parameter
isoclinic subgroups, three left-handed and three right-handed. Left and right
are the two mirror Hopf fibrations, with opposite linking.

So the recipe is: **rates on one complementary pair, equal magnitude, every
other plane at zero.**

## Measured against the real `Rotation`

Angle spread is the max minus min rotation angle over 200 random points of S³.
An isoclinic rotation moves every point by the same angle, so zero is the
condition; it needs no coordinates and does not care about chirality.
Separation is the nearest approach of one whole orbit to another, min and max
along the first, so a constant value means Clifford parallel.

| rotation | angle spread | closure | separation |
|---|---|---|---|
| planes 0,5 at +1 +1 | 2.3e-07 | 1.2e-04 | 0.4285–0.4285 |
| planes 0,5 at +1 −1 | 3.5e-05 | 1.2e-04 | 0.6736–0.6736 |
| planes 1,4 at +1 −1 | 3.5e-05 | 1.2e-04 | 0.5669–0.5669 |
| planes 2,3 at +1 +1 | 2.4e-07 | 1.2e-04 | 0.7363–0.7363 |
| planes 0,5 at +1 +1.5 (detuned) | 2.6e-01 | 1.1e+00 | 0.5357–0.6895 |
| planes 0,1 (share an axis) | 1.04 | 1.3e-04 | 0.0598–0.6881 |
| all six at +1 (reach-1 unison) | 1.06 | 2.2e-04 | 0.0157–0.7075 |

The residual 3.5e-05 on the mixed-sign rows is sine-table interpolation, four
orders below the failures.

**Closure is not the interesting property, and that is the trap.** Two planes
sharing an axis close just as tidily, and so does every plane at unison. What
those settings do not give you is a *fibration*: their orbits crowd together,
approaching within 0.016 in the unison case, where a Hopf pair holds a constant
0.43 apart everywhere. A closed orbit is common. A closed orbit that is one
fibre of a foliation of the whole space is not.

**Reach-1 unison is not Hopf.** Locking all six planes to one rate gives the
generator L1 + R2 + L3, which has both chiralities and is therefore not
isoclinic — the 1.06 row. The other four planes have to be *off*, which the
rate knob's dead zone makes easy, and the coupling skips zero-rate planes by
construction.

**The coupling already seeks it.** Put rates on one complementary pair only,
turn Coupling up, and 1:1 is the widest Arnold tongue there is: it snaps to the
isoclinic condition and holds it against drift. That was not designed and is
the nicest accident of the night.

## What is missing

**The topology.** Hopf fibres live on S³, and our space is a clamped cube or,
under Wrap, a 4-torus. `Topo::Sphere` is an enum value with no implementation
(see the checklist). Some of the behaviour is audible without it — an isoclinic
rotation about the cube's centre still traces a closed circle in ℝ⁴, so as long
as the radius keeps it off the walls the loop closes today — but the space is
not fibred, so there is no S² of loops to steer with.

## Why it would be worth having

Not as a motion mode. As a **control space**.

The Hopf map sends S³ to S², so *a point of S² names an entire closed timbral
loop*. Two knobs choose which loop, and the loop plays itself; every loop is
closed, so nothing ever drifts; every pair is linked, so neighbouring choices
give loops that thread through one another rather than lying near one another.
That is a different way to hold this instrument from anything on the panel now,
and it is Will's own criterion promoted from a moment to a trajectory: instead
of catching a waveform locking into a familiar shape as you cross a plane, the
whole path is the shape, and the two knobs pick which one.

Cost is small and known: a sphere chart in `FoldAxis`, a rate mode that drives
a complementary pair together, and a readout. The readout is the interesting
part — any R in SO(4) splits into a left and a right quaternion factor, so the
page could show how much of the current rotation is left-handed against
right-handed, which is a direct display of how near the player is to a
fibration. Nothing here is built.

---

# The shape tables, and why nothing ever locked onto a saw (2026-09-09)

Will, after playing every world in the rack: *"I never see things lock into
hard square or saws or other known base shapes, only stacks of sines rolling
through triangles, their walls sometimes stiffening."*

That is an exact description of the defect, and the cause was not a missing
world. It was the representation.

## Random phase was throwing the waveform away

Every world up to this point renders against a fixed **random** phase
spectrum. That choice bought the click-free morph — the frame is linear in the
magnitude vector, so partials cannot cancel — and nobody noticed what it cost,
because the spectrum is right. Measured, rendering the saw magnitude series
1/h at each phase convention and asking how much the result actually *is* a
saw:

| phase convention | crest | match to saw | match to square |
|---|---|---|---|
| random (shipped everywhere until now) | 2.14 | 0.789 | 0.789 |
| zero / cosine (the `seed 0` path) | 5.26 | 0.623 | 0.747 |
| **sine, a quarter turn** | **2.02** | **1.0000** | **1.0000** |

Match is the best circular cross-correlation against an ideal band-limited
target. At random phase a saw's discontinuity is smeared into a noise burst:
stacks of sines with the right spectrum, walls that stiffen where the spectrum
tilts and never become walls. Sine phase reproduces the shape *exactly*, and
its crest factor is slightly better than the random phase we were paying for.

The catch is that sine phase only pays off with **signed** coefficients. A
triangle alternates sign and a bipolar pulse needs a sign change part-way up
the series:

| wave | crest | match |
|---|---|---|
| triangle, signed | 1.72 | 1.0000 |
| triangle, magnitudes only | 1.29 | 0.9746 |
| 25% pulse, signed | — | 1.0000 |
| 25% pulse, magnitudes only | — | 0.8296 |

A negative coefficient is a half turn of phase and nothing more, so blending
stays linear and the morph stays exactly as click-free as it was. Both changes
are opt-in per world (`World::Phase`), so every existing golden is unmoved.

## The table

`Shapes` and `Shapes R` are the Erica GraphicVCO's wavetable matrix, which is
where this instrument started: a 4×4 grid of real waveforms on axes 0 and 1,
bilinear between them, with the Morph knob deciding how hard you land on a
node. Row 0 is the four everybody knows, and they are exact:

| node | vs textbook series | crest | ideal |
|---|---|---|---|
| (0,0) sine | 1.0000 | 1.414 | √2 |
| (1,0) triangle | 1.0000 | 1.721 | √3 |
| (2,0) saw | 1.0000 | 2.024 | ~2.0 with Gibbs |
| (3,0) square | 1.0000 | 1.183 | ~1.1 with Gibbs |

Each column then travels to a genuinely different destination rather than a
brighter copy of itself: one partial to eight; triangle to a rounded square,
by moving the sign pattern rather than the spectrum; saw to a resonant saw;
square to an 8% pulse. The first version had rows as brightness only and the
whole row axis measured 0.18 against 1.85 for the columns — a knob that did
nothing. Worst crest across the grid is 4.00, inside the 4.3 the output gain
allows.

Morph decides how much of the travel sits on a named shape: 79% at zero, 92%
at half, 97% at full.

## The shaper axes

Axes 2 and 3 run on the rendered cycle rather than the spectrum, because a
wavefolder has no closed form in the harmonics. Which shapers is a property of
the world, which is the "set of worlds" half of the request: `Shapes` is fold
and phase modulation, `Shapes R` is fold and ring modulation.

Aliasing, measured the way `tests/alias_check` measures it — worst
non-harmonic bin, dB below the loudest harmonic, at the saw node:

| shaper | dry | full depth |
|---|---|---|
| wavefold | −67.5 | −32.2 |
| phase modulation | −67.5 | −62.1 |
| ring mod | −67.5 | −59.3 |

Ring modulation is nearly free of it because it lands on harmonics by
construction. The folder is the dirty one, which is what a folder is for.

**Casio's phase distortion had to be thrown away.** The first version was the
real thing: a two-segment warp that hurries through the first part of the
cycle and dawdles through the rest. It measured **0.0 dB** — the worst
non-harmonic bin as loud as the loudest harmonic. Two linear segments meet in
a kink, a derivative discontinuity radiates energy falling off only as 1/h²,
and that is broadband, so pulling the band limit in ahead of it does nothing:
capping the knee moved 0.0 dB to −3.2 and no further. Replacing it with a
*sine* warp — phase modulation of the read pointer, which is also exactly
single-operator FM — has no discontinuity in any derivative, spreads a
harmonic h to about h·(1+A), and measures −62.1 dB. Casio shipped the
aliasing; that is not a reason to.

## Cost, and the desktop lying about it

This world shipped once in a state that made the module unplayable — "the
Shapes worlds now absolutely obliterate the CPU and are therefore noisy and
lossy" — while measuring perfectly reasonable on the desktop. Worth recording
carefully, because the gap was an order of magnitude and the desktop numbers
gave no hint of it.

| stage | x86 µs, first ship | x86 µs, now | M7 instructions, then → now |
|---|---|---|---|
| Evaluate (the table) | 7.79 → 1.44 | **0.195** | — |
| RenderFrame (the transform) | 4.14 | 4.13 | — |
| wavefold | 2.72 | **0.47** | 90 → 34 |
| phase modulation | 5.37 → 1.75 | **1.22** | 68 → 50 |
| ring modulation | 3.19 → 0.49 | **0.48** | 43 → 40 |

The shaped world now costs about 8% more per render than an unshaped one,
where it was closer to 80%.

**The wavefolder was calling `SinCosTurns`.** That routine computes sine *and*
cosine, interpolates both, and then runs a Newton step to put the pair back
exactly on the unit circle — because it exists to build rotation matrices that
stay orthonormal through long products. A folder needs none of it. Compiled
for the M7 the loop was about thirty VFP instructions in one serial dependency
chain, which at M7 latencies is on the order of 125 to 190 µs for a single
1024-sample call against a 500 µs block. On x86, which reorders around that
chain, the same code measured 2.7 µs. A raw table read with no interpolation
is ten instructions, and the staircase it leaves is one part in 2048 — about
−72 dB, against a folder whose own aliasing floor is −32 dB. Measured, the
alias figure did not move: −42.2 dB became −42.3.

**Nothing copies a buffer any more.** The frame is rendered into the scratch
and the last shaper writes the oscillator's frame, so the out-of-order shaper
gets a distinct source for free.

**The node spectra never change.** There are sixteen of them and they depend
only on which node they are, so recomputing one per render was pure waste —
and the pulse column costs a table sine per harmonic, which put up to 512 of
them into every Evaluate. They are built once, into 4 KB of DTCM that lives
with the World.

`make armcost` prints the instruction counts for these loops compiled for the
M7. A stopwatch on a laptop does not catch this class of bug; counting what
the target compiler emits does.

## What this world is not

It measures 1.80 / 0.38 / 5.89 / 5.30 across its four axes in *frame* space,
a spread of 15x, and `kykworlds` reads it worse still because that tool only
sees the spectrum and the shaper axes do not touch the spectrum. That is not a
failure to fix. Every other world in this instrument is trying to be isotropic
so that rotating the control frame finds new ground. This one is trying to be
**legible** — to put a saw where you can find it and let you fold it — and
those are different jobs. It is the world you reach for when you want to know
what you are hearing.

---

# Two more, added rather than retrofitted (2026-09-09)

Will's instruction after the shape tables landed: *"Let's add more instead of
changing the ones that are there."* So Braids, the three vertex worlds, Stack
and the fields all still render at random phase and are untouched. These two
are new.

## Lock — the 24-cell, with waveforms you can name

The vertex worlds exist for one sentence: *"if you align on any given plane you
catch that waveform locking into a familiar shape, and as quickly as you cross
it you're pulled back into the mire."* They have never delivered it, and the
reason is now known — they render at random phase, so the "saw" on a vertex has
a saw's spectrum and none of a saw's shape.

Same polytope, same distance weighting, rendered at sine phase with signed
coefficients. And the placement now does the other half of the sentence. The
24-cell's vertices are every permutation of (±1, ±1, 0, 0), which falls into
six groups of four — one group per coordinate plane, and those are exactly the
six Givens planes the rotation turns in. So each plane carries a **family**:

| plane | family |
|---|---|
| (0,1) | square, and pulses at 35%, 22%, 12% |
| (0,2) | the saw, brighter and darker |
| (0,3) | the triangle, sharpening toward a parabola |
| (1,2) | sine, and stacks of two, three, four partials |
| (1,3) | bigger stacks: six, eight, twelve, sixteen |
| (2,3) | combs — every second, third, fourth, fifth harmonic |

Which plane you line up on decides what kind of thing you find there, which is
a great deal more interesting than twenty-four unrelated waves scattered over a
solid. All four canonical shapes are on it, one at the head of each of the
first four groups.

Its basins were opened too wide at first, at sigma 0.26: everything averaged
into everything and the world measured 0.37 variety, below the old 24-cell's
0.66. At 0.17 it measures 0.82, and its four axes are the most evenly matched
of any legible world here at **1.6x**.

## Unison — one wave stacked on itself

From the original brief: *"one dimension is saw waves, stacking on themselves
in one direction."*

A voice at root r contributes to harmonics r, 2r, 3r… When r is a whole number
those land on harmonics and the stack is one thick tone. When it is not, they
land between harmonics — which a frame periodic at f0 cannot hold — so each
partial is split across the two bins it falls between, the same trick the FM
world uses. That is not a detuned oscillator, which this engine cannot produce,
but it is what a detuned stack *sounds* like: energy smeared onto neighbouring
harmonics, with signed deposits so voices cancel where they overlap.

It does **not** beat, and the first version of this note said it did. A frame
periodic at f0 holds exact harmonics and nothing else, so two partials in it
can never drift against one another. The chorusing is real but comes from
motion: when the detune axis moves, the split ratio slides and adjacent
partials trade amplitude continuously. Park the position and it stops dead,
which is the test that tells the two apart.

Axes are voices (one to seven, the last fading in), interval, detune, and the
copied waveform from saw through square to a narrow pulse. The interval axis
moves every partial of every voice at once, so at its first range of 1 to 4 it
did **eight times** the work of any other axis and the world measured 18.6x
lopsided; narrowed to 2.6, and with the wave axis given the full saw-to-pulse
range instead of only saw-to-square, it is 7.2x.

Both worlds are free of transcendentals per harmonic — the lesson from the
vowel world and then again from the shape tables. Evaluate costs 0.81 µs for
Lock and 0.80 for Unison, against 4.1 µs for the transform they feed.

## The table, with everything on it

| world | backend | variety | spread | twins | covers | median |
|---|---|---|---|---|---|---|
| Braids | analytic | 1.466 | 2.14x | 0.4% | 32% | 0.0505 |
| 24-cell | vertices | 0.658 | 3.64x | 0.4% | 16% | 0.1766 |
| 16-cell | vertices | 0.382 | 3.14x | 2.3% | 14% | 0.2148 |
| Tesseract | vertices | 0.469 | 4.22x | 1.2% | 16% | 0.1667 |
| Stack | lattice | 3.433 | 3.69x | 0.1% | 27% | 0.0492 |
| Field | lattice | 2.813 | 1.31x | 0.0% | 9% | 0.0907 |
| Field II | lattice | 5.869 | 1.39x | 0.0% | 10% | 0.0838 |
| Torus | lattice | 4.325 | 1.26x | 0.0% | 11% | 0.0935 |
| Harmonic | lattice | 0.199 | 6.13x | 12.8% | 15% | 0.2401 |
| FM | fm | 12.758 | 1.83x | 0.0% | 11% | 0.0695 |
| Vowel | formant | 1.503 | 2.87x | 0.0% | 29% | 0.0358 |
| Shapes | table | 0.986 | 8.47x | 2.7% | 12% | 0.2045 |
| Shapes R | table | 0.986 | 8.47x | 2.7% | 12% | 0.2045 |
| **Lock** | lock | 0.820 | 4.72x | 0.1% | 18% | 0.1079 |
| **Unison** | unison | 3.988 | 4.42x | 0.0% | 2% | 0.0953 |

Two notes on reading it. The shape tables' spread covers their first two axes
only, since the other two drive frame shapers this tool cannot see. And
coverage is now measured on **magnitudes** on both sides: the corpus is
analysed to magnitudes and carries no phase, so comparing it against signed
coefficients would count a triangle's alternating signs as distance from a
triangle. As it happens the numbers did not move, because coverage takes a
minimum over eight thousand points and the nearest is almost always an
all-positive spectrum — but the metric was ill-defined for signed worlds and
now is not.

---

# A modal world — design note, measured but not built (2026-09-09)

Will asked whether we could precompute tables from a physical model without
the synth running one. Yes, and it is what the architecture is for: a world is
a corpus, a representation and an extrapolation, and a physical model is a
corpus generator. It runs in `tools/`, never in the audio path. If the model
has a closed form — and the canonical shapes do — it can be an *analytic*
world, 1.3 KB evaluated live, with no lattice at all.

What precomputing does **not** do is dodge the harmonic grid. Whatever is
baked, the renderer places partials on integers. Worst placement error for
real modal ratios, as a fraction of each partial's own frequency:

| system | as rendered | 3-period frame | 8-period frame |
|---|---|---|---|
| ideal bar | 8.9% | 3.2% | 0.65% |
| circular membrane | 25.5% | 6.3% | 2.0% |
| square plate | 9.2% | 2.8% | 2.0% |

A semitone is 5.95%. A membrane is over four semitones out. So a modal world
here is a *tuned* instrument, in the sense that vibraphone and handpan makers
mean it — the mode ratios are deliberately rational. True inharmonicity needs
the M-period frame or a per-partial additive renderer, both already on this
list and both a different engine.

That is a smaller loss than it sounds, because **the frequencies are not where
a struck object gets its character — the amplitudes are.** Strike position,
geometry and decay all live in which modes are loud, and that is exactly
representable.

## Three things the prototype measured

A rectangular plate: strike position, side ratio, hammer width, stiffness.

**Deposit fractionally or it clicks.** Rounding each mode to its nearest
harmonic makes modes *hop* between bins as the geometry moves. That measured
6.00 variety and it was mostly discontinuity: splitting each mode across the
two bins it falls between, exactly as FM and Unison do, brought it to 1.454
with a worst step of 0.0025 over 4000 points. Three quarters of the apparent
variety was clicks.

**Decay time and hammer hardness are the same axis.** Both are lowpass filters
on the mode set, so they are collinear in spectrum space. Spending two of four
axes on them left "time since strike" at 0.07 against 0.83 for strike
position, whatever the scaling. Pick one.

**Aspect ratio is the strong axis**, because it changes *which* modes exist
rather than tilting the ones that do — modes reorder and collide as the plate
reshapes. It measured 2.58 against 0.55 for hammer width.

With those three applied: **variety 1.454, direction spread 3.05x, per-axis
4.7x**, which sits beside Vowel at 1.50 / 2.87x and Braids at 1.47 / 2.14x.

## What would need deciding before building

Which object families to ship, and whether to keep a decay axis at all given
it duplicates hammer width. The obvious four axes are strike position, aspect
ratio, hammer width and stiffness — no decay — with the object family either a
fifth axis or a set of separate worlds.

---

# The modal worlds, built (2026-09-09)

Three struck objects: **Plate**, **Bar**, **Drum**. The design note above said
what to watch for and all three warnings paid off.

| world | variety | spread | twins | covers | median |
|---|---|---|---|---|---|
| Plate | 1.037 | **2.28x** | 0.4% | 20% | 0.0784 |
| Bar | 1.223 | 3.83x | 0.2% | 19% | 0.1617 |
| Drum | 0.654 | 4.06x | 3.9% | 24% | 0.0837 |

The Plate is the second most isotropic world in the instrument, behind only
the featureless correlated-noise fields, and unlike them it is legible.

## What the design note predicted, and what it missed

**Deposit, never round** — correct, and now enforced by `tests/cont_check`.

**Decay and mallet hardness are collinear** — correct, so there is no decay
axis.

What it got wrong was the replacement. It proposed *stiffness* as a fourth
axis, and stiffness turned out to be collinear with geometry for the same
reason decay was collinear with the mallet: both stretch the ratio set. It
measured 0.03 against 0.64 on the drum.

**Temper** replaced it — how hard the modes are pulled onto whole numbers,
which is this world's own premise made playable. One pass of the smooth
attractor moves a mode by at most 1/2π of a harmonic, and a bar's second mode
needs a quarter of one to reach a whole number, so a single pass measured 0.012
and was still a dead knob. The map's fixed points are the whole numbers, so
iterating walks a mode home: 2.756 → 2.907 → 2.990 → 3.000. Three passes, and
each is monotone below a temper of one so the composition stays continuous.

And temper is still dead on the Plate, because its modes are a 2-D grid whose
(1,1) member sits at exactly one harmonic and dominates. The plate got the axis
it should always have had: **a second strike coordinate**. You strike a plate
at a point, and a point on a plate is two numbers. That took it from 38x
per-axis imbalance to 3.1x.

| world | strike | geometry | mallet | fourth |
|---|---|---|---|---|
| Plate | 1.256 | 1.588 | 0.716 | 0.509 (strike y) |
| Bar | 0.996 | 1.641 | 0.588 | 0.246 (temper) |
| Drum | 0.723 | 0.689 | 0.740 | 0.249 (temper) |

Worst crest over 4000 random points is 2.81, 2.92 and 2.75, all inside the 4.3
the output gain allows. Evaluate costs 1.25 µs for the Plate and about 0.55 for
the other two, against 4.1 µs for the transform they feed.

## The time axis, after Will heard what the numbers missed

The first build of these had no time axis, on measured grounds: time-since-
strike and mallet hardness are the same direction in spectrum space, both
being lowpass filters on the mode set, and time read 0.07 against 0.83 for the
strike position whatever the scaling.

Will played them and said the plate and the drum were "a mild wiggle on the
sine", and that a struck thing needs a time axis by its nature — we are making
wavetables from the output of something like Rings across all its knobs.

Both halves of that were right, and the second explains why the first
measurement was worthless. Share of energy in the fundamental, and how many
partials hold ninety percent of it:

| world | energy in h1 | partials at 90% |
|---|---|---|
| FM | 5.2% | 11.1 |
| Vowel | 13.2% | 7.0 |
| Drum, as shipped | **72.7%** | **1.9** |
| Bar, as shipped | 73.4% | 2.4 |
| Plate, as shipped | 29.9% | 3.2 |

The drum was holding ninety percent of its energy in under two partials. In a
spectrum that empty every axis measures small and any two of them look alike,
so "time is collinear with the mallet" was a statement about the emptiness, not
about the axes. The immediate cause was the mallet itself: `sinc(n·w)` has its
first zero at `n = 1/w`, and the width ran to 0.32, so at the middle of the
knob six modes survived. The drum also had only twelve modes, all inside two
octaves.

Rebuilt around what an exciter and a resonator actually have: **strike,
geometry, time, damping**. The last two are a pair rather than two lowpasses —
damping does nothing at time zero, time does nothing under uniform damping, and
together they sweep a family of decay *shapes*. The mallet is fixed at a hard
0.03 and gave its slot to time.

| world | strike | geometry | time | damping | per-axis |
|---|---|---|---|---|---|
| Plate | 2.638 | 0.670 | 0.828 | 0.627 | 4.2x |
| Bar | 1.173 | 1.883 | 1.066 | 0.302 | 6.2x |
| Drum | 1.291 | 1.030 | 1.403 | 0.369 | 3.8x |

| world | variety | spread | twins |
|---|---|---|---|
| Plate | 1.314 | 4.78x | 0.1% |
| Bar | 1.152 | 3.31x | 0.2% |
| Drum | 0.880 | 3.52x | 1.2% |

And the strike is bright again. Partials above −40 dB, sweeping the time axis:

| world | t = 0 | t = 0.35 | t = 0.7 |
|---|---|---|---|
| Plate | 8 | 8 | 8 |
| Bar | 20 | 17 | 11 |
| Drum | 14 | 11 | 7 |

Two implementation notes, because both are the same lesson a fourth time.

The obvious way to write "damping rises with frequency to a power" is `freq^p`
and the obvious decay is `exp(−a·t)`. That is two series evaluations per mode,
and the plate has sixty-four: it measured **5.46 µs an evaluation, more than
the transform it feeds**. The power became a crossfade between three exponents
that need no arithmetic — `sqrt(f)`, `f`, `f²` — which covers the same range of
decay *shapes*, and shapes are the point. The decay became `1/(1 + a·t)`:
smooth, monotone, one at zero, and a single divide. As a spectral tilt it is
indistinguishable from an exponential, and real damping is not a clean
exponential across modes anyway. 5.46 µs → 0.75.

And the mode-amplitude law was mine rather than the physics. A strike is close
to an impulse and excites the modes roughly equally; what shapes them is where
it lands and how wide the mallet is. `1/n` and `1/(m·n)` were putting half the
energy in the fundamental before anything else got a say.

## A protocol bug they exposed

Adding three worlds took the list to eighteen, and `GET_WORLDS` writes every
name and note into one frame: about 1160 bytes against a 1024-byte body.
`FrameWriter` refuses to overflow, `Encode` then returns zero, and the reply is
simply **never sent**. Nothing reported an error anywhere — the page and the
selftest just hung waiting for a frame that would never arrive.

The list only grows, so it is paged rather than merely made to fit. The request
takes an optional start index, empty meaning zero, which is what every host
sent before this existed; the reply carries the total, the current world, the
start and how many it managed. `fetchWorlds` walks the pages. Descriptor
extension version 4.

---

# Single-shape worlds: Saw, Pulse, Edge (2026-09-09)

Will's note: *"we could use some funky tables. Square and saw only table,
square manipulated, saw manipulated, etc. Escape the idea of 'all in one' and
unlock different worlds."*

That is the better organising idea and it fixes something that had been going
wrong on its own. Every world before this tried to span a wide range of timbre
with four balanced axes, and their axes kept fighting: the modal geometry axis
doing eight times the work of the mallet, the unison interval doing eight times
the work of the wave. Every one of them needed a tuning round to stop one axis
swallowing the others. A world that already knows what it is can spend all four
axes on manipulating that one thing, and each axis gets room.

| world | variety | spread | twins | crest |
|---|---|---|---|---|
| **Saw** | 1.444 | **2.03x** | 0.0% | 4.18 |
| Pulse | 2.794 | 3.36x | 0.0% | 3.40 |
| Edge | 1.862 | 2.57x | 0.0% | 3.07 |

Saw at 2.03x is the most isotropic world in the instrument that has anything
legible in it — behind only FM at 1.83 and the featureless noise fields. All
three have no near-duplicates at all, and at 0.25–0.31 µs an evaluation they
are the cheapest worlds here.

## The bends

**tilt** crossfades between three harmonic laws, h^-1.6, h^-1 and h^-0.75.
The obvious way to write a tilt is `r^h`, and it is a trap: once r passes one
the series stops falling, and a series that does not fall is an impulse. Crest
4.07 on that axis alone and 6.76 in the corner with parity, against the 4.3 the
output gain allows. Bounding r to 1.01 kept the crest but left the axis
measuring 0.41, the weakest of the three worlds. A power law always falls
whatever the exponent, so it can go far brighter at no cost in crest — and
three fixed exponents from tables need no power function per harmonic.

**parity** fades the even harmonics out and then past zero: at a half a saw has
become a square, and beyond that the evens return inverted, which is a
different waveform with the same spectrum.

**comb** is a periodic notch across the harmonic series whose period sweeps —
the spectral shape a phaser makes, except static per frame.

**duty** is the bipolar pulse.

**fold point** flips the sign of every harmonic above a moving point, and
deserves a caveat rather than a boast. It leaves the magnitude spectrum exactly
unchanged, so on a steady tone its direct audibility is limited — Ohm's law of
phases, true since Helmholtz. What it changes is the waveform shape and the
crest factor, which is what a wavefolder or ring modulator downstream has to
chew on, and it is dramatic on the frame display. It is here because a
phase-aware representation can offer it and a magnitude-only one cannot, not
because it is loud.

Nothing evaluates a transcendental per harmonic. The pulse and the comb both
need cos(2·pi·h·x) at successive integer h, which is a rotation: carry
(cos, sin) forward with four multiplies and two adds. Four worlds in this
project were written the expensive way first.
