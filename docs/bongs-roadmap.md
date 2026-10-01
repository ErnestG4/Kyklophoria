# Bongs: the roadmap

The live list for the modal firmware (Bongs), as of 30 September 2026. It
replaces `docs/modal-plan-2026-09-22.md` as the place to start. The newest
`docs/overnight-*.md` has the detail and the measurements behind each line;
`docs/bongs-controls.md` the panel and the page; `docs/exciters.md` the
exciters.

## Where it stands

- **Beta shared** with the community: `bongs.bin` and the **velocity-layered
  worlds only** (`-vel`), which Combust finds sound good. The others overrun
  under fast playing (below).
- **Published** on GitHub, `ErnestG4/Kyklophoria`, one repo for both
  firmwares; `main` is current (fast-forwarded to `modal` on 30 September).
  The page is GitHub Pages from the `pages` branch:
  https://ernestg4.github.io/Kyklophoria/?page=modal. To publish a new page:
  `bash web/publish-pages.sh`, then `git push --force-with-lease github pages`
  (the script rebuilds the branch, so it never fast-forwards). Never push
  `modal-before-purge` (recorded libraries we cannot publish).

## Next, in order

1. **Overruns on the non-velocity worlds under fast playing.** Wurli, Guitar,
   Banjo and others, spamming keys: 56 avg, 156 max, engine 154, strike 32,
   66 At/s, **0 staged**, 116 hurried (Combust, 30 September). The strike
   itself is a third of a block; the overrun is the engine playing. But
   **staged 0** says the staging is not working on the module: every strike
   built its voice in the audio callback, where the desktop stages 265 of 400
   on every world. Next: the same CPU line on a `-vel` world played the same
   way. Staged 0 there too → fix the staging (let a strike wait a few ms more
   for a plan being built, or serve the plan from a timer rather than the main
   loop). Staged > 0 there → find what those worlds do differently.
2. **Remove the recorded exciters** (Combust: "we've moved beyond them").
   Scope to decide: the menu only; the engine (no recorded attack ever plays;
   Trained's untrained notes fall back to the hand hammer); or the engine and
   the worlds (the export and the card drop the attack audio, so nothing
   sampled ships; the trainer needs the recordings, so this comes after
   training).
3. **Train every instrument** (about a night, plus an hour or two of prep).
   `tools/exccampaign.sh` takes only pp/mf/ff takes; the card's other sets
   label theirs v04-v16 (Salamander), vl1-vl4 (VCSL, Upright), piano/forte
   (Guitar, Banjo, Mandolin), MED/MAX (EP), or have one take a note (EP-vel,
   tine, reed, Crotales, Harp, Piano, Wurli). Map them; train families
   member by member (excbake refuses a family); pickup worlds are untried.
   About 520 notes, 10-45 s a note; gate every result with `exclevel`; nothing
   goes on a card before Combust listens.
4. **The sustained exciters' level** against each world's (bow, reed, lips):
   reed-vel's reed and lips about 14 dB under, the pianos 7-10 over. Not the
   world's displacement scale; a per-note level check like the hammer's would
   even them. Changes how they sit, so it waits for a listen.
5. **The hand's hammer** on the pickup worlds is 6-8 dB under (the check
   compares displacement, the ear hears the pickup's output), and on EP-vel
   its fast playing peaks at 1.60x the recorded attack's (during the contact,
   before the check).
6. **The bow's tuning** outliers: Guitar G2 -32 c and G4 +37 c; the banjo's
   lowest notes bow another partial.

## Owed a listen (built 29-30 September)

- Turning Play P3/P5/P6 under a ringing note (the crackle, gone by measure).
- The bow on the pizzicato worlds and the Guitar; lifts (no thump).
- The hand's hammer and pluck at the recording's level.
- Gates: B2 and the pad let go; a pause after a gate.
- Trained marimba, vibraphone and bells: `ModalBake/out/card-exc6`.

## Waiting on Combust

- The wavetable backports in `docs/bongs-controls.md` (its page's layout).
- Older: the dark stereo circle (a screenshot), the EP-bass refit, Epi's
  licence line, the refitted records' 5-12 ms misalignment.

## Known, not to fix

- The late-CV window: a pitch change within 30 ms of a strike retunes that
  note. It cannot tell a late sequencer CV from a deliberate retune.
- The reed's and the lips' "clicks" in a click count are one corner a period:
  their waveform.

## Budget

Modal SRAM 96.4% (about 17 KB free; the image runs from SRAM, so code
counts). The 29-30 September work cost about 7 KB.
