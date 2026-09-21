#!/usr/bin/env python3
"""ringers.py <recdir> [--ratio 3] — modes that ring longer than the recording
does at their frequency. Per mode, the recording's own decay is read off an
STFT bin track (46 ms Hann, the bin nearest the mode, a line through its
log energy from 50 ms past the strike to where it meets the floor); a mode
whose fitted T60 is more than --ratio times that is a ringer: a partial the
loss did not pin because it is quiet, left to the cap. Printed per record
with the worst three, and the set's totals — the thing to look at when a
render has a high resonance the recording has not."""
import argparse, math, os, sys
import numpy as np, soundfile as sf


def track_t60(x, sr, hz, start=0.05):
    n = 2048; hop = 512
    k = int(round(hz * n / sr))
    if k < 1 or k >= n // 2:
        return None
    w = np.hanning(n)
    e = []
    for s in range(int(start * sr), len(x) - n, hop):
        X = np.fft.rfft(x[s:s + n] * w)
        e.append(np.abs(X[max(0, k - 1):k + 2]).max() ** 2)
    e = np.array(e) + 1e-20
    if len(e) < 4:
        return None
    le = np.log(e)
    floor = np.log(e[-3:].mean() * 2)
    ok = (le > floor) & (le > le.max() - math.log(1e4))
    if ok.sum() < 3:
        return None
    t = np.arange(len(e)) * hop / sr
    p = np.polyfit(t[ok], le[ok], 1)
    if p[0] >= 0:
        return 60.0
    return 6.91 / (-p[0] / 2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('recdir'); ap.add_argument('--ratio', type=float, default=3.0)
    a = ap.parse_args()
    rows = [l.split('\t') for l in open(os.path.join(a.recdir, 'fits.tsv')).read().splitlines()[1:]]
    tot = ring = 0; energy_ring = []
    for r in rows:
        rid = r[0]; rp = os.path.join(a.recdir, rid + '.mmr'); tp = os.path.join(a.recdir, rid + '-target.wav')
        if not (os.path.exists(rp) and os.path.exists(tp)):
            continue
        modes = [(float(w[3]), float(w[5]), float(w[w.index('gains') + 1])) for w in (l.split() for l in open(rp)) if w and w[0] == 'mode']
        x, sr = sf.read(tp)
        worst = []
        for hz, zeta, g in modes:
            t60 = 6.91 / (zeta * 2 * math.pi * hz)
            m = track_t60(x, sr, hz)
            tot += 1
            if m is None:
                continue
            if t60 > a.ratio * m and t60 > 0.3:
                ring += 1
                worst.append((t60 / m, hz, t60, m, g))
        worst.sort(reverse=True)
        if worst:
            print('  %-12s %2d ringers: %s' % (rid, len(worst), '  '.join('%.0fHz fit %.1fs rec %.2fs (x%.0f, gain %.3f)' % (h, t, m, q, g) for q, h, t, m, g in worst[:3])))
    print('%s: %d of %d modes ring more than x%.0f longer than the recording at their frequency' % (a.recdir, ring, tot, a.ratio))


if __name__ == '__main__':
    main()
