#!/usr/bin/env python3
"""export.py — a fitted world, condensed for the module.

    export.py records out/fit/wurli out/worlds/wurli.kykm
    export.py shaped out/fit/ep-vel out/worlds/ep-vel.kykm
    export.py corpus out/corpus.mdb wurli out/worlds/wurli-slots.kykm

One file a world, small enough to sit in SDRAM beside the wavetables:

    'KYKM' u16 version=6  u16 N  u16 P  u8 form  u8 kind
    f32 param_lo  f32 param_hi
    kind: what the parameter is. 0 = a note (MIDI), which the runtime takes
    from the pitch it is played at; 1 = an index, a row of bodies, which it
    takes from a position axis. (Version 4 wrote a `body` count here that
    nothing ever read; a 4 reads as kind 0.)
    P points, each:
        f32 param
        f32 h  f32 w  f32 K  f32 fc  f32 Q  f32 swing_soft  f32 swing_hard   (the stage at this point; zeros if none)
        f32 loudest   (the absolute gain the level bytes are relative to: the swing into the field needs it)
        N modes of
        u16 cents from 20 Hz  (12 * 100 * log2(hz / 20): 20 Hz .. 20 kHz in 1 cent)
        u8  decay  (-10 ln zeta, clamped: zeta 1 .. 1e-11, a tenth of a neper)
        u8  level  (dB under the point's loudest, in quarter dB, 0 .. 63.75)
        u8  phase  (0 .. 255 over a turn: the fitted phase at the strike, where the
                    hammer's timing per mode lives — from zero phase every partial
                    rises together and the onset is a spike the recording never had)

        then u8 nbands and per band f32 level, f32 t60: the wash (tools/noise.py),
        white noise through octave band-passes from 62.5 Hz under those envelopes
        then u16 nbursts, and per burst: f32 swing, f32 scale, u16 len, i16 samples[len]
        at 48 kHz — the attack the modes are not (tools/bursts.py): the recording's
        first 40 ms minus the model's, played at strike time after the pickup,
        scaled by swing / this burst's swing; a shaped world crossfades the two
        bursts bracketing the strike's swing

Five bytes a mode and 36 a point, plus ~4 KB a burst; the EP with two
bursts a note is 700 KB, the Wurlitzer 45 KB. The stage
is per point because it is: the fitted voicing walks up the keyboard. The runtime decodes a point
at note-on (exp2, exp, a table), never a sample.

`records` takes a fitted set's records as they are, every mode a note
sorted by frequency, with the phase and the burst: the fit puts a cluster
at a harmonic — a pair, a thump, a double decay — whose phases partly
cancel, and the corpus's one-mode-a-harmonic slot keeps only the loudest of
them, which on the Wurlitzer's C4 was a third harmonic at 2.7 where the
record's cluster sums to a fraction of that. Slot k is then the kth partial
by frequency, which the runtime interpolates between recorded notes; about
right when clusters are absent and approximate when not. `shaped` is the
same for a fitvel set, with the stage and a burst a take. `corpus` takes a
pitched family out of the aligned corpus, slot k harmonic k, which is the
bake's representation and not the sound's. form 0 is no stage, 1 the bell,
2 the gap.
"""
import math
import os
import struct
import sys

import numpy as np


def cents(hz):
    # fifths of a cent from 20 Hz (version 6; a whole cent before, which
    # used 5.5x less of the field than it has and moved a beat pair's rate
    # by more than a quarter on a third of the piano's pairs). 65535 is
    # 13107 cents: 38.8 kHz
    return int(np.clip(round(5 * 1200 * math.log2(max(hz, 20.0) / 20.0)), 0, 65535))


def decay8(zeta):
    return int(np.clip(round(-10.0 * math.log(max(zeta, 1e-11))), 0, 255))


def level8(gain, loudest):
    # 255 is silence — a ghost, a slot with no mode — and not -63.75 dB,
    # which a third of the Wurlitzer's slots rang at
    if gain == 0.0 or loudest <= 0:
        return 255
    db = 20 * math.log10(max(abs(gain), 1e-12) / loudest)
    return int(np.clip(round(-db * 4), 0, 254))


