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
      four principal components. The corpus is *analysed* now even though no
      world is baked from it: `tools/importfit` runs all 48,007 waveforms
      through the page's own import path, and the answer is in
      docs/importfit-2026-09-13.txt. Short version: the band limit is not the
      problem and the sine-only projection is.
- [ ] **Import discards the cosine half, and that is the binding constraint.**
      Measured over the whole AKWF corpus, all figures as a fraction of the
      file's own energy: import keeps a median 95.2% and a p10 of 74.9%
      against a K=64 ceiling of 100.0% and 96.0%, and the rendered wave
      correlates 0.976 with the file at the median and 0.865 at the p10
      against 1.000 and 0.980. One waveform in six comes out under 0.90; with
      phase it would be one in thirty-three. This is
      not a CPU problem — `RenderFrame` already takes a phase per harmonic and
      builds a conjugate-symmetric half-spectrum, so a per-node complex
      coefficient is the same butterflies. What it costs is 2x the storage per
      node, 2x the multiply-adds in the blend, and the thing the shared phase
      spectrum was chosen to make unreachable: partials that cancel when two
      nodes are blended. Every other wavetable synth has that, because
      crossfading frames *is* that. Before building it, extend `make armcost`
      to the blend loop — nothing has ever measured render against blend, and
      "small next to an FFT" is a guess until it is counted.
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
- [x] **A phase-correct vertex world** — `Lock`, world 13. The 24-cell with
      real waveforms and one family per Givens plane, so which plane you align
      on decides what kind of thing you find. Added beside the old 24-cell
      rather than replacing it, per Combust: add rather than change.
- [x] **Stacks of saws** — `Unison`, world 14, from the original brief. Voices,
      interval, detune and the copied waveform. Non-integer roots deposit
      across two bins, so a stack detunes continuously instead of stepping.
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

- [x] **Modal worlds** — Plate, Bar and Drum, worlds 15 to 17. Physical models
      run as formulas, never as integrators. Frequencies are harmonicised, so
      these are tuned objects in the sense a vibraphone maker means it; the
      physics that survives is the amplitude structure, which is where a struck
      object's character lives anyway. Plate measures 2.28x, the most isotropic
      legible world here.
- [ ] **True inharmonicity** still needs the M-period frame or a per-partial
      additive renderer. A circular membrane's modes sit 25.5% off the harmonic
      grid, over four semitones, so no amount of baking reaches it.

- [x] **Single-shape worlds** — Saw, Pulse and Edge, worlds 18 to 20. One
      waveform and four ways to bend it, rather than one world trying to span
      everything. Combust's idea, and it fixes the recurring problem that a
      broad world's axes fight each other. Saw measures 2.03x, the most
      isotropic legible world here.
- [x] **The Kepler body was being integrated into the control frame.** `Apply`
      added the offset into `c_`, which is persistent, and the slew is a rate
      limiter that removes at most `block_ms/slew_ms` per block — 0.1 at the
      shipping default, against an orbit radius knob that reaches 0.55. So
      `ctl` diverged: measured -156 after four seconds, with the position
      pinned in a cube corner. Every position the module reported while Kepler
      was running was noise. The offset now goes to a copy. Invisible on the
      desktop, which leaves the slew at zero.

## Audio-path rules, learned the hard way

- [x] **Never call `SinCosTurns` per sample.** It computes sine and cosine,
      interpolates both and runs a Newton orthonormality step, because it
      exists for rotation matrices. In the wavefolder that was ~30 VFP
      instructions in one serial dependency chain, around 150 µs for a
      1024-sample call against a 500 µs block — and 2.7 µs on x86, which
      reorders around the chain. A raw table read is ten instructions and cost
      nothing measurable in aliasing.
- [ ] **Desktop timings do not predict the M7 for anything with a dependency
      chain**, and the gap here was an order of magnitude. `make armcost`
      counts instructions the target compiler emits for the audio-path inner
      loops; use it before claiming an audio-path change is cheap. It does not
      model latency or memory, so it is a smell test, not a budget.
- [x] **Every axis of every world must be continuous**, and `tests/cont_check`
      now proves it for all seventy-two of them. A threshold on step size
      cannot tell a cliff from a steep slope, which is why every earlier test
      passed a wavefolder that stepped the band limit by a whole harmonic.
      Sweeping the same axis at two step sizes can: a continuous function
      halves its largest step when the step halves, a discontinuity does not
      move. It caught three shipped bugs within an hour of existing.
