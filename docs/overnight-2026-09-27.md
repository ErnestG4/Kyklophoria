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
   - [x] runtime: v8 attack references (505021d); Attach refuses a world
         whose bytes run out (7fab4ae — found by the filler's own bug)
   - [x] runtime readout: phases and stage a voice was built with
   - [x] ModalBake tools/worldfill (c794ddc + fix): every semitone struck in
         old and new and heard, loudness within 0.2-1.2 dB
   - [x] tests: resonate_engine_check 21 (v8 references, refusals; clean
         under ASan; faults / reports without the change)
   - [x] filled: out/card8, card-B8, card-r38, card-abc8 — 102 worlds, all
         attach and render. At() at a semitone 1.3 us (blend was 3.2-4.1,
         nearest 1.6)
2. [x] **The piano world's labels** — no change: the fitted modes put
   "G7" at G7 +54 c (a stretch-tuned top, not a G#7; piano030 is the G#7)
   and "A#7" at A#7 +38 c, not a damaged C5. tools/pitchcheck misreads the
   top octave; I passed its reading on as fact on the 24th.
3. [x] **README**: the jack map is the firmware's now (J4 out A/trigger,
   J5-J8 positions, J1 in, J2 clock).
4. [x] **The steal**: Engine::SetReleaseMs (default 40 ms), and a tail
   still sounding kept through the next steal (test 22). A/B build
   kyklophoria-release200.bin defaults to 200 ms.
5. **Stereo points on a resonator** (the fun, now the models are good
   enough): L and R hear the same bank from two strike/pickup positions,
   spread and spinnable like the wavetable's, at a few per cent of CPU.
6. **Instrument morph**: a family's body axis glides between members by the
   partial pairing At() already has, instead of switching.
7. **The 8-world-slot limit** (feedback 2026-09-23): what fails past 8.
8. **Exciters coupled through feedback** (docs/litreview in ModalBake):
   design note first, then a prototype on the desktop.

## For the morning (needs Combust)

- **Flash kyklophoria.bin (3f2773d or later) first** (version 8 worlds need it; older
  firmware refuses them cleanly). Then the **filled** cards: out/card-abc8
  (the A/B/C, every note a point: the pops and overruns on the pianos
  should be gone) — the unfilled card-abc is the same sound with the pops.
- A/B builds beside it (each differs from kyklophoria.bin in one thing):
  kyklophoria-fullintro.bin — the whole recorded intro at every velocity;
  kyklophoria-release200.bin — a stolen voice falls over 200 ms, not 40
  ("I can now hear the voice stealing").
- Pushes: Kyklophoria `modal` and `pages`, ModalBake `bake` (key).

## Log

- 00:55 — list written; v8 references in core/kyk_resonate.h written, not
  yet tested.
- 01:05 — item 1 done. worldfill's first check paired a piano's unison
  pairs crosswise and failed identical worlds; now it renders and listens.
  Its first cut stamped a version 6 world 8 and the runtime walked off the
  end — fixed in the tool, and the runtime now refuses such a world at
  Attach (test 21). Firmware 7fab4ae built; -nearest dropped (a filled world
  never blends at a semitone), -fullintro rebuilt on 7fab4ae.
- 01:40 — item 4: the release a setting and tails kept (a 200 ms release
  at notes 30 ms apart stepped 0.18 dropping the tail; kept, 0.001).
  Firmware 3f2773d with -fullintro and -release200.
- 01:15 — items 2 and 3: the piano labels were right (the fits' own
  loudest partials); README jack map brought up to date.