def phase8(ph):
    return int(round((ph % (2 * math.pi)) / (2 * math.pi) * 256)) % 256


def write(path, N, points, form=0, kind=0):
    """points: [(param, modes, stage[, bursts])] with stage = (h, w, K, fc, Q, swing_soft, swing_hard) or None,
    bursts = [(swing, samples_at_48k)]"""
    params = [p[0] for p in points]
    out = b'KYKM' + struct.pack('<HHHBB', 6, N, len(points), form, kind)
    out += struct.pack('<ff', min(params), max(params))
    for pt in points:
        p, modes, stage = pt[0], pt[1], pt[2]
        bursts = pt[3] if len(pt) > 3 else []
        noise = pt[4] if len(pt) > 4 else []
        out += struct.pack('<f', p)
        loudest = max((abs(m[2]) for m in modes), default=1.0)
        out += struct.pack('<8f', *(stage or (0, 1, 1, 0, 1, 1, 1)), loudest)
        # a record with more modes than the bank keeps its loudest by ring
        # energy, not its lowest: the ones over the budget are the quiet
        # ones, wherever they sit
        ms = [tuple(m) + (0.0,) * (4 - len(m)) for m in modes]
        if len(ms) > N:
            ms = sorted(sorted(ms, key=lambda m: -(m[2] ** 2 / max(m[1] * m[0], 1e-9)))[:N])
        rows = ms + [(20.0, 1.0, 0.0, 0.0)] * max(0, N - len(ms))
        for hz, zeta, g, ph in rows:
            out += struct.pack('<HBBB', cents(hz), decay8(zeta), level8(g, loudest), phase8(ph))
        out += struct.pack('<B', len(noise))
        for level, t60 in noise:
            out += struct.pack('<ff', level, t60)
        out += struct.pack('<H', len(bursts))
        for b in bursts:
            swing, samples = b[0], b[1]
            fade = int(b[2]) if len(b) > 2 else len(samples) // 3      # samples at 48 kHz the modes come in over
            scale = float(np.max(np.abs(samples))) if len(samples) else 1.0
            q = np.clip(np.round(samples / (scale or 1.0) * 32767), -32768, 32767).astype('<i2')
            out += struct.pack('<ffHH', swing, scale, len(q), min(fade, len(q))) + q.tobytes()
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    open(path, 'wb').write(out)
    print('%s: %d points x %d modes, form %d, %d bytes' % (path, len(points), N, form, len(out)))


