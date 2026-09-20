#!/usr/bin/env python3
"""playvel.py — play a shaped record: the metal at a velocity, into its pickup.

    playvel.py out/fit/tine-vel out/wav/tine-vel-bark.wav            # every note, soft to hard
    playvel.py out/fit/ep-vel out/wav/ep-vel-bark.wav --note 60 --velocities 8

A shaped record (fitvel) is the metal's modes, `shaper bell h w K fc q`, and a
`take` line per velocity with the swing the fit gave it. Velocity here is a
swing: 0 is the softest take's, 1 the hardest's, log-interpolated between,
and above 1 is allowed — the field does not care that nobody recorded it.
The chain is the fit's: decaying sines at their fitted phases with the 3 ms attack, the bell,
Faraday as a first difference, the coil as a second-order low-pass. Every
note is struck at the same swing scale so that what changes with velocity is
the field's doing, then the file is levelled to -20 dBFS RMS.
"""
import argparse
import math
import os
import re
import sys

import numpy as np
import soundfile as sf
from scipy import signal


def read(path):
    modes, shaper, takes, bursts = [], None, [], []
    for line in open(path):
        w = line.split()
        if not w:
            continue
        if w[0] == 'mode':
            modes.append((float(w[3]), float(w[5]), float(w[w.index('gains') + 1]), float(w[w.index('phase') + 1]) if 'phase' in w else 0.0))
        elif w[0] == 'shaper':
            shaper = [w[1]] + [float(v) for v in w[2:7]]
        elif w[0] == 'take':
            takes.append((w[1], float(w[3])))
        elif w[0] == 'burst':
            bursts.append((w[1], w[2]) if len(w) > 2 else (None, w[1]))
    return modes, shaper, takes, bursts


def burst_for(recdir, bursts, takes, swing, sr):
    """the burst nearest the swing, resampled to sr and scaled by swing over
    its own — the attack the modes are not, as the runtime plays it"""
    if not bursts:
        return None
    from scipy.signal import resample_poly
    sw = dict(takes)
    best = min(bursts, key=lambda b: abs(math.log((sw.get(b[0], 1.0) or 1.0) / swing)))
    x, bsr = sf.read(os.path.join(recdir, best[1]))
    if bsr != sr:
        x = resample_poly(x, sr, bsr)
    ref = sw.get(best[0], 1.0) or 1.0
    return x * (swing / ref)


def note(modes, shaper, swing, seconds, sr):
    form, h, w, K, fc, Q = shaper
    t = np.arange(int(seconds * sr)) / sr
    u = np.zeros(len(t))
    for hz, zeta, a, ph in modes:
        r = zeta * 2 * math.pi * hz
        u += a * np.exp(-r * t) * np.sin(2 * math.pi * hz * t + ph)
    k = int(0.003 * sr)
    u[:k] *= 0.5 - 0.5 * np.cos(np.pi * np.arange(k) / k)
    u *= swing
    if form == 'none':
        y = u
        k = min(len(y), int(0.1 * sr))
        y[-k:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(k) / k)
        return y
    if form == 'gap':
        phi = 1.0 / (1.0 - 0.9 * np.tanh(u / (0.9 * w)))
    else:
        phi = 1.0 / (1.0 + ((u - h) / w) ** 2)
    d = np.diff(phi, prepend=phi[0])
    # the coil: H(s) = 1 / (1 - (f/fc)^2 + j f/(fc Q)), as a biquad
    w0 = 2 * math.pi * fc / sr
    alpha = math.sin(w0) / (2 * Q)
    b0 = (1 - math.cos(w0)) / 2; b1 = 1 - math.cos(w0); b2 = b0
    a0 = 1 + alpha; a1 = -2 * math.cos(w0); a2 = 1 - alpha
    y = signal.lfilter([b0 / a0, b1 / a0, b2 / a0], [1, a1 / a0, a2 / a0], d)
    # a tine rings for half a minute and the buffer does not: the last 100 ms
    # go out on a raised cosine, or every note ends in a step and the file
    # pops at every strike time plus the ring
    k = min(len(y), int(0.1 * sr))
    y[-k:] *= 0.5 + 0.5 * np.cos(np.pi * np.arange(k) / k)
    return K * y


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('recdir')
    ap.add_argument('out')
    ap.add_argument('--note', type=int, default=0, help='one midi note (default: every note in the set, ascending)')
    ap.add_argument('--velocities', type=int, default=6, help='strikes per note, softest to hardest')
    ap.add_argument('--top', type=float, default=1.0, help='the hardest strike, 1 = the hardest take')
    ap.add_argument('--interval', type=float, default=0.35)
    ap.add_argument('--ring', type=float, default=2.5)
    ap.add_argument('--sr', type=int, default=48000)
    a = ap.parse_args()
    recs = []
    for line in open(os.path.join(a.recdir, 'fits.tsv')).read().splitlines()[1:]:
        c = line.split('\t')
        recs.append((int(c[3]), c[0]))
    recs.sort()
    if a.note:
        recs = [r for r in recs if r[0] == a.note] or [min(recs, key=lambda r: abs(r[0] - a.note))]
    sr = a.sr
    strikes = len(recs) * a.velocities
    out = np.zeros(int((strikes * a.interval + a.ring) * sr))
    i = 0
    for midi, rid in recs:
        modes, shaper, takes, bursts = read(os.path.join(a.recdir, rid + '.mmr'))
        if not modes:
            continue
        if shaper is None:
            shaper = ['none', 0, 1, 1, 0, 1]      # a pitched record: the modes are the sound
        if not takes:
            takes = [('-', 1.0)]
        g0, g1 = min(g for _, g in takes), max(g for _, g in takes)
        for v in range(a.velocities):
            x = (v / max(1, a.velocities - 1)) * a.top
            swing = g0 * (g1 / g0) ** x if g1 > g0 else g0
            y = note(modes, shaper, swing, a.ring, sr)
            b = burst_for(a.recdir, bursts, takes, swing, sr)
            if b is not None:
                y[:len(b)] += b[:len(y)]
            at = int(i * a.interval * sr)
            n = min(len(y), len(out) - at)
            out[at:at + n] += y[:n]
            i += 1
    rms = np.sqrt(np.mean(out ** 2)) or 1.0
    out *= 0.1 / rms
    out = np.tanh(out)
    sf.write(a.out, out, sr)
    print('%s: %d notes x %d velocities, %.1f s' % (a.out, len(recs), a.velocities, len(out) / sr))


if __name__ == '__main__':
    main()
