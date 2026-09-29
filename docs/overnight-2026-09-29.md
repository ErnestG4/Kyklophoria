# Overnight, 29 September 2026

Combust, 00:50: "please run an overnight with 30 minute checkpoints and work
on getting each webserial page separated, finish out all the exciters, and
continue on the roadmap. I can always help tweak things after the fact or test
old builds along the way and see if we get lost."

Rules as the last two: every item a test that fails without it, `make test`
green, a commit; nothing pushed; nothing outside Kyklophoria and ModalBake.
And one learned yesterday the hard way: **anything that makes sound is checked
for level — a re-strike, fast playing, every velocity — before it is offered**
("C1 may have just deafened me": a trained re-strike ran 300-690x; fixed in
aa9e48d, with a brake on any runaway voice).

## Where it stood at 00:55

- Two firmwares (`make MODE=wavetable|modal`); the page shows the connected
  firmware's controls (230913f). Combust is testing the wavetable one.
- The modal firmware's Exciter page (Recorded / Hammer / Pluck / Trained),
  format 9 (each point's trained hammer), the Iowa grand trained and baked
  (ModalBake out/card-exc). Firmware aa9e48d fixes the re-strike runaway.
- Open: the trained hammer's **pop** at every note — the weight correction
  lifted high modes up to 512x, all starting in phase; the first 3 ms carry
  up to 13.6 dB more high band than the recording (C2). Agreed fix: the
  correction cuts only, never lifts, and the trainer's loss hears the onset at
  1 ms. The edit did not apply the first time (a text match); nothing changed.

## The list, in order

1. **The pop.** Cut-only weights and the 1 ms onset term in excfit; C2 and C4
   against the pop measure (first-3 ms high band vs the take); then the grand
   retrained, smoothed, baked; the card rebuilt; its level checked (every
   note, every velocity, re-strikes) before it is offered.
2. **Separate pages for the two firmwares.** One page for the wavetable
   firmware and one for the modal, each with only what its firmware does,
   sharing the link and the drawing code; the published site with both and a
   choice between them.
3. **The sustained exciters on the modal firmware**: bow, reed, lips (the
   desktop prototypes, core/kyk_exciter.h) driving the newest voice, the
   energy from the velocity axis every block; on the Exciter page; tested for
   level (a bow pushed hard, a reed over-blown) and for passivity.
4. **Roadmap**: the modal firmware's room (its unused wavetable code out),
   the trained exciter's treble and ff, J1 as a force into the contact.

## For the morning (needs Combust)

(Drafted at 03:58, refreshed at the end.)

**To try, in this order:**

1. **The modal firmware** (`shell/alchemy`, `make MODE=modal`, build at the
   end) with the card **ModalBake/out/card-exc5/kyklophoria**, the
   level-safe trained grand. Not `card-exc` from yesterday: its trained
   world still fails the level gate through tonight's engine (24 strikes
   over, fast playing 4.06). If you did load yesterday's card, the engine
   now holds its hammers to physical ranges; it's safer, but still not
   what to judge.
   - The pop, against the recordings with each onset on the note itself:
     medians -1.6 / -0.3 / +1.7 dB (bass / mid / treble), yesterday's +3.7
     / +5.2 / +2.5. The worst tenth are still +5 to +9, and pianissimo is
     the brightest layer (+2.2 median, +9.3 p90). Listen to soft notes
     first.
   - To A/B: **ModalBake/out/card-exc5-soft/kyklophoria** is the same grand
     with the 19 brightest keys (over +6 dB at any layer) on their recorded
     attack. It's gated the same (0 of 650, fast playing safe) and its worst
     tenth is +3 to +5 dB.
   - Before flashing, you can listen to `build/demos/grand-{recorded,trained}-v20|v55|v90.wav`.
2. **The Exciter page's new types**: Bow, Reed, Lips (between Pluck and
   Trained; Trained is still the default). **Play P4 (velocity) is the
   energy**: turn it down and a bow lifts. Timbre is bow pressure, the
   reed's embouchure, or the lips' register (four quarters). Mass is the
   reed's impedance or the lip's Q. Demos are in `build/demos/`
   (bow/reed/lips phrases). J1 now plays *into* a bow or reed as a force.
3. **Both pages**: `?page=wavetable` and `?page=modal` (or `modal.html` /
   `wavetable.html`). The pages branch is rebuilt at the end.

**Decisions:**

- **What's left of the pop** is a few keys (the worst tenth +5 to +9 dB)
  and pianissimo a little bright. Two trainer options were tried on six
  notes and left off: a real felt's exponent (mixed), and a hammer-length
  contact floor (the contacts became a hammer's, 1.7-4 ms, but the pop no
  better, and four strikes failed the gate). Your ears on card-exc5 decide
  whether it needs more.
