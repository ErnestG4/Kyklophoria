#!/usr/bin/env python3
"""washcheck.py <recdir> [--window 0.3 1.3] — the sustain band error of a set:
per record, the model (modes + burst + wash, the wash simulated exactly as
the runtime plays it — white noise through the eight RBJ band-passes with
per-band envelopes, scaled by the filter's measured noise gain) against the
target, as the mean over octave bands of |dB difference| of band energy in
the window, over the bands the target has above its floor. Printed per
record and as the set's mean, with and without the wash, so a change to
noise.py or the runtime's calibration is a number and not an opinion."""
import argparse, math, os, sys
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modalfit  # noqa: E402
from scipy.signal import lfilter  # noqa: E402

EDGES = [62.5 * 2 ** k for k in range(9)]


def wash(levels, t60s, n, sr, seed=0x9E3779B9):
    rng = np.random.default_rng(seed)
    x = rng.uniform(-math.sqrt(3), math.sqrt(3), n)
    out = np.zeros(n)
    for k in range(8):
        if levels[k] <= 0 or t60s[k] <= 0:
            continue
        lo, hi = EDGES[k], EDGES[k + 1]; fc = math.sqrt(lo * hi)
        w0 = 2 * math.pi * fc / sr; alpha = math.sin(w0) / (2 * 1.41421); a0 = 1 + alpha
        b = [alpha / a0, 0, -alpha / a0]; a = [1, -2 * math.cos(w0) / a0, (1 - alpha) / a0]
        g = math.sqrt((lfilter(b, a, np.eye(1, 2048)[0]) ** 2).sum())
        env = (levels[k] / g) * np.exp(-6.91 * np.arange(n) / (t60s[k] * sr))
        out += env * lfilter(b, a, x)
    return out


def bands(x, sr, t0, t1):
    seg = x[int(t0 * sr):int(t1 * sr)]
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)))) ** 2
    fr = np.fft.rfftfreq(len(seg), 1 / sr)
    return np.array([S[(fr >= EDGES[k]) & (fr < EDGES[k + 1])].sum() for k in range(8)])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('recdir'); ap.add_argument('--window', type=float, nargs=2, default=[0.3, 1.3])
    a = ap.parse_args()
    rows = [l.split('\t') for l in open(os.path.join(a.recdir, 'fits.tsv')).read().splitlines()[1:]]
    tot = [[], []]
    for r in rows:
        rid = r[0]; rp = os.path.join(a.recdir, rid + '.mmr'); tp = os.path.join(a.recdir, rid + '-target.wav')
        if not (os.path.exists(rp) and os.path.exists(tp)):
            continue
        lines = open(rp).read().splitlines()
        modes = [(float(w[3]), float(w[5]), float(w[w.index('gains') + 1]), float(w[w.index('phase') + 1]) if 'phase' in w else 0.0)
                 for w in (l.split() for l in lines) if w and w[0] == 'mode']
        noise = [float(v) for l in lines if l.startswith('noise ') for v in l.split()[1:]]
        bursts = [l.split() for l in lines if l.startswith('burst ')]
        x, sr = sf.read(tp); x = x / 0.5
        if len(x) < a.window[1] * sr:
            continue
        f = np.array([m[0] for m in modes]); z = np.array([m[1] for m in modes]); g = np.array([m[2] for m in modes]); ph = np.array([m[3] for m in modes])
        y = modalfit.resynth(f, z * 2 * math.pi * f, g, len(x), sr, ph) if len(f) else np.zeros_like(x)
        if bursts and len(bursts[0]) == 2:
            b, _ = sf.read(os.path.join(a.recdir, bursts[0][1])); y[:len(b)] += b[:len(y)]
        tb = bands(x, sr, *a.window); floor = tb.max() * 1e-4     # bands within 40 dB of the loudest
        use = tb > floor
        errs = []
        for withwash in (0, 1):
            yy = y + (wash(noise[0::2], noise[1::2], len(x), sr) if withwash and noise else 0)
            yb = bands(yy, sr, *a.window)
            e = np.minimum(30.0, np.abs(10 * np.log10((yb[use] + 1e-20) / (tb[use] + 1e-20)))).mean()   # an empty band counts 30 dB, not infinity
            errs.append(e); tot[withwash].append(e)
        print('  %-14s modes %5.1f dB  with wash %5.1f dB%s' % (rid, errs[0], errs[1], '' if noise else ' (none)'))
    if tot[0]:
        print('%s: %d records, sustain band error %.1f dB without the wash, %.1f with' % (a.recdir, len(tot[0]), np.mean(tot[0]), np.mean(tot[1])))


if __name__ == '__main__':
    main()
