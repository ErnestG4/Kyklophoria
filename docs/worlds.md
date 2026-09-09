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
