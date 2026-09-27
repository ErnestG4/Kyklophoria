# Overnight, 27 September 2026

Combust, 00:50: "Let's call the models good enough for the moment and continue
the rest of the work as an overnight ... Make sure to write tests and check
functionality as much as possible." Check-ins every 30 minutes until 11:00;
this file is the working list and the log. Every item: a test that fails
without the change, `make test` green, a commit; nothing pushed, nothing
outside Kyklophoria and ModalBake.

## The list, in order

1. **Every note a point (format v8)** — the pops and overruns on the pianos.
   Since f0f451f a note between two recorded points is built from both at
   every strike, 2.7x the old rebuild on the desktop; on the module the strike
   block runs over and each overrun is a pop. Worlds with a point every
   semitone (the Iowa grands) are spared, which is what Combust heard. Fix:
   the export writes a point for every semitone from the same blend, so the
   module plays each note's own point by the cheap path. The new points
   refer to the nearer recorded point's attack (v8: burst count 0xFFFF, then
   the point's index) rather than copying it — the attacks are most of a
   world and copies would not fit the 4 MB slot.
   - [ ] runtime: v8 attack references (BurstPoint, Bursts, BurstEnd,
         Attach accepts 8) — written, untested
   - [ ] runtime readout: the phases a voice was built with, for the filler
   - [ ] ModalBake tools/worldfill: a .kykm in, every missing semitone added
         from the runtime's own At(), v8 out; families member by member
   - [ ] tests: v8 attach and references; a filled world plays at every
         semitone what the unfilled one blended (within quantisation); the
         filled world's At() at a semitone costs what the nearest path did
   - [ ] fill the A, B and C cards (out/card-abc, card-B, card-r3) and the
         main card; check every world attaches and renders through the runtime
2. **The piano world's labels**: its "G7" is a G#7, its "A#7" a damaged
   take (tools/pitchcheck). Fix the manifest, re-export `piano`.
3. **README**: the J4–J8 map is out of date.
4. **The steal**: the 40 ms tail as an option with a longer, natural fade —
   built and tested, default unchanged until Combust has heard it.
5. **Stereo points on a resonator** (the fun, now the models are good
   enough): L and R hear the same bank from two strike/pickup positions,
   spread and spinnable like the wavetable's, at a few per cent of CPU.
6. **Instrument morph**: a family's body axis glides between members by the
   partial pairing At() already has, instead of switching.
7. **The 8-world-slot limit** (feedback 2026-09-23): what fails past 8.
8. **Exciters coupled through feedback** (docs/litreview in ModalBake):
   design note first, then a prototype on the desktop.

## For the morning (needs Combust)

- A/B/C of the refits: out/card-abc/kyklophoria (A the card as it was, B the
  first refits and voicing, C refit against the recordings).
- Pushes: Kyklophoria `modal` and `pages`, ModalBake `bake` (key).

## Log

- 00:55 — list written; v8 references in core/kyk_resonate.h written, not
  yet tested.
