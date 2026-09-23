#!/usr/bin/env python3
"""specaudit.py — what a world plays that its recordings do not: stray
spectra and static, measured through the runtime itself.

    specaudit.py out/card/kyklophoria/banjo.kykm              # sets from manifests/card.tsv
    specaudit.py out/worlds/guitar-sula.kykm --fit out/fit/guitar-sula
    specaudit.py --card [--json out.json] [--tune]            # every world on the card
    specaudit.py --selftest out/card/kyklophoria/wurli.kykm   # the audit's own test

Every point of a world (every member of a family) is played at its own note
through Kyklophoria's build/host/kykdesk — one voice, one strike, every axis
at its centre, which is the world as fitted — and set beside the note's own
recording, the record's `-target.wav`, pulled to the pitch the export pulled
the modes to (the world's mode frequencies over the record's, the median of
the matched ones) and resampled to 48 kHz. The two are lined up on the
burst — the first 30 ms of a render IS the recording, faded, so a
cross-correlation finds the strike to the sample — and levelled on it: the
render scaled so its first 25 ms, 60 Hz - 4 kHz, carry the recording's
energy. Every dB below is between those two.

Velocity: 0.8 on a world fitted from one take. On a world with velocity
layers (swing_soft < swing_hard: the Philharmonia's dynamics, the pianos'
layers, a pickup world's takes) 1.0, because at any other velocity the
render crossfades two takes' attacks over modes at a swing no take was
recorded at, and no recording holds that; at 1.0 it is the loudest take,
and the loudest take's record is the one the point's modes are.

Windows from the strike: 0-50 ms, 50-300 ms, 0.3-1, 1-2, 2-4 and 4-6 s, each
where the recording covers 40% of it (the recordings run 0.2 to 6 s). Each
is a Blackman-Harris spectrum (sidelobes at -92 dB, so the gaps between the
partials hold what is there and not leakage), zero-padded four times.

  spurious   the render's peaks (10 dB prominence, within 60 dB of its
             loudest) that stand more than 10 dB over the recording's
             loudest bin within +-1.5% (or two bins, where that is wider),
             or 6 dB over it where the recording has no partial there (a
             partial: 10 dB prominence and 15 dB over the floor of its third
             of an octave): their energy in dB of the render's whole window.
             Late windows are where a stray mode sings alone, so the
             headline is each point's last window.
  static     the floor between the partials: per octave band (62.5 Hz -
             16 kHz) the 20th percentile of the dB spectrum over the bins a
             main lobe away from any peak of either, render minus recording,
             and the window's static is the median over the bands. One
             band's floor wanders 10 dB between two stretches of the same
             recording 100 ms apart; the median over the bands does so in
             0.4% of windows, and a hiss is in every band. Over +10 dB is
             static.
  missing    the recording's partials within 40 dB of its loudest that the
             render has 10 dB less of: a count. Information only; the bank
             has 48 modes and a piano's bass note has hundreds of partials.
  level      the window's whole energy, render over recording: where a
             note's sustain is too loud or dies too early, whatever it is
             made of.
  subf0      the energy under 0.85 f0 from 50 ms to the end, in dB of the
             whole, for both (the piano's top-octave knock modes live here).

and per point the render's peak sample, its RMS over the first second and
(--tune) export.settled_cents read off the render.

--selftest WORLD holds each recording against itself 100 ms later (the
audit's false-alarm floor: it should find nothing), then audits the world as
it is, with a stray mode at an inharmonic 3.37 f0 20 dB under each point's
loudest, and with a wash in every band 50 dB under it: the stray mode has to
come out as stray peaks at that frequency, the wash as static.
--inject-mode HZ:DB (or rRATIO:DB) and --inject-noise DB do the same to any
world, for a look by hand.
"""
import argparse
import glob
import json
import math
import os
import struct
import subprocess
import sys
import tempfile

import numpy as np
import soundfile as sf
from scipy import signal as sg

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.realpath(__file__)), '..'))
KYKDESK = os.path.normpath(os.path.join(ROOT, '..', 'Kyklophoria', 'build', 'host', 'kykdesk'))
SR = 48000
T_STRIKE = 0.05
WINDOWS = [(0.0, 0.05), (0.05, 0.3), (0.3, 1.0), (1.0, 2.0), (2.0, 4.0), (4.0, 6.0)]
WNAMES = ['0-50ms', '50-300ms', '0.3-1s', '1-2s', '2-4s', '4-6s']
EDGES = [62.5 * 2 ** k for k in range(9)]
SPUR_DB = 10.0          # a render peak this far over the recording is stray
STATIC_DB = 10.0        # a window's median band floor this far over the recording's is static
NOPART_DB = 6.0         # or this far, where the recording has no partial at all: the recording
                        # held against itself 100 ms on stands 6 dB over itself at 0.2% of such
                        # peaks and 3 dB at 1.2% (see --selftest)
