#!/bin/sh
# after run.sh: smooth the grand's hammers across the keys, write each note's
# final weights and levels with them, bake the version 9 world and a card.
# Stop it by PID.
cd /home/combust/Daisy/ModalBake
D=out/exc/iowa
python3 tools/excsmooth.py $D/results.tsv $D/smooth.tsv >> $D/progress.log 2>&1
: > $D/results-smooth.tsv
mkdir -p $D/logs-smooth out/wav/excfit/iowa-smooth
while IFS="$(printf '\t')" read -r m params; do
  takes=$(awk -v m="$m" '$1 == m { $1 = ""; print }' $D/jobs.txt)
  [ -n "$takes" ] && echo "$m|$params|$takes"
done < $D/smooth.tsv | xargs -P 6 -I{} sh -c 'cd /home/combust/Daisy/ModalBake; line="{}"; m=${line%%|*}; rest=${line#*|}; params=${rest%%|*}; takes=${rest#*|}; EXCFIT_PARAMS="$params" EXCFIT_RESULTS=out/exc/iowa/results-smooth.tsv EXCFIT_WEIGHTS=ref ./build/excfit-next out/card8/kyklophoria/piano-iowa-vel.kykm "$m" out/wav/excfit/iowa-smooth/n$m $takes > out/exc/iowa/logs-smooth/$m.log 2>&1'
./build/excbake out/card8/kyklophoria/piano-iowa-vel.kykm $D/results-smooth.tsv out/exc/piano-iowa-vel.kykm >> $D/progress.log 2>&1
mkdir -p out/card-exc/kyklophoria
cp out/card8/kyklophoria/*.kykm out/card-exc/kyklophoria/
cp out/exc/piano-iowa-vel.kykm out/card-exc/kyklophoria/piano-iowa-vel.kykm
echo "$(date +%H:%M:%S) FINISHED: out/card-exc/kyklophoria" >> $D/progress.log
