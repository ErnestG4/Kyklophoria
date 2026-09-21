#!/usr/bin/env python3
"""pitchcheck.py <fit dir> [...]   — every record's label against its audio

The Wurlitzer shipped with a C2 file labelled C3 (a manifest pitched by an
earlier ear), so the module's B2 played an octave down, and two of its bass
fits had no fundamental at all, so C2 and G2 played an octave up. Combust:
"why can't we actually make this accurate?" This is the check that makes
the first kind impossible to ship: the pitch of each record's source file
(tools/pitchman.py's detector — the lowest line with a harmonic series
over it, which survives a weak fundamental) against the note in fits.tsv.
Past half a semitone is a wrong label and the exit code says so. The
second kind is tools/fundcheck.py.

    ~/fmexplorer/bin/python tools/pitchcheck.py out/fit/wurli out/fit/piano
"""
import os, sys
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(__file__))
from pitchman import pitch, NAMES


def check(fitdir, tol=0.5):
    rows = [l.split('\t') for l in open(os.path.join(fitdir, 'fits.tsv')).read().splitlines()]
    hdr, rows = rows[0], rows[1:]
    ip, iv = hdr.index('param'), hdr.index('value')
    bad = []
    n = 0
    for r in rows:
        if r[ip] != 'midi':
            continue
        src = None
        for w in (l.split(None, 1) for l in open(os.path.join(fitdir, r[0] + '.mmr'))):
            if w and w[0] == 'source':
                src = w[1].strip(); break
        if not src or not os.path.exists(src):
            print('  %-12s source missing (%s)' % (r[0], src)); continue
        n += 1
        x, sr = sf.read(src, always_2d=True); x = x.mean(axis=1)
        f = pitch(x, sr, 21, 108)
        midi = 69 + 12 * np.log2(f / 440)
        lab = float(r[iv])
        d = midi - lab
        if abs(d) > tol:
            m = int(round(midi))
            bad.append('%-12s labelled %3d %-4s  sounds %6.1f Hz = %3d %-4s (%+.1f st)  %s' % (
                r[0], lab, NAMES[int(lab) % 12] + str(int(lab) // 12 - 1), f, m, NAMES[m % 12] + str(m // 12 - 1), d, os.path.basename(src)))
    print('%s: %d note records against their audio, %d mislabelled' % (fitdir, n, len(bad)))
    for b in bad:
        print('    ' + b)
    return len(bad)


if __name__ == '__main__':
    dirs = sys.argv[1:] or sorted(d for d in (os.path.join('out/fit', s) for s in os.listdir('out/fit')) if os.path.exists(os.path.join(d, 'fits.tsv')))
    total = sum(check(d) for d in dirs)
    sys.exit(1 if total else 0)