FLOOR = -120.0          # dB clamp for "nothing"
TUNE = False            # --tune: settled_cents on every render


# ── the world ──────────────────────────────────────────────────────────
def parse_world(b):
    assert b[:4] == b'KYKM', 'not a world'
    ver, N, P, form, kind = struct.unpack_from('<HHHBB', b, 4)
    lo, hi = struct.unpack_from('<ff', b, 12)
    w = dict(ver=ver, N=N, P=P, form=form, kind=kind, lo=lo, hi=hi)
    if kind == 2:
        M = b[20]
        w['members'] = []
        for m in range(M):
            off, size, name = struct.unpack_from('<II16s', b, 21 + 24 * m)
            w['members'].append((name.rstrip(b'\0').decode(), b[off:off + size]))
        return w
    o = 20
    w['body'] = None
    if ver >= 7:
        w['body'] = struct.unpack_from('<8f', b, o); o += 32
    head = 12 if ver >= 6 else 10
    pts = []
    for p in range(P):
        start = o
        param = struct.unpack_from('<f', b, o)[0]; o += 4
        stage = struct.unpack_from('<8f', b, o); o += 32
        modes = []
        mode_at = o
        for m in range(N):
            modes.append(struct.unpack_from('<HBBB', b, o)); o += 5
        noise_at = o
        nb = b[o]; o += 1
        noise = [struct.unpack_from('<ff', b, o + 8 * k) for k in range(nb)]; o += 8 * nb
        burst_at = o
        nbur = struct.unpack_from('<H', b, o)[0]; o += 2
        bursts = []
        for k in range(nbur):
            if head == 12:
                sw, sc, ln, fade = struct.unpack_from('<ffHH', b, o)
            else:
                sw, sc, ln = struct.unpack_from('<ffH', b, o); fade = ln // 3
            o += head + 2 * ln
            bursts.append((sw, sc, ln, fade))
        pts.append(dict(param=param, stage=stage, modes=modes, noise=noise, bursts=bursts,
                        start=start, mode_at=mode_at, noise_at=noise_at, burst_at=burst_at, end=o))
    w['points'] = pts
    return w


def decode(pt, ver):
    """[(hz, zeta, gain, phase)] of a point's live modes, absolute gains"""
    loud = pt['stage'][7]
    out = []
    for c, d, l, ph in pt['modes']:
        if l == 255:
            continue
        hz = 20.0 * 2 ** (c / (6000.0 if ver >= 6 else 1200.0))
        out.append((hz, math.exp(-0.1 * d), loud * 10 ** (-0.25 * l / 20), ph / 256 * 2 * math.pi))
    return out


# ── the sets ───────────────────────────────────────────────────────────
def card_rows(path=None):
    rows = {}
    for l in open(path or os.path.join(ROOT, 'manifests', 'card.tsv')).read().splitlines():
        if not l.strip() or l.startswith('#'):
            continue
        f = l.split('\t')
        rows[f[0]] = (f[1].split(',') if len(f) > 1 and f[1] else [], len(f) > 3 and f[3].strip() == 'gated')
    return rows


def load_set(d):
    lines = open(os.path.join(d, 'fits.tsv')).read().splitlines()
    col = {k: i for i, k in enumerate(lines[0].split('\t'))}
    recs = []
    for line in lines[1:]:
        c = line.split('\t')
        rp = os.path.join(d, c[0] + '.mmr')
        if not os.path.exists(rp):
            continue
        modes, takes = [], []
        for l in open(rp):
            w = l.split()
            if not w:
                continue
            if w[0] == 'mode':
                modes.append((float(w[3]), float(w[5]), float(w[w.index('gains') + 1]), float(w[w.index('phase') + 1]) if 'phase' in w else 0.0))
            elif w[0] == 'take':
                takes.append((w[1], float(w[3])))
        if takes:
            top = max(takes, key=lambda t: t[1])[0]
            tp = os.path.join(d, '%s-%s-target.wav' % (c[0], top))
        else:
            tp = os.path.join(d, c[0] + '-target.wav')
        recs.append(dict(id=c[0], dir=d, value=float(c[col['value']]), modes=modes, target=tp,
                         shaped=bool(takes), loss=float(c[col['loss']]) if 'loss' in col else float('nan'),
                         ring=float(c[col['decay_ratio']]) if 'decay_ratio' in col else float('nan')))
    return recs


