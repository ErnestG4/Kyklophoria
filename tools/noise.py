#!/usr/bin/env python3
"""noise.py — the wash: what a dense body leaves after its modes and its burst.

    noise.py out/fit/perc [--bands 8]

A tam-tam is hundreds of modes; a bank of 48 with a burst fits a quarter of
its sustain and three times the modes buys five per cent (measured, the
findings). What is left is lines too dense to name, which is noise to the
ear, so it is modelled as noise: the residual after the modes and the burst,
the energy the target has and the model does not, per octave band
(62.5 Hz to 16 kHz, eight bands) over 100 ms frames from 0.1 s on, a line
through the log of each — a level at the strike and a T60 a band. The
deficit and not the residual, because a well-fitted partial with its phase
a little off leaves a residual as loud as itself. Sixteen numbers a note. The runtime plays white
noise through eight second-order band-passes under those envelopes, ~64
instructions a sample, scaled by the strike. Written into the record as
`noise L0 T0 L1 T1 ...`; bands the residual does not have (level under the
burst's floor or a T60 under 50 ms) are written as zero. Run after
bursts.py, on acoustic bodies: an electric world has no body, its deficit
is fit error, and a wash on the Wurlitzer measured worse (2.4 -> 3.0 dB).
Acoustic sets measured: guitar 4.5 -> 3.4 dB of sustain band error, viola
6.7 -> 5.9, mandolin 3.2 -> 2.8, banjo and violin slightly.
"""
import argparse
import math
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modalfit  # noqa: E402

EDGES = [62.5 * 2 ** k for k in range(9)]   # 62.5 .. 16000


def parse(line):
    w = line.split()
    return float(w[3]), float(w[5]), float(w[w.index('gains') + 1]), float(w[w.index('phase') + 1]) if 'phase' in w else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('recdir')
    ap.add_argument('--start', type=float, default=0.1, help='the residual is read from here, past the burst')
    a = ap.parse_args()
    rows = [l.split('\t') for l in open(os.path.join(a.recdir, 'fits.tsv')).read().splitlines()[1:]]
    done = 0
    for r in rows:
        rid = r[0]
        rp = os.path.join(a.recdir, rid + '.mmr')
        tp = os.path.join(a.recdir, rid + '-target.wav')
        if not (os.path.exists(rp) and os.path.exists(tp)):
            continue
        lines = [l for l in open(rp).read().splitlines() if not l.startswith('noise ')]
        modes = [parse(l) for l in lines if l.startswith('mode ')]
        bursts = [l.split() for l in lines if l.startswith('burst ')]
        x, sr = sf.read(tp)
        x = x / 0.5
        f = np.array([m[0] for m in modes]); z = np.array([m[1] for m in modes]); g = np.array([m[2] for m in modes]); ph = np.array([m[3] for m in modes])
        y = modalfit.resynth(f, z * 2 * math.pi * f, g, len(x), sr, ph) if len(f) else np.zeros_like(x)
        if bursts and len(bursts[0]) == 2:
            b, _ = sf.read(os.path.join(a.recdir, bursts[0][1]))
            y[:len(b)] += b[:len(y)]
        nfft, hop = 2048, int(0.1 * sr)
        frames = range(int(a.start * sr), len(x) - nfft, hop)
        if len(frames) < 4:
            continue
        # the deficit, not the residual: a well-fitted partial with its
        # phase a little off leaves a residual as loud as itself, and a wash
        # fitted to that put 13 dB of noise on a cowbell that had none. What
        # is missing is the target's band energy less the model's, where
        # that is positive; the wash fills only that
        St = np.array([np.abs(np.fft.rfft(x[s:s + nfft] * np.hanning(nfft))) ** 2 for s in frames])
        Sy = np.array([np.abs(np.fft.rfft(y[s:s + nfft] * np.hanning(nfft))) ** 2 for s in frames])
        fr = np.fft.rfftfreq(nfft, 1.0 / sr)
        t = np.array([s / sr for s in frames])
        out = []
        for k in range(8):
            sel = (fr >= EDGES[k]) & (fr < EDGES[k + 1])
            # band power per sample: Parseval through a Hann window is
            # 3/8 N^2 over all bins, half of that one-sided. It was N^2 / 8,
            # "roughly", which read 1.8 dB high
            tb = St[:, sel].sum(axis=1) / (3.0 * nfft * nfft / 16)
            yb = Sy[:, sel].sum(axis=1) / (3.0 * nfft * nfft / 16)
            eb = np.maximum(tb - yb, 0.0)
            le = np.log(eb + 1e-12)
            # the fall is fitted only where there is a fall to fit: above the
            # recording's own floor (its last frames) and within 40 dB of the
            # band's peak. A line through the floor read a 1 s T60 as 2.7 s
            # and its intercept 25 dB low; a line through a hand-damped
            # cymbal's cliff put the intercept 9 dB above the recording
            floor = 2.0 * tb[-3:].mean()
            ok = (le > np.log(1e-12) + 2) & (eb > floor) & (eb > eb.max() * 1e-4)
            if ok.sum() < 4:
                out += [0.0, 0.0]; continue
            p = np.polyfit(t[ok], le[ok], 1)
            t60 = 6.91 / max(-p[0] / 2, 1e-3) / 1.0 if p[0] < 0 else 60.0   # energy falls at 2r
            level = math.exp(p[1] / 2)                            # amplitude at t = 0
            # a band the residual holds no more of than the target's own tail is nothing
            # a band the model already carries within 3 dB, or that the
            # target holds no more of than its own tail, is nothing
            if t60 < 0.05 or level <= 0 or eb[:3].mean() < 0.5 * tb[:3].mean() or eb[:3].mean() < 2 * tb[-3:].mean():
                out += [0.0, 0.0]
            else:
                out += [level, min(t60, 60.0)]
        lines.append('noise ' + ' '.join('%.6g %.4g' % (out[2 * k], out[2 * k + 1]) for k in range(8)))
        open(rp, 'w').write('\n'.join(lines) + '\n')
        done += 1
        live = sum(1 for k in range(8) if out[2 * k] > 0)
        print('  %-18s %d bands live: %s' % (rid, live, ' '.join('%.0fHz L%.2g T%.1fs' % (math.sqrt(EDGES[k] * EDGES[k + 1]), out[2 * k], out[2 * k + 1]) for k in range(8) if out[2 * k] > 0)))
    print('  %s: %d records with a noise layer' % (a.recdir, done))


if __name__ == '__main__':
    main()