- **Refitted records are misaligned** 5-12 ms with their bursts (the
  training guide's finding, confirmed). It's fixed for future refits;
  existing refitted sets need re-bursting and re-exporting, which changes
  worlds on the card.
- **J4 as a gate** for the sustained exciters: energy alone articulates
  them today.
- Still waiting from before: the dark stereo circle and trail on a
  wavetable world (a screenshot, the world, whether the ground is dark
  too); the EP-bass refit; Epi's licence line.

## Log

- 00:58 — list written; check-ins at :13 and :43, wrap-up 07:52.
- 01:00 — the pop, part one (ModalBake 6e6b3a2): cut-only weights, the 1 ms
  onset term, the hammer's mass bounded 5-15 g and a width. C1 +8.7 -> +2.2
  dB of early high band over the take; C4 about +1; C2 still about +8.
- 01:03 — the grand retrained with them (iowa2), gated for level.
- 01:20 — item 2 done (441889e): `?page=wavetable` and `?page=modal`, with
  `modal.html` and `wavetable.html` forwarding to them; a banner when the
  connected firmware is the other one.
- 01:35 — **iowa2 refused by the level gate**: 222 of 830 strikes over twice
  the recorded peak, all from MIDI 67 up. The card before it fails the same
  gate (132); it predates tonight. RMS matches the recording (1.1-1.5x); the
  peak is 5-9x: a smooth 2 ms bump at the contact. That is the pop.
- 01:45 — item 3 done (83dbf80): Bow, Reed, Lips on the Exciter page
  (between Pluck and Trained; Trained stays on top, so the default and old
  presets are unchanged). Each driven by the velocity axis every block,
  under a limiter at 0.8. What it took to play a fitted piano, not an ideal
  string: the bow's force inside Schelleng's window; a string's losses (the
  fundamental must be the least lossy mode, or the bow takes the octave);
  a bore's losses for the winds (Q 30); only the note's harmonics driven
  (ghost modes, the soundboard and body modes made the reed relax at 340 Hz
  on a C5 and the lips play 100 cents sharp). Tuned within 40 cents across
  C2-C5; 216 level corners bounded; seven mutations each caught.
- 01:50 — **the pop's cause** (e6a4a58, ModalBake 709fb76): every mode was
  pushed the same way, so all started in phase and piled into a pulse. Each
  mode now takes its recorded polarity, sign(g cos phase). The contact can't
  tell; the output can. D5 7.1x -> 1.3x the recorded peak before
  retraining; two notes retrained with it: G4 0.8x, D5 1.6x, loudness 1.1x.
  The grand is retraining with it (iowa3, launched 01:49).
- 02:00 — **a sustained voice let go came back at the wrong level** (285eb8e):
  the fast-playing level check caught a reed coming back 3.3x louder when the
  next note took the exciter (3.35 peak). The same hand-over as the trained
  contact's now. Lifted and put back: a step of x1.24 at most.
- 02:08 — **J1 into the loop** (b362be1), item 4's third part: bowed, blown
  and struck notes hear J1 as a force at the contact. Two runaways found on
  the way by the level checks (J1's gain over the limiter; a reed opening
  without limit at C6) and fixed.
- 02:21 — iowa3 **refused by the gate again**, 131 of 830 (from 222). What
  was left: the treble's pop as a smooth 1.5 ms bump the loss could not
  hear, and an A3 whose near-rigid trained felt ran away struck again (6.97,
  the brake caught it).
- 02:35 — fixes: the trainer hears the onset's peak (ModalBake 461c1a9; six
  treble notes back to the recording's peak), a contact step's impulse
  capped at an elastic collision's, ghost modes not struck (109309a). The
  grand is retraining with all of it (iowa4, launched 02:32).
- The modal firmware's room: **not done, on purpose**. It is at 99.30% SRAM
  (3.4 KB free). A `KYK_WAVETABLE` switch would take out about 35 KB (the
  sine table and morph world, 8 KB each; the spectra, shapes, lattice and
  aim search, about 20 KB). But the oscillator and the resonator share one
  render path, and the page's telemetry reads the rendered frame on resonate
  worlds too. So it wants a modal-only desktop build checked against the
  page, not a 2 am edit.
- 02:43 — check-in. iowa4 training (note 39 of 108, about 03:25 to go).
  Both firmwares build (wavetable SRAM 82.68%, modal 99.34%). Demos of the
  bow, reed and lips on the Iowa grand are in `build/demos/` (not in git).
  The sustained loops cost about 20 M7 instructions a mode a sample, about
  10% of the block at one voice's 48 modes and 2.5% at four voices' share.