def pull_of(world_hz, rec_hz):
    """the one ratio that puts the most of a point's modes on a record's:
    (k, matched). The world's modes are the record's times k, quantised to a
    fifth of a cent; candidates are every pairing of the world's loudest
    few with every record mode within 130 cents"""
    if not len(world_hz) or not len(rec_hz):
        return 1.0, 0
    lw = np.log(np.asarray(world_hz)); lr = np.sort(np.log(np.asarray(rec_hz)))
    best = (0, 1.0)
    for a in lw[:6]:
        for b in lr:
            lk = a - b
            if abs(lk) > 0.078:
                continue
            q = lw - lk
            i = np.clip(np.searchsorted(lr, q), 1, len(lr) - 1)
            d = np.minimum(np.abs(lr[i] - q), np.abs(lr[i - 1] - q))
            n = int((d < 4e-4).sum())
            if n > best[0]:
                # refine: the median of the matched
                sel = d < 4e-4
                j = np.where(np.abs(lr[i] - q) < np.abs(lr[i - 1] - q), i, i - 1)
                best = (n, float(np.exp(np.median(lw[sel] - lr[j[sel]]))))
    return best[1], best[0]


def match_point(pt, ver, recs):
    """the record a world's point was exported from, and the pull"""
    ms = sorted(decode(pt, ver), key=lambda m: -m[2])
    wh = [m[0] for m in ms]
    cands = [r for r in recs if abs(r['value'] - pt['param']) < 1e-3]
    best = None
    for r in cands:
        k, n = pull_of(wh, [m[0] for m in r['modes']])
        if best is None or n > best[2]:
            best = (r, k, n)
    if best is None:
        return None, 1.0, 0, len(wh)
    return best[0], best[1], best[2], len(wh)


# ── rendering ──────────────────────────────────────────────────────────
def render(blob, midi, vel, dur, tmp, pos=None):
    """one strike at a note (a note world) or at a place on the row (an
    index world, pos 0..1 on axis 0); every other axis at its centre, the
    world as fitted"""
    wp = os.path.join(tmp, 'w.kykm'); sp = os.path.join(tmp, 's.txt'); op = os.path.join(tmp, 'o.wav')
    open(wp, 'wb').write(blob)
    f0 = 440.0 * 2 ** ((midi - 69) / 12) if pos is None else 220.0
    open(sp, 'w').write('0 poly 1\n%s0 f0 %.6f\n%g strike %g\n%g dur\n' % ('' if pos is None else '0 pos %.6f 0.5 0.5 0.5\n' % pos, f0, T_STRIKE, vel, T_STRIKE + dur))
    r = subprocess.run([KYKDESK, '--resonate', wp, '--script', sp, '--out', op], capture_output=True, text=True)
    if r.returncode:
        raise RuntimeError(r.stderr)
    y, sr = sf.read(op)
    assert sr == SR
    return y


def recording(path, k):
    """the target at record scale (it is written at half), at 48 kHz, its
    pitch times k"""
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1) / 0.5
    n = int(round(len(x) * SR / sr / k))
    return sg.resample(x, n)


def align(y, x, i0):
    """where in the render the recording's sample 0 sits: the burst is the
    recording, so the lag that best correlates the first 30 ms"""
    n = int(0.03 * SR)
    seg = x[:n]
    lo, hi = i0 - int(0.005 * SR), i0 + int(0.02 * SR)
    r = y[lo:hi + n]
    c = np.correlate(r, seg, mode='valid')
    e = np.sqrt(np.convolve(r ** 2, np.ones(n), mode='valid')) + 1e-12
    j = int(np.argmax(c / e))
    return lo + j


def band(x, lo=60.0, hi=4000.0):
    return sg.sosfiltfilt(sg.butter(4, [lo, hi], 'bandpass', fs=SR, output='sos'), x)


