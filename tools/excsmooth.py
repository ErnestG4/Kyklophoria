#!/usr/bin/env python3
"""excsmooth.py <results.tsv> <out-params.tsv> [window]

A keyboard's trained hammers smoothed across it. excfit trains each note on
its own takes, and the loss barely constrains some of what it finds — on the
Iowa grand the pp speed wandered from 0.01 to 1.5 m/s between neighbouring
keys, an mf reached 20 m/s — where a real grand's hammers change slowly along
the keys. So each parameter (in logs, alpha as it is) is replaced by the
median of the notes within `window` keys (5 by default), and each note's
speeds are kept in order, pp <= mf <= ff.

In: excfit's results (EXCFIT_RESULTS), the last line for a note winning. Out:
a line a note — midi, then the nine numbers EXCFIT_PARAMS takes (k alpha mu
mass noise nfc speed_pp speed_mf speed_ff) — for the pass that writes the
final weights and levels with the smoothed hammer. Standard library only.
"""
import math
import statistics
import sys

NAMES = ['k', 'alpha', 'mu', 'mass', 'noise', 'nfc', 'sp_pp', 'sp_mf', 'sp_ff']
LOG = {'k', 'mu', 'mass', 'noise', 'nfc', 'sp_pp', 'sp_mf', 'sp_ff'}


def main():
    src, dst = sys.argv[1], sys.argv[2]
    window = int(sys.argv[3]) if len(sys.argv) > 3 else 5
    notes = {}
    for line in open(src):
        f = line.split('\t')
        if len(f) < 14:
            continue
        midi = float(f[0])
        vals = [float(x) for x in f[1:10]]            # k alpha mu mass noise nfc sp_pp sp_mf sp_ff
        notes[round(midi, 2)] = dict(zip(NAMES, vals))
    keys = sorted(notes)
    half = window // 2
    out = []
    for i, m in enumerate(keys):
        near = [notes[keys[j]] for j in range(max(0, i - half), min(len(keys), i + half + 1))]
        sm = {}
        for n in NAMES:
            xs = [math.log(max(v[n], 1e-12)) if n in LOG else v[n] for v in near]
            med = statistics.median(xs)
            sm[n] = math.exp(med) if n in LOG else med
        sm['sp_mf'] = max(sm['sp_mf'], sm['sp_pp'])
        sm['sp_ff'] = max(sm['sp_ff'], sm['sp_mf'])
        out.append((m, sm))
    with open(dst, 'w') as w:
        for m, sm in out:
            w.write('%g\t%s\n' % (m, ' '.join('%.6g' % sm[n] for n in NAMES)))
    sp = [sm['sp_pp'] for _, sm in out]
    print('%s: %d notes smoothed over %d keys; pp speed %.2f..%.2f m/s, ff %.2f..%.2f' %
          (dst, len(out), window, min(sp), max(sp), min(sm['sp_ff'] for _, sm in out), max(sm['sp_ff'] for _, sm in out)))


if __name__ == '__main__':
    main()
