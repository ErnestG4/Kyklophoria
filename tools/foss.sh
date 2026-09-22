#!/bin/sh
# The free libraries, fitted: the University of Iowa's Musical Instrument
# Samples ("may be downloaded and used for any projects, without
# restrictions") and Versilian's VCSL (CC0). What they carry that our
# first sets did not: a chromatic piano at three dynamics, and the
# strings and the guitar recorded STRING BY STRING, so the same note on
# two strings is two points and the neck becomes an axis.
#
# Two lanes, as tools/refit.sh: three put WSL past its 12 GB and it closed.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
L=out/overnight; mkdir -p $L
I=samples/foss/iowa
log() { echo "== $* $(date +%H:%M:%S)" >> $L/foss.log; }

# ── organise and split, which is cheap and needs no GPU ────────────────
if [ ! -f $L/foss-prep.done ]; then
  log "organise the Iowa piano"
  $PY tools/organise.py out/gen/piano-iowa $I/Piano_Other/piano/*.aif* >> $L/foss.log 2>&1
  for inst in violin viola cello bass guitar; do
    d=$(ls -d $I/*/$inst 2>/dev/null | head -1)
    [ -z "$d" ] && continue
    log "split $inst by string"
    $PY tools/splitnotes.py out/gen/$inst-iowa "$d"/*.aif* --per-string >> $L/foss.log 2>&1
  done
  touch $L/foss-prep.done
fi

lane() { # name, then fitset args
    name=$1; shift
    [ -f $L/foss-$name.done ] && return
    log "$name"
    nice -n 10 $PY -u tools/fitset.py "$@" >> $L/foss-$name.log 2>&1 && touch $L/foss-$name.done
}

# ── the fits ──────────────────────────────────────────────────────────
# the piano first, one dynamic, to see what a chromatic set is worth
( lane piano-iowa piano out/gen/piano-iowa/mf out/fit/piano-iowa --layout folders --steps 1200 --max-seconds 5.0
  lane piano-iowa-ff piano out/gen/piano-iowa/ff out/fit/piano-iowa-ff --layout folders --steps 1200 --max-seconds 5.0 ) &
( for s in sule sula suld sulg sulb; do
    [ -d out/gen/guitar-iowa/$s ] || continue
    lane guitar-$s guitar out/gen/guitar-iowa/$s out/fit/guitar-$s --layout folders --steps 1200 --max-seconds 4.0
  done ) &
wait
log "fits done"

# ── worlds, a family of the guitar's strings, and the checks ──────────
for d in piano-iowa piano-iowa-ff; do
  [ -f out/fit/$d/fits.tsv ] && { $PY tools/bursts.py out/fit/$d --ms auto >> $L/foss.log 2>&1; $PY tools/export.py records out/fit/$d out/worlds/$d.kykm >> $L/foss.log 2>&1; }
done
args=""
for s in sule sula suld sulg sulb; do
  [ -f out/fit/guitar-$s/fits.tsv ] || continue
  $PY tools/bursts.py out/fit/guitar-$s --ms auto >> $L/foss.log 2>&1
  $PY tools/export.py records out/fit/guitar-$s out/worlds/guitar-$s.kykm >> $L/foss.log 2>&1
  args="$args ${s#sul}=out/worlds/guitar-$s.kykm"
done
[ -n "$args" ] && $PY tools/export.py family out/worlds/guitar-strings.kykm $args >> $L/foss.log 2>&1
for w in piano-iowa piano-iowa-ff guitar-strings; do
  [ -f out/worlds/$w.kykm ] && $PY tools/notecheck.py out/worlds/$w.kykm 36 96 >> $L/foss.log 2>&1
done
log "done"
