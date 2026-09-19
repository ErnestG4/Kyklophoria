#!/bin/sh
# The overnight: every recorded set through the fitter's second pass with the
# long schedule, two lanes on the one GPU, then the corpus, the alignment, the
# fitted worlds and their renders. Logs under out/overnight/. Idempotent per
# lane: a lane that finished leaves a .done file and is skipped.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
STEPS=${STEPS:-2000}
L=out/overnight; mkdir -p $L
P=samples/philharmonic-all-samples
W="samples/CNCD samples/real/wurlitzr"

lane() { # name, then fitset args
    name=$1; shift
    [ -f $L/$name.done ] && return
    echo "== $name $(date +%H:%M:%S)" >> $L/$name.log
    $PY -u tools/fitset.py "$@" --steps $STEPS >> $L/$name.log 2>&1 && touch $L/$name.done
}

( lane wurli    wurli    "$W"        out/fit/wurli    --layout manifest --keep-ids
  lane ep       ep       samples/EP  out/fit/ep       --layout folders --octave 1 --max-seconds 5.0
  lane mandolin mandolin $P/mandolin out/fit/mandolin ) &
( lane guitar   guitar   $P/guitar   out/fit/guitar
  lane banjo    banjo    $P/banjo    out/fit/banjo ) &
wait
echo "== fits done $(date +%H:%M:%S)" >> $L/run.log
make corpus align fitted fitted-renders >> $L/run.log 2>&1 && touch $L/bake.done
echo "== all done $(date +%H:%M:%S)" >> $L/run.log