# ── spectra ────────────────────────────────────────────────────────────
def spectrum(x):
    n = len(x)
    w = sg.windows.blackmanharris(n)
    nfft = 1 << int(math.ceil(math.log2(4 * n)))
    P = np.abs(np.fft.rfft(x * w, nfft)) ** 2
    f = np.fft.rfftfreq(nfft, 1.0 / SR)
    return f, P, SR / n          # the unpadded resolution


def db(P):
    return 10 * np.log10(P + 1e-30)


def peaks(D, f, df, prom, within, fmin=25.0):
    top = D[(f > fmin)].max()
    dist = max(1, int(round(1.0 * df / (f[1] - f[0]))))
    p, _ = sg.find_peaks(D, prominence=prom, distance=dist)
    return p[(D[p] > top - within) & (f[p] > fmin) & (f[p] < 20000.0)]


def local_floor(D, f):
    """the spectrum's floor around each bin: the 20th percentile of its dB
    over the third of an octave it sits in, interpolated in log frequency"""
    edges = 20.0 * 2 ** (np.arange(0, 31) / 3.0)
    c, v = [], []
    for a, b in zip(edges[:-1], edges[1:]):
        sel = (f >= a) & (f < b)
        if sel.sum() >= 4:
            c.append(math.sqrt(a * b)); v.append(np.percentile(D[sel], 20))
    return np.interp(np.log(np.maximum(f, 1.0)), np.log(c), v) if c else np.full(len(f), FLOOR)


def partials(D, f, df, within, floor):
    """the recording's partials: peaks of 10 dB prominence that stand 15 dB
    over the floor around them (a noise floor's own ripples are peaks too)"""
    p = peaks(D, f, df, 10.0, within)
    return p[D[p] > floor[p] + 15.0]


def window_metrics(yr, xr, f0):
    f, Pr, df = spectrum(yr)
    _, Px, _ = spectrum(xr)
    Dr, Dx = db(Pr), db(Px)
    bin_ = f[1] - f[0]
    hw = int(math.ceil(4 * df / bin_))                    # BH main lobe, half
    tot_r, tot_x = Pr.sum(), Px.sum()
    pr = peaks(Dr, f, df, 10.0, 60.0)
    fx = local_floor(Dx, f)
    px = partials(Dx, f, df, 60.0, fx)
    px_f = f[px]
    spur = []; spur_e = 0.0; exc_e = 0.0
    for p in pr:
        tol = max(0.015 * f[p], 2 * df)
        a, b = np.searchsorted(f, f[p] - tol), np.searchsorted(f, f[p] + tol) + 1
        xm = Dx[a:b].max()
        exc = Dr[p] - xm
        has = np.any(np.abs(px_f - f[p]) <= tol)
        if exc > SPUR_DB or (not has and exc > NOPART_DB):
            e = Pr[max(0, p - hw):p + hw + 1].sum()
            spur_e += e; exc_e += e * (1 - 10 ** (-exc / 10))
            spur.append((float(f[p]), float(exc), float(Dr[p] - Dr[pr].max()) if len(pr) else 0.0, float(10 * math.log10(e / tot_r + 1e-30))))
    # missing: the recording's partials the render lacks
    pxm = partials(Dx, f, df, 40.0, fx)
    miss_e = 0.0; nmiss = 0
    for p in pxm:
        tol = max(0.015 * f[p], 2 * df)
        a, b = np.searchsorted(f, f[p] - tol), np.searchsorted(f, f[p] + tol) + 1
        if Dr[a:b].max() < Dx[p] - SPUR_DB:
            nmiss += 1; miss_e += Px[max(0, p - hw):p + hw + 1].sum()
    # static: the floor between partials, per octave band
    near = np.zeros(len(f), bool)
    for p in np.concatenate([peaks(Dr, f, df, 6.0, 100.0), peaks(Dx, f, df, 6.0, 100.0)]):
        near[max(0, p - hw):p + hw + 1] = True
    rmax = Dr[f > 25].max()
    st = []
    for k in range(8):
        sel = (f >= EDGES[k]) & (f < EDGES[k + 1]) & ~near
        if sel.sum() < 16:
            st.append(None); continue
        fr_, fx_ = np.percentile(Dr[sel], 20), np.percentile(Dx[sel], 20)
        st.append((float(fr_ - fx_), float(fr_ - rmax)))
    # static: the median over the bands of the floor's difference — one
    # band's floor wanders by 10 dB between two stretches of the same
    # recording 100 ms apart (1% of band-windows), the median over the
    # bands by 8 dB at 1% and 10 dB at 0.4%; a hiss is in every band
    have = [(v[0], k) for k, v in enumerate(st) if v is not None]
    worst = max(have) if have else (FLOOR, -1)
    return dict(static=float(np.median([v[0] for v in have])) if have else FLOOR,
                static_worst=worst[0], static_band=worst[1], static_bands=st,
                spur=float(10 * math.log10(spur_e / tot_r)) if spur_e > 0 else FLOOR,
                spur_excess=float(10 * math.log10(exc_e / tot_r)) if exc_e > 0 else FLOOR,
                nspur=len(spur), spur_peaks=sorted(spur, key=lambda s: -s[3])[:4],
                missing=nmiss, missing_db=float(10 * math.log10(miss_e / tot_x)) if miss_e > 0 else FLOOR,
                level=float(10 * math.log10(tot_r / tot_x)) if tot_x > 0 and tot_r > 0 else FLOOR)


