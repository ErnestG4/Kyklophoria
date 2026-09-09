# Checklist — 2026-09-08

Working state after the first rack session. Supersedes the scattered "Open"
sections in the milestone notes. Order within each block is my recommendation,
not a dependency chain.

## Blocked on Will

- [ ] **Max CPU reading** after the last two cuts (position deadband, and
      telemetry encoding moved off the audio callback). Last measured was 33%
      average and 97% max, and both cuts came after that. The number decides
      whether render divider 1 can go back on the knob.
- [x] **Licence** — AGPL-3.0, decided 2026-09-08. `LICENSE` added,
      `THIRD_PARTY.md` records that the SDK, libDaisy and the Braids corpus
      are all MIT and all build-time or bake-time rather than vendored, so the
      combination is distributable. Optional follow-up nobody has asked for:
      an `SPDX-License-Identifier` line per source file.
- [ ] **Does the eigenspace sound better than the field in the rack?** They
      grade differently on paper (field is more even, eigen is made of real
      waves) and only playing them settles it. Whichever wins should become
      the boot space.

## Next

- [ ] **Orbit needs a bench listen.** It renders continuously by nature, so it
      is the worst case for CPU; see the max reading above.

- [x] **Orbit mode.** Done. A signed rate per plane in turns per second,
      advanced once per block, on its own panel page with the centre stopped
      and an exponential taper either side (0.01 to 1.0 turns per second, so a
      hundred seconds per revolution at the slow end). `m2_orbit` is a golden;
      with two planes at unrelated rates the path never returns closer than
      0.15 to its start over six seconds, which is the quasi-periodic
      behaviour the whole design is for.
- [ ] **Kuramoto coupling** on those orbit rates, so ratio lock is emergent
      rather than a menu (`docs/worlds.md`). Six oscillators, one sine per
      pair, negligible cost. Wants listening rather than theory to tune.
- [ ] **Payload lanes.** Eight are computed per node and exactly one is used
      (CV out A). The filter, drive and FM index are the difference between a
      space of waveforms and an instrument. Note the drive stage must be
      oversampled or it will do to the aliasing figure what the soft clip did
      (`docs/m2-notes.md`).
- [ ] **FM and sync inputs** on J1 and J2, which are wired to nothing.
- [ ] **Wrap and sphere** on the panel. Both are implemented and seam-tested
      in the core; neither is reachable from the module.

## Parametric axes — a proposal from Sonnet, and what measurement says

Argued for replacing corpus-derived axes with closed-form parametric ones
(tilt, parity, stretch, fold), on the grounds that a curated bank has one
meaningful axis and stacking four banks makes rotation meaningless.

- [x] **Measured.** Coverage of the Braids corpus, nearest-point cosine
      distance over matched candidate counts:

      | space | covered (<0.02) | badly missed (>0.20) | direction spread |
      |---|---|---|---|
      | Harmonic, parametric, side 8 | 15% | 55% | 11.0x |
      | Field, random, side 8 | 18% | 12% | 1.5x |
      | Eigen, PCA from Braids, side 8 | 33% | 11% | 2.7x |

      The parametric family we already have *is* closed-form legible axes, and
      it is the worst on both counts. So "closed-form axes" is not the fix for
      rotation; separability is the cause of the anisotropy, not the cure.
- [ ] **Still worth building**: tilt / parity / fold as a fourth family, to be
      graded on the same numbers rather than argued about. Legible axes have
      real value that PCA lacks — nobody can learn "PC2".
- [ ] **Stretch cannot be built as proposed.** `f_n = n·f0·sqrt(1+B·n²)` is
      inharmonic, and a single-cycle frame read cyclically at f0 is strictly
      periodic at f0, so it can hold harmonics of f0 and nothing else. It
      needs either an M-period frame (1/M-harmonic resolution, K grows by M)
      or a per-partial additive renderer. Real idea, different engine.
- [ ] Even the eigenspace only lands within 0.02 of a third of the corpus it
      was built from, so four components is a real limit, not just a tuning
      choice.

## Worlds

- [x] **Two backends, analytic and tabulated** (`core/kyk_world.h`). A world
      with a formula is evaluated live; one without is expanded to a lattice.
      1.3 KB against 1.18 MB for the same eigenspace, and switching to an
      analytic world is a pointer write. See docs/worlds.md for the numbers.
