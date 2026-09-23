#!/bin/sh
# The free libraries, stage two: wait for the fetch, split what is
# recorded string by string, and fit everything in two lanes. Resumable
# throughout (fitset --resume, a .done a lane), because this machine has
# gone down twice mid-set.
#
#   Iowa piano   88 chromatic notes at pp, mf and ff — three real dynamic
#                layers, where our first piano had 34 notes at one
#   Iowa strings violin, viola, cello, double bass, guitar, each recorded
#                STRING BY STRING: the same note on two strings is two
#                points, and a family of them makes the neck an axis
#   Iowa mallets marimba, vibraphone, xylophone, bells, crotales
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
L=out/overnight; mkdir -p $L
I=samples/foss/iowa
log() { echo "== $* $(date +%H:%M:%S)" >> $L/foss2.log; }

# wait for the download, but not forever
n=0
while ! grep -q "all done" $I/fetch.log 2>/dev/null; do
  n=$((n+1)); [ $n -gt 240 ] && break
  sleep 30
done
log "fetch: $(find $I -name '*.aif*' | wc -l) files"

# ── split what is per string ──────────────────────────────────────────
if [ ! -f $L/foss2-split.done ]; then
  for inst in violin viola cello bass; do
    d=$(ls -d $I/Strings/$inst 2>/dev/null | head -1)
    [ -z "$d" ] && continue
    log "split $inst"
    $PY tools/splitnotes.py out/gen/$inst-iowa "$d"/*.aif* --per-string --dynamic ff >> $L/foss2.log 2>&1
  done
  for inst in marimba vibraphone xylophone bells crotales; do
    d=$(find $I -type d -iname "*$inst*" | head -1)
    [ -z "$d" ] && continue
    log "split $inst"
    $PY tools/splitnotes.py out/gen/$inst-iowa "$d"/*.aif* >> $L/foss2.log 2>&1
  done
  touch $L/foss2-split.done
fi

lane() { # name, family, indir, outdir, extra args
    name=$1; fam=$2; ind=$3; outd=$4; shift 4
    [ -f $L/foss2-$name.done ] && return
    [ -d "$ind" ] || return
    log "$name"
    nice -n 10 $PY -u tools/fitset.py "$fam" "$ind" "$outd" --resume "$@" >> $L/foss2-$name.log 2>&1 && touch $L/foss2-$name.done
}

# lane A: the piano's other two dynamics, then the mallets
( for dyn in pp mf; do
    mkdir -p out/gen/piano-iowa-$dyn && ln -sfn ../piano-iowa/$dyn out/gen/piano-iowa-$dyn/$dyn
    lane piano-$dyn piano out/gen/piano-iowa-$dyn out/fit/piano-iowa-$dyn --layout folders --steps 1200 --max-seconds 5.0
  done
  for inst in marimba vibraphone xylophone bells crotales; do
    [ -d out/gen/$inst-iowa ] || continue
    mkdir -p out/gen/$inst-set && ln -sfn ../$inst-iowa out/gen/$inst-set/x
    lane $inst $inst out/gen/$inst-set out/fit/$inst-iowa --layout folders --steps 1200 --max-seconds 6.0
  done ) &

# lane B: the strings, one set per string
( for inst in violin viola cello bass; do
    for s in out/gen/$inst-iowa/*/; do
      [ -d "$s" ] || continue
      str=$(basename "$s")
      mkdir -p out/gen/$inst-$str && ln -sfn ../$inst-iowa/$str out/gen/$inst-$str/ff
      lane $inst-$str $inst out/gen/$inst-$str out/fit/$inst-$str --layout folders --steps 1200 --max-seconds 4.0
    done
  done ) &
wait
log "fits done"

# ── worlds, families of strings, and the checks ───────────────────────
for d in out/fit/*-iowa out/fit/guitar-sul* out/fit/violin-sul* out/fit/viola-sul* out/fit/cello-sul* out/fit/bass-sul*; do
  [ -f "$d/fits.tsv" ] || continue
  n=$(basename "$d")
  $PY tools/bursts.py "$d" --ms auto >> $L/foss2.log 2>&1
  $PY tools/export.py records "$d" out/worlds/$n.kykm >> $L/foss2.log 2>&1
done
# --by-pitch orders the members by their lowest fitted note and names them
# for it, so the position axis walks the neck low string to high instead of
# following the filenames' alphabet — which put the guitar's low E between
# its B and its G. The names come out as the open strings: the violin's four
# are G3 D4 A4 E5 exactly.
for inst in guitar violin viola cello bass; do
  args=""
  for w in out/worlds/$inst-sul*.kykm; do
    [ -f "$w" ] || continue
    args="$args $w"
  done
  [ -n "$args" ] && $PY tools/export.py family --by-pitch out/worlds/$inst-strings.kykm $args >> $L/foss2.log 2>&1
done
for w in out/worlds/piano-iowa.kykm out/worlds/guitar-strings.kykm; do
  [ -f "$w" ] && $PY tools/notecheck.py "$w" 36 96 >> $L/foss2.log 2>&1
done
$PY tools/fundcheck.py >> $L/foss2.log 2>&1
log "done"
