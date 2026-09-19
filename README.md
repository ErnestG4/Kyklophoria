# ModalBake

A feasibility bake: does interpolating modal operators across instrument
models produce a space worth navigating? Measured with the grading tools built
for [Kyklophoria](../Kyklophoria), which is not modified by anything here.

The output is three numbers and a listening set, not an instrument. The
findings are in `docs/findings.md`; the numbers there are the numbers that came
out, not the numbers anyone wanted.

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
    tools/modalfem/      mesh2faust's FEM path with signed gains at 12 positions
    tools/pack.py        records -> one corpus file, padded to N=48 (stdlib)
    tools/align/         MAC, Hungarian, signs; the alignment tables
    tools/bake/          the PCA, from kykeigen; log/exp maps in common/space.h
    tools/grade/         Spread from kykworlds, continuity from cont_check
    tools/render/        WAVs of morph paths
    lineage/             the three Kyklophoria files these started from, verbatim
    docs/                findings

## Dependencies

Beside this repo, not inside it: `../faust` (GRAME's, for mesh2faust's Vega and
Spectra; `git submodule update --init tools/physicalModeling/mesh2faust/spectra`)
and `../eigen` (header-only). Host C++ and Python stdlib; no npm, no numpy.
