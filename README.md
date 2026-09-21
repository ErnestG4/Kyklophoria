# ModalBake

A feasibility bake: does interpolating modal operators across instrument
models produce a space worth navigating? Measured with the grading tools built
for [Kyklophoria](../Kyklophoria), which is not modified by anything here.

The output is three numbers and a listening set, not an instrument. The
findings are in `docs/findings.md`; the numbers there are the numbers that came
out, not the numbers anyone wanted. The short version: the eigenvector half of
a modal operator adds about a tenth of the spectral motion the frequency half
adds, so a space of both is a space of frequencies, and a space of frequencies
is a crossfade. The manifold machinery keeps the space valid — no negative
frequencies, no cliffs — and does not make it varied.

    variant                        spread     variety   (pitch-normalised)
    (a) full, tangent-space PCA     6.60x      19.0     5.07x   12.5
    (b) frequencies only            5.97x      19.5     9.08x   17.8
    (c) linear PCA on raw (Λ, G)   20.27x      38.0    25.06x   29.1
    (d) shapes only                18.73x       1.6    14.17x    1.7
    Kyklophoria FM                  1.83x      12.76
    Kyklophoria Saw                 2.03x       1.44

    one family alone (pitch-normalised)
    Rhodes tine, length swept        2.89x       3.0     shapes 2.0, frequencies 1.05
    bell, flare swept               21.06x       4.1
    plate, aspect swept              5.36x       4.8
    bar, taper swept                 6.60x       0.8

The tine is the first family where the shapes carry more than the frequencies,
and the roadmap (`docs/roadmap.md`) starts there.

## Stages

    make corpus     Stage 1  meshes -> FEM -> out/corpus.mdb, out/manifest.tsv
    make align      Stage 2  MAC + Hungarian -> out/align.txt, out/align.bin
    make bake       Stage 3  tangent-space PCA, three ways -> out/space-*.msp, out/bake-*.txt
    make grade      Stage 4  Spread, continuity, veering -> out/grade-*.txt
    make renders    the listening set -> out/wav/

Each stage's artifact is a checkpoint; a later stage only reruns if an earlier
artifact changed.

## What is here

    tools/meshgen.py     the three families as structured tet volumes (stdlib)
    tools/modalfem/      mesh2faust's FEM path with signed gains at 12 positions,
                         and a clamped boundary for tines and reeds
    tools/pack.py        records -> one corpus file, padded to N=48 (stdlib)
    tools/align/         MAC, Hungarian, signs; the alignment tables
    tools/bake/          the PCA, from kykeigen; log/exp maps in common/space.h
    tools/grade/         Spread from kykworlds, continuity from cont_check
    tools/render/        WAVs of morph paths
    tools/modalfit.py    a recording -> a modal record, by gradient on the GPU (torch);
                         and the pickup stage, fitted across velocities
    tools/fitset.py      a sample set through the fitter; fits.tsv with excess_db,
                         decay_ratio per note
    tools/fitvel.py      a note at several velocities -> metal + pickup (fit_shaped)
    tools/epigen/        Epi's physical pianos (../epi, GPL-3.0) as a generator of
                         labelled notes: any velocity, voicing, metal
    tools/bursts.py      the attack as a stored burst: the recording's first 60 ms
                         minus the model's, into every record; fixes the sign the
                         STFT loss cannot see
    tools/noise.py       the wash: a dense body's energy deficit per octave band, as noise
    tools/playvel.py     a record played soft to hard, burst and all (numpy/scipy)
    tools/earcheck.py    grades every rendered wav for rails, silence, level, clicks
    tools/export.py      a fitted world condensed for the module (.kykm, out/worlds/)
    runtime/             the runtime prototype: resonator bank, hammer, pickup, and
                         the world loader — header-only, no heap; tools/modaltest
                         renders records and worlds through it, make armcost counts
                         it on the M7
    lineage/             the three Kyklophoria files these started from, verbatim
    docs/                findings

## Dependencies

Beside this repo, not inside it: `../faust` (GRAME's, for mesh2faust's Vega and
Spectra; `git submodule update --init tools/physicalModeling/mesh2faust/spectra`)
and `../eigen` (header-only). Host C++ and Python stdlib; no npm, no numpy.

The fitters are the exception: `tools/modalfit.py`, `fitset.py` and
`fitvel.py` want torch, numpy, scipy and soundfile, and a GPU; they run in the
`~/fmexplorer` venv on the desktop and never on the module. `tools/epigen`
links `../epi` (DatanoiseTV's Epi, GPL-3.0), engine only, no JUCE.
