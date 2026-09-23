"""measure.py — a render against its note's recording, in numbers.

Everything is at the world's scale on both sides (kp.recording_aligned):
no render is re-levelled, so a level difference is a level difference.

  windows   specaudit's own window measures (stray peaks, static, missing
            partials, level) over its windows 0-50 ms ... 4-6 s
  early     the band-energy match: 12 log bands (the noise bands' own),
            5 ms frames every 2.5 ms, rms of the dB difference over
            0-50 ms, 50-150 ms and 150-400 ms (frames within 60 dB of the
            recording's loudest band-frame; quieter frames are floor on
            both sides and clipped there)
  env       the level envelope: 10 ms RMS, render minus recording in dB,
            median and worst |.| over 0-0.5 s and 0.5-2 s
  seam      where today's burst hands over (its lead + fade): the
            envelope's render-minus-recording difference over the 20 ms
            after the fade against the 20 ms before the fade begins, broad
            and above the bank's top mode (the cliff)
  double    (c) only: the waveguide's part against the modes' part at each
            of the bank's mode frequencies, 0.3-1.3 s: the worst ratio,
            and the beat it could make, 20 log (1 + r) peak
"""
import math

import numpy as np
from scipy import signal as sg

import kp

SR = kp.SR
sa = kp.sa


def early_match(y, x, spans=((0.0, 0.05), (0.05, 0.15), (0.15, 0.4))):
    n = int((spans[-1][1] + 0.02) * SR)
    Y, cs = kp.band_energy(y[:n], spans[-1][1])
    X, _ = kp.band_energy(x[:n], spans[-1][1])
    top = X.max()
    fl = top * 1e-6
    d = 10 * np.log10(np.maximum(Y, fl)) - 10 * np.log10(np.maximum(X, fl))
    out = []
    for a, b in spans:
        sel = (cs >= a) & (cs < b)
        out.append(float(np.sqrt(np.mean(d[:, sel] ** 2))))
    # per band over 0-150 ms, for a look
    sel = cs < 0.15
    per_band = np.sqrt(np.mean(d[:, sel] ** 2, axis=1))
    return out, per_band.tolist()


def envelope(sig, hop=0.01):
    h = int(hop * SR)
    n = len(sig) // h
    return 10 * np.log10(np.mean(sig[:n * h].reshape(n, h) ** 2, axis=1) + 1e-20), np.arange(n) * hop + hop / 2


def env_diff(y, x, spans=((0.0, 0.5), (0.5, 2.0))):
    n = min(len(y), len(x))
    ey, t = envelope(y[:n]); ex, _ = envelope(x[:n])
    ok = ex > ex.max() - 60
    out = []
    for a, b in spans:
        sel = (t >= a) & (t < b) & ok
        if sel.sum() < 3:
            out.append((float('nan'), float('nan'))); continue
        d = ey[sel] - ex[sel]
        out.append((float(np.median(d)), float(np.max(np.abs(d)))))
    return out


def seam(y, x, lead_s, end_s, ftop):
    """(broad, above ftop): dB of render-minus-recording after the fade
    against before it"""
    def lvl(sig, a, b, hp=None):
        s = sig
        if hp:
            s = sg.sosfiltfilt(sg.butter(4, min(hp, 0.45 * SR), 'highpass', fs=SR, output='sos'), sig[:int((b + 0.05) * SR)])
        seg = s[int(a * SR):int(b * SR)]
        return 10 * math.log10(np.mean(seg ** 2) + 1e-20)
    out = []
    for hp in (None, 1.05 * ftop if ftop and ftop * 1.05 < 0.45 * SR else None):
        if hp is None and out:
            out.append(float('nan')); continue
        before = lvl(y, max(0.0, lead_s - 0.02), lead_s, hp) - lvl(x, max(0.0, lead_s - 0.02), lead_s, hp)
        after = lvl(y, end_s, end_s + 0.02, hp) - lvl(x, end_s, end_s + 0.02, hp)
        out.append(after - before)
    return out


def doubling(modes_part, wg_part, hz, t0=0.3, t1=1.3):
    """the waveguide against the modes at every bank mode: (worst dB, its hz,
    the beat it could make in dB peak)"""
    a, b = int(t0 * SR), min(int(t1 * SR), len(modes_part))
    if b - a < SR // 4 or not np.any(wg_part[a:b]):
        return None
    w = np.hanning(b - a)
    n = 1 << int(math.ceil(math.log2(4 * (b - a))))
    M = np.abs(np.fft.rfft(modes_part[a:b] * w, n)); G = np.abs(np.fft.rfft(wg_part[a:b] * w, n))
    f = np.fft.rfftfreq(n, 1 / SR)
    worst = (-200.0, 0.0)
    for h in hz:
        j = int(round(h * n / SR))
        if j < 2 or j >= len(f) - 2:
            continue
        m = M[j - 2:j + 3].max(); g = G[j - 2:j + 3].max()
        if m <= 0:
            continue
        r = 20 * math.log10(g / m + 1e-12)
        if r > worst[0]:
            worst = (r, float(h))
    r = 10 ** (worst[0] / 20)
    return worst[0], worst[1], 20 * math.log10(1 + r)


def windows(y, x, f0):
    out = []
    n = min(len(y), len(x))
    for (a, b), name in zip(sa.WINDOWS, sa.WNAMES):
        a_, b_ = int(a * SR), min(int(b * SR), n - int(0.01 * SR))
        if b_ - a_ < max(0.4 * (b - a) * SR, 0.04 * SR):
            out.append(None); continue
        m = sa.window_metrics(y[a_:b_], x[a_:b_], f0)
        out.append(dict(spur=m['spur'], static=m['static'], missing=m['missing'], missing_db=m['missing_db'], level=m['level']))
    return out


def high_band(y, x, ftop, spans=((0.05, 0.3), (0.3, 1.0), (1.0, 2.0))):
    """the part above the bank's top mode: render over recording, dB, per span"""
    if not ftop or 1.05 * ftop > 0.44 * SR:
        return [float('nan')] * len(spans)
    sos = sg.butter(6, 1.05 * ftop, 'highpass', fs=SR, output='sos')
    n = min(len(y), len(x), int((spans[-1][1] + 0.05) * SR))
    Y = sg.sosfiltfilt(sos, y[:n]); X = sg.sosfiltfilt(sos, x[:n])
    out = []
    for a, b in spans:
        a_, b_ = int(a * SR), min(int(b * SR), n)
        if b_ - a_ < int(0.05 * SR):
            out.append(float('nan')); continue
        out.append(10 * math.log10((np.mean(Y[a_:b_] ** 2) + 1e-20) / (np.mean(X[a_:b_] ** 2) + 1e-20)))
    return out