- [ ] **There is still no cycle number from the module in any test.** Every
      CPU claim in this repo is a desktop proxy. `GET_STATS` reports cycles
      and the page shows them, but nothing records them, so a regression is
      only ever caught by someone playing it.

## Link

- [x] **Connecting twice over serial.** Not module state: a link that does not
      close tidily leaves the module's parser holding half a frame, so the
      first HELLO is appended to it, delimited into one malformed frame and
      dropped — correctly; that is the resync path in the SDK's `frame.h`.
      Nothing answers, HELLO times out, and clicking Connect again works
      because the failed attempt's own delimiter cleared the accumulator. Hence
      often twice and never three times. `Link.start` sends a lone zero first
      (the documented "throw away what you have"), `hello` retries, and the
      page raises DTR on open. The selftest writes half a frame into kykdesk
      before starting the link and checks HELLO answers first time and without
      burning a retry: 522 ms and one timeout without the fix, under a
      millisecond with it.

## Web, legibility

- [x] **The knob mirror drew the pot, not the value.** The panel catches, so
      arriving on a page leaves each pot where the hand left it while the
      parameter keeps its value — meaning the mirror was showing six numbers
      that were not in effect on every page you had just arrived on. Telemetry
      now carries `u8 vals_valid · u8 vals[6]` from `pager.Value(pot)`, and the
      mirror draws the value as the knob with the pot as a tick outside the
      ring. When they agree it reads as one mark; when they do not, the gap is
      how far there is to turn before the knob takes hold.
- [x] **Text contrast, measured rather than eyeballed.** `--dim` was 3.51:1
      against the background — under the 4.5 readable floor — and it carries
      the world bar, the footer, the build row and every state line. The canvas
      was worse: captions 3.64, the card fit 2.76, the empty-state hints 2.11,
      the pages-you-are-not-on 1.72. All lifted in place with hue and
      saturation kept, `--dim` to 7:1, nothing text under 4.5:1.

## Worlds, named and described

- [x] **The world called Braids is called Crop.** It is four principal
      components of Emilie Gillet's 256-wave bank and sounds like neither the
      bank nor the module it came from, so naming it after somebody else's
      instrument was both inaccurate and a liberty. The credit is in the note,
      in THIRD_PARTY.md and in the README, which is where it belongs.
- [x] **Every world says what its axes do.** The notes were labels — "eigenspace
      of a 256-wave bank" says where it came from and nothing about what
      turning a knob will do. They now name the four axes and give the formula
      where there is one: exp(-d^2/2s^2) for the vertex blends, the Bessel
      ranges for FM, the tilt exponents for the bend worlds, the chaining for
      Vowel. About 2.9 KB across twenty-one worlds, which took the list from
      one page to four — the paging was built for this and had never carried
      more than one page in anger, so the selftest now walks it and checks
      every world arrives exactly once.
- [ ] The notes are one string each, so the page cannot lay out an axis list as
      a list. A per-axis array in the world entry would let the panel mirror
      name position 0 to 3 for the world you are actually in, which is the
      thing the panel cannot currently tell you.

## Hearing it

- [x] **A/B the same waveform three ways.** The build view plays the selected
      node as imported, band-limited to K, and as the module will render it —
      all three looping the same buffer length at rate 1.0, started together so
      they stay sample-aligned, switched by gain with a 4 ms ramp so the switch
      itself does not click. Levels matched, because the quieter of two sounds
      is reliably judged the worse one and the projection genuinely loses
      energy. Imported against band-limited is what K costs; band-limited
      against rendered is what the *phase* costs, which is the question the
      whole representation argument turns on.
- [x] **The page had the render upside down.** `RenderFrame` sums
      mag·cos(hθ+φ), and for a sine-phase world the engine's φ makes that
      *minus* sin(hθ). The page summed plus, so the inspector drew every
      rendered trace mirrored against the imported one and every fit looked
      worse than it was. Inaudible on its own; wrong everywhere it was used.
      Now pinned twice: `selftest` sends a world of known coefficients and
      correlates the frame the module returns, `pagecheck` holds the page to
      the same formula. Flipping either sign gives corr −1.0000.

