# The rough remainder — what is wrong with the recordings, not the fits

Item 12 of Kyklophoria's overnight list (docs/overnight-2026-09-27.md
there). After refit3, 85 records still ring more than 3 dB from their
recordings (tools/fitcheck.py, out/audit-r/fitcheck-C.tsv; it was 166).
The worst twelve, looked at in the audio: onsets (10 ms RMS rising 12 dB),
where the peak sits, the strongest partial of the sustain against the label,
how the file ends. **Nothing has been changed**; this is the list to decide
on.

| record | note | off by | what the audio shows | suggestion |
|---|---|---|---|---|
| violin-pizz3-sule / violin007 | F7 ff | 29.2 dB | Falls 20 dB in 20 ms; its energy is at 480 and 890 Hz — body and room — nothing near F7 (2794 Hz). A pizzicato that high is a click. | **Drop** F7 (and check E string above ~C7): the neighbours transpose up |
| cello-pizz3-suld / cello024 | A4 pp | 14.9 dB | The sustain's strongest partial is +190 cents off the label — most of a whole tone. | **Relabel or drop**: listen to it; likely B4, or a sympathetic string dominating a quiet take |
| viola-pizz3-sulc / viola007 | E3 ff | 11.8 dB | 0.8 s long; the strongest energy an octave under the note (−1158 c): a thump or body resonance, not the string. | **Drop** (a broken take) |
| cello-pizz3-sula / cello029 | B3 pp | 9.6 dB | The file ends 12 dB under its peak at 1.25 s — cut off mid-ring. The fit cannot know the decay the file never shows. | **Re-split** with a longer tail (tools/splitnotes.py) |
| banjo / banjo066 | G4 | 8.8 dB | Two onsets, 0.15 and 0.32 s: a double pluck, or the next note in the file. | **Re-split** from the second onset, or drop |
| cello-pizz3-sulg / cello016 | C#4 mf | 16.0 dB | Starts 1.3 s into its file (silence before the pluck); the sustain +31 c sharp. | **Re-split** (a late cut, the lesson of the 22nd), then refit |
| cello-pizz3-sulg / cello029 | G3 mf | 9.9 dB | Starts 1.2 s in; −44 c. | **Re-split**, then refit |
| bass-pizz3-suld / bass009 | G3 ff | 11.3 dB | Starts 0.5 s in; in tune; one clean onset. | A fit problem: try refitn again with more tries |
| mandolin / mandolin014 | C#5 | 11.7 dB | Clean, in tune (−3 c). | A fit problem (the mandolin's paired courses beat): more modes? |
| guitar / guitar021 | C6 | 9.0 dB | Clean, in tune (+11 c). | A fit problem |
| banjo / banjo067 | G5 | 8.3 dB | Clean, in tune. | A fit problem |
| piano-salamander / piano015 | C8 | 10.7 dB | The top note; the spectral pitch read is unreliable at 4 kHz (as pitchcheck's was). | Leave: the top octave is thin on every piano |

## What the pattern says

- **Four of twelve are the recordings** (violin F7, viola E3, cello A4 pp,
  and the truncated cello B3 pp) and **three are cuts** (cello C#4 and G3
  starting over a second late, banjo G4 with two onsets). The splitter's
  true-start fix of the 22nd did not reach every string set: the cello's G
  string has two records starting 1.2-1.3 s in.
- **Four are fits** on clean recordings (bass G3, mandolin C#5, guitar C6,
  banjo G5); the mandolin's and banjo's paired strings beat, which a record
  of single modes renders only if it spends two modes on each.
- A pizzicato at the very top of a violin or viola is not a note a model of
  modes can hold; the gate (tools/gate.py) could drop anything whose
  recording falls 20 dB in 30 ms.

## To do, once decided

1. Drop the three broken takes (violin F7, viola E3, and cello A4 pp unless
   a listen says it is a B4): `export.py --gate` leaves out what the gate
   fails; a drop list in the manifest is the explicit way.
2. Re-split cello-pizz3-sulg (C#4, G3), cello-pizz3-sula B3 pp and banjo
   G4, and refit those four.
3. Refit the four clean ones with `refitn --neighbours 3 --tries 4`.
