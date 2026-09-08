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

Modes are a page-level idea, not an engine one: all three drive the same
angles, so the engine does not need to know which is running.
