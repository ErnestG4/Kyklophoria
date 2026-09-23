#!/bin/sh
# The free libraries, stage three: the strings PLUCKED, and the two free
# grands. Resumable throughout (fitset --resume, a .done a lane), two lanes
# only — three have taken WSL down against its 12 GB cap.
#
# Why pizzicato: stage two fitted the Iowa strings from their ARCO takes,
# and a bowed note is the wrong thing to fit a struck bank to — the attack
# a burst replays is a bow catching, the partials smear under vibrato, and
# violin and viola came out thin (20 of 307 points under 8 modes, five a
# bare sine). Iowa recorded the same four instruments pizzicato, string by
# string, at pp/mf/ff. And the double bass was never fitted at all: stage
# two looked for Strings/bass and Iowa calls it Strings/doublebass, so the
# card's bass is still the 12-note Philharmonia fit.
#
# Every world lands under a NEW name (…-pizz-…, piano-salamander,
# piano-vcsl) beside the ones on the card, never over them: which is
# better is for ears on the module to say.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
L=out/overnight; mkdir -p $L
I=samples/foss/iowa/Strings
log() { echo "== $* $(date +%H:%M:%S)" >> $L/foss3.log; }

# ── split the pizzicato, one directory per string ────────────────────
if [ ! -f $L/foss3-split.done ]; then
  for pair in doublebass:bass cello:cello viola:viola violin:violin; do
    dir=${pair%%:*}; inst=${pair##*:}
    log "split $inst pizz"
    $PY tools/splitnotes.py out/gen/$inst-pizz-ff $I/$dir/*pizz*.aif* --per-string --dynamic ff >> $L/foss3.log 2>&1
  done
  touch $L/foss3-split.done
fi

lane() { # name, family, indir, outdir, extra args
    name=$1; fam=$2; ind=$3; outd=$4; shift 4
    [ -f $L/foss3-$name.done ] && return
    [ -d "$ind" ] || return
    log "$name"
    nice -n 10 $PY -u tools/fitset.py "$fam" "$ind" "$outd" --resume "$@" >> $L/foss3-$name.log 2>&1 && touch $L/foss3-$name.done
}

strings() { # inst, max seconds
    inst=$1; secs=$2
    for s in out/gen/$inst-pizz-ff/*/; do
      [ -d "$s" ] || continue
      str=$(basename "$s")
      mkdir -p out/gen/$inst-pizz-$str && ln -sfn ../$inst-pizz-ff/$str out/gen/$inst-pizz-$str/ff
      lane $inst-pizz-$str $inst out/gen/$inst-pizz-$str out/fit/$inst-pizz-$str --layout folders --steps 1200 --max-seconds $secs
    done
}

# the world for a finished instrument: bursts, one world a string, and the
# family whose position walks the neck (--by-pitch: low string to high)
world() { # inst
    inst=$1; args=""
    for d in out/fit/$inst-pizz-sul*; do
      [ -f "$d/fits.tsv" ] || continue
      n=$(basename "$d")
      $PY tools/bursts.py "$d" --ms auto >> $L/foss3.log 2>&1
      $PY tools/export.py records "$d" out/worlds/$n.kykm >> $L/foss3.log 2>&1
      $PY tools/ringers.py "$d" >> $L/foss3.log 2>&1
      [ -f out/worlds/$n.kykm ] && args="$args out/worlds/$n.kykm"
    done
    [ -n "$args" ] && $PY tools/export.py family --by-pitch out/worlds/$inst-pizz.kykm $args >> $L/foss3.log 2>&1
    log "world $inst-pizz"
}

# lane A: the double bass first (the chimes Combust hears are the old
# fit), then the cello, then the Salamander grand at four of its sixteen
# velocity layers
( strings bass 5.0; world bass
  strings cello 4.0; world cello
  mkdir -p out/gen/piano-sal4
  for v in v04 v08 v12 v16; do ln -sfn ../piano-salamander/$v out/gen/piano-sal4/$v; done
  lane piano-salamander piano out/gen/piano-sal4 out/fit/piano-salamander --layout folders --steps 1200 --max-seconds 5.0
) &

# lane B: violin, viola, then the VCSL grand at its three layers
( strings violin 3.0; world violin
  strings viola 3.0; world viola
  lane piano-vcsl piano out/gen/piano-vcsl out/fit/piano-vcsl --layout folders --steps 1200 --max-seconds 5.0
) &
wait
for n in piano-salamander piano-vcsl; do
  d=out/fit/$n
  [ -f "$d/fits.tsv" ] || continue
  $PY tools/bursts.py "$d" --ms auto >> $L/foss3.log 2>&1
  $PY tools/export.py records "$d" out/worlds/$n.kykm >> $L/foss3.log 2>&1
  $PY tools/ringers.py "$d" >> $L/foss3.log 2>&1
done
$PY tools/fundcheck.py >> $L/foss3.log 2>&1
log "done"
