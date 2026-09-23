#!/bin/sh
# Stage seven: stage four's VCSL sets, re-labelled and fitted again.
#
# VCSL names its octaves with middle C as C3 for most instruments and
# organise.py read them as C4, so fourteen of stage four's sets were fitted
# an octave low: pitchcheck.py called every note of them mislabelled, and
# since the fitter seeds and keeps the labelled fundamental, each record
# carries a mode an octave under its note (the Knight upright's A4: 216 Hz at
# -12 dB, one of its four loudest). The two harps were named right and stay.
# The tubular glockenspiel read SIX octaves off (its names are not parsed at
# all) and is left out until its naming is read properly.
#
# Organised again with --octave 1 into out/gen/<set>-o, every note pitched
# against its name first (tools/labelcheck.py moves out a note more than 0.7
# semitone from it), fitted into out/fit/<set>2, exported --gate --clean over
# stage four's broken worlds of the same names.
#
# Runs as lane A of the GPU: it waits for the Iowa piano's three-dynamic fit
# (PID given as $1) to finish, fits, then the guitar's three dynamics (stage
# six's lane A, whose shell was stopped so this could go first), then waits
# for stage six's lane B shell (PID $2) and hands over to foss6.sh for its
# exports. Never more than two fitting processes.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
L=out/overnight; mkdir -p $L
V=samples/foss/vcsl
log() { echo "== $* $(date +%H:%M:%S)" >> $L/foss7.log; }
PIANO=${1:-0}; LANEB=${2:-0}

org() { # set name, then the files
    name=$1; shift
    [ -d out/gen/$name-o ] && return
    $PY tools/organise.py out/gen/$name-o --octave 1 "$@" >> $L/foss7.log 2>&1
    $PY tools/labelcheck.py out/gen/$name-o >> $L/foss7.log 2>&1
}
if [ ! -f $L/foss7-organise.done ]; then
  log "organise, an octave up, and check every label"
  org kalimba-tz  "$V/Idiophones/Plucked Idiophones/Kalimba, Tanzania"/*.wav
  org kalimba-ke  "$V/Idiophones/Plucked Idiophones/Kalimba, Kenya"/*.wav
  org mbira       "$V/Idiophones/Plucked Idiophones/Mbira dzaVadzimu Nyamaropa, Zimbabwe, Low B"/*.wav
  org nyunga      "$V/Idiophones/Plucked Idiophones/Nyunga Nyunga, Mozambique, Low F"/*.wav
  org harp-folk   "$V/Chordophones/Composite Chordophones/Folk Harp"/*.wav
  org strumstick  "$V/Chordophones/Composite Chordophones/Strumstick/Finger"/*.wav
  org harpsi-flemish "$V/Chordophones/Zithers/Harpsichord, Flemish/Sustains"/*/*.wav
  org harpsi-english "$V/Chordophones/Zithers/Harpsichord, English/Sustains/Normal"/*.wav
  org upright-yamaha "$V/Chordophones/Zithers/Upright Piano, Yamaha/Sustains"/*.wav
  org upright-knight "$V/Chordophones/Zithers/Upright Piano, Knight/Sustains"/*.wav
  org glock       "$V/Idiophones/Struck Idiophones/Glockenspiel"/*.wav
  org tubular     "$V/Idiophones/Struck Idiophones/Tubular Bells 2"/*.wav
  org handchimes  "$V/Idiophones/Struck Idiophones/Hand Chimes"/*.wav
  org balafon     "$V/Idiophones/Struck Idiophones/Balafon/Traditional Mallet"/*.wav
  touch $L/foss7-organise.done
  log "organised"
fi
[ "${3:-}" = "prep" ] && exit 0

while [ "$PIANO" -gt 0 ] && kill -0 "$PIANO" 2>/dev/null; do sleep 30; done
log "lane A free"

lane() { # name, family, max seconds
    name=$1; fam=$2; secs=$3
    [ -f $L/foss7-$name.done ] && return
    [ -d out/gen/$name-o ] || return
    log "$name"
    nice -n 10 $PY -u tools/fitset.py "$fam" out/gen/$name-o out/fit/${name}2 --layout folders --resume \
        --steps 1200 --max-seconds $secs >> $L/foss7-$name.log 2>&1 && touch $L/foss7-$name.done
}
lane upright-knight upright 5.0;  lane upright-yamaha upright 5.0
lane harpsi-flemish harpsi 4.0;  lane harpsi-english harpsi 4.0
lane strumstick strum 4.0;  lane harp-folk harp 5.0
lane kalimba-tz kalimba 4.0;  lane kalimba-ke kalimba 4.0;  lane mbira mbira 4.0;  lane nyunga nyunga 4.0
lane glock glock 4.0;  lane tubular tubular 6.0;  lane handchimes chimes 5.0;  lane balafon balafon 3.0
log "fits done"
for n in upright-knight upright-yamaha harpsi-flemish harpsi-english strumstick harp-folk kalimba-tz kalimba-ke mbira nyunga glock tubular handchimes balafon; do
  d=out/fit/${n}2
  [ -f "$d/fits.tsv" ] || continue
  $PY tools/bursts.py "$d" --ms auto >> $L/foss7.log 2>&1
  $PY tools/export.py records "$d" out/worlds/$n.kykm --gate --clean >> $L/foss7.log 2>&1
done
$PY tools/pitchcheck.py $(for n in upright-knight upright-yamaha harpsi-flemish harpsi-english strumstick harp-folk kalimba-tz kalimba-ke mbira nyunga glock tubular handchimes balafon; do echo out/fit/${n}2; done) >> $L/foss7.log 2>&1
python3 tools/gate.py --brief out/fit/*2 >> $L/foss7.log 2>&1
log "worlds done"

# stage six's lane A, which this replaced: the guitar's three dynamics
g6() { # name, family, indir, outdir, max seconds
    name=$1; fam=$2; ind=$3; outd=$4; secs=$5
    [ -f $L/foss6-$name.done ] && return
    [ -d "$ind" ] || return
    echo "== $name $(date +%H:%M:%S)" >> $L/foss6.log
    nice -n 10 $PY -u tools/fitset.py "$fam" "$ind" "$outd" --layout folders --resume \
        --steps 1200 --max-seconds $secs >> $L/foss6-$name.log 2>&1 && touch $L/foss6-$name.done
}
for s in out/gen/guitar3-ff/*/; do
  [ -d "$s" ] || continue
  str=$(basename "$s")
  mkdir -p out/gen/guitar3-$str
  for dyn in pp mf ff; do [ -d out/gen/guitar3-$dyn/$str ] && ln -sfn ../guitar3-$dyn/$str out/gen/guitar3-$str/$dyn; done
  g6 guitar3-$str guitar out/gen/guitar3-$str out/fit/guitar3-$str 4.0
done
log "guitar done; waiting for stage six's lane B"
while [ "$LANEB" -gt 0 ] && kill -0 "$LANEB" 2>/dev/null; do sleep 30; done
[ -f out/fit/piano-iowa3/fits.tsv ] && touch $L/foss6-piano-iowa3.done
log "handing over to foss6 for its exports"
exec tools/foss6.sh
