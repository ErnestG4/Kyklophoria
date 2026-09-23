# The modal mode: where it stands and what is next — 2026-09-22

Written to be picked up cold. `docs/modal-mode.md` is the design; this is the
state of the work and the jobs in front of it, in the order I would take them.
Fitting lives in the sibling repo `~/Daisy/ModalBake` (branch `bake`); the
runtime is `core/kyk_resonate.h` here, with a verbatim copy at
`ModalBake/runtime/kyk_resonate.h` that `make -C ../ModalBake check-runtime`
enforces.

## The headline: the neck is an axis, and it works

Combust asked for it directly — *"for a guitar we would have 6 strings and be
able to transpose our positions while keeping the same notes"*. It is built.
Four families, each member a note world fitted from ONE STRING of the
instrument, the position axis choosing the string and v/oct the note within it:

    out/worlds/guitar-strings.kykm   6 strings  F2 A2 D3 G3 B3 E4   708 KB
    out/worlds/violin-strings.kykm   4 strings  G3 D4 A4 E5        1176 KB
    out/worlds/viola-strings.kykm    4 strings  C3 G3 D4 A4         664 KB
    out/worlds/cello-strings.kykm    4 strings  C2 G2 D3 A3        1029 KB

The ranges overlap the way a neck does: **G3 is playable on four of the
guitar's six strings**, so the same pitch at four positions is four timbres.
That is the whole point of the axis and it is the thing to listen for.

The member ORDER is derived, not tabled. A note world's `lo` is the lowest note
fitted on that string, which for an open-string recording is the open string —
measured, not assumed: the violin's four came out 55/62/69/76, which is
G3 D4 A4 E5 exactly, and the viola's and cello's likewise. So
`export.py family --by-pitch` sorts by `lo` and names each member for it,
which also settles the guitar's two E strings without a special case. The
campaign had built them from a glob, so the axis ran high-e, A, B, D, low-E, G
— sweeping that walks nothing. The guitar's low E reads F2 because the lowest
note fitted on it was the first fret.

`--by-pitch` is opt-in because a family of different INSTRUMENTS — the piano
row, the electrics — is an order somebody chose and not a ladder.

## Job 1 — get guitar and cello onto the card and listen

These two are healthy. Violin and viola are not (job 2), so do not judge the
idea on them.

    cp ~/Daisy/ModalBake/out/worlds/guitar-strings.kykm \
       ~/Daisy/ModalBake/out/worlds/cello-strings.kykm  <card>/kyklophoria/

The module reads `0:/kyklophoria`, not the card root (`kWorldDir` in
`shell/alchemy/main.cpp`). Region size is 4 MB and there are 8 of them, so
both fit with room to spare. The filename minus `.kykm` becomes the world name
against a 16-character cap; `guitar-strings` is 14 and `cello-strings` is 13.

Scan the card, load each into a slot, make it live. Then **hold one note and
sweep position**. On the guitar, G3 should give four distinct timbres at the
same pitch. If it does, the axis is an instrument.

## Job 2 — refit the thin points (the blocker on violin and viola)

**20 of 307 points across the string worlds come out under 8 live modes, and
five are the fundamental ALONE.** `violin-suld`'s B4 and D5 are literally
sines. Per string world:

    cello-sula   D#5=7  A5=4          viola-sula   A#5=5  C#6=6
    guitar-sulb  C5=6                 viola-sulc   G4=2
    guitar-sulg  D#4=5 E4=5 G#4=2     viola-suld   B5=7  C6=7
    violin-sula  B4=3  C5=1           viola-sulg   D#5=5
    violin-suld  E4=6  B4=1  D5=1
    violin-sule  G5=1  G#5=7  B5=7

**It is the fit, not the export.** `violin015`'s record has twelve modes at
1.00x, 6.01x, 7.02x, 8.70x, 9.03x, 11.01x, 12.02x — it skipped harmonics two
through five entirely, and a bowed string is very nearly harmonic. The export
is faithful to a bad fit, which is why `export.py records` now WARNS about
thin points rather than repairing them: a silent world on the card is worse
than a noisy build.

It does NOT split by excitation the way it first looked. Mean live modes a
point: guitar (plucked) 33.2, cello (bowed) 35.0, viola 22.3, violin 15.1,
piano 36.6, vibraphone 38.4, marimba 15.8, **xylophone 9.8**. It splits by
REGISTER, and part of it is honest — a xylophone bar at 2 kHz has few partials
under Nyquist at all. A 1-mode violin C5 at 523 Hz is not honest.

What to try, in order:
1. A **harmonic-comb prior** on the mode frequencies for the string sets: a
   bowed or plucked string's partials are within a percent or two of k*f0, and
   nothing in the loss says so. This is the likeliest single fix.
2. More steps on just the thin records. Both GPU lanes are free. `fitset.py`
   takes `--resume` and `--only`, so it can be pointed at the named notes
   without redoing the set. **Two lanes maximum** — three has taken the
   machine down twice against the 12 GB `.wslconfig` cap.
3. Check whether vibrato is the cause: Iowa's arco notes are played with it,
   and a fixed-frequency mode cannot match a partial that is smearing +/-20
   cents, with the smear in Hz growing with harmonic number. That would
   predict h1 fitted and the low-middle harmonics skipped, which is what
   `violin015` shows. If it is vibrato, the answer is a wider mode (higher
   zeta) rather than more modes, and that is a loss-function question.

Then rebuild the worlds and the families, and re-run the check:

    for i in guitar violin viola cello; do
      python tools/export.py family --by-pitch out/worlds/$i-strings.kykm out/worlds/$i-sul*.kykm
    done

