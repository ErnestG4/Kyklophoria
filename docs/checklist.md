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
- [x] **Kepler mode** — `core/kyk_kepler.h`, its own panel page. A body falls
      through a softened central potential and the space is read wherever it
      is, so the second law does the work: it rushes through periapsis and
      lingers at apoapsis. Verified: equal areas in equal times, constant to
      one part in ten thousand. Orbits are planar, as a central force
      requires, in one of the same Givens planes the rotation uses. Gravity
      spans about half a second to a hundred seconds per revolution.
- [x] **Kepler under Wrap** — done, and it is strange. Whether the attractor
      wraps is derived from the world's topology on the orbit plane's axes
      rather than switched. At high eccentricity the body stops swinging out
      to a far apoapsis and starts leaving one side to arrive from the other:
      23 laps in 200 s flat, 166 on the torus. A Torus world (all four axes
      Wrap) exists so it is reachable.
- [x] **Kepler in the telemetry and on the page** — a motion block at the end
      of the telemetry frame behind a new flags bit, carrying the body, its
      rush, the coupling and the lock. The page draws the attractor in the
      main view when the orbit plane is the pair of axes on screen, and always
      draws an inset of the conic in its own unrotated plane, because once the
      rotation is doing anything the main view shows a shadow of the orbit
      rather than the orbit.
- [x] **Kuramoto coupling** — done, but not the textbook equation. Plain
      Kuramoto locks phase as well as frequency, which collapses six planes
      into one rotation. The coupling is on q·θᵢ − p·θⱼ for the nearest simple
      ratio instead, so ratio lock is what emerges: 2.7% of rate settings land
      on a simple ratio at zero coupling and 87.9% at 0.80. Numbers in
      docs/m3-notes.md.
- [ ] **Payload lanes.** Eight are computed per node and exactly one is used
      (CV out A). The filter, drive and FM index are the difference between a
      space of waveforms and an instrument. Note the drive stage must be
      oversampled or it will do to the aliasing figure what the soft clip did
      (`docs/m2-notes.md`).
- [ ] **FM and sync inputs** on J1 and J2, which are wired to nothing.
- [ ] **Wrap** on the panel. Implemented and seam-tested in the core
      (`core_check`: wrap seam midpoint and seam continuity), not reachable
      from the module.
- [ ] **Sphere is not implemented at all.** This line used to claim it was.
      `Topo::Sphere` is an enum value that `Space::Attach` accepts and that
      `FoldAxis` has no case for, so it falls through to Clamp silently. Worth
      more than it looks: with every axis on a sphere chart the space is S³,
      and an isoclinic rotation — two *complementary* planes at equal rate,
      plane indices p and 5−p — fibres it by Hopf circles. Every orbit closes,
      every pair of orbits is linked, and the Hopf map hands you a 2-sphere
      whose every point names one closed timbral loop. Measured against the
      real `Rotation`: angle spread across 200 points of S³ is 2e-7 for a
      complementary pair against 1.0 for two planes sharing an axis, and the
      orbits stay Clifford parallel to four decimals. The coupling already
      seeks this, since 1:1 is the widest tongue. See docs/worlds.md.

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
- [x] **Three vertex worlds** — 24-cell, 16-cell and tesseract. The tesseract
      puts its vertices on the cube corners, so axis-aligned motion aims
      straight at them, which is the most direct form of the lock.
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
      on harmonics rather than between them. The ratio-lock machinery now
      exists (`Rotation::SetCouple`), so this is the remaining half: a rate
      mode that multiplies f0 instead of seconds.
- [x] **An FM axis** — `core/kyk_fm.h`, world 9. It was the right guess.
      J_k(I) does not factor and the sideband positions move with the ratio,
      so it is the first legible world that is not separable: 1.83x direction
      spread against 2.1x to 6.1x for everything else legible, and twice the
      variety of any world at any price. Bessel amplitudes by Miller's
      downward recurrence, 0.84 µs an evaluation.
- [x] **A formant world** — `core/kyk_formant.h`, world 10. Three resonances
      chained so the first axis moves all three, which is what keeps it from
      being three separable filters. Lands closest to real material of
      anything here. The peaks ride the pitch rather than staying at fixed
      frequencies, because a World is told nothing about f0 on purpose.
