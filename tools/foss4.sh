#!/bin/sh
# The free libraries, stage four: VCSL's and VSCO 2's plucked and struck
# instruments — the ones a struck bank models honestly. Waits for stage
# three (tools/foss3.sh) to finish so there are never more than two lanes
# on the GPU, then fits in two resumable lanes, exports, and grades.
#
# Only the takes a strike explains: sustains, never the release samples
# (VCSL keeps key-up thumps beside the notes under Rel/ and Releases/) nor
# the FX, glissandi and tremolo folders. Where a library records one note
# several ways (the balafon's three mallets, the Knight upright's pedal
# up and down) one way is chosen and named here.
#
# Every world is new; tools/gate.py grades each set and tools/card.py
# decides what reaches the card.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
L=out/overnight; mkdir -p $L
V=samples/foss/vcsl; S=samples/foss/vsco2
log() { echo "== $* $(date +%H:%M:%S)" >> $L/foss4.log; }

org() { # set name, then the files
    name=$1; shift
    [ -d out/gen/$name ] && return
    log "organise $name"
    $PY tools/organise.py out/gen/$name "$@" >> $L/foss4.log 2>&1
}

# ── organise ──────────────────────────────────────────────────────────
if [ ! -f $L/foss4-organise.done ]; then
  org kalimba-tz  "$V/Idiophones/Plucked Idiophones/Kalimba, Tanzania"/*.wav
  org kalimba-ke  "$V/Idiophones/Plucked Idiophones/Kalimba, Kenya"/*.wav
  org mbira       "$V/Idiophones/Plucked Idiophones/Mbira dzaVadzimu Nyamaropa, Zimbabwe, Low B"/*.wav
  org nyunga      "$V/Idiophones/Plucked Idiophones/Nyunga Nyunga, Mozambique, Low F"/*.wav
  org harp-concert "$V/Chordophones/Composite Chordophones/Concert Harp"/*.wav
  org harp-folk   "$V/Chordophones/Composite Chordophones/Folk Harp"/*.wav
  org harp-vsco   "$S/Strings/Harp"/*.wav
  org strumstick  "$V/Chordophones/Composite Chordophones/Strumstick/Finger"/*.wav
  org harpsi-flemish "$V/Chordophones/Zithers/Harpsichord, Flemish/Sustains"/*/*.wav
  org harpsi-english "$V/Chordophones/Zithers/Harpsichord, English/Sustains/Normal"/*.wav
  org upright-yamaha "$V/Chordophones/Zithers/Upright Piano, Yamaha/Sustains"/*.wav
  org upright-knight "$V/Chordophones/Zithers/Upright Piano, Knight/Sustains"/*.wav
  org glock       "$V/Idiophones/Struck Idiophones/Glockenspiel"/*.wav
  org tubular     "$V/Idiophones/Struck Idiophones/Tubular Bells 2"/*.wav
  org tubeglock   "$V/Idiophones/Struck Idiophones/Tubular Glockenspiel"/*.wav
  org handchimes  "$V/Idiophones/Struck Idiophones/Hand Chimes"/*.wav
  org balafon     "$V/Idiophones/Struck Idiophones/Balafon/Traditional Mallet"/*.wav
  touch $L/foss4-organise.done
fi

# ── wait for stage three to let go of the GPU ─────────────────────────
n=0
while ! grep -q '^== done' $L/foss3.log 2>/dev/null; do
  n=$((n+1)); [ $n -gt 2880 ] && { log "gave up waiting for foss3"; exit 1; }
  sleep 30
done
log "foss3 done, fitting"

lane() { # name, family, max seconds
    name=$1; fam=$2; secs=$3
    [ -f $L/foss4-$name.done ] && return
    [ -d out/gen/$name ] || return
    log "$name"
    nice -n 10 $PY -u tools/fitset.py "$fam" out/gen/$name out/fit/$name --layout folders --resume \
        --steps 1200 --max-seconds $secs >> $L/foss4-$name.log 2>&1 && touch $L/foss4-$name.done
}

( lane kalimba-tz kalimba 4.0;  lane kalimba-ke kalimba 4.0;  lane mbira mbira 4.0;  lane nyunga nyunga 4.0
  lane glock glock 4.0;  lane tubular tubular 6.0;  lane tubeglock tubeglock 5.0
  lane handchimes chimes 5.0;  lane balafon balafon 3.0 ) &
( lane harp-concert harp 5.0;  lane harp-folk harp 5.0;  lane harp-vsco harp 5.0;  lane strumstick strum 4.0
  lane harpsi-flemish harpsi 4.0;  lane harpsi-english harpsi 4.0
  lane upright-yamaha upright 5.0;  lane upright-knight upright 5.0 ) &
wait
log "fits done"

for n in kalimba-tz kalimba-ke mbira nyunga glock tubular tubeglock handchimes balafon \
         harp-concert harp-folk harp-vsco strumstick harpsi-flemish harpsi-english upright-yamaha upright-knight; do
  d=out/fit/$n
  [ -f "$d/fits.tsv" ] || continue
  $PY tools/bursts.py "$d" --ms auto >> $L/foss4.log 2>&1
  $PY tools/export.py records "$d" out/worlds/$n.kykm >> $L/foss4.log 2>&1
  $PY tools/ringers.py "$d" >> $L/foss4.log 2>&1
done
python3 tools/gate.py --brief out/fit/*/ >> $L/foss4.log 2>&1
log "done"