def subf0(y, f0):
    P = np.abs(np.fft.rfft(y * np.hanning(len(y)))) ** 2
    f = np.fft.rfftfreq(len(y), 1.0 / SR)
    s = P[(f > 15) & (f < 0.85 * f0)].sum()
    return float(10 * math.log10(s / P.sum() + 1e-30))


# ── one point ──────────────────────────────────────────────────────────
def audit_point(blob, w, i, rec, k, vel, tmp):
    pt = w['points'][i]
    x = recording(rec['target'], k)
    dur = min(6.1, len(x) / SR + 0.05)
    pos = None if w['kind'] != 1 else ((pt['param'] - w['lo']) / (w['hi'] - w['lo']) if w['hi'] > w['lo'] else 0.0)
    y = render(blob, pt['param'], vel, dur, tmp, pos)
    cents = None
    if TUNE and w['kind'] == 0:
        # the export's own reading of where a note settles, on the render
        sys.path.insert(0, os.path.join(ROOT, 'tools'))
        import export
        cents = export.settled_cents(os.path.join(tmp, 'o.wav'), int(round(pt['param'])))
    i0 = int(round(T_STRIKE * SR))
    s = align(y, x, i0)
    n = min(len(y) - s, len(x))
    yr, xr = y[s:s + n], x[:n]
    # levelled on the burst's lead: what sounds before the modes come in
    sw, sc, ln, fade = pt['bursts'][-1] if pt['bursts'] else (1, 1, 0, 0)
    lead = int(min(0.025 * SR, max(0.005 * SR, ln - fade)))
    ey, ex = (band(yr[:int(0.2 * SR)])[:lead] ** 2).sum(), (band(xr[:int(0.2 * SR)])[:lead] ** 2).sum()
    g = math.sqrt(ex / ey) if ey > 0 else 1.0
    # an index world's point has no note: its lowest heard mode stands in
    # for f0 where one is needed (the sub-f0 share)
    f0 = 440.0 * 2 ** ((pt['param'] - 69) / 12) if w['kind'] != 1 else min((m[0] for m in decode(pt, w['ver'])), default=100.0)
    out = dict(param=pt['param'], rec=rec['id'], k_cents=1200 * math.log2(k), lag=s - i0, cents=cents,
               level_db=20 * math.log10(g) if g > 0 else 0.0,
               peak=float(np.abs(y).max()), rms1=float(20 * math.log10(np.sqrt(np.mean(y[i0:i0 + SR] ** 2)) + 1e-12)),
               windows=[])
    for (a, b), name in zip(WINDOWS, WNAMES):
        a_, b_ = int(a * SR), min(int(b * SR), n - int(0.01 * SR))
        if b_ - a_ < max(0.4 * (b - a) * SR, 0.04 * SR):     # 40% of the window, 40 ms at least
            out['windows'].append(None); continue
        m = window_metrics(g * yr[a_:b_], xr[a_:b_], f0)
        m['span'] = (a_ / SR, b_ / SR)
        out['windows'].append(m)
    t5 = int(0.05 * SR)
    out['subf0_r'] = subf0(yr[t5:], f0); out['subf0_x'] = subf0(xr[t5:], f0)
    return out