def noise_of(recdir, rid):
    """[(level, t60)] x 8 from a record's noise line, or []"""
    for w in (l.split() for l in open(os.path.join(recdir, rid + '.mmr'))):
        if w and w[0] == 'noise':
            v = [float(x) for x in w[1:]]
            return [(v[2 * k], v[2 * k + 1]) for k in range(len(v) // 2)]
    return []


def bursts_of(recdir, rid, swings=None):
    """[(swing, samples at 48 kHz, fade samples at 48 kHz)] from a record's
    burst lines — `burst <file> <fade>` on a pitched record, one at swing 1;
    `burst <take> <file> <fade>` on a shaped record, one a take at the
    take's swing. The fade is how many samples the modes come in over."""
    import soundfile as sf
    from scipy.signal import resample_poly
    out = []
    for w in (l.split() for l in open(os.path.join(recdir, rid + '.mmr'))):
        if not w or w[0] != 'burst':
            continue
        toks = w[1:]
        fade = None
        if toks and toks[-1].isdigit():
            fade = int(toks[-1]); toks = toks[:-1]
        take, fname = (toks[0], toks[1]) if len(toks) > 1 else (None, toks[0])
        x, sr = sf.read(os.path.join(recdir, fname))
        if fade is None:
            fade = len(x) // 3
        if sr != 48000:
            x = resample_poly(x, 48000, sr); fade = int(fade * 48000 / sr)
        out.append(((swings or {}).get(take, 1.0), x.astype(np.float32), fade))
    return out


def source_rms(recdir, rid):
    """the recording's own level, before the fit normalised it: the RMS of
    its loudest half second, from the file the record names. None if the
    file is not there (the samples are not in the repo)"""
    import soundfile as sf
    src = None
    for w in (l.split() for l in open(os.path.join(recdir, rid + '.mmr'))):
        if w and w[0] == 'source':
            src = ' '.join(w[1:]); break
    if not src or not os.path.exists(src):
        return None
    x, sr = sf.read(src, always_2d=True); x = x.mean(axis=1)
    n = int(0.5 * sr)
    if len(x) <= n:
        return float(np.sqrt(np.mean(x ** 2)) + 1e-9)
    # a running mean by cumulative sum: np.convolve with a 22050-point
    # kernel over a 3.5 s file was 3e9 multiplies a file, and the banjo's
    # export ran for an hour at five cores
    c = np.concatenate([[0.0], np.cumsum(x.astype(np.float64) ** 2)])
    e = (c[n:] - c[:-n]) / n
    return float(np.sqrt(e.max()) + 1e-9)


def layer(recdir, pts):
    """Records at the same parameter are one point with velocity layers,
    not several points. The Philharmonia sets have two or three dynamics a
    note, and exported as separate points the runtime played whichever came
    last — a keyboard walking piano and forte at random, which Combust
    heard as the violin and viola sounding wrong. The fit normalised every
    recording, so the dynamics' levels are read back from the source files:
    the loudest take is the point (its modes, its wash), each take's swing
    is its level over the loudest's, and each take's burst — its own attack,
    which is where a pizzicato's dynamics differ most — is placed at that
    swing, scaled into the loudest take's units, so a strike between two
    dynamics crossfades their attacks over the shared modes at the level
    between them. Without the source files the loudest-fitting record
    stands alone and the others are dropped, and it says so."""
    groups = {}
    for pt in pts:
        groups.setdefault(pt[0], []).append(pt)
    out = []
    for param, g in groups.items():
        if len(g) == 1:
            out.append(g[0][:5]); continue
        lv = [(source_rms(recdir, pt[5]), pt) for pt in g]
        if any(l is None for l, _ in lv):
            print('  %s: %d records at param %g and no source levels: keeping %s, dropping the rest' % (recdir, len(g), param, g[-1][5]))
            out.append(g[-1][:5]); continue
        lv.sort(key=lambda t: -t[0])
        top, carrier = lv[0]
        bursts = []
        for l, pt in lv:
            sw = l / top
            for b in pt[3]:
                bursts.append((sw, b[1] * sw, b[2] if len(b) > 2 else len(b[1]) // 3))
        bursts.sort(key=lambda b: b[0])
        stage = tuple(carrier[2][:5]) + (bursts[0][0] if bursts else 1.0, 1.0)
        print('  %s: param %g, %d takes as layers: %s' % (recdir, param, len(g), ' '.join('%s@%.2f' % (pt[5], l / top) for l, pt in lv)))
        out.append((param, carrier[1], stage, bursts, carrier[4]))
    return out


def intune(pts, kind, max_cents=120.0):
    """Each point pulled to its nominal note. A point played at the pitch
    the recording had — the Philharmonia's within a few cents, a sampler's
    top octave 40 cents out and two files 67 cents apart — so a world was in
    tune with itself and not with A440 along the keyboard, and a v/oct that
    asked for a C4 got the C4 file's idea of one. The fundamental is the
    fitted mode nearest ratio 1 with a fifth of the loudest gain (a
    Wurlitzer's bass fundamental is weak, so a strong second harmonic at
    ratio 2 is checked as well); every mode is scaled by nominal over
    measured, so the inharmonicity stays and the note lands on the note.
    Corrections past 120 cents are an octave error in a manifest, not a
    tuning, and are refused and reported. Index worlds have no note."""
    if kind != 0:
        return pts
    out = []
    corr = []
    for param, modes, *rest in pts:
        f = 440.0 * 2 ** ((param - 69) / 12)
        loud = max((abs(m[2]) for m in modes), default=0.0)
        cands = [m for m in modes if abs(m[2]) > 0.2 * loud and 0.9 < m[0] / f < 1.1]
        ratio = 1.0
        if not cands:
            cands = [m for m in modes if abs(m[2]) > 0.2 * loud and 1.8 < m[0] / f < 2.2]
            ratio = 2.0
        if not cands:
            corr.append((param, None)); out.append((param, modes, *rest)); continue
        # the fundamental's frequency is the energy-weighted centre of the
        # modes within 6% of the loudest candidate, not the loudest alone: a
        # pizzicato's pitch settles after the pluck and the fit spends two
        # or three modes on it, and the loudest is any one of them
        top = max(cands, key=lambda m: abs(m[2]))[0]
        clus = [m for m in cands if abs(m[0] / top - 1) < 0.06]
        fm = sum(m[0] * m[2] ** 2 for m in clus) / sum(m[2] ** 2 for m in clus) / ratio
        cents = 1200 * math.log2(f / fm)
        if abs(cents) > max_cents:
            corr.append((param, cents)); out.append((param, modes, *rest)); continue
        k = f / fm
        rest = list(rest)                 # stage, bursts, noise[, id]
        if len(rest) > 1 and rest[1]:
            # the burst pulled by the same k, or it plays at the recorded
            # pitch under modes at the note (up to 110 cents apart on the
            # piano): a linear read at step k, as the runtime's own
            nb = []
            for b in rest[1]:
                x = b[1]; m = int(len(x) / k)
                nb.append((b[0], np.interp(np.arange(m) * k, np.arange(len(x)), x).astype(np.float32), int(b[2] / k) if len(b) > 2 else m // 3))
            rest[1] = nb
        out.append((param, [(m[0] * k, m[1], m[2], m[3]) for m in modes], *rest))
        corr.append((param, cents))
    fixed = [c for _, c in corr if c is not None and abs(c) <= max_cents]
    refused = [(p, c) for p, c in corr if c is not None and abs(c) > max_cents]
    print('  in tune: %d of %d points pulled to their note, |cents| median %.1f max %.1f%s' % (
        len(fixed), len(pts), np.median(np.abs(fixed)) if fixed else 0, max(np.abs(fixed)) if fixed else 0,
        ''.join('; %g refused at %+.0f cents (an octave error?)' % (p, c) for p, c in refused)))
    return out


def align(pts, kind, cap=48):
    """The points padded to one width, with ghosts that mean something.

    A note world (kind 0) is no longer interpolated slot by slot: the
    runtime plays the nearest point transposed to the note (docs/
    holistic-math.md, its item 2 — the interpolated bank at a midpoint
    was up to 12-19 dB louder than either point, the fitter's antiphase
    pairs un-cancelling), so its slots need no alignment, only a common
    width: each point's modes in frequency order, and silence — the note
    at zero gain, level byte 255 — where it has fewer than the widest.
    The chain alignment by ratio that was here is in the history (d33fb45)
    should slot interpolation of a note world ever come back.

    An index world (kind 1), a row of bodies, is still interpolated along
    the row — the glide from gong to woodblock is the point of it — and a
    body with fewer modes than its neighbour used to be padded with 20 Hz,
    which a real mode was interpolated against on the way: a 12.6 kHz mode
    through 502 Hz at -1 dB between two bodies. A body's missing slot now
    holds its nearest neighbour's frequency in that rank at zero gain, so
    the neighbour's mode fades out on the way rather than diving."""
    N = min(cap, max(len(pt[1]) for pt in pts))
    out = []
    for i, (param, modes, *rest) in enumerate(pts):
        ms = sorted(modes)
        if len(ms) > N:
            ms = sorted(sorted(ms, key=lambda m: -(m[2] ** 2 / max(m[1] * m[0], 1e-9)))[:N])
        while len(ms) < N:
            s_ = len(ms)
            if kind == 1:
                near = min((pts[j] for j in range(len(pts)) if j != i and len(pts[j][1]) > s_),
                           key=lambda pt: abs(pt[0] - param), default=None)
                hz = sorted(near[1])[s_][0] if near else (ms[-1][0] if ms else 440.0)
                ms.append((hz, ms[-1][1] if ms else 0.01, 0.0, 0.0))
            else:
                ms.append((440.0 * 2 ** ((param - 69) / 12), 0.01, 0.0, 0.0))
        out.append((param, ms, *rest))
    return out


def read_corpus(path):
    b = open(path, 'rb').read()
    assert b[:4] == b'MODB'
    ver, n, N, P, nfam = struct.unpack_from('<IIIII', b, 4)
    off = 24
    fams = [struct.unpack_from('<16s', b, off + 16 * i)[0].rstrip(b'\0').decode() for i in range(nfam)]
    off += 16 * nfam
    rows = []
    for _ in range(n):
        name, fam, fitted, value, nreal = struct.unpack_from('<16sBB2xfI', b, off); off += 28
        hz = struct.unpack_from('<%df' % N, b, off); off += 4 * N
        zeta = struct.unpack_from('<%df' % N, b, off); off += 4 * N
        g = struct.unpack_from('<%df' % (N * P), b, off); off += 4 * N * P
        rows.append((name.rstrip(b'\0').decode(), fams[fam], fitted, value, nreal, hz, zeta, [g[i * P] for i in range(N)]))
    return N, rows


def main():
    kind = sys.argv[1]
    if kind == 'corpus':
        N, rows = read_corpus(sys.argv[2])
        fam = sys.argv[3]
        # the corpus carries no phase; the family's records do, matched by frequency
        phases = {}
        recdir = os.path.join('out', 'fit', fam)
        if os.path.isdir(recdir):
            for r in rows:
                if r[1] != fam:
                    continue
                rp = os.path.join(recdir, r[0] + '.mmr')
                if os.path.exists(rp):
                    phases[r[0]] = [(float(w[3]), float(w[w.index('phase') + 1])) for w in (l.split() for l in open(rp)) if w and w[0] == 'mode' and 'phase' in w]

        def phase_of(rid, hz):
            # the corpus holds float32 frequencies: the nearest record mode within 0.01%
            best = min(((abs(f / hz - 1), ph) for f, ph in phases.get(rid, [])), default=(1.0, 0.0))
            return best[1] if best[0] < 1e-4 else 0.0

        pts = sorted([(r[3], [(h, z, g, phase_of(r[0], h)) for h, z, g in zip(r[5], r[6], r[7])], None,
                       bursts_of(recdir, r[0]) if os.path.exists(os.path.join(recdir, r[0] + '.mmr')) else [],
                       noise_of(recdir, r[0]) if os.path.exists(os.path.join(recdir, r[0] + '.mmr')) else [])
                      for r in rows if r[1] == fam], key=lambda p: p[0])
        if not pts:
            print('no family', fam); return 1
        write(sys.argv[4], N, pts)
    elif kind in ('shaped', 'records'):
        d = sys.argv[2]
        pts, form, kind = [], 0, 0
        for line in open(os.path.join(d, 'fits.tsv')).read().splitlines()[1:]:
            c = line.split('\t')
            kind = 1 if c[2] == 'index' else 0
            modes, shaper, swings = [], None, [1e9, 0]
            for l in open(os.path.join(d, c[0] + '.mmr')):
                w = l.split()
                if not w:
                    continue
                if w[0] == 'mode':
                    modes.append((float(w[3]), float(w[5]), float(w[w.index('gains') + 1]), float(w[w.index('phase') + 1]) if 'phase' in w else 0.0))
                elif w[0] == 'shaper':
                    form = {'bell': 1, 'gap': 2}[w[1]]
                    shaper = tuple(float(v) for v in w[2:7])
                elif w[0] == 'take':
                    swings[0] = min(swings[0], float(w[3])); swings[1] = max(swings[1], float(w[3]))
            takes = {}
            for l in open(os.path.join(d, c[0] + '.mmr')):
                w = l.split()
                if w and w[0] == 'take':
                    takes[w[1]] = float(w[3])
            if swings[1] <= 0:
                swings = [1.0, 1.0]
            pts.append((float(c[3]), sorted(modes), (shaper or (0, 1, 1, 0, 1)) + tuple(swings), bursts_of(d, c[0], takes), noise_of(d, c[0]), c[0]))
        pts = layer(d, pts)
        pts.sort(key=lambda p: p[0])
        pts = intune(pts, kind)
        pts = align(pts, kind)
        N = max(len(pt[1]) for pt in pts)
        write(sys.argv[3], N, pts, form, kind)
    return 0


if __name__ == '__main__':
    sys.exit(main())