## The overnight reviews, 2026-09-13

Five agents, scoped to the page, the firmware, the DSP core, the untrusted-input
surface and the documented claims. What was fixed the same night:

- [ ] **REVERTED — the Morph knob at the very top can render digital silence,
      and that is the lesser problem.** The "fix" below was applied and taken
      back out the same day, on Combust's instruction, because the hard cutoff
      it removed is the behaviour the knob exists for: "smooth blend to hard
      steps" (README). Written as a numerically stable lerp, the small corner
      weights survive as w², normalise back up, and the top of the travel
      merges worlds instead of stepping between them — reported from the rack
      as "it just merges them even at 100%" and "can't handle variance in
      planar impacts". The silence corner stays open, and is preferable.

      If anyone closes it later: **do not change the expression.** Catch the
      all-zero case where it is already detected (`if(sum <= 0.f) return;`) and
      fall back to the largest weight instead of returning silence. That is
      what infinite sharpening means, it leaves every non-degenerate position
      bit-identical, and it was the fix that should have been written first.

      The original write-up, kept because the mistake is the useful part:
      `SharpenWeights` lerped as `w + (w2 - w) * f`, and once `w²` is negligible
      beside `w` the subtraction rounds to exactly `-w`, so the expression
      evaluated to zero. Where that zeroed *every* corner weight, `sum` came out
      zero and the blend wrote an all-zero spectrum. Fixed to `w·(1−f) + w²·f`,
      which never subtracts two nearly equal numbers.

      **Scope, measured over 500 random positions per world**, because the first
      write-up of this overstated it badly and Combust's own impression that
      morph worked well was the more accurate report:

      | | silent at sharp = 1.0 | at sharp = 0.99 |
      |---|---|---|
      | the five lattice worlds | 0.4% to 1.8% of positions | 0.0% |
      | the sixteen formula worlds | 0.0% | 0.0% |

      So it needed the pot at its absolute top — `VirtualKnob::Norm()` clamps at
      exactly 1.0f, and 0.99 is clean — on one of five worlds, at about one
      position in a hundred. A dropout you would hit occasionally and blame on
      the patch, not a knob that mutes the instrument.

      `SharpenWeights` is reached only from the lattice branch
      (`kyk_world.h:355`). The other sixteen worlds each handle `sharp`
      themselves, and `EvalLock` narrows sigma — so "Morph tightens the lock" on
      the 24-cell and Lock, which is where the knob is most musical, runs on a
      path this bug never touched.

      One correction to the commit that fixed it: it says Torus survived. That
      was a single test position. Across the space Torus is affected too, at
      1.8%, the worst of the five.

      What went wrong in the fixing, beyond the code: the silence was measured
      and the thing the line was *for* was not. A rate of 0.4% to 1.8% looked
      decisive on its own, and it is not a number that can be decisive without
      knowing what the other 98% is doing musically. Combust had played this
      for weeks and had not met the corner; the behaviour removed was noticed
      within minutes of a flash. When a review finds a bug in a line somebody
      plays every day, the question to answer first is what the line is doing
      when it is not failing.
- [x] **No test had ever set `sharp`.** `grep sharp tests/*.cpp` returned
      nothing, which is why the above shipped. The Morph knob is a control a
      player turns and it was outside every sweep.
- [x] **GET_BASIS rewrote the playing world.** `Basis()` built into the same
      `VertexTable` the live world holds pointers into, so *reading* another
      world's formula moved the playing world's partials by up to 22 magnitude
      steps, ~8.8 dB. Its own table now, and a selftest check.
- [x] **The desktop descriptor still said fw 0.2.0** while HELLO said 0.4.0 —
      the exact drift `KYK_FW_VERSION` exists to prevent, in the one place it
      was not used.
- [x] **The page's frame parser had no cap.** 4 MB of undelimited input took
      the heap to 96 MB and killed the tab while the module was fine. The SDK
      parser it was ported from drops the chunk and resyncs; the port kept the
      accumulate half. Capped, with a check that a real frame still parses
      immediately after a flood.
- [x] **Two connects in flight left you with neither.** The loser's catch tore
      down the winner's link. Generation counter.
- [x] **`space` was refreshed only by `selectWorld`** — the sixth instance of
      the pattern — so a panel switch, a card load or a send left the play view
      drawing the previous world's lattice under the new world's name.
