#!/bin/sh
# Stage nine: the other mallets, one model per strike type (Combust: "Don't
# we need a model per strike type?"): marimba rubber and cord, xylophone hard
# rubber, beside stage eight's yarn marimba and rosewood xylophone. Same
# pipeline as stage eight: pp, mf and ff split apart and fitted together as
# velocity layers, two lanes, resumable, worlds under new names (-vel),
# --gate --clean.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
L=out/overnight; mkdir -p $L
P=samples/foss/iowa/Percussion
log() { echo "== $* $(date +%H:%M:%S)" >> $L/foss9.log; }

split() { # name, folder, articulation
    name=$1; dir=$2; art=$3
    for dyn in pp mf ff; do
      ls $P/$dir/*.$art.$dyn.*.aif* >/dev/null 2>&1 || continue
      mkdir -p out/gen/$name-mset
      [ -d out/gen/$name-m-$dyn ] || $PY tools/splitnotes.py out/gen/$name-m-$dyn $P/$dir/*.$art.$dyn.*.aif* --dynamic $dyn >> $L/foss9.log 2>&1
      ln -sfn ../$name-m-$dyn out/gen/$name-mset/$dyn
    done
}
if [ ! -f $L/foss9-split.done ]; then
  split marimba-rubber Marimba rubber
  split marimba-cord Marimba cord
  split xylophone-hard Xylophone hardrubber
  touch $L/foss9-split.done; log "split"
fi

lane() { # name, family, max seconds
    name=$1; fam=$2; secs=$3
    [ -f $L/foss9-$name.done ] && return
    [ -d out/gen/$name-mset ] || return
    log "$name"
    nice -n 10 $PY -u tools/fitset.py $fam out/gen/$name-mset out/fit/$name-vel --layout folders --resume \
        --steps 1200 --max-seconds $secs >> $L/foss9-$name.log 2>&1 && touch $L/foss9-$name.done
    $PY tools/bursts.py out/fit/$name-vel --ms auto >> $L/foss9.log 2>&1
    $PY tools/export.py records out/fit/$name-vel out/worlds/$name-vel.kykm --gate --clean >> $L/foss9.log 2>&1
    log "world $name-vel"
}
( lane marimba-rubber marimba 6.0 ) &
( lane marimba-cord marimba 6.0;  lane xylophone-hard xylophone 4.0 ) &
wait
$PY tools/pitchcheck.py out/fit/marimba-rubber-vel out/fit/marimba-cord-vel out/fit/xylophone-hard-vel >> $L/foss9.log 2>&1
python3 tools/gate.py --brief out/fit/*-vel >> $L/foss9.log 2>&1
log "done"
