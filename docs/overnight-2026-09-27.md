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
5. [x] **Stereo points on a resonator** (ed21c67): Stereo P1 spread = the
   two ears' distance along the string, the stereo plane's angle = where
   the pair sits (orbit it to spin). 1.5x the block with spread up (desktop,
   4 voices), identical to mono at 0. Test 23. Plain worlds only (pickup
   worlds stay mono).
6. [x] **Instrument morph** (2a38f4f): between two members of a family the
   voice is both, paired and blended, built on the nearer; off by default,
   kyklophoria-morph.bin has it on. Test 24 (a sweep steps 0.5 dB where the
   switch stepped 7.7). Different kinds (EP beside Wurlitzer) still switch.
7. [x] **The 8-world-slot limit** (e912838): it was eight 4 MB SDRAM
   regions; now four of 4 MB and twenty of 2 MB, a world in the smallest
   that fits — the whole card (24 worlds) loads at once. tests/regions_check.
8. [x] **Exciters coupled through feedback**: design note docs/exciters.md
   (strike, pluck, bow, reed, lips on one loop; noise tied to the contact;
   the waveguide dropped as a separate path). Stage 1, the bow, prototyped
   and tested (core/kyk_exciter.h, tests/exciter_check): it plays Schelleng's
   diagram by itself. Not wired into the engine: stages 2-4 in the note.

## For the morning (needs Combust)

- **Flash kyklophoria.bin (2a38f4f) first** (version 8 worlds need it; older
  firmware refuses them cleanly). Then the **filled** cards: out/card-abc8
  (the A/B/C, every note a point: the pops and overruns on the pianos
  should be gone) — the unfilled card-abc is the same sound with the pops.
- Load more than eight resonate worlds: all 24 of the card should load now.
- New to try: on a plain resonator (a piano, guitar, harp), turn up the
  Stereo page's spread; set an orbit rate on the stereo plane to spin the
  two ears along the string. Watch the CPU footer with it up.
- A/B builds beside it (each differs from kyklophoria.bin in one thing):
  kyklophoria-fullintro.bin — the whole recorded intro at every velocity;
  kyklophoria-release200.bin — a stolen voice falls over 200 ms, not 40
  ("I can now hear the voice stealing");
  kyklophoria-morph.bin — a family's body axis (Play P3/J5) morphs between
  members instead of switching: on the string families it walks string to
  string, on bass-pizz-vel E -> A -> D -> G.
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
- 01:08 — items 2 and 3: the piano labels were right (the fits' own
  loudest partials); README jack map brought up to date.
- 01:12 — item 4: the release a setting and tails kept (a 200 ms release
  at notes 30 ms apart stepped 0.18 dropping the tail; kept, 0.001).
  Firmware 3f2773d with -fullintro and -release200.
- 01:20 — item 5: two ears on the bank. First cut 2.6x the block; modes
  both ears hear whole now run in the shared paired loop and the weights
  are cached per voice: 1.5x. Mono path untouched (goldens identical).
- 01:45 — item 7 (before 6: a bug before a feature): 24 worlds at once.
  SDRAM 60.8 of 64 MB. The card path itself is module-only; the policy is
  tested pure.
- 02:00-07:15 — lost: the session stopped continuing after a finished
  command, and the half-hourly check-ins queued rather than ran.
- 07:20 — the At()/PairBlend split committed (901d03e, bit-identical);
  firmware 901d03e with -fullintro and -release200. Item 6 (morph) not
  started beyond that split; item 8 not started.
- 07:27 — item 6: the morph. First build overflowed the M7's SRAM by 24 KB
  (scratch voices in both engines of the stereo pair); the shell now lends
  the room, SDRAM on the module. Firmware 2a38f4f + fullintro, release200,
  morph. Next: item 8, the exciter design note.
- 07:45 — item 8: the design note, and the bow on the desktop. A harmonic
  string bowed through the coupled loop finds its own playable band of
  force, plays its fundamental within 2 cents, and its amplitude follows
  the bow's speed — Helmholtz motion. Every item on the list is done; the
  exciter's next stages (the strike through the loop, the reed and the
  lips, the module's Exciter page) are the next list.
