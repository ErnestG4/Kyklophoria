#!/bin/sh
# the Iowa grand a fifth time: iowa4's (the polarity, the onset's peak, the
# contact's impulse capped) and a hammer's numbers bounded where a hammer's
# are (the C2 had found K 4.4e13, mu 30, 0.017-29.6 m/s). Smoothed and raw
# both baked and gated for level; a card only from one that passes,
# smoothed first
cd /home/combust/Daisy/ModalBake
D=out/exc/iowa5
xargs -P 6 -L 1 sh -c 'cd /home/combust/Daisy/ModalBake; m=$0; EXCFIT_RESULTS=out/exc/iowa5/results.tsv EXCFIT_WEIGHTS=ref EXCFIT_STARTS=5 ./build/excfit out/card8/kyklophoria/piano-iowa-vel.kykm "$m" out/wav/excfit/iowa5/n$m "$@" > out/exc/iowa5/logs/$m.log 2>&1; echo "$(date +%H:%M:%S) note $m done" >> out/exc/iowa5/progress.log' < $D/jobs.txt
echo "$(date +%H:%M:%S) TRAINED" >> $D/progress.log
python3 tools/excsmooth.py $D/results.tsv $D/smooth.tsv >> $D/progress.log 2>&1
: > $D/results-smooth.tsv
while IFS="$(printf '\t')" read -r m params; do
  takes=$(awk -v m="$m" '$1 == m { $1 = ""; print }' $D/jobs.txt)
  [ -n "$takes" ] && echo "$m|$params|$takes"
done < $D/smooth.tsv | xargs -P 6 -I{} sh -c 'cd /home/combust/Daisy/ModalBake; line="{}"; m=${line%%|*}; rest=${line#*|}; params=${rest%%|*}; takes=${rest#*|}; EXCFIT_PARAMS="$params" EXCFIT_RESULTS=out/exc/iowa5/results-smooth.tsv EXCFIT_WEIGHTS=ref ./build/excfit out/card8/kyklophoria/piano-iowa-vel.kykm "$m" out/wav/excfit/iowa5-smooth/n$m $takes > out/exc/iowa5/logs-smooth/$m.log 2>&1'
./build/excbake out/card8/kyklophoria/piano-iowa-vel.kykm $D/results-smooth.tsv out/exc/piano-iowa-vel-5.kykm >> $D/progress.log 2>&1
./build/excbake out/card8/kyklophoria/piano-iowa-vel.kykm $D/results.tsv out/exc/piano-iowa-vel-5raw.kykm >> $D/progress.log 2>&1
pick=""
if ./build/exclevel out/exc/piano-iowa-vel-5.kykm > $D/level.txt 2>&1; then pick=out/exc/piano-iowa-vel-5.kykm
elif ./build/exclevel out/exc/piano-iowa-vel-5raw.kykm > $D/level-raw.txt 2>&1; then pick=out/exc/piano-iowa-vel-5raw.kykm; fi
if [ -n "$pick" ]; then
  mkdir -p out/card-exc5/kyklophoria
  cp out/card8/kyklophoria/*.kykm out/card-exc5/kyklophoria/
  cp "$pick" out/card-exc5/kyklophoria/piano-iowa-vel.kykm
  echo "$(date +%H:%M:%S) FINISHED, level safe ($pick): out/card-exc5/kyklophoria" >> $D/progress.log
else
  ./build/exclevel out/exc/piano-iowa-vel-5raw.kykm > $D/level-raw.txt 2>&1
  echo "$(date +%H:%M:%S) LEVEL CHECK FAILED, smoothed and raw — no card written (level.txt, level-raw.txt)" >> $D/progress.log
fi
