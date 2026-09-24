#!/bin/sh
# Refit campaign one (2026-09-23): every plain note set on the card through
# tools/refitn.py --auto — each record whose fit is flagged (decay ratio
# outside 0.4..2.5, loss over 1.3, under 4 modes) fitted again from its good
# neighbours, kept only where it wins. 511 records, two lanes. Combust: "We can
# do better with fits across the board ... now with all the lessons we've
# learned combined."
#
# Waits for a PID (the piano-iowa3 pilot) if given. Before touching a set it
# copies the set's .mmr and fits.tsv to out/refit1-backup/<set>/ — prev/ holds
# only the last version of a record, and a second pass would overwrite it.
# Refits and bursts only: exports, the card and the audit are done by hand
# after the pilot's numbers are read.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
L=out/overnight; mkdir -p $L out/refit1-backup
log() { echo "== $* $(date +%H:%M:%S)" >> $L/refit1.log; }
[ $# -ge 1 ] && while kill -0 "$1" 2>/dev/null; do sleep 30; done

one() { # a fit set directory under out/fit
    d=$1; s=$(basename $d)
    [ -f $L/refit1-$s.done ] && return
    [ -f $d/fits.tsv ] || return
    if [ ! -d out/refit1-backup/$s ]; then
        mkdir -p out/refit1-backup/$s && cp $d/*.mmr $d/fits.tsv out/refit1-backup/$s/
    fi
    log "$s"
    nice -n 10 $PY -u tools/refitn.py $d --auto >> $L/refit1-$s.log 2>&1 && \
      $PY tools/bursts.py $d --ms auto >> $L/refit1-$s.log 2>&1 && touch $L/refit1-$s.done
    log "$s done: $(grep -c 'refitted from' $L/refit1-$s.log) refitted"
}
lane() { for d in "$@"; do one $d; done; }

log "start"
( lane out/fit/piano out/fit/piano-iowa out/fit/piano-salamander out/fit/piano-vcsl out/fit/upright-knight2 \
       out/fit/wurli out/fit/ep out/fit/guitar out/fit/banjo out/fit/mandolin out/fit/harp-concert ) &
( lane out/fit/guitar3-sul* out/fit/bass-pizz3-sul* out/fit/cello-pizz3-sul* out/fit/viola-pizz3-sul* out/fit/violin-pizz3-sul* \
       out/fit/marimba-vel out/fit/vibraphone-vel out/fit/bells-vel out/fit/crotales-iowa ) &
wait
log "done"
