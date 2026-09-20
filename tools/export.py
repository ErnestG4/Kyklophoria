#!/usr/bin/env python3
"""export.py — a fitted world, condensed for the module.

    export.py corpus out/corpus.mdb wurli out/worlds/wurli.kykm
    export.py shaped out/fit/ep-vel out/worlds/ep-vel.kykm

One file a world, small enough to sit in SDRAM beside the wavetables:

    'KYKM' u16 version=2  u16 N  u16 P  u8 form  u8 body
    f32 param_lo  f32 param_hi
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

Five bytes a mode and 36 a point; 48 modes x 85 notes is 23 KB. The stage
is per point because it is: the fitted voicing walks up the keyboard. The runtime decodes a point
at note-on (exp2, exp, a table), never a sample.

`corpus` takes a pitched family out of the aligned corpus: slot k is
harmonic k, so the runtime can interpolate between the points it has (a
Wurlitzer with eleven notes recorded) by slot. `shaped` takes a fitvel set:
the metal's few modes by frequency rank, and the stage. form 0 is no stage,
1 the bell, 2 the gap.
"""
import math
import os
import struct
import sys

import numpy as np


def cents(hz):
    return int(np.clip(round(1200 * math.log2(max(hz, 20.0) / 20.0)), 0, 65535))


def decay8(zeta):
    return int(np.clip(round(-10.0 * math.log(max(zeta, 1e-11))), 0, 255))


def level8(gain, loudest):
    db = 20 * math.log10(max(abs(gain), 1e-12) / loudest) if loudest > 0 else -60
    return int(np.clip(round(-db * 4), 0, 255))


def phase8(ph):
    return int(round((ph % (2 * math.pi)) / (2 * math.pi) * 256)) % 256


def write(path, N, points, form=0, body=0):
    """points: [(param, modes, stage)] with stage = (h, w, K, fc, Q, swing_soft, swing_hard) or None"""
    params = [p[0] for p in points]
    out = b'KYKM' + struct.pack('<HHHBB', 2, N, len(points), form, body)
    out += struct.pack('<ff', min(params), max(params))
    for p, modes, stage in points:
        out += struct.pack('<f', p)
        loudest = max((abs(m[2]) for m in modes), default=1.0)
        out += struct.pack('<8f', *(stage or (0, 1, 1, 0, 1, 1, 1)), loudest)
        rows = [tuple(m) + (0.0,) * (4 - len(m)) for m in list(modes)[:N]] + [(20.0, 1.0, 0.0, 0.0)] * max(0, N - len(modes))
        for hz, zeta, g, ph in rows:
            out += struct.pack('<HBBB', cents(hz), decay8(zeta), level8(g, loudest), phase8(ph))
    os.makedirs(os.path.dirname(path) or '.', exist_ok=True)
    open(path, 'wb').write(out)
    print('%s: %d points x %d modes, form %d, %d bytes' % (path, len(points), N, form, len(out)))


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
                    for w in (l.split() for l in open(rp)):
                        if w and w[0] == 'mode' and 'phase' in w:
                            phases[(r[0], round(float(w[3]), 3))] = float(w[w.index('phase') + 1])
        pts = sorted([(r[3], [(h, z, g, phases.get((r[0], round(h, 3)), 0.0)) for h, z, g in zip(r[5], r[6], r[7])], None)
                      for r in rows if r[1] == fam], key=lambda p: p[0])
        if not pts:
            print('no family', fam); return 1
        write(sys.argv[4], N, pts)
    elif kind == 'shaped':
        d = sys.argv[2]
        pts, form = [], 0
        for line in open(os.path.join(d, 'fits.tsv')).read().splitlines()[1:]:
            c = line.split('\t')
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
            pts.append((float(c[3]), sorted(modes), (shaper or (0, 1, 1, 0, 1)) + tuple(swings)))
        pts.sort(key=lambda p: p[0])
        N = max(len(m) for _, m, _ in pts)
        write(sys.argv[3], N, pts, form, 0)
    return 0


if __name__ == '__main__':
    sys.exit(main())
