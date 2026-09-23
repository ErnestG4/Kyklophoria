#!/bin/sh
# Stage five: the double bass and the guitar fitted again, string by string,
# from splits that start where the note does — then stage four.
#
# The note-start audit found splitnotes cutting a note where its envelope
# rose fastest, which is a swell in the ring when the pluck's own segment
# was let go: 33 of the plucked bass's 55 splits started over 40 ms late,
# its E1 800 ms and its A#1 1.3 s into the ring, and the guitar's high-e B4
# 360 ms in. A fit of the middle of a ring has no attack to learn, and its
# burst replays the middle of a ring. splitnotes now walks each cut back to
# the silence before the pluck (never into the last note kept) and writes
# splits.tsv saying where every note came from.
#
# Waits for stage three to let go of the GPU; two lanes, resumable. The
# worlds are exported under their old names (bass-pizz, guitar-strings, and
# their strings) with the gate, in string order, and stage four follows.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
L=out/overnight; mkdir -p $L
I=samples/foss/iowa
log() { echo "== $* $(date +%H:%M:%S)" >> $L/foss5.log; }

if [ ! -f $L/foss5-split.done ]; then
  [ -d out/gen/bass-pizz2-ff ] || $PY tools/splitnotes.py out/gen/bass-pizz2-ff $I/Strings/doublebass/*pizz*.aif* --per-string --dynamic ff --articulation pizz >> $L/foss5.log 2>&1
  [ -d out/gen/guitar2-ff ] || $PY tools/splitnotes.py out/gen/guitar2-ff $I/Piano_Other/guitar/*.aif* --per-string --dynamic ff >> $L/foss5.log 2>&1
  touch $L/foss5-split.done
  log "split"
fi

n=0
while ! grep -q '^== done' $L/foss3.log 2>/dev/null; do
  n=$((n+1)); [ $n -gt 2880 ] && { log "gave up waiting for foss3"; exit 1; }
  sleep 30
done
log "foss3 done, fitting"

lane() { # name, family, indir, outdir, max seconds
    name=$1; fam=$2; ind=$3; outd=$4; secs=$5
    [ -f $L/foss5-$name.done ] && return
    [ -d "$ind" ] || return
    log "$name"
    nice -n 10 $PY -u tools/fitset.py "$fam" "$ind" "$outd" --layout folders --resume \
        --steps 1200 --max-seconds $secs >> $L/foss5-$name.log 2>&1 && touch $L/foss5-$name.done
}
strings() { # inst, gen dir, max seconds
    inst=$1; gen=$2; secs=$3
    for s in out/gen/$gen/*/; do
      [ -d "$s" ] || continue
      str=$(basename "$s")
      mkdir -p out/gen/$inst-$str && ln -sfn ../$gen/$str out/gen/$inst-$str/ff
      lane $inst-$str $(echo $inst | sed 's/[-0-9].*//') out/gen/$inst-$str out/fit/$inst-$str $secs
    done
}
( strings bass-pizz2 bass-pizz2-ff 5.0 ) &
( strings guitar2 guitar2-ff 4.0 ) &
wait
log "fits done"

world() { # fit prefix, world prefix
    for d in out/fit/$1-sul*; do
      [ -f "$d/fits.tsv" ] || continue
      s=${d##*-}
      $PY tools/bursts.py "$d" --ms auto >> $L/foss5.log 2>&1
      $PY tools/export.py records "$d" out/worlds/$2-$s.kykm --gate >> $L/foss5.log 2>&1
      $PY tools/ringers.py "$d" >> $L/foss5.log 2>&1
    done
}
world bass-pizz2 bass-pizz
$PY tools/export.py family out/worlds/bass-pizz.kykm E1=out/worlds/bass-pizz-sule.kykm A1=out/worlds/bass-pizz-sula.kykm \
    D2=out/worlds/bass-pizz-suld.kykm G2=out/worlds/bass-pizz-sulg.kykm >> $L/foss5.log 2>&1
world guitar2 guitar
$PY tools/export.py family out/worlds/guitar-strings.kykm E2=out/worlds/guitar-sule.kykm A2=out/worlds/guitar-sula.kykm \
    D3=out/worlds/guitar-suld.kykm G3=out/worlds/guitar-sulg.kykm B3=out/worlds/guitar-sulb.kykm E4=out/worlds/guitar-sul_e.kykm >> $L/foss5.log 2>&1
python3 tools/gate.py --brief out/fit/bass-pizz2-* out/fit/guitar2-* >> $L/foss5.log 2>&1
log "done"
exec tools/foss4.sh
