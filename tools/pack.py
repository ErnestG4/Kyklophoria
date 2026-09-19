#!/usr/bin/env python3
"""pack — a directory of modalfem records into one corpus file, padded.

    pack.py <meshes.tsv> <mmr dir> <out.mdb> <manifest.tsv> [--N 48] [--P 12]

The one rule from the brief that this enforces is that dimension is constant
across the corpus: every model has exactly N modes and every mode exactly P
gains. A model with more real modes than N keeps its lowest N. A model with
fewer is padded with junk — modes above the top of hearing, damped to nothing
in a few milliseconds, with a gain pattern that is small but not zero, because
a row of zeros has no direction and the alignment and tangent-space stages both
need every row to have one. The manifest records how many modes were real, so
nothing downstream has to guess.

Junk modes are the same in every padded model on purpose: two models that are
both mostly padding should agree on their padding rather than differ by it.
Each junk mode's gain row is a different fixed pseudo-random pattern, not one
pattern repeated: the bake takes the column space of G, and a bar with ten real
modes and thirty-eight copies of one padding row has a G of rank eleven, which
is not a point on the Grassmann manifold at all. Measured — it was the first
thing the bake's round-trip check caught.

── corpus.mdb ─────────────────────────────────────────────────────────────

    0    char[4]   'MODB'
    4    u32       version, 1
    8    u32       M, models
   12    u32       N, modes per model
   16    u32       P, gain positions per mode
   20    per model, M times:
           char[16]  id, NUL padded
           u8        family: 0 bar, 1 plate, 2 bell
           u8[3]     zero
           f32       parameter value (taper, aspect, flare)
           u32       modes that were real, the rest padding
           f32[N]    mode frequencies, Hz, ascending over the real ones
           f32[N]    damping ratio zeta, Rayleigh, per mode
           f32[N*P]  gains, mode-major: G[i*P + p]

Little-endian throughout. Everything is f32 because that is what the FEM gave
and the C++ side reads it into doubles anyway.
"""
import os
import struct
import sys

FAMILY = {'bar': 0, 'plate': 1, 'bell': 2}
JUNK_HZ0, JUNK_HZ_STEP, JUNK_ZETA, JUNK_GAIN = 22000.0, 250.0, 0.5, 1e-3


def junk_pattern(k, p):
    """A fixed pseudo-random value in [-1, 1] for junk mode k at position p, the
    same in every model and every run. A small LCG, because the point is
    determinism and independence between rows, not quality."""
    x = (1103515245 * (k * 131 + p * 7 + 17) + 12345) & 0x7fffffff
    x = (1103515245 * x + 12345) & 0x7fffffff
    return (x % 20001) / 10000.0 - 1.0


def read_mmr(path):
    hz, zeta, g = [], [], []
    with open(path) as f:
        for line in f:
            if not line.startswith('mode '):
                continue
            w = line.split()
            hz.append(float(w[3]))
            zeta.append(float(w[5]))
            g.append([float(x) for x in w[7:]])
    return hz, zeta, g


def main():
    if len(sys.argv) < 5:
        print(__doc__)
        return 2
    meshes, mmrdir, out, manifest = sys.argv[1:5]
    N, P = 48, 12
    i = 5
    while i < len(sys.argv):
        if sys.argv[i] == '--N':
            N = int(sys.argv[i + 1]); i += 2
        elif sys.argv[i] == '--P':
            P = int(sys.argv[i + 1]); i += 2
        else:
            print('unknown option', sys.argv[i]); return 2
    rows = []
    with open(meshes) as f:
        head = f.readline().split('\t')
        for line in f:
            w = line.rstrip('\n').split('\t')
            rows.append(dict(zip(head, w)))
    body = b''
    man = open(manifest, 'w')
    man.write('id\tfamily\tparam\tvalue\tmodes_real\tlowest_hz\thighest_real_hz\n')
    for r in rows:
        hz, zeta, g = read_mmr(os.path.join(mmrdir, r['id'] + '.mmr'))
        if any(len(row) != P for row in g):
            print(r['id'], 'has a gain row that is not', P, 'wide'); return 1
        nreal = min(len(hz), N)
        hz, zeta, g = hz[:nreal], zeta[:nreal], g[:nreal]
        for k in range(nreal, N):
            hz.append(JUNK_HZ0 + JUNK_HZ_STEP * (k - nreal))
            zeta.append(JUNK_ZETA)
            g.append([JUNK_GAIN * junk_pattern(k, p) for p in range(P)])
        body += struct.pack('<16sB3xfI', r['id'].encode()[:16], FAMILY[r['family']], float(r['value']), nreal)
        body += struct.pack('<%df' % N, *hz)
        body += struct.pack('<%df' % N, *zeta)
        body += struct.pack('<%df' % (N * P), *[x for row in g for x in row])
        man.write('%s\t%s\t%s\t%s\t%d\t%.1f\t%.1f\n' % (r['id'], r['family'], r['param'], r['value'], nreal,
                                                        hz[0], hz[nreal - 1] if nreal else 0.0))
        print('  %-8s %-6s %s=%-7s %2d real modes of %d  (%.0f .. %.0f Hz)' % (
            r['id'], r['family'], r['param'], r['value'], nreal, N, hz[0], hz[nreal - 1] if nreal else 0))
    man.close()
    with open(out, 'wb') as f:
        f.write(b'MODB' + struct.pack('<IIII', 1, len(rows), N, P) + body)
    print('  %s: %d models, N=%d P=%d, %d bytes' % (out, len(rows), N, P, 20 + len(body)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
