# M2 notes — content and morph quality (2026-09-08)

Triggered by two questions from Will: how do we fill an N-dimensional space
with material worth exploring, and why do wavetable oscillators click when
they morph. The second turned out to have a cleaner answer than expected and
the first a worse one. Survey behind both: `docs/lit/`.

## The morph clicks, and whether we have them

**We do not, and it is structural.** Every cell shares one phase spectrum, so
the rendered frame is a linear function of the magnitude vector:
`render(a·x + b·y) = a·render(x) + b·render(y)`, measured to 1.4e-6 over two
hundred cell pairs. Partials therefore cannot cancel, whatever weights the
interpolator picks. A design storing time-domain frames at unrelated phases
gets comb filtering and a level dip through every crossfade; that whole class
of glitch is unreachable here. Vital independently arrived at the same
architecture, storing spectra per frame and IFFT-ing per render
(`docs/lit/software.md`).

Measured artifacts, non-harmonic energy against harmonic energy:

| case | before | after |
|---|---|---|
| static position (floor) | −153 dB | −153 dB |
| continuous morph, frame every block | −96.9 dB | **−97.6 dB** |
| continuous morph, frame every 4th block | −82.2 dB | −82.1 dB |
| band-limit steps across five octaves | no discontinuity | no discontinuity |

The gain in row two came from a real bug. `Osc::Process` ran its crossfade
coefficient from `1/n` to `1`, so the fade never started at the old frame,
leaving a step of `(new − old)/n` at the near edge of every block that
rendered. It scaled with how much the frame had changed: invisible on a slow
sweep, and on a jump it was a step at every block boundary. The coefficient
now runs 0 to 1 inclusive, so the hand-over to the next block is continuous.

### A measurement I got wrong

I first reported an instant position jump producing "+93 dB of high-band
energy", and told Will that was our one remaining click. It was not. The
metric compared a window at the jump against a window after it, and the two
positions have genuinely different brightness, so it measured a timbre
difference and called it a transient. Under a metric that cannot make that
mistake — worst curvature at the event against the 99.99th percentile of the
same render, where a real click is 50x or more — an instant full-range jump
measures **0.84x with the slew off**. There is no click. `tests/morph_check`
now carries the correct test and the story of the wrong one.

The control-frame slew stayed anyway, at 5 ms by default, because it halves
the curvature at a jump and turns a one-block timbre switch into a short
morph, which is what a stepped sequencer CV wants. It is a smoothing control,
not a fix. Vital crossfades wavetable changes over about 7 ms for the same
reason. It is a rate limiter, so slow motion passes through untouched.

## Level across a cell

Parseval makes the rendered RMS proportional to `||m||₂`, and a weighted sum
of unit vectors is shorter than a unit vector unless they are parallel. The
worst case is `3.01·N` dB, which is 12.04 dB at N=4, and it needs corner
spectra with disjoint support.

| content | cell-centre dip |
|---|---|
| the shipped Harmonic space | −0.02 dB |
| broad independent random spectra | −1.2 dB |
| narrow formants, width 2 harmonics | −4.2 dB |
| narrow formants, width 0.5 harmonics | −10.2 dB |

So it was not a defect and would have become one the moment a formant or bell
family landed. `Blend` now rescales to the weighted average of the corner
lengths (`BlendLevel::Preserve`, the default), measured flat to 0.000 dB. The
cost is that a blend is no longer literally the average of two waveforms; it
is that average times a smooth gain, which keeps every continuity property.
The tests that check interpolator geometry ask for `BlendLevel::Raw`.

## Filling the space

The Harmonic family is a poor space, and the reason is worse than dullness.

| | Harmonic, side 4 | field, side 4 | field, side 8 |
|---|---|---|---|
| variety per unit of CV travel | 0.22 | 0.50 | 1.14 |
| spread of that across directions | 9.8x | 2.3x | 1.8x |
| near-duplicate cell pairs | 10.2% | 0% | 0% |
| spectral centroid range | 1.0 to 5.5 | — | 1.0 to 63.7 |