## Job 3 — the mandolin and the banjo, which are not modelled as they are

Combust raised this and it is not done: *"The mandolin is two strings tuned and
plucked together, sometimes sliiightly detuned to beat. The banjo has a high
string at the top."* Both are currently single note worlds fitted from mixed
recordings, so the mandolin's beating is baked into whatever the fit found and
the banjo's drone string is not a member.

The family container already expresses both without new format: a mandolin
course is two members at a few cents apart, and the banjo's fifth string is a
member whose range is one note. What it needs is per-course and per-string
source recordings, which Iowa may not have — check VCSL (CC0) and VSCO 2
Community Edition (CC0) before assuming.

Note his own conclusion, which still holds: *"We're stuck on 4 voices so it
makes sense to NOT go that path, but we have to consider it."* Two members
sounding together costs two voices.

## Job 4 — the three-way render, now a quality question

Originally proposed to get out from under recorded-audio licensing. That reason
is gone: the shipping sets are fitted from Iowa's samples, which allow any use,
so the bursts are ours to distribute. What remains is whether a synthesised
attack sounds BETTER, which is worth knowing but no longer urgent.

Render each world three ways and A/B on the piano first:
1. the current burst (the recording's attack, band-split and faded),
2. the bank alone from t=0 (no burst at all),
3. the bank plus a parametric high band built from the burst's band split.

## Job 5 — the waveguide prototype

Combust: *"What is the impact of a waveguide on our process?... I don't want to
lose the earnest synthesis that allows real-time control"*, and then *"I think
option 1 is our overall path. We'll need to prototype to see for sure."* Option
1 is the waveguide ABOVE the modes, for the bass notes' cliff, not replacing
them — the modal bank keeps the real-time axes and the guide supplies the dense
high partials a low note needs and 48 modes cannot hold. `docs/lit-runtime.md`
has the reading. Unstarted.

## What Combust still owes the work

- **A bench listen.** Twice now the thing that mattered was found by playing
  it, not by measuring it. He reported *"still some small issues I think with
  overlap I need to test and get back to you"* and that has not come back.
- **Guitar vs cello vs piano**, which of the three families is worth keeping on
  the card, since 8 regions is the budget.

## Tooling that changed today, so the checks mean what they say

- **`notecheck.py` grades against the recording, not the ask.** It called 34 of
  405 semitones of the string sets off and was mostly wrong. A viola's C string
  at D3 radiates its second harmonic 21 dB ABOVE its fundamental — measured on
  the Iowa recording — and a period detector handed a clean sum of decaying
  sinusoids hears the octave where on the recording it found the period from
  the bow noise and the inharmonicity. F#3's resynthesis matched the
  recording's balance to 0.0 dB and was still called an octave error. There are
  now three verdicts: the recording reads the same way (the instrument), the
  balance matches but the read differs (for the ear), the balance moved (ours).
  Only the last fails. The balance measurement carries a floor, because
  comparing two numbers that were both the noise floor produced "140 dB
  shifts" that meant only that one signal was silent at that frequency.
- **`export.py records` warns on points under 8 live modes** (job 2).
- **`export.py family --by-pitch`** (the headline).
- **`earcheck` knows an onset is not a click.** A hard mallet's attack is a
  burst above 12 kHz gone in 3 ms, and so is a coefficient step; what separates
  them is that a step interrupts a signal already there while an attack starts
  from nothing and RISES into it. Measured: a 161x onset whose first sample
  past a hundredth of its peak sits at 1.1% of it.
- **`tests/run.sh` refuses a tracked `.kykm` with no entry in
  `tests/data/SOURCES.md`.** A fitted world carries a burst per point, so it IS
  a recording however much it looks like numbers.

## Two runtime changes that were tried and measured WORSE

Both about the ring carried across a body-row position move. Neither is in the
tree; do not re-try them without reading this first.

1. **Widening the carry's match window** beyond a fifth. The match is greedy,
   so a wide window lets an early mode claim the old mode a later one needed
   and the whole row of levels slips by one. Position 0.5 improved and
   position 1.0 went from carrying 1.1x of the energy to 7e-5.
2. **Matching by mode index instead of frequency** on an index row, which the
   fold's mode-for-mode pairing seems to justify. Worse nearly everywhere: the
   level scaling is `new mode's unit-strike gain over the old's`, and index
   pairing puts wildly different gains together.

What is actually happening is correct: a nudge inside one body keeps 0.92,
0.88 and 0.75 of the energy, and crossing to a body an octave away in its
partials keeps almost none — because the carry is capped at the level the new
mode's own strike would give it, and a bell taking a bass marimba's ring
belongs near zero. `tests/resonate_engine_check.cpp` now moves a twentieth of
the travel and says so.

## Licensing, settled

`tests/data/SOURCES.md` and `ModalBake/SOURCES.md` are the record. Short
version: Iowa's Musical Instrument Samples are free for any use (quoted whole
in both files, checked at source 2026-09-22), VCSL and VSCO 2 CE are CC0,
Salamander Grand is CC-BY. Philharmonia is CC BY-NC-SA and the CNCD Wurlitzer
and the commercial EP libraries are not redistributable, so nothing fitted from
them ships. The three fixtures here — `piano.kykm`, `tine.kykm`, `bodies.kykm`
— are Iowa and our own `epi` respectively.

One thread open: the pre-purge commits are still served by SHA on Codeberg.
Forgejo's `cron.git_gc_repos` is an extended cron task, off by default, and
`git gc` holds unreachable objects for `gc.pruneExpire`, two weeks by default.
Check whether `ff24b4d` still answers; if it matters more than that, the
certain fix is deleting and recreating the repo.
