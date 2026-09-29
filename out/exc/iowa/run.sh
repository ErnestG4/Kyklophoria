#!/bin/sh
# the Iowa grand's exciters, a note a job, six at a time (excfit is CPU and a
# few MB). Results appended to results.tsv, a log a note. Stop it by PID.
cd /home/combust/Daisy/ModalBake
xargs -P 6 -L 1 sh -c 'cd /home/combust/Daisy/ModalBake; m=$0; EXCFIT_RESULTS=out/exc/iowa/results.tsv EXCFIT_WEIGHTS=ref EXCFIT_STARTS=3 ./build/excfit out/card8/kyklophoria/piano-iowa-vel.kykm "$m" out/wav/excfit/iowa/n$m "$@" > out/exc/iowa/logs/$m.log 2>&1; echo "$(date +%H:%M:%S) note $m done" >> out/exc/iowa/progress.log' < out/exc/iowa/jobs.txt
echo "$(date +%H:%M:%S) ALL DONE" >> out/exc/iowa/progress.log