- [x] **World switching over HostLink**: 0x65 lists them, 0x66 sends an
      analytic world's formula so a host can evaluate the space itself, and
      ACTION 4 selects one. The module now boots into the analytic Braids
      world, so it makes sound immediately with nothing to expand.
- [ ] **World manifest**: one small text file naming corpus, representation,
      extrapolation and parameters, that the tools execute to bake a `.kyk`.
      The runtime needs no change; the format is already world-agnostic.
- [ ] **Plaits corpus extractor.** Plaits generates its tables from
      `wavetables.py` rather than shipping an array, so it needs a few lines
      that Braids did not.
- [ ] **AKWF**, several thousand CC0 single cycles. 256 waves is thin for
      four principal components.
- [x] **The Erica-like world** — shipped as `Family::Stack`, world 1. One
      waveform idea per axis: stack count on a logarithmic axis so n×2 and n/2
      are equal and opposite steps, spectral tilt, pulse width, parity. I
      predicted it would measure as badly as the Harmonic family since the
      axes are separable in log-magnitude. Half right: it is the second most
      lopsided space at 6.2x, but also the *liveliest*, with the most timbral
      movement per unit of CV travel of anything we have, 0.4% duplicates and
      better Braids coverage than the random field. Three inert axes were what
      sank Harmonic, not separability alone.
- [x] **Geometric-solid worlds** — `core/kyk_solids.h`, shipped as the 24-cell.
      A waveform per vertex, weighted by distance, with Morph tightening the
      lock. My hypothesis that distance-coupling would give isotropy was
      wrong: 4.5-5.9x spread, better than the other legible families and far
      off the random field's 1.5x. It does deliver the lock-and-mire
      behaviour that was the actual request. Numbers in docs/worlds.md.
- [x] **Audio-rate rotation, measured.** Usable to about 200 Hz; the damage
      starts there and it is broken by 800, because the position is only
      sampled at the 2 kHz block rate. Numbers in docs/worlds.md.
- [ ] **Orbit rate as a ratio of v/oct**, so at those rates the sidebands land
      on harmonics rather than between them. Same ratio-lock machinery the
      Ptolemaic orbits want.
- [ ] **An FM or waveshaping axis** — the untested route to genuine
      non-separability, since the spectrum would stop being a product of
      per-axis factors. FM of integer-ratio partials lands on harmonics, so it
      fits the representation; it needs Bessel weights.
- [ ] **General MIDI world**, the big one. Sampled instruments are not single
      cycles, so it needs pitch tracking and cycle extraction before analysis.

## Known warts

- [ ] `kykeigen` spans a symmetric ±2.2 sd on every axis, but the corpus is
      lopsided: axis 1 runs −1.6 to +5.5. Per-axis percentiles would waste
      less of the lattice.
- [ ] The eigenspace has 4.8% near-duplicate cell pairs, from the
      reconstruction flattening out at the far corners. The field has none.
- [ ] Slew is not seam-aware: a jump across a wrapped axis travels the long
      way round instead of across the seam.
- [ ] **The telemetry BAD_STATE root cause was never found.** It is papered
      over by always encoding on demand, which is robust but means we never
      learned why the snapshot path failed on the bench.
- [ ] CMSIS decision. Verified correct against ours to 2e-7, but only 1.16x
      faster on x86, so the gain no longer obviously justifies losing
      desktop-and-module bit-identity. Revisit only if CPU stays tight.
- [ ] Render divider 1 is off the knob. See the first item.

## Web

- [ ] Orbit trail and the rotation arcs, once orbit mode exists.
- [ ] Panel mirror (spec §8 view 4) reading the jack list from the descriptor.
- [ ] Attract mode: replay a recorded telemetry log so the page demos itself
      with no module attached.
- [ ] The dev drawer only appears over the bridge, which is right, but there
      is no equivalent way to drive the module from the page.

## Settled, so nobody reopens it

Name, I/O map, stereo as an angular spread, derived phases, frame 1024 and
K 64, cube-map sphere chart (spec §9). The morph is provably click-free and
that property is tested. Aliasing is −88 dBFS over five octaves. A soft clip
is not available without oversampling. Random phases beat Schroeder for our
tilted spectra. Whitening is required after PCA, not optional.
