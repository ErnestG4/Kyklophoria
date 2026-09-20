#!/usr/bin/env python3
"""remeasure.py — recompute decay_ratio in a set's fits.tsv from its records
and targets, after a change to the metric. No refit."""
import csv, math, os, sys
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modalfit
d = sys.argv[1]
rows = list(csv.DictReader(open(os.path.join(d, 'fits.tsv')), delimiter='\t'))
fields = rows[0].keys()
for r in rows:
    rec = [(float(l.split()[3]), float(l.split()[5]), float(l.split()[7])) for l in open(os.path.join(d, r['id'] + '.mmr')) if l.startswith('mode ')]
    f = np.array([m[0] for m in rec]); z = np.array([m[1] for m in rec]); a = np.array([m[2] for m in rec])
    x, sr = sf.read(os.path.join(d, r['id'] + '-target.wav'))
    r['decay_ratio'] = '%.3f' % modalfit.decay_ratio(f, z * 2 * math.pi * f, a, x, sr)
with open(os.path.join(d, 'fits.tsv'), 'w') as o:
    w = csv.DictWriter(o, fieldnames=fields, delimiter='\t', lineterminator='\n'); w.writeheader(); w.writerows(rows)
dr = np.array([float(r['decay_ratio']) for r in rows])
print('%s: %d notes, decay ratio x%.2f geometric mean, median x%.2f, under 0.5: %d' % (d, len(rows), np.exp(np.log(dr).mean()), np.median(dr), (dr < 0.5).sum()))
