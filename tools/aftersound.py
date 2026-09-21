#!/usr/bin/env python3
"""aftersound.py <recdir> [--db -24] [--apply]

A note's prompt sound and its aftersound (Weinreich: two or three strings
a note, in phase then out of phase, the bridge moving then still) is a
partial with two decays, and a single exponential fitted to it lands
between — over the prompt, under the aftersound. Combust heard the
Wurlitzer's notes die too soon; measured, its C3's fundamental sits 3 to
7 dB under the recording from 0.4 s on while its fitted decay ratio reads
x1.6. Per strong mode this reads the recording's own bin track (the same
46 ms STFT ringers.py uses), asks modalfit.knee_of whether it falls fast
then slow, and where it does and the mode has no fitted neighbour within
1% (a pair is already a double decay) splits the mode into two at the
same frequency and phase: the fast rate at a/(1+s), the slow at a s/(1+s),
so the strike is what it was and the tail is what the recording says.
--apply writes the records; without it, it reports."""
import argparse, math, os, sys
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modalfit  # noqa: E402


def track_of(x, sr, hz, n=2048, hop=512):
    k = int(round(hz * n / sr))
    if k < 1 or k >= n // 2:
        return None, None
    w = np.hanning(n); e = []
    for s in range(0, len(x) - n, hop):
        X = np.fft.rfft(x[s:s + n] * w); e.append(np.abs(X[max(0, k - 1):k + 2]).max())
    e = np.array(e) + 1e-12
    return np.log(e), np.arange(len(e)) * hop / sr


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('recdir'); ap.add_argument('--db', type=float, default=-24.0, help='modes this far under the loudest are looked at')
    ap.add_argument('--apply', action='store_true')
    a = ap.parse_args()
    rows = [l.split('\t') for l in open(os.path.join(a.recdir, 'fits.tsv')).read().splitlines()[1:]]
    total = 0
    for r in rows:
        rid = r[0]; rp = os.path.join(a.recdir, rid + '.mmr'); tp = os.path.join(a.recdir, rid + '-target.wav')
        if not (os.path.exists(rp) and os.path.exists(tp)):
            continue
        lines = open(rp).read().splitlines()
        modes = [(i, l) for i, l in enumerate(lines) if l.startswith('mode ')]
        parsed = []
        for i, l in modes:
            w = l.split(); parsed.append((i, float(w[3]), float(w[5]), float(w[w.index('gains') + 1]), w))
        if not parsed:
            continue
        loud = max(abs(p[3]) for p in parsed)
        x, sr = sf.read(tp)
        added = []
        for i, hz, zeta, g, w in parsed:
            if abs(g) < loud * 10 ** (a.db / 20):
                continue
            if any(q is not w and abs(q[1] / hz - 1) < 0.01 for q in [(p[0], p[1], p[2], p[3], p[4]) for p in parsed]):
                continue
            tr, t = track_of(x, sr, hz)
            if tr is None:
                continue
            fitted = zeta * 2 * math.pi * hz
            knee = modalfit.knee_of(tr, t)
            if knee:
                (rf, _), (rs, share) = knee
                # only where the fit's single rate is not already the slow
                # one, and the slow rate is a real tail (a T60 over a second)
                if rs > 0.8 * fitted or 6.91 / rs < 1.0:
                    continue
                zf, zs = rf / (2 * math.pi * hz), rs / (2 * math.pi * hz)
                af, as_ = g / (1 + share), g * share / (1 + share)
                added.append((i, hz, zeta, g, zf, af, zs, as_, w, 'knee'))
                continue
            # a swell: the partial rises after the strike — a Wurlitzer's
            # fundamental peaks 0.6 s in — and a single exponential can only
            # fall from its start, so the fit sat under the recording's
            # sustain by 3-7 dB. The same partial at the same phase and
            # frequency with the opposite sign and a fast decay takes the
            # start down and lets the slow one rise to the track's peak:
            # A q e^{-r t} - A (q - 1) e^{-r2 t}, q the peak over the level
            # at 50 ms, r2 such that the fast one is gone by the peak
            pk = int(np.argmax(tr)); tp = t[pk]
            i50 = int(np.argmin(np.abs(t - 0.05)))
            if tp < 0.15 or tp > 2.0 or pk + 3 >= len(tr):
                continue
            q = float(np.exp(tr[pk] - tr[i50]))
            if q < 1.5:
                continue
            r2 = 2.3 / tp
            zf, zs = r2 / (2 * math.pi * hz), zeta                # the fast (negative) one, and the fitted rate kept for the slow
            af, as_ = -g * (q - 1.0), g * q
            added.append((i, hz, zeta, g, zf, af, zs, as_, w, 'swell x%.1f at %.2fs' % (q, tp)))
        total += len(added)
        if added:
            print('  %-10s %d: %s' % (rid, len(added), '  '.join('%.0fHz %s' % (hz, kind) for i, hz, zeta, g, zf, af, zs, as_, w, kind in added[:4])))
        if a.apply and added:
            out = list(lines)
            extra = []
            for i, hz, zeta, g, zf, af, zs, as_, w, kind in added:
                wf = list(w); wf[5] = '%.6g' % zf; wf[wf.index('gains') + 1] = '%.6g' % af
                ws = list(w); ws[5] = '%.6g' % zs; ws[ws.index('gains') + 1] = '%.6g' % as_
                out[i] = ' '.join(wf); extra.append(' '.join(ws))
            last = max(i for i, _ in modes)
            out = out[:last + 1] + extra + out[last + 1:]
            # renumber the mode lines and the count
            k = 0
            for j, l in enumerate(out):
                if l.startswith('mode '):
                    ww = l.split(); ww[1] = str(k); out[j] = ' '.join(ww); k += 1
            out = ['modes %d' % k if l.startswith('modes ') else l for l in out]
            open(rp, 'w').write('\n'.join(out) + '\n')
    print('%s: %d aftersounds%s' % (a.recdir, total, ' written' if a.apply else ' (dry run; --apply writes)'))


if __name__ == '__main__':
    main()
