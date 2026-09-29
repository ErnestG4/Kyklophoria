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

(written at the end)

## Log

- 00:58 — list written; check-ins at :13 and :43, wrap-up 07:52.
