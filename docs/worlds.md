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
