# Overnight, 30 September 2026

Combust, 00:40: "OK please set the overnight timer until 8 am and continue
with the roadmap. One extra thing is that it crackles when adjusting some
settings like knobs 3 and 4 on page 1."

Rules as before: every fix gets a test that fails without it, `make test`
green, a commit; nothing pushed; nothing outside Kyklophoria and ModalBake;
anything that makes sound is checked for level (a re-strike, fast playing,
every velocity) before it is offered. Check-ins at :13 and :43, wrap-up at
07:52.

## For the morning

(written at the wrap-up)

## Where it stood at 00:41

- Bongs at 4ad1855: gates lift a note until the next strike (B2, the pad, and
  the lapse of a gate's hold); the Play tab keeps one hand on each setting;
  Lips speak on the tine, EP and reed worlds and on Guitar at 55 and 60.
- Salamander: 15-20 hurried/s on the module; the full CPU line is asked for.
  On the desktop it is no heavier than Iowa or VCSL.
- ModalBake: the marimba-vel campaign is on its smoothed pass; vibraphone-vel
  follows (tools/exccampaign.sh).

## The list, in order

1. **The crackle on Play P3 and P4** (body and velocity) while turning them:
   reproduce on the desktop against a ringing note, find what steps, fix it
   without costing the M7 a rebuild per block.
2. **The mallet campaigns**: gated for level; results reported, nothing put
   on a card.
3. **Roadmap**: the Salamander load (what the desktop can say about the
   strike block on the M7: `make armcost`, the build path's instruction
   count); the other pots checked for the same crackle as item 1; the
   remaining open items in docs/bongs-controls.md.

## Log

- 00:41 — list written.
- 01:03 — the crackle under P3/P5/P6 fixed (29c66f5): the carry's level glided, the coil's crossfade queued. Now P4 under the sustained exciters.
- 01:05 — P4 checked: on struck notes it only sets the next strike's
  velocity and is clean on every world. Under the reed it is the breath,
  and the "clicks" a turn shows are the reed's own waveform corners, one a
  period (245-246 samples apart at G3, the same in a steady note at that
  pressure): its buzz, not a crackle. What is left on a turn: the Guitar
  family's instruments switched on P3 (9-12 at about -60 dB, at the member
  crossings); a unmatched-ring fade through the tail was tried and changed
  nothing measurable, so it was taken out.
- 01:07 — the mallet campaigns had trained through and stopped at the
  bake: excbake refused the card's version 7 worlds. It takes 7s now
  (ModalBake f16e9e2, the same layout), and exccampaign.sh resumes at the
  bake (EXC_FROM=bake); both sets baking and gating. The Lips' "click
  trains" (open since yesterday) are one a period on Guitar (245-246
  samples at G3): the brassy waveform's corner, as the reed's, not a fault.
- 01:09 — **mallets trained and level-safe** (ModalBake 0576f06):
  marimba-vel 42 of 61 notes trained (19 keep the recorded attack),
  vibraphone-vel 42 of 42; 0 of 420 strikes over for each, fast playing
  under the recorded attack's loudest. Gated again on tonight's core (the
  glide): the same figures, and the grand on card-exc5 still safe (0 of
  810). A card to try them: ModalBake/out/card-exc6 (card-exc5 with the two
  trained mallet worlds in). Nothing on your SD card. Both firmwares built
  at 29c66f5.
- 01:34 — **the bow** (672e424), found chasing the P5 crackle under a bow:
  its output carried the string's static deflection (+0.05 to +0.18 of
  full scale, a DC offset on the module's output), and on any string lossier
  than about Q 120 — every pizzicato world, the Guitar, the Iowa C2 — the bow
  slid without a sound and the deflection was all there was. Letting go
  swung it back at once: x326 to x16000 the note's own sample step at a lift
  (a thump; likely some of the bowed pops). Now a DC blocker a voice, and
  a force at least 1.5x the least that starts the string: 169 of 169
  bowed notes sound (135 before), 162 in tune (130), every lift under
  x1.06. What a P5 turn still counts under a bow is the bowed waveform:
  one corner a period (Viola, Guitar: 245 samples at G3) or two (a double
  slip, 122), the same in a steady note at that decay. The one irregular
  case is the Mandolin muted to a fortieth (decay pot about 0.2): 78-88
  samples apart, a raucous bow on a nearly dead string — silent before.
- 02:05 — **the hand's hammer** (f8bbad4): it was 5-25 dB under the recorded
  attack on most worlds because its level came from predicting an impulse's
  ring, and the hammer bounces off the string's stiffness over 5-11 ms (a
  slow heavy one on a high note mostly rests on the string). The ring it
  actually leaves is now checked when the contact lets go and glided to the
  recording's. Card: -3.0 dB on average (the 0.7 trim is -3.1), spread
  -8.4..-0.8 from -18.8..+4.3; re-strikes x1.32 the recorded peak (x1.68
  before), fast playing x1.60 its loudest on EP-vel (x1.65 before; the gate
  for trained worlds asks 1.5, and this was over it before tonight too).
  The pickup worlds are still the lowest (-6 to -8): the check compares the
  modes' displacement, you hear the pickup's output. And a flaw in tonight's
  glide fixed with it: moving a mode's radius without rewriting its second
  sample misread the ring's amplitude and phase under low modes.
  Modal SRAM 96.36%.
- 02:06 — the pluck steady across its stiffness now too (timbre 1: -2.4, -0.7, -3.8 dB on the tine, the grand, the guitar; -14, -10, -17 before). bells-vel campaign started (the one other set with fits and a card world).
- 02:08 — the sustained exciters' level against each world's (reed-vel reed/lips -14 dB, the pianos +7..+10): not the world's displacement scale (no correlation over 24 worlds), so no change; a per-note level check like the hammer's is possible and is left for your ears.
- 02:10 — Salamander's load, what the desktop can rule out: the same 48
  modes and 88 points as the Iowa grand, the same strikes built in the
  callback (4 of 400, every world alike), no point lookup walking the blob
  (kMaxPoints 128), and its borrowed attacks (version 8) take the same path
  as recorded ones. Its attacks do stay active longer (8.7 slots a block
  against 5.1), a few microseconds a block. The module's CPU line is what
  is left: avg, max, engine, strike, At/s, staged/s, hurried/s while it
  overruns, and the same on the Iowa grand. A safety sweep of tonight's
  core (every world, all seven exciters, a staged random roll with swells
  and lifts): finite throughout, nothing past the limiter, as before.
- 02:10 — check-in. Since the last: the hammer's level check and the glide fix (f8bbad4), exciters doc (96d5dbc), Salamander ruled out on the desktop. Mallets finished and level-safe (01:07). In progress: bells-vel training (37 notes).
