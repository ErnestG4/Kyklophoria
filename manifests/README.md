# Manifests for sets whose files carry no note name

`tools/fitset.py --layout manifest` reads `fits.tsv` beside the files; the
copies here are the ones that ship, kept in the repo because `samples/` is
not. Copy one beside its files before a fit:

    cp manifests/wurli.tsv "samples/CNCD samples/real/wurlitzr/fits.tsv"

**wurli** — the CNCD Wurlitzer, 50 files of C and G in every octave, C2 to
C7, several takes a note (peak-normalised, so not velocity layers): one
file a pitch, the first take. The first manifest had `wurlz001` as the C3;
it is a C2 (tools/pitchcheck.py), and the module's B2 played an octave
down until the module said so. Every label here is the audio's, by
`tools/pitchman.py` and checked by `tools/pitchcheck.py`.

**piano** — the Syntec "Stereo Grand" set, 34 files named `untitled(n).flac`,
pitched by `pitchman` and checked against the run: `piano001` = the first
file in name order, E1, up to `piano034`, C8. Copy the files beside it as
`pianoNNN.flac` (`tools/pitchman.py piano "<the set>" --copy out/gen/piano`,
then shift the ids up by one — pitchman counts from 0 and this set was made
when it counted the directory's first entry). The first copy lived in a
session's /tmp and went with it.
