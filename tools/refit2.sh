#!/bin/sh
# Refit campaign two (2026-09-24): the records that still carry a bad ring
# after campaign one (decay ratio outside 0.5..2 or loss over 1.3), only the
# ones whose modes are played — in a layered world the loudest take's; the
# quieter takes give their attacks — fitted harder: two good neighbours a
# side, two tries from each, and the record itself polished again, the best
# kept by refitn's score. The list is out/overnight/refit2-targets.txt (set,
# ids), made from the manifests; two lanes. Refits and bursts only.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
L=out/overnight
log() { echo "== $* $(date +%H:%M:%S)" >> $L/refit2.log; }
one() { # set dir, ids
    d=$1; ids=$2; s=$(basename $d)
    [ -f $L/refit2-$s.done ] && return
    log "$s"
    nice -n 10 $PY -u tools/refitn.py $d --only $ids --redo --neighbours 2 --tries 2 >> $L/refit2-$s.log 2>&1 && \
      $PY tools/bursts.py $d --ms auto >> $L/refit2-$s.log 2>&1 && touch $L/refit2-$s.done
    log "$s done: $(grep -c 'refitted from' $L/refit2-$s.log) refitted"
}
lane() { while read d ids; do one $d $ids; done; }
log "start"
awk 'NR % 2 == 1' $L/refit2-targets.txt | lane &
awk 'NR % 2 == 0' $L/refit2-targets.txt | lane &
wait
log "done"
