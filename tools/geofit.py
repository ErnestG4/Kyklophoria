#!/usr/bin/env python3
"""geofit.py — fit the FEM's inputs to what a recording measured.

    geofit.py out/fit/ep-vel out/geofit-ep.tsv [--notes 48,60,72,79]

The other half of fitting (roadmap, step 4): not the modes of a recording,
but the *geometry* that would make them. For a tine piano the shaped
records give two numbers a note that the metal owns — the fundamental and
the second bending mode's ratio to it — and the FEM tine family has two
knobs that set them: the length, and where the tuning spring sits. The FEM
is not differentiable, so this is Nelder-Mead over the two, a fresh mesh and
a modalfem run per evaluation (a second each at this mesh), minimising
(ln f1/f0)^2 + (ln ratio/ratio_measured)^2. The in-plane bending modes are
told from the out-of-plane ones by their gains at the strike positions.

What it answers: whether a physical geometry exists that matches the
measured ratio, and what it is — a length and a spring position per note,
which is how the instrument was actually built.
"""
import csv
import math
import os
import subprocess
import sys
import tempfile

import numpy as np
from scipy.optimize import minimize

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import meshgen as mg  # noqa: E402

D = 0.002
W = D * math.sqrt(12.0 / 16.0)


def fem(L, d_tip, scale=2.2, ny=3, tmp='/tmp'):
    """(f1, f2) of the in-plane bending modes of a tine of length L with the
    lump d_tip from the free end."""
    nx = int(round(L / (W / ny)))
    lump_from, lump_len = L - d_tip - 0.003, 0.006
    side = lambda x: W * (scale if lump_from <= x <= lump_from + lump_len else 1.0)
    vol = mg.Vol()
    mg.grid(vol, nx, ny, ny, lambda i, j, k: (L * i / nx, side(L * i / nx) * (j / ny - 0.5), side(L * i / nx) * (k / ny - 0.5)))
    expos = [((L * (i + 0.5) / 12, 0.0, 0.5 * side(L * (i + 0.5) / 12)), (0.0, 0.0, -1.0)) for i in range(12)]
    t, e, m = os.path.join(tmp, 'g.tet'), os.path.join(tmp, 'g.expos'), os.path.join(tmp, 'g.mmr')
    mg.write_tet(t, vol)
    mg.write_expos(e, expos)
    subprocess.run(['build/modalfem', '--tet', t, '--expos', e, '--out', m, '--nmodes', '10', '--quiet', '--clamp', '0', '-1e-9', '1e-9'], capture_output=True)
    modes = []
    for l in open(m):
        if l.startswith('mode '):
            w = l.split()
            modes.append((float(w[3]), max(abs(float(v)) for v in w[7:])))
    # the two polarisations of a bending mode sit within ~15% of each other
    # (the hex-to-tet split is not symmetric in y and z); cluster them and
    # keep the one the strike reaches, then the two lowest clusters the
    # strike reaches at all
    if not modes:
        return None
    clusters = []
    for f, g in sorted(modes):
        if clusters and f / clusters[-1][0] < 1.15:
            if g > clusters[-1][1]:
                clusters[-1] = (f, g)
        else:
            clusters.append((f, g))
    thr = max(g for _, g in clusters) * 0.2
    inplane = [f for f, g in clusters if g > thr]
    return (inplane[0], inplane[1]) if len(inplane) >= 2 else None


def main():
    recdir, out = sys.argv[1], sys.argv[2]
    notes = [48, 60, 72, 79]
    if '--notes' in sys.argv:
        notes = [int(v) for v in sys.argv[sys.argv.index('--notes') + 1].split(',')]
    rows = {int(r['value']): r for r in csv.DictReader(open(os.path.join(recdir, 'fits.tsv')), delimiter='\t')}
    tmp = tempfile.mkdtemp()
    with open(out, 'w') as o:
        o.write('midi\tf0_hz\tratio_measured\tlength_mm\tspring_from_tip_mm\tf1_fem\tratio_fem\tevaluations\n')
        for midi in notes:
            r = rows.get(midi)
            if not r:
                print('no record for midi', midi); continue
            f0 = 440.0 * 2 ** ((midi - 69) / 12)
            modes = [float(l.split()[3]) for l in open(os.path.join(recdir, r['id'] + '.mmr')) if l.startswith('mode ')]
            m2 = [f / f0 for f in modes if 5.0 < f / f0 < 8.0]
            if not m2:
                print('midi %d: no second bending mode in the record' % midi); continue
            ratio = m2[0]
            evals = [0]

            def cost(p):
                L, frac = math.exp(p[0]), 1 / (1 + math.exp(-p[1]))     # length, spring position as a fraction of L from the tip
                d = 0.004 + frac * (L - 0.014)
                res = fem(L, d, tmp=tmp)
                evals[0] += 1
                if res is None:
                    return 10.0
                f1, f2 = res
                return math.log(f1 / f0) ** 2 + math.log((f2 / f1) / ratio) ** 2

            # start from the family's own scaling: f1 ~ 1/L^2, the 8 cm tine at 195 Hz
            L0 = 0.08 * math.sqrt(195.0 / f0)
            # a starting simplex that actually moves the spring: Nelder-Mead's
            # default step at a zero coordinate is 0.00025, and it sat still
            x0 = [math.log(L0), 0.0]
            best = minimize(cost, x0, method='Nelder-Mead',
                            options={'xatol': 1e-3, 'fatol': 1e-6, 'maxiter': 80,
                                     'initial_simplex': [x0, [x0[0] + 0.15, 0.0], [x0[0], 1.5]]})
            L, frac = math.exp(best.x[0]), 1 / (1 + math.exp(-best.x[1]))
            d = 0.004 + frac * (L - 0.014)
            f1, f2 = fem(L, d, tmp=tmp)
            o.write('%d\t%.1f\t%.3f\t%.1f\t%.1f\t%.1f\t%.3f\t%d\n' % (midi, f0, ratio, L * 1000, d * 1000, f1, f2 / f1, evals[0]))
            o.flush()
            print('midi %3d  f0 %6.1f  measured ratio %.2f  ->  length %.1f mm, spring %.1f mm from tip: f1 %.1f, ratio %.2f  (%d FEMs)' % (midi, f0, ratio, L * 1000, d * 1000, f1, f2 / f1, evals[0]))


if __name__ == '__main__':
    main()
