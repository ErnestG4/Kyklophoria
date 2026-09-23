#!/bin/sh
# Stage eight: the Iowa mallets again, one articulation each, with real
# dynamics. Combust: "Marimba-iowa world has a repeating note on F3. Like it
# was trained on a roll. It's also on several from F6 upwards." It was: stage
# two split every file in each Iowa percussion folder together, and those
# folders mix rolls, dead strokes and five mallets (marimba), bowed, dampened
# and short notes (vibraphone), glissandi (xylophone), brass and plastic
# (bells); the splitter kept whichever take of a note came first.
#
# One normal strike each: marimba yarn, vibraphone sustain, xylophone
# rosewood, bells plastic; pp, mf and ff split apart (the splitter that
# starts a note at its onset) and fitted together as velocity layers. Two
# lanes, resumable; worlds under new names (-vel), --gate --clean.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
L=out/overnight; mkdir -p $L
P=samples/foss/iowa/Percussion
log() { echo "== $* $(date +%H:%M:%S)" >> $L/foss8.log; }

split() { # name, folder, articulation
    name=$1; dir=$2; art=$3
    for dyn in pp mf ff; do
      ls $P/$dir/*.$art.$dyn.*.aif* >/dev/null 2>&1 || continue
      mkdir -p out/gen/$name-mset
      [ -d out/gen/$name-m-$dyn ] || $PY tools/splitnotes.py out/gen/$name-m-$dyn $P/$dir/*.$art.$dyn.*.aif* --dynamic $dyn >> $L/foss8.log 2>&1
      ln -sfn ../$name-m-$dyn out/gen/$name-mset/$dyn
    done
}
if [ ! -f $L/foss8-split.done ]; then
  split marimba Marimba yarn
  split vibraphone Vibraphone sustain
  split xylophone Xylophone rosewood
  split bells Bells plastic
  touch $L/foss8-split.done; log "split"
fi

lane() { # name, family, max seconds
    name=$1; fam=$2; secs=$3
    [ -f $L/foss8-$name.done ] && return
    [ -d out/gen/$name-mset ] || return
    log "$name"
    nice -n 10 $PY -u tools/fitset.py $fam out/gen/$name-mset out/fit/$name-vel --layout folders --resume \
        --steps 1200 --max-seconds $secs >> $L/foss8-$name.log 2>&1 && touch $L/foss8-$name.done
    $PY tools/bursts.py out/fit/$name-vel --ms auto >> $L/foss8.log 2>&1
    $PY tools/export.py records out/fit/$name-vel out/worlds/$name-vel.kykm --gate --clean >> $L/foss8.log 2>&1
    log "world $name-vel"
}
( lane marimba marimba 6.0;  lane bells bells 6.0 ) &
( lane vibraphone vibraphone 6.0;  lane xylophone xylophone 4.0 ) &
wait
$PY tools/pitchcheck.py out/fit/marimba-vel out/fit/vibraphone-vel out/fit/xylophone-vel out/fit/bells-vel >> $L/foss8.log 2>&1
python3 tools/gate.py --brief out/fit/*-vel >> $L/foss8.log 2>&1
log "done"
