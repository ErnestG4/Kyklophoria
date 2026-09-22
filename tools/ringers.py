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


def band_envelope(x, sr, modes, ratio=1.5, pct=80):
    """Each octave band's longest honest decay in the recording: the pct-th
    percentile of the per-mode track decays in that band, so one partial
    that really does ring is not cut to its neighbours' length, and a band
    where the recording has only noise (a flat track, 60 s) is left alone.
    A body reflects its low tones and absorbs its high ones — the double
    bass's fitted modes at 300-2000 Hz rang 8-16 dB over the recording at
    one second — so the envelope is what the recording says at each
    frequency, not a curve chosen for it."""
    import collections
    per = collections.defaultdict(list)
    for hz, zeta, g, li in modes:
        m = track_t60(x, sr, hz)
        if m is None or m >= 59.9:
            continue
        per[int(math.floor(math.log2(max(hz, 1.0))))].append(m)
    env = {}
    for k, v in per.items():
        if len(v) >= 2:
            env[k] = float(np.percentile(v, pct))
    return env


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('recdir'); ap.add_argument('--ratio', type=float, default=3.0)
    ap.add_argument('--envelope', action='store_true',
                    help="cap every mode at --ratio x its octave band's own decay in the recording: a body absorbs high tones faster than low ones and a fit that does not is a high ring the instrument has not (Combust, on the double bass)")
    ap.add_argument('--fix', action='store_true', help='cap a ringer\'s T60 at the recording\'s, for modes under a tenth of the loudest with no neighbour within 1%%; writes the records')
    a = ap.parse_args()
    rows = [l.split('\t') for l in open(os.path.join(a.recdir, 'fits.tsv')).read().splitlines()[1:]]
    tot = ring = 0; energy_ring = []
    for r in rows:
        rid = r[0]; rp = os.path.join(a.recdir, rid + '.mmr'); tp = os.path.join(a.recdir, rid + '-target.wav')
        if not (os.path.exists(rp) and os.path.exists(tp)):
            continue
        lines = open(rp).read().splitlines()
        modes = [(float(w[3]), float(w[5]), float(w[w.index('gains') + 1]), i) for i, w in ((i, l.split()) for i, l in enumerate(lines)) if w and w[0] == 'mode']
        loud = max((abs(m[2]) for m in modes), default=0.0)
        x, sr = sf.read(tp)
        worst = []; fixed = 0
        for hz, zeta, g, li in modes:
            t60 = 6.91 / (zeta * 2 * math.pi * hz)
            m = track_t60(x, sr, hz)
            tot += 1
            if m is None:
                continue
            if t60 > a.ratio * m and t60 > 0.3:
                ring += 1
                worst.append((t60 / m, hz, t60, m, g))
                # a quiet ringer with no neighbour: the loss could not pin
                # its decay and the cap let it run; the recording's own is
                # the better number. The bass rang at 445 Hz for 3.4 s where
                # the recording says half a second
                if a.fix and abs(g) < 0.1 * loud and not any(abs(q[0] / hz - 1) < 0.01 for q in modes if q[3] != li):
                    w = lines[li].split(); w[5] = '%.6g' % (6.91 / (m * 2 * math.pi * hz)); lines[li] = ' '.join(w); fixed += 1
        if a.fix and fixed:
            open(rp, 'w').write('\n'.join(lines) + '\n')
        if a.envelope:
            env = band_envelope(x, sr, modes)
            capped = 0; worst_cap = 0.0
            for hz, zeta, g, li in modes:
                k = int(math.floor(math.log2(max(hz, 1.0))))
                if k not in env:
                    continue
                cap = a.ratio * env[k]
                t60 = 6.91 / (zeta * 2 * math.pi * hz)
                if t60 > cap:
                    worst_cap = max(worst_cap, t60 / cap)
                    w = lines[li].split(); w[5] = '%.6g' % (6.91 / (cap * 2 * math.pi * hz)); lines[li] = ' '.join(w)
                    capped += 1
            if capped:
                open(rp, 'w').write('\n'.join(lines) + '\n')
                print('  %-12s %2d of %2d modes capped at the band envelope (worst x%.1f)' % (rid, capped, len(modes), worst_cap))
            continue
        worst.sort(reverse=True)
        if worst:
            print('  %-12s %2d ringers%s: %s' % (rid, len(worst), ' (%d capped)' % fixed if fixed else '', '  '.join('%.0fHz fit %.1fs rec %.2fs (x%.0f, gain %.3f)' % (h, t, m, q, g) for q, h, t, m, g in worst[:3])))
    if a.envelope:
        print('%s: capped at x%.1f of each octave band\'s own decay in the recording' % (a.recdir, a.ratio))
    else:
        print('%s: %d of %d modes ring more than x%.0f longer than the recording at their frequency' % (a.recdir, ring, tot, a.ratio))


if __name__ == '__main__':
    main()
