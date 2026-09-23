#!/bin/sh
# Stage six: real dynamics. Every plucked and struck world so far was fitted
# at ONE dynamic (the strings and the guitar at ff, the Iowa piano's ff, mf
# and pp each on their own and never one world), so velocity only scaled a
# single recording. Iowa recorded all of them at pp, mf and ff; fitted
# together, export's layer() makes each point three velocity layers.
#
#   lane A: the Iowa piano, pp/mf/ff in one set (~260 notes), then the
#           guitar string by string, pp/mf/ff
#   lane B: double bass, cello, viola, violin pizzicato, string by string,
#           pp/mf/ff, every take split by the splitter that starts a note at
#           its pluck (stage five's), articulation pizz only
#
# Two lanes, resumable (fitset --resume, a .done a lane). Worlds are new
# names (-vel), exported --gate --clean, families in string order by name.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
L=out/overnight; mkdir -p $L
I=samples/foss/iowa
log() { echo "== $* $(date +%H:%M:%S)" >> $L/foss6.log; }

if [ ! -f $L/foss6-split.done ]; then
  for pair in doublebass:bass cello:cello viola:viola violin:violin; do
    dir=${pair%%:*}; inst=${pair##*:}
    for dyn in pp mf ff; do
      [ -d out/gen/$inst-pizz3-$dyn ] || $PY tools/splitnotes.py out/gen/$inst-pizz3-$dyn $I/Strings/$dir/*pizz*.aif* \
          --per-string --dynamic $dyn --articulation pizz >> $L/foss6.log 2>&1
    done
  done
  for dyn in pp mf ff; do
    [ -d out/gen/guitar3-$dyn ] || $PY tools/splitnotes.py out/gen/guitar3-$dyn $I/Piano_Other/guitar/*.aif* --per-string --dynamic $dyn >> $L/foss6.log 2>&1
  done
  touch $L/foss6-split.done
  log "split"
fi

lane() { # name, family, indir, outdir, max seconds
    name=$1; fam=$2; ind=$3; outd=$4; secs=$5
    [ -f $L/foss6-$name.done ] && return
    [ -d "$ind" ] || return
    log "$name"
    nice -n 10 $PY -u tools/fitset.py "$fam" "$ind" "$outd" --layout folders --resume \
        --steps 1200 --max-seconds $secs >> $L/foss6-$name.log 2>&1 && touch $L/foss6-$name.done
}
strings() { # inst (bass cello viola violin guitar), gen prefix, fit prefix, max seconds
    inst=$1; gen=$2; fitp=$3; secs=$4
    for s in out/gen/$gen-ff/*/; do
      [ -d "$s" ] || continue
      str=$(basename "$s")
      mkdir -p out/gen/$fitp-$str
      for dyn in pp mf ff; do
        [ -d out/gen/$gen-$dyn/$str ] && ln -sfn ../$gen-$dyn/$str out/gen/$fitp-$str/$dyn
      done
      lane $fitp-$str $inst out/gen/$fitp-$str out/fit/$fitp-$str $secs
    done
}

( lane piano-iowa3 piano out/gen/piano-iowa out/fit/piano-iowa3 5.0
  strings guitar guitar3 guitar3 4.0 ) &
( strings bass bass-pizz3 bass-pizz3 5.0
  strings cello cello-pizz3 cello-pizz3 4.0
  strings viola viola-pizz3 viola-pizz3 3.0
  strings violin violin-pizz3 violin-pizz3 3.0 ) &
wait
log "fits done"

members() { # fit prefix, world prefix: bursts, gated+cleaned export of each string
    for d in out/fit/$1-sul*; do
      [ -f "$d/fits.tsv" ] || continue
      s=${d##*-}
      $PY tools/bursts.py "$d" --ms auto >> $L/foss6.log 2>&1
      $PY tools/export.py records "$d" out/worlds/$2-$s.kykm --gate --clean >> $L/foss6.log 2>&1
    done
}
if [ -f out/fit/piano-iowa3/fits.tsv ]; then
  $PY tools/bursts.py out/fit/piano-iowa3 --ms auto >> $L/foss6.log 2>&1
  $PY tools/export.py records out/fit/piano-iowa3 out/worlds/piano-iowa-vel.kykm --gate --clean >> $L/foss6.log 2>&1
fi
members bass-pizz3 bass-pizzv
$PY tools/export.py family out/worlds/bass-pizz-vel.kykm E1=out/worlds/bass-pizzv-sule.kykm A1=out/worlds/bass-pizzv-sula.kykm \
    D2=out/worlds/bass-pizzv-suld.kykm G2=out/worlds/bass-pizzv-sulg.kykm >> $L/foss6.log 2>&1
members cello-pizz3 cello-pizzv
$PY tools/export.py family out/worlds/cello-pizz-vel.kykm C2=out/worlds/cello-pizzv-sulc.kykm G2=out/worlds/cello-pizzv-sulg.kykm \
    D3=out/worlds/cello-pizzv-suld.kykm A3=out/worlds/cello-pizzv-sula.kykm >> $L/foss6.log 2>&1
members viola-pizz3 viola-pizzv
$PY tools/export.py family out/worlds/viola-pizz-vel.kykm C3=out/worlds/viola-pizzv-sulc.kykm G3=out/worlds/viola-pizzv-sulg.kykm \
    D4=out/worlds/viola-pizzv-suld.kykm A4=out/worlds/viola-pizzv-sula.kykm >> $L/foss6.log 2>&1
members violin-pizz3 violin-pizzv
$PY tools/export.py family out/worlds/violin-pizz-vel.kykm G3=out/worlds/violin-pizzv-sulg.kykm D4=out/worlds/violin-pizzv-suld.kykm \
    A4=out/worlds/violin-pizzv-sula.kykm E5=out/worlds/violin-pizzv-sule.kykm >> $L/foss6.log 2>&1
members guitar3 guitarv
$PY tools/export.py family out/worlds/guitar-str-vel.kykm E2=out/worlds/guitarv-sule.kykm A2=out/worlds/guitarv-sula.kykm \
    D3=out/worlds/guitarv-suld.kykm G3=out/worlds/guitarv-sulg.kykm B3=out/worlds/guitarv-sulb.kykm E4=out/worlds/guitarv-sul_e.kykm >> $L/foss6.log 2>&1
python3 tools/gate.py --brief out/fit/piano-iowa3 out/fit/*-pizz3-* out/fit/guitar3-* >> $L/foss6.log 2>&1
log "done"