- [x] **A waveshaping axis** — `core/kyk_shapes.h`, worlds 11 and 12. Fold,
      phase modulation and ring modulation, running on the rendered cycle
      because a folder has no closed form in the harmonics. Aliasing at full
      depth: fold -32.2 dB, phase modulation -62.1, ring mod -59.3, against
      -67.5 dry.
- [x] **Real waveforms.** The instrument could not produce a recognisable saw
      or square, and it was the representation, not a missing world: random
      phase renders a saw's spectrum at 0.79 correlation to a saw. Sine phase
      plus signed coefficients gives 1.0000 at a *better* crest factor. Opt-in
      per world, so nothing existing moved. See docs/worlds.md.
- [ ] **The other worlds are still phase-blind.** Braids, Stack, the vertex
      worlds and the fields all still render at random phase, and the vertex
      worlds in particular put a "saw" and a "square" on their vertices that
      are neither. Switching them over is not free — their spectra are
      magnitudes, so a triangle or a pulse would need signs adding — but the
      24-cell was built to give exactly the lock-onto-a-shape behaviour that
      phase is currently denying it.
- [ ] **General MIDI world**, the big one. Sampled instruments are not single
      cycles, so it needs pitch tracking and cycle extraction before analysis.

## Boot-path rules, learned the hard way

- [x] **Nothing in SDRAM may have a default member initialiser.** `.init_array`
      runs before `main()`, and `main()` is where `hw.Init()` brings up the FMC
      that makes SDRAM addressable. A non-trivially-constructible type in
      `.sdram_bss` gets a constructor call from there, which stores into a
      controller that is not up: bus fault, hard fault, dead on every boot
      before a line of our code runs. `solids::VertexTable` shipped that way in
      0.3.0 and bricked the module. Now guarded by a `static_assert` at the
      point of declaration in the shell, so it is a compile error rather than a
      brick. Internal SRAM (DTCM, AXI) is fine — it is live at reset, which is
      why `gEng` and `gWorlds` never had the problem.
- [ ] **Nothing else checks the boot path.** The desktop build cannot catch
      this class at all: the same object is ordinary memory there, every test
      passes, and the first sign of trouble is a module that will not boot.
      Worth a `tools/` script that greps the ELF's `.init_array` reach for
      stores into `0xc0000000`, so it is caught in CI rather than in the rack.

## Known warts

- [ ] **v/oct is read at control rate, not audio rate.** `hw.cv[0].Volts()`
      goes through the SDK's one-pole `AnalogControl`, updated on the 1 kHz
      control poll, so an audio-rate signal into v/oct is undersampled and
      smeared before the engine ever sees it. Audiothurgist reads the ADC DMA
      buffer directly (`hw.seed.adc.GetPtr`) for exactly this reason. Until we
      do the same, audio-rate pitch modulation will not track cleanly however
      much CPU we free up.

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
- [ ] **A control frame at the exact centre of the cube cannot be rotated.**
      The rotation pivots about 0.5 on every axis, so the centre is a fixed
      point of every rotation: the angles turn and the position does not move.
      Gravity is exempt, since it adds an offset rather than turning the
      frame. Unreachable from a physical pot but easy from the page or a
      script, and it looks exactly like a broken orbit. No fix proposed; a
      pivot anywhere else would be worse.
- [ ] **The FM world's variety is 13.3**, twice anything else. Its worst
      one-render step is proportionate and shows no discontinuity, so it is
      not a defect, but a space that changes that fast may be hard to steer.
      Only playing it settles that.

## Web

- [x] Orbit trail and the rotation arcs. The Kepler body gets its own inset
      showing the conic in its own unrotated plane, and the coupling lock has
      a bar beside the plane gauges.
- [x] The page evaluates the FM and vowel worlds itself, so both draw a
      terrain rather than a blank field. Their formulas ship over GET_BASIS
      behind a 0xFF marker where a dimension count would be, so a page that
      predates them draws nothing rather than garbage. The web selftest
      compares the page's evaluation against the module's own spectrum in
      decibels, which is the only comparison the wire can settle.
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
