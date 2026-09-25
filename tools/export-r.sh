#!/bin/sh
# The card's worlds again from the refitted sets (tools/refit1.sh, refit2.sh),
# voiced (export.py --voice), each at its card copy's level (--level-of), into
# out/worlds-r/ — the card's own worlds are not touched. Gated worlds as they
# were, --gate --clean; the rest plain. The string and guitar families from
# their strings, as tools/foss6.sh built them. Then the audit of old and new,
# and out/card-r/: the card with the new worlds in place, for an A/B.
set -u
cd "$(dirname "$0")/.."
PY=${PY:-$HOME/fmexplorer/bin/python}
R=${R:-out/worlds-r}; C=out/card/kyklophoria; L=${LOG:-out/overnight/export-r.log}; AUD=${AUD:-out/audit-r}; CARD=${CARD:-out/card-r}
mkdir -p $R $AUD
: > $L

one() { # world, fit set, gated?
    w=$1; d=$2; g=$3
    flags="--voice --level-of=$C/$w.kykm"
    [ "$g" = gated ] && flags="--gate --clean $flags"
    $PY tools/export.py records $d $R/$w.kykm $flags > $R/$w.log 2>&1 || echo "FAILED $w" >> $L
    echo "$w: $(grep -m1 'voice:' $R/$w.log | cut -c1-160)" >> $L
}
fam() { # family world, prefix of fit sets, member prefix, NAME=string ...
    w=$1; fit=$2; mem=$3; shift 3
    args=""
    for pair in "$@"; do
        nm=${pair%%=*}; s=${pair#*=}
        lv=""; [ -f out/worlds/$mem-$s.kykm ] && lv="--level-of=out/worlds/$mem-$s.kykm"   # the string the card's family was built from
        $PY tools/export.py records out/fit/$fit-$s $R/$mem-$s.kykm --gate --clean --voice $lv > $R/$mem-$s.log 2>&1 || echo "FAILED $mem-$s" >> $L
        args="$args $nm=$R/$mem-$s.kykm"
    done
    $PY tools/export.py family $R/$w.kykm $args > $R/$w.log 2>&1 || echo "FAILED $w" >> $L
    echo "$w: family of$(echo $args | wc -w) strings" >> $L
}

# single-set worlds, three at a time
cat > $R/jobs.txt <<'J'
piano piano -
piano-iowa piano-iowa -
piano-iowa-vel piano-iowa3 gated
piano-salamander piano-salamander gated
piano-vcsl piano-vcsl -
upright-knight upright-knight2 gated
wurli wurli -
ep ep -
guitar guitar -
banjo banjo -
mandolin mandolin -
harp-concert harp-concert gated
marimba-vel marimba-vel gated
vibraphone-vel vibraphone-vel gated
bells-vel bells-vel gated
crotales-iowa crotales-iowa -
J
while read w d g; do
    while [ "$(jobs -p | wc -l)" -ge 3 ]; do sleep 2; done
    one $w out/fit/$d $g &
done < $R/jobs.txt
wait
fam bass-pizz-vel bass-pizz3 bass-pizzv E1=sule A1=sula D2=suld G2=sulg &
fam cello-pizz-vel cello-pizz3 cello-pizzv C2=sulc G2=sulg D3=suld A3=sula &
fam viola-pizz-vel viola-pizz3 viola-pizzv C3=sulc G3=sulg D4=suld A4=sula &
wait
fam violin-pizz-vel violin-pizz3 violin-pizzv G3=sulg D4=suld A4=sula E5=sule &
fam guitar-str-vel guitar3 guitarv E2=sule A2=sula D3=suld G3=sulg B3=sulb E4=sul_e &
wait
# the audit, old beside new
for f in $C/*.kykm; do
    w=$(basename $f .kykm)
    [ -f $R/$w.kykm ] || continue
    build/worldaudit $f --tsv $AUD/$w-old.tsv > $AUD/$w.txt 2>&1
    build/worldaudit $R/$w.kykm --tsv $AUD/$w-new.tsv >> $AUD/$w.txt 2>&1
done
python3 tools/card.py --prefer $R --out $CARD >> $L 2>&1
echo "done $(date +%H:%M)" >> $L