- [x] **The inspector drew imported against rendered as mirror images**, a
      regression from the same night's polarity fix: one side was negated and
      the other was not. Nothing compared the two traces the view actually
      draws; now something does.

Still open, ranked, from the same reviews:

- [ ] **`SetMorph` is called every block and sets `dirty_` unconditionally**,
      which defeats the render deadband entirely: 10,000 renders per 20,000
      blocks against 2 with the deadband alone, on a still patch, whether or
      not a morph target is armed. The module renders at the maximum rate the
      divider allows, permanently — and the 33% average / 97% max reading the
      whole CPU question is blocked on was measured with this in place. Fix and
      re-measure before trusting any CPU number.
- [ ] **The morph target is rebuilt in place under the audio ISR**
      (`main.cpp:759-778`). `gMorphIdx` stays valid throughout, so the ISR
      keeps reading a lattice being rewritten: ~10 ms of wrong sound at full
      level on a tabulated target change. Clear the index and `dmb` first.
- [ ] **The aim offset survives a change of morph target** in both shells:
      `SetMorph` clears it on the pointer changing, and both shells pass the
      address of one static World, so the guard can never fire. The morph then
      reads the new world at coordinates searched against a different one, and
      telemetry reports `aimed`.
- [ ] **Two `putWorld` transfers can interleave.** `sendImported` and
      `syncPlacement` both send urgent chunks and only the latter coalesces, so
      a drag-drop plus a send button within the same second refuses one
      transfer mid-stream and can leave the success message on screen.
- [ ] **In the build view the footer may be auto-placed into row 2**, above the
      pane, because the nav that held that row is hidden. Derived from the grid
      spec, not seen in a browser — open the build tab and look.
- [ ] **Stale readouts after disconnect**: block, f0, kcut, link stats, space,
      morph state and the card and morph selects all keep the dead module's
      values and read as live.
- [ ] **The poll loops duplicate across a quick reconnect**, since they test
      `polling` only after sleeping and nothing ties a loop to the link that
      started it.
- [ ] **`Space::Attach` validates the header and never the coefficients**, so
      one non-finite or merely huge float in a `.kyk` NaNs the whole output.
      Desktop-only today; live the moment the card path reads `.kyk`.
      `ParseUserWorld` has the finite check that this one lacks.
- [ ] **32-bit overflow in `Space::Attach`'s size check**, demonstrated on the
      target compiler: `point_count · stride · 4` wraps, so a 64-byte file can
      claim 4 GB and pass. Not reachable until the card reads `.kyk`.
- [ ] **`alias_check` proves −88 dBFS for one world at one position.** Over all
      21 at three positions, Field II measures −76.3 dBFS, and a wider scan
      finds −67.5 — past the suite's own −80 limit.
- [ ] **`putWorld` reads `link.maxBody`, which is never assigned**, so it
      always chunks at 1024 regardless of what the module negotiated.
- [ ] Audition loose ends: re-place clears the selection while the sound keeps
      playing, gain nodes are never disconnected, and switching to the play tab
      leaves it running with its stop button on the other tab.
- [ ] **Does `cycleToNode` have the sign right?** The engine plays
      −Σ m·sin(hθ) and the projection stores +sine coefficients, so the module
      plays every imported wave inverted. Inaudible in isolation, and changing
      it would invert every `.kykw` already written. A decision, not a bug.

## The library, and the card

- [x] **Thirty-two slots**, blobs in SDRAM, expanded on demand. A world you made
      can now be played, returned to, and be *either end* of a morph — the
      target index space was the twenty-one built-ins, so nothing you made could
      be one before.
- [x] **Drag a slot onto another to exchange them.** On the module, because a
      host does not have the blob for a slot it did not store. `live` and
      `target` follow the contents rather than the numbers.
- [x] **The card is writable.** `/kyklophoria/<name>.kykw` through a temp file
      and a rename; refuses an existing file unless overwriting is asked for;
      the page asks and offers a suffixed name. Synchronous on the module,
      unlike a card *read*, because a save touches no audio state.
- [x] **The desktop shell has a card now** (`--card <dir>`). The whole card path
      previously had no desktop implementation at all, so reading was untested
      and writing would have shipped the same way — which is not a thing to do
      with the first code here that can destroy somebody's file. It caught a
      real hole immediately: the name check was in the module and not in the
      desktop, and `../escape` wrote a file outside the card directory. Proof it
      was real is that the failing run left one in /tmp. The check now lives in
      the shared handler both shells go through.