# ── injection (the audit's own test) ───────────────────────────────────
def inject(blob, w, mode=None, noise=None):
    """a copy of a plain world with a stray mode in every point's quietest
    slot, and/or a wash in every band"""
    b = bytearray(blob)
    fifth = 6000.0 if w['ver'] >= 6 else 1200.0
    extra_noise = 0
    out = bytearray(b[:w['points'][0]['start']])
    for pt in w['points']:
        seg = bytearray(b[pt['start']:pt['end']])
        if mode:
            hz_spec, dbl = mode
            f0 = 440.0 * 2 ** ((pt['param'] - 69) / 12)
            hz = f0 * float(hz_spec[1:]) if hz_spec.startswith('r') else float(hz_spec)
            ms = pt['modes']
            # the slot: a silent one, or the quietest
            slot = next((j for j, m in enumerate(ms) if m[2] == 255), max(range(len(ms)), key=lambda j: ms[j][2]))
            longest = max((m[1] for m in ms if m[2] != 255), default=60)      # the decay byte is -10 ln zeta: larger rings longer
            c = int(np.clip(round(fifth * math.log2(hz / 20.0)), 0, 65535))
            lv = int(np.clip(round(-dbl * 4), 0, 254))
            o = pt['mode_at'] - pt['start'] + 5 * slot
            struct.pack_into('<HBBB', seg, o, c, longest, lv, 0)
        if noise is not None:
            loud = pt['stage'][7]
            o = pt['noise_at'] - pt['start']
            nb = seg[o]
            lvl = loud * 10 ** (noise / 20)
            new = struct.pack('<B', 8) + b''.join(struct.pack('<ff', lvl, 4.0) for _ in range(8))
            seg = seg[:o] + new + seg[o + 1 + 8 * nb:]
        out += seg
    return bytes(out)


# ── a world ────────────────────────────────────────────────────────────
def sets_for(name, fits, card):
    if fits:
        return fits
    if name in card:
        return sorted(set(d for s in card[name][0] for d in glob.glob(os.path.join(ROOT, 'out', 'fit', s))
                          if os.path.exists(os.path.join(d, 'fits.tsv'))))
    d = os.path.join(ROOT, 'out', 'fit', name)
    return [d] if os.path.exists(os.path.join(d, 'fits.tsv')) else []


