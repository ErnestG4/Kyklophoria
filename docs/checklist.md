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

## Worlds

- [ ] **World manifest**: one small text file naming corpus, representation,
      extrapolation and parameters, that the tools execute to bake a `.kyk`.
      The runtime needs no change; the format is already world-agnostic.
- [ ] **Plaits corpus extractor.** Plaits generates its tables from
      `wavetables.py` rather than shipping an array, so it needs a few lines
      that Braids did not.
- [ ] **AKWF**, several thousand CC0 single cycles. 256 waves is thin for
      four principal components.
- [ ] **The Erica-like world**: stacked saws and sines on a *logarithmic*
      stack-count axis, which is the only way n×2, n/2 and n±1 all sit on one
      straight line from a point.
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