- 02:53 — **the modal firmware's room, done after all** (c499ad5). A closer
  look showed a resonate world's spectrum was always silence, so the switch
  only has to leave out what never sounded there. `KYK_WAVETABLE=0` in the
  modal build: every world evaluates to silence and the frame transform is
  not linked. SRAM 99.34% -> 94.83% (about 22 KB back); the wavetable
  firmware is unchanged. Proven in the suite: every resonate golden bit for
  bit and its telemetry line for line through the modal core (a first try
  moved the telemetry's kcut, and this check caught it), and a wavetable
  world through it is silence. One consequence to know: a wavetable world
  sent to the modal firmware now plays silence.
- 02:53 — check-in. iowa4 at note 66 of 108; waiting on it, then its level gate.
- 03:08 — iowa4 **refused again, but closer**: 54 of 830 (raw, unsmoothed,
  28). What's left: a C2 whose search found a corner that is no hammer (K
  4.4e13, mu 30, 0.017-29.6 m/s) and ran to 15-19x struck again, and the
  smoothing undoing some of the onset term's work. Fixed: excfit searches
  only a hammer's ranges (ModalBake c3f8eea), and the engine holds any
  stored hammer to them, so a card baked earlier is safe too (6d92e44).
  iowa5 launched 03:09, gating both the smoothed and raw bakes.
- 03:21 — check-in. iowa5 training (note 48 done).
- 03:43 — iowa5 (a hammer's numbers bounded) refused smoothed (58) and raw
  (47), on different handfuls of keys. **A level-safe card now exists**:
  ModalBake tools/excpick.py takes each note's hammer from the first bake in
  which exclevel passed it (70 smoothed, 15 raw), and 36 and 40 keep their
  recorded attack. Gated whole: 0 of 810 strikes over. exclevel now also
  plays fast (2000 strikes, 4 voices, 5-40 ms): 2.47 against the recorded
  2.39. The card is **ModalBake/out/card-exc5/kyklophoria** (db782f8).
  Yesterday's trained world still fails through tonight's engine (24 over,
  fast playing 4.06), so use card-exc5, not card-exc.
- 03:50 — the pop against the takes (the first 3 ms's high band):
  bass +3.1 dB median (yesterday +10.5), mid +1.9 (+7.7), treble +4.1
  (+5.4). By layer it's now a pianissimo problem: pp median +8.9 dB, ff
  -0.1. A real felt's exponent (alpha >= 2.2) was tried on six notes and was
  mixed; it's an option, not the default (ModalBake 3be2a2b). The trainer is
  resting here: the card is level-safe, and ears on it will say more than
  another retrain. On the module, Exciter page Timbre (the felt's
  stiffness) turned down softens every trained attack.
- 03:55 — ModalBake housekeeping from the training guide's findings:
  `make check-runtime` passes again (runtime/kyk_resonate.h refreshed,
  5cd2091); refitn's misalignment confirmed (a refitted banjo record 9.7 ms
  off its burst) and fixed for future refits, but the records already
  refitted need re-bursting and re-exporting, which is Combust's call
  (ModalBake fix committed); modal-mode.md's region sizes corrected
  (156d8ba). A/B renders of the new card, trained against recorded at three
  velocities, are in `build/demos/grand-*.wav`.
- 03:56 — check-in. Card-exc5 level-safe; the pp pop is the open part of item 1, being looked at.
- 04:01 — **correction to 03:43**: the pp "pop" of +8.9 dB was mostly the
  measure. Its onset (3% of the peak) landed on the pp takes' quiet
  precursor, 6-8 ms before the note, while a trained strike sounds at once.
  With the onset at 10%, card-exc5 against the takes is bass -1.6, mid
  -0.3, treble +1.7 dB (yesterday +3.7, +5.2, +2.5); by layer pp +2.2, mf
  -1.8, ff -2.8. Also found: the trained contacts are far shorter than a
  hammer's (C3 ff two samples). A contact floor made them a hammer's
  (1.7-4 ms) without helping the pop, so it's an option, off (ModalBake
  2ad5758).
- 04:02 — card-exc5-soft written for an A/B (ModalBake 181571f): 19 brightest keys on their recorded attack, 65 trained, gated safe; worst tenth +3 to +5 dB against the takes.
- 04:21 — check-in. The list is worked through; two level-safe cards for an A/B; the rest waits on ears or a decision (For the morning). Holding for the wrap-up at 07:52.