- [x] **A blank card gets its world folder made for it** on the first save,
      rather than failing with a generic device error and no hint that the
      directory was missing. Created on the save path and not behind an
      "initialize" the player has to know to press first, because needing to be
      told to press a button first is the same bug with an extra step.
- [ ] **Exporting the built-in worlds to the card is not worth doing, and here
      is the reason so nobody re-proposes it.** `SaveUserWorld` refuses anything
      that is not `Kind::Lock`, and exactly one of the twenty-one is — `Lock`
      itself. FM is a Bessel formula of four parameters, the modal worlds are
      formulas, the fields are 256-point lattices. Writing them as 24 nodes x 64
      coefficients would replace 1.3 KB of formula with 6.5 KB of samples that
      cannot be re-rendered at another K, under the same name, beside the
      original which already ships in the firmware.
- [x] **Capture what is playing**, which is what that idea was reaching for. A
      button on the build page opens the live world as an editable set: read back
      exactly if it came from a slot, sampled at the 24-cell vertices if it is a
      built-in. Needed two things on the wire — 0x6C to read a slot back, and
      action 16 to snapshot — because the page can evaluate FM, Vowel and the
      modal and bend worlds from their formulas but not a lattice world and not
      a Lock world, and "edit the world I just made" is the case that matters
      most.
- [x] **New from Lock**, which is the answer to "should slots be blank-inited".
      They should not: a blank world plays silence, and silence that looks
      deliberate is the worst state a synth can show. A slot is either a world or
      nothing and says which. What was wanted was a *starting point*, and Lock is
      the natural one — twenty-four real waveforms, one family per rotation
      plane. Snapshotted by name so it does not interrupt the patch, which took
      an optional world index on action 16.
- [ ] ~~What that idea is actually reaching for is a starting point to edit~~,
      and that is worth building: snapshot the *live* world into a slot by
      sampling it at the 24-cell vertices, so any world — FM, Plate, Vowel —
      becomes twenty-four waveforms you can hand-edit rather than starting from
      twenty-four imported wavs. Honest label required: the snapshot is
      rendered at sine phase, so a phase-blind world will not sound like its
      original, and for some of them it will sound better.
- [ ] The slots are still RAM: a power cycle keeps only what is on the card.
      Auto-restoring the slots from the card at boot would close that, and is a
      decision about boot time rather than a missing mechanism.
- [ ] Nothing writes a `.kyk` *space* to the card, only `.kykw` worlds — and
      `Space::Attach` has the two holes the reviews found (no coefficient
      validation, a 32-bit overflow in its size check), both of which go live
      the day the card reads one.

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

- [x] **Export a `.kykw` from the page.** A name field and a save button on the
      import row. Send and save build the blob in one place, so the file is
      byte-for-byte what goes on the wire; the name lands both in the char[16]
      header and in the filename, so the file on the card and the name in the
      module's list agree. Checked in `web/pagecheck.mjs` (round trip, naming,
      placement) and in `web/selftest.mjs`, which now puts a page-built world
      through the real C++ reader — the only place in the suite that crosses
      the language boundary, since `tests/user_check.cpp` round-trips the
      reader against our own writer and would agree with itself about a format
      both halves got wrong.
- [x] **The desktop shell was loading a sent world and not telling the engine.**
      `PutWorld` in `shell/desktop/serve.h` wrote `*world` and never called
      `eng->SetWorld`, so the phase convention, the render cache, the
      band-limit hold and the aim offset all still described the previous
      world. Fifth instance of the pattern in CLAUDE.md. Found by asking
      whether the *sound* changed after a send, which nothing had asked.
- [x] **Move imported nodes.** Dragged in the space view with pointer events, so
      a finger works too: a drag sets the two axes on screen and leaves the
      rest, and the other two are reached by switching the pair. Nodes fade
      with their distance from the slice being drawn, which is the only honest
      way for a plane to say "this one is over there". Once the set has been
      sent, a drop re-sends it — coalesced, so a drag cannot pile up 6.5 KB
      transfers — and selecting another world or dropping the link disarms
      that, because the module is no longer holding the set.
