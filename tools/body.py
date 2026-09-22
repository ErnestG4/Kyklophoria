#!/usr/bin/env python3
"""body.py <fit dir> [...] — the radiation envelope of an instrument, measured

A point played at another note is transposed, which carries its whole
spectrum with it: a double bass point taken two octaves up puts its 2 kHz
partials at 8 kHz at the same level, which no bass does. What should stay
put is the body — the air, the plates and the wood radiate and absorb as a
function of *absolute* frequency, not of the note being played — so a
transposed mode wants re-weighting by the instrument's own curve at where
it now sounds over where it was fitted.

This measures that curve rather than assuming one. Over every fitted mode
of every note in a set we have three numbers: the mode's absolute
frequency, its level under its own note's loudest mode, and which partial
of that note it is. Level is modelled as the sum of two things — a source
term that depends on the partial number (a plucked string's own rolloff,
the same at every pitch) and a body term that depends on the octave band
the partial lands in — and the two are separated by alternating medians,
which is robust to the fit's outliers. The body term, normalised to 0 dB
at its loudest band, is the curve; it is written into the world and the
runtime applies its ratio across a transposition.

    ~/fmexplorer/bin/python tools/body.py out/fit/bass out/fit/guitar
"""
import math, os, sys
import numpy as np

EDGES = [62.5 * 2 ** k for k in range(9)]          # 62.5 Hz .. 16 kHz, the wash's bands


def band_of(f):
    for k in range(8):
        if EDGES[k] <= f < EDGES[k + 1]:
            return k
    return 0 if f < EDGES[0] else 7


def measure(fitdir, rounds=12):
    rows = [l.split('\t') for l in open(os.path.join(fitdir, 'fits.tsv')).read().splitlines()]
    hdr, rows = rows[0], rows[1:]
    ip, iv = hdr.index('param'), hdr.index('value')
    obs = []            # (band, harmonic, level dB)
    for r in rows:
        if r[ip] != 'midi':
            continue
        f0 = 440.0 * 2 ** ((float(r[iv]) - 69) / 12)
        ms = []
        for w in (l.split() for l in open(os.path.join(fitdir, r[0] + '.mmr'))):
            if w and w[0] == 'mode':
                hz = float(w[3]); g = max(abs(float(x)) for x in w[w.index('gains') + 1:])
                if g > 0 and hz > 0:
                    ms.append((hz, g))
        if len(ms) < 4:
            continue
        loud = max(g for _, g in ms)
        for hz, g in ms:
            h = int(round(hz / f0))
            if h < 1 or h > 24:
                continue
            obs.append((band_of(hz), h, 20 * math.log10(g / loud)))
    if len(obs) < 40:
        return None, 0
    b = np.zeros(8); s = np.zeros(25)
    B = np.array([o[0] for o in obs]); H = np.array([o[1] for o in obs]); L = np.array([o[2] for o in obs])
    for _ in range(rounds):
        for k in range(8):                      # the body term, given the source term
            m = B == k
            if m.any(): b[k] = np.median(L[m] - s[H[m]])
        for h in range(1, 25):                  # the source term, given the body term
            m = H == h
            if m.any(): s[h] = np.median(L[m] - b[B[m]])
    seen = np.array([(B == k).sum() for k in range(8)])
    b = np.where(seen >= 8, b, np.nan)          # a band with too little in it says nothing
    if np.all(np.isnan(b)):
        return None, len(obs)
    b = b - np.nanmax(b)
    return b, len(obs)


def main():
    for d in sys.argv[1:]:
        b, n = measure(d)
        if b is None:
            print('%-20s not enough modes (%d)' % (d, n)); continue
        print('%-20s %5d modes' % (d, n))
        print('   ' + '  '.join('%5.0fHz' % math.sqrt(EDGES[k] * EDGES[k + 1]) for k in range(8)))
        print('   ' + '  '.join(('%+5.1f  ' % b[k]) if not math.isnan(b[k]) else '   .   ' for k in range(8)))


if __name__ == '__main__':
    main()
