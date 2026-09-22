#!/bin/sh
# The refit from the holistic pass (docs/holistic-math.md items 9, 10, 7,
# and the fundamental prior of 21 Sept): every recorded set through the
# fitter again, two lanes on the one GPU — three lanes and the session
# put WSL past its 12 GB and it closed, taking the first attempt with it
# — then the bursts, the worlds, the families, and the checks. Logs under
# out/overnight/refit-*.log; a lane that finished leaves a .done file and
# is skipped, so the script can be run again after an interruption.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
STEPS=${STEPS:-2000}
L=out/overnight; mkdir -p $L
P=samples/philharmonic-all-samples
W="samples/CNCD samples/real/wurlitzr"
cp manifests/wurli.tsv "$W/fits.tsv"

lane() { # name, then fitset args
    name=$1; shift
    [ -f $L/refit-$name.done ] && return
    echo "== $name $(date +%H:%M:%S)" >> $L/refit-$name.log
    nice -n 10 $PY -u tools/fitset.py "$@" --steps $STEPS >> $L/refit-$name.log 2>&1 && touch $L/refit-$name.done
}
vlane() { # name, then fitvel args
    name=$1; shift
    [ -f $L/refit-$name.done ] && return
    echo "== $name $(date +%H:%M:%S)" >> $L/refit-$name.log
    nice -n 10 $PY -u tools/fitvel.py "$@" >> $L/refit-$name.log 2>&1 && touch $L/refit-$name.done
}

echo "== refit starts $(date +%H:%M:%S)" >> $L/refit-night.log
( lane ep       ep       samples/EP     out/fit/ep       --layout folders --octave 1 --max-seconds 5.0
  vlane ep-vel  epv      samples/EP     out/fit/ep-vel   --order MED,MAX --octave 1 --normalised --bar --coil-from-set --steps 800 ) &
( lane guitar   guitar   $P/guitar      out/fit/guitar
  lane banjo    banjo    $P/banjo       out/fit/banjo
  lane mandolin mandolin $P/mandolin    out/fit/mandolin
  lane violin   violin   $P/violin      out/fit/violin   --articulation pizz-normal
  lane piano    piano    out/gen/piano  out/fit/piano    --layout manifest --keep-ids --max-seconds 6
  lane wurli    wurli    "$W"           out/fit/wurli    --layout manifest --keep-ids
  lane bass     bass     "$P/double bass" out/fit/bass   --articulation pizz-normal
  lane viola    viola    $P/viola       out/fit/viola    --articulation pizz-normal ) &
wait
echo "== fits done $(date +%H:%M:%S)" >> $L/refit-night.log

# the bursts, the worlds, the families
for d in wurli piano guitar banjo mandolin violin viola bass ep; do $PY tools/bursts.py out/fit/$d --ms auto >> $L/refit-night.log 2>&1; done
$PY tools/bursts.py out/fit/ep-vel --shaped --ms auto >> $L/refit-night.log 2>&1
for d in wurli ep ep-vel tine-vel reed-vel guitar banjo mandolin violin viola bass piano perc; do $PY tools/export.py records out/fit/$d out/worlds/$d.kykm >> $L/refit-night.log 2>&1; done
$PY tools/export.py family out/worlds/strings.kykm violin=out/worlds/violin.kykm viola=out/worlds/viola.kykm bass=out/worlds/bass.kykm guitar=out/worlds/guitar.kykm banjo=out/worlds/banjo.kykm mandolin=out/worlds/mandolin.kykm >> $L/refit-night.log 2>&1
$PY tools/export.py family out/worlds/pianos.kykm grand=out/worlds/piano.kykm wurli=out/worlds/wurli.kykm ep=out/worlds/ep.kykm ep-vel=out/worlds/ep-vel.kykm >> $L/refit-night.log 2>&1
$PY tools/export.py family out/worlds/electric.kykm ep-vel=out/worlds/ep-vel.kykm tine-vel=out/worlds/tine-vel.kykm reed-vel=out/worlds/reed-vel.kykm wurli=out/worlds/wurli.kykm >> $L/refit-night.log 2>&1
echo "== worlds done $(date +%H:%M:%S)" >> $L/refit-night.log

# the checks: every note world's semitones through the engine, the
# fundamentals, the labels, and the renders by ear
$PY tools/fundcheck.py >> $L/refit-night.log 2>&1
$PY tools/pitchcheck.py out/fit/wurli out/fit/piano >> $L/refit-night.log 2>&1
for d in wurli ep-vel guitar banjo mandolin violin viola bass piano; do $PY tools/notecheck.py out/worlds/$d.kykm 36 96 >> $L/refit-night.log 2>&1; done
make fitted-renders >> $L/refit-night.log 2>&1
echo "== done $(date +%H:%M:%S)" >> $L/refit-night.log
