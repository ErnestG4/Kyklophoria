#!/bin/sh
# the Iowa grand a fourth time: the polarity (iowa3), the loss hearing the
# onset's peak (the treble's pop was a smooth 1.5 ms bump the harshness term
# missed), and the contact's impulse capped at an elastic collision's (a
# near-rigid felt ran away struck again); smoothed, re-rendered, baked, gated
# for level, and only then a card
cd /home/combust/Daisy/ModalBake
D=out/exc/iowa4
xargs -P 6 -L 1 sh -c 'cd /home/combust/Daisy/ModalBake; m=$0; EXCFIT_RESULTS=out/exc/iowa4/results.tsv EXCFIT_WEIGHTS=ref EXCFIT_STARTS=5 ./build/excfit out/card8/kyklophoria/piano-iowa-vel.kykm "$m" out/wav/excfit/iowa4/n$m "$@" > out/exc/iowa4/logs/$m.log 2>&1; echo "$(date +%H:%M:%S) note $m done" >> out/exc/iowa4/progress.log' < $D/jobs.txt
echo "$(date +%H:%M:%S) TRAINED" >> $D/progress.log
python3 tools/excsmooth.py $D/results.tsv $D/smooth.tsv >> $D/progress.log 2>&1
: > $D/results-smooth.tsv
while IFS="$(printf '\t')" read -r m params; do
  takes=$(awk -v m="$m" '$1 == m { $1 = ""; print }' $D/jobs.txt)
  [ -n "$takes" ] && echo "$m|$params|$takes"
done < $D/smooth.tsv | xargs -P 6 -I{} sh -c 'cd /home/combust/Daisy/ModalBake; line="{}"; m=${line%%|*}; rest=${line#*|}; params=${rest%%|*}; takes=${rest#*|}; EXCFIT_PARAMS="$params" EXCFIT_RESULTS=out/exc/iowa4/results-smooth.tsv EXCFIT_WEIGHTS=ref ./build/excfit out/card8/kyklophoria/piano-iowa-vel.kykm "$m" out/wav/excfit/iowa4-smooth/n$m $takes > out/exc/iowa4/logs-smooth/$m.log 2>&1'
./build/excbake out/card8/kyklophoria/piano-iowa-vel.kykm $D/results-smooth.tsv out/exc/piano-iowa-vel-4.kykm >> $D/progress.log 2>&1
if ./build/exclevel out/exc/piano-iowa-vel-4.kykm > $D/level.txt 2>&1; then
  mkdir -p out/card-exc4/kyklophoria
  cp out/card8/kyklophoria/*.kykm out/card-exc4/kyklophoria/
  cp out/exc/piano-iowa-vel-4.kykm out/card-exc4/kyklophoria/piano-iowa-vel.kykm
  echo "$(date +%H:%M:%S) FINISHED, level safe: out/card-exc4/kyklophoria" >> $D/progress.log
else
  echo "$(date +%H:%M:%S) LEVEL CHECK FAILED — no card written (level.txt)" >> $D/progress.log
fi