def audit_world(blob, sets, vel=None, only=None, label='', workers=1):
    w = parse_world(blob)
    assert w['kind'] != 2
    recsets = [(d, load_set(d)) for d in sets]
    # the set: the one whose records match the most points
    best = None
    for d, recs in recsets:
        n = 0
        for pt in w['points'][:: max(1, len(w['points']) // 8)]:
            r, k, m, nw = match_point(pt, w['ver'], recs)
            n += m
        if best is None or n > best[1]:
            best = (d, n, recs)
    d, _, recs = best
    layered = any(pt['stage'][6] > pt['stage'][5] * 1.0001 for pt in w['points'])
    v = vel if vel is not None else (1.0 if layered else 0.8)
    res = dict(world=label, set=os.path.relpath(d, ROOT), velocity=v, form=w['form'], kind=w['kind'], points=[])
    jobs = []
    for i, pt in enumerate(w['points']):
        if only and pt['param'] not in only:
            continue
        rec, k, m, nw = match_point(pt, w['ver'], recs)
        if rec is None or not os.path.exists(rec['target']):
            res['points'].append(dict(param=pt['param'], error='no record at this note')); continue
        jobs.append((i, rec, k, m, nw))
    def one(job):
        i, rec, k, m, nw = job
        with tempfile.TemporaryDirectory() as tmp:
            try:
                o = audit_point(blob, w, i, rec, k, v, tmp)
            except Exception as e:                # noqa: BLE001
                return dict(param=w['points'][i]['param'], rec=rec['id'], error=str(e))
        o['matched'] = '%d/%d' % (m, nw); o['loss'] = rec['loss']; o['ring'] = rec['ring']
        return o
    if workers > 1:
        from concurrent.futures import ThreadPoolExecutor
        with ThreadPoolExecutor(workers) as ex:
            res['points'] += list(ex.map(one, jobs))
    else:
        res['points'] += [one(j) for j in jobs]
    res['points'].sort(key=lambda p: p['param'])
    return res


def pmean(v):
    v = [x for x in v if x is not None]
    if not v:
        return FLOOR
    return float(10 * math.log10(np.mean([10 ** (x / 10) for x in v]) + 1e-30))


def summary(res):
    ok = [p for p in res['points'] if 'error' not in p]
    s = dict(points=len(ok), errors=len(res['points']) - len(ok))
    last = lambda p: next((m for m in reversed(p['windows']) if m), None)   # noqa: E731
    for j, name in enumerate(WNAMES):
        ws = [p['windows'][j] for p in ok if p['windows'][j]]
        s['spur_' + name] = pmean([m['spur'] for m in ws])
        s['static_' + name] = float(np.median([m['static'] for m in ws])) if ws else FLOOR
    ls = [last(p) for p in ok]
    s['spur_late'] = pmean([m['spur'] for m in ls if m])
    s['spur_late_n20'] = sum(1 for m in ls if m and m['spur'] > -20)
    s['static_late'] = float(np.median([m['static'] for m in ls if m])) if ls else FLOOR
    # static past the attack: a window whose median band floor stands 10 dB
    # over the recording's
    s['static_n10'] = sum(1 for p in ok if any(m and m['static'] > STATIC_DB for m in p['windows'][1:]))
    s['missing'] = float(np.mean([sum(m['missing'] for m in p['windows'] if m) for p in ok])) if ok else 0
    s['subf0_r'] = pmean([p['subf0_r'] for p in ok]); s['subf0_x'] = pmean([p['subf0_x'] for p in ok])
    s['peak'] = max((p['peak'] for p in ok), default=0.0)
    s['rms1'] = float(np.mean([p['rms1'] for p in ok])) if ok else 0.0
    return s


def show(res, top=8):
    s = summary(res)
    print('%s  [%s, velocity %.1f]  %d points%s' % (res['world'], res['set'], res['velocity'], s['points'],
                                                    ', %d not audited' % s['errors'] if s['errors'] else ''))
    print('  spurious dB re render:  %s   late %.1f (%d points over -20)' % (
        '  '.join('%s %.1f' % (n, s['spur_' + n]) for n in WNAMES), s['spur_late'], s['spur_late_n20']))
    print('  static dB (median):     %s   late %+.1f (%d points over +%g past 50 ms)' % (
        '  '.join('%s %+.1f' % (n, s['static_' + n]) for n in WNAMES), s['static_late'], s['static_n10'], STATIC_DB))
    print('  missing partials/point %.1f   sub-f0 render %.1f dB, recording %.1f dB   peak %.3f   rms(1s) %.1f dBFS' % (
        s['missing'], s['subf0_r'], s['subf0_x'], s['peak'], s['rms1']))
    ok = [p for p in res['points'] if 'error' not in p]
    def late(p):
        return next((m for m in reversed(p['windows']) if m), None)
    worst = sorted(ok, key=lambda p: -(late(p)['spur'] if late(p) else FLOOR))[:top]
    for p in worst:
        m = late(p)
        if not m or m['spur'] <= -40:
            continue
        print('    midi %-5g %-12s late spur %6.1f dB  static %+5.1f  %s' % (
            p['param'], p['rec'], m['spur'], m['static'],
            ' '.join('%.0fHz+%.0f' % (q[0], q[1]) for q in m['spur_peaks'][:3])))
    for p in res['points']:
        if 'error' in p:
            print('    midi %-5g %s' % (p['param'], p['error']))
    return s


def selftest(path, sets, workers, n_self=60):
    """The audit's own test, on one world. (1) Each recording against itself
    100 ms later, through the same window measures: what the audit calls
    stray or static in a signal that IS the recording is its false-alarm
    floor. (2) The world as it is, (3) with a stray mode at an inharmonic
    3.37 x f0, 20 dB under each point's loudest, at its longest decay, and
    (4) with a wash in every band 50 dB under the loudest, T60 4 s: the
    audit has to see (3) as stray peaks at that frequency and (4) as static."""
    blob = open(path, 'rb').read()
    w = parse_world(blob)
    recs = [r for d in sets for r in load_set(d)]
    fa = []
    for r in recs[:n_self]:
        if not os.path.exists(r['target']):
            continue
        x = recording(r['target'], 1.0)
        for (a, b), name in list(zip(WINDOWS, WNAMES))[1:]:
            a_, b_ = int(a * SR), min(int(b * SR), len(x) - int(0.11 * SR))
            if b_ - a_ < max(0.4 * (b - a) * SR, 0.04 * SR):
                continue
            m = window_metrics(x[a_ + int(0.1 * SR):b_ + int(0.1 * SR)], x[a_:b_], 100.0)
            fa.append((m['spur'], m['static'], m['nspur']))
    fa = np.array(fa)
    print('(1) the recording against itself 100 ms later, %d windows of %d records: spurious > -40 dB in %.1f%% of windows (power mean %.1f dB), '
          'static over +%g dB in %.1f%% (median %+.1f dB)' % (len(fa), min(n_self, len(recs)), 100 * (fa[:, 0] > -40).mean(), pmean(list(fa[:, 0])), STATIC_DB, 100 * (fa[:, 1] > STATIC_DB).mean(), np.median(fa[:, 1])))
    base = audit_world(blob, sets, None, None, os.path.basename(path)[:-5], workers)
    print('(2) as it is:'); s0 = show(base, 0)
    ratio = 3.37
    mod = audit_world(inject(blob, w, ('r%g' % ratio, 20.0), None), sets, None, None, os.path.basename(path)[:-5] + ' + a stray mode at %.2f f0, -20 dB' % ratio, workers)
    print('(3) with a stray mode:'); s1 = show(mod, 0)
    hit = tot = 0
    for p in mod['points']:
        if 'error' in p:
            continue
        f0 = 440.0 * 2 ** ((p['param'] - 69) / 12)
        tot += 1
        if any(m and any(abs(q[0] / (ratio * f0) - 1) < 0.02 for q in m['spur_peaks']) for m in p['windows'][1:]):
            hit += 1
    print('    the injected mode is among the stray peaks the audit lists at %d of %d points' % (hit, tot))
    noi = audit_world(inject(blob, w, None, -50.0), sets, None, None, os.path.basename(path)[:-5] + ' + a wash 50 dB under the loudest', workers)
    print('(4) with a noise floor:'); s2 = show(noi, 0)
    return s0, s1, s2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('worlds', nargs='*')
    ap.add_argument('--fit', nargs='*', default=None, help='the fitted set(s) the world came from; default from manifests/card.tsv, then out/fit/<name>')
    ap.add_argument('--card', action='store_true', help='every world in out/card/kyklophoria')
    ap.add_argument('--velocity', type=float, default=None)
    ap.add_argument('--only', default='', help='comma-separated midi values')
    ap.add_argument('--json', default=None)
    ap.add_argument('--workers', type=int, default=4)
    ap.add_argument('--inject-mode', default=None, help='HZ:DB or rRATIO:DB, a stray mode in every point')
    ap.add_argument('--inject-noise', type=float, default=None, help='DB, a wash in every band re the loudest mode')
    ap.add_argument('--top', type=int, default=8)
    ap.add_argument('--selftest', action='store_true', help="the audit's own test on the (first) world named: see selftest()")
    ap.add_argument('--tune', action='store_true', help="also read each render's settled pitch (export.settled_cents)")
    a = ap.parse_args()
    global TUNE
    TUNE = a.tune
    card = card_rows()
    if a.selftest:
        path = a.worlds[0]
        selftest(path, sets_for(os.path.basename(path)[:-5], a.fit, card), a.workers)
        return 0
    paths = list(a.worlds)
    if a.card:
        paths += sorted(glob.glob(os.path.join(ROOT, 'out', 'card', 'kyklophoria', '*.kykm')))
    only = set(float(v) for v in a.only.split(',') if v)
    allres = []
    for path in paths:
        name = os.path.basename(path)[:-5]
        blob = open(path, 'rb').read()
        w = parse_world(blob)
        members = w['members'] if w['kind'] == 2 else [(None, blob)]
        for mname, mb in members:
            label = name if mname is None else '%s/%s' % (name, mname)
            sets = sets_for(name, a.fit, card)
            if not sets:
                print('%s: no fitted set found' % label); continue
            mw = parse_world(mb)
            if a.inject_mode or a.inject_noise is not None:
                mode = None
                if a.inject_mode:
                    hz, dbl = a.inject_mode.split(':'); mode = (hz, float(dbl))
                mb = inject(mb, mw, mode, a.inject_noise)
                label += ' [injected %s%s]' % (a.inject_mode or '', ' noise %g' % a.inject_noise if a.inject_noise is not None else '')
            res = audit_world(mb, sets, a.velocity, only, label, a.workers)
            res['summary'] = show(res, a.top)
            allres.append(res)
    if a.json:
        json.dump(allres, open(a.json, 'w'), indent=1)
    return 0


if __name__ == '__main__':
    sys.exit(main())