- [x] **A build view.** Placement was bolted onto the space pane, which is a
      readout of the module: the rings sat on top of a live position display
      and the import row was a count and a worst fit. Now a tab of its own with
      the set as drawn cards (waveform and per-node fit), two placement squares
      and an inspector showing the imported cycle against the one the engine
      will render, to the same scale. The play view keeps the rings, read-only.
- [x] **The complementary plane, live.** The inset draws the two axes the main
      square does not and is dragged the same way, so with four dimensions a
      node is finished without switching the pair. Everything that draws or
      hit-tests a node takes a box — which two axes, where, how big — so the
      play square, the main square and the inset are three boxes and one piece
      of code.
- [x] **The play view drew the draft, not the module's world.** It is a
      readout, and it was showing the set being edited on the build tab —
      including edits never sent, and sets the module had never seen. The page
      cannot ask what the module holds (the world list says "not a built-in"
      and GET_BASIS gives a lock world's count and sigma with no positions), so
      it keeps a record of what it sent and drops it through one function when
      anything could have replaced it. Found by Combust, not by the suite:
      every check asked what the builder did and none asked what the other view
      said about it.
- [x] **A world switched from the module's own panel is invisible to the page.**
      Closed the proper way: `u8 world` at the end of the telemetry motion
      block, 0xFF for a user world. The page drops the held record and corrects
      the world bar within a frame, and `selectWorld` now waits on the stream
      it is already reading instead of asking for the world list every 60 ms —
      a round trip per attempt, at exactly the moment the module is busy
      expanding a world. One byte a frame. Absent is not 0xFF: firmware that
      predates the field sends nothing, and reading that as a user world would
      libel every older module.
- [ ] Nothing lets you hear a single node on its own. Morph at full narrows the
      basins until a vertex is that exact waveform, so the module can already
      do it; the page would only need to park the position on the node you have
      picked. That is the shortest path from "this card looks wrong" to knowing
      whether it sounds wrong.
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
- [x] **The world-change race in the selftest.** A change is applied on the
      request but only reaches telemetry on the next render, and a render is
      not every block. Two versions of the wait failed intermittently before
      one worked; worse, settling generously made the check *vacuous*, because
      a late render picks up the new contents anyway. Replaced with something
      the bug breaks permanently: arriving at a sent world from three different
      worlds must give byte-identical frames — switch_check's invariant asked
      of the one path that is not a world switch. 0 against 173 of 255.
- [x] **Gestures, not just the functions under them.** `pagecheck`'s stub threw
      listeners away, so every placement check went at `pickNode` and
      `moveNodeTo` directly and the handler wiring a pointer to them had no
      coverage at all — which box was pressed, what is held, when the send
      fires. The stub keeps listeners now and the tests press, move and
      release. It also needed `setPointerCapture`, whose absence meant the
      handler could not have run here even if something had called it.
- [x] **A watch for draws at a coordinate that is not a number.** `arc(NaN, …)`
      is not an error on any canvas — it draws nothing, which is
      indistinguishable from a mark that is off screen — so this class of bug
      shows up only as something quietly missing. `pagecheck` now records the
      geometry calls and fails on a non-finite argument. It caught a node drawn
      on an axis it does not have (a 4-D set placed while the module reports
      6-D) the first time it was armed.
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

**Resonance stays external.** Asked whether Rings' resonator could be a
parameter, an effect axis, or a stage in the signal path. Settled: none of them.
It cannot be an axis — a world is a spectrum rendered as one cycle read
cyclically, and the existing effect axes work because fold, phase modulation and
ring modulation are *memoryless*; a resonator has state and its output is not
periodic at f0. It could technically be a post-oscillator block, and being
linear it would not wreck the aliasing figure the way the rejected soft clip did
— but Combust's answer is the right one and it is a scope decision, not a
technical one: **this is an oscillator, and in a rack every sound it makes can go
through an ADSR into a real Rings.** Building a worse resonator inside a module
that sits two cables away from a better one is work spent to lose.

What the struck bodies are actually missing is not resonance. It is true
inharmonicity — a membrane sits 25.5% off the harmonic grid and a frame periodic
at f0 holds harmonics of f0 and nothing else — and time evolution, since the
"how long ago" axis is a frozen instant of a decay rather than a decay. Both are
open items above, and both are engine work rather than a parameter.