The spread column is the one that decides whether rotation is worth having.
Sampling three hundred directions through the centre, the Harmonic space's
richest direction gives about ten times the timbral movement of its poorest,
so rotating the control frame is a lottery and most angles land a CV on a
nearly dead direction. Three of its four axes barely move the spectral
centroid at all, because every operation in that generator either keeps the
saw's rolloff or removes energy. Nothing brightens.

A stationary correlated field has covariance depending only on distance, so
it has no privileged directions by construction, and the measurement agrees:
1.8x spread at side 8 against 9.8x. That is the property that makes rotation
pay, and a grid of independent parameter axes cannot have it.

`Family::Field` in `core/kyk_gen.h` builds one: white noise per cell and
harmonic, smoothed along each lattice axis to set the correlation length,
smoothed lightly along the harmonic index so each cell reads as an envelope
rather than hash, rescaled to unit variance because smoothing eats most of
it, then exponentiated onto a tilted base and normalised per cell. Smoothing
honours each axis's topology, so a wrapped axis has no seam in its content.
All scratch is one lattice line, so it allocates nothing and runs at boot on
the module. Both families ship; the space file is a blob and does not record
which one made it.

The module's boot space is now the field at side 8, which costs 1.18 MB of
the 64 MB SDRAM and takes the firmware to 1.86% of it. `side` 4 leaves no
room for a correlation length, which is why the numbers above improve so much
at 8.

## Determinism
`detail::Ln` and `detail::Exp` are series, not libm, so the desktop and the
module generate byte-identical spaces; both are accurate to about 5e-7 against
libm. `Sqrt` is `__builtin_sqrtf`, which IEEE-754 requires to be correctly
rounded, so unlike the transcendentals it is safe to use directly. Gaussian
noise is twelve summed uniforms rather than Box-Muller, avoiding log and cos.

## Bench, first session (Will, 2026-09-08)

| | before | after |
|---|---|---|
| CPU max | 160% of budget, module crashing | 97% |
| CPU average | not reported | 33% |

The crash was the render divider at 1, a frame every block for both voices.
The real-valued transform bought about 2x and the divider now starts at 2.
97% max still leaves no headroom against the 30% target, so CMSIS is still
wanted.

**Popping was clipping.** Cells are unit RMS, so peak depends on how the
harmonics happen to line up; measured crest factor is median 2.5, p95 3.2 and
worst 4.2, and the old gain of 0.5 put that well past full scale. Two things
came out of chasing it:

- Phase rule does not help. Measured crest across a baked space: zero phases
  median 2.40 and worst 7.19, random 1.83 and 2.99, Schroeder 2.01 and 3.47.
  Random, which we already use, wins. Schroeder is optimal for flat spectra
  and ours are tilted, so the textbook answer is the wrong one here.
- **A soft clip is not available to us.** Adding one dropped the aliasing
  figure from −88 to −27 dBFS, because a memoryless nonlinearity multiplies
  the bandwidth of a signal that is bandlimited right up to Nyquist.
  `tests/alias_check` caught it immediately. Any saturation has to be
  oversampled, which makes it an M3 job for the drive lane. The fix shipped
  instead is headroom: gain 0.23, which keeps even the worst cell inside full
  scale.

Two knobs on the Stereo page were reported dead. Both were working: P3 was CV
output depth, which only moves a voltage on J8, and P4 is the render divider,
which is inaudible *by design*. P3 is now **Morph**, sharpening the
interpolation from smooth blending toward hard stepping between cells, which
is the control both halves of the survey said to steal. CV depth moved to P6.

## Open
- Goldens were regenerated: the level-preserving blend and the crossfade fix
  both change output. `m2_field` is a new golden covering the field family,
  rotation and the stereo pair.
- Still no bench numbers. Everything above is desktop measurement.
- The seam-aware slew (a jump across a wrapped axis currently travels the long
  way round) is not done.
