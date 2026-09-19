#!/usr/bin/env python3
"""meshgen — the three parametric families, as closed triangle meshes.

    meshgen.py <outdir> [--per-family 12] [--seg 24]

The brief says OpenSCAD. This machine has no OpenSCAD and no way to install one
without a password, so the families are authored here instead: each is a few
lines of parametric geometry, and writing them in Python rather than SCAD
changes nothing about what is being swept. The families and their parameters
are exactly the brief's.

Every mesh is a structured tetrahedral *volume* in metres, not a surface.
mesh2faust takes a surface and hands it to Vega's tet mesher, and that was tried
first: the mesher refines for tet quality, not for size, so a 4 mm bar came out
one or two linear tets thick, and one layer of linear tets is about twice too
stiff in bending — 2942 Hz for the uniform bar's first mode against the
Euler–Bernoulli 1441. A structured grid puts a chosen number of layers through
the thickness (ten for the bar, five for the plate and the bell wall — a
convergence run on the uniform bar gave 1683, 1512 and 1473 Hz for six, ten
and fourteen layers against 1441 analytic, and ten is where a model costs ten
seconds rather than a minute) and
splits each hexahedron into six tets along a shared diagonal, so the volume is
the author's choice rather than a mesher's guess. The grid is the same across a
family, so the FEM is refined the same way at every parameter value; a sweep
whose resolution also changed would be sweeping two things.

Twelve canonical excitation positions per model are written beside the mesh as
`<id>.expos`: coordinate and strike direction, so the extractor can find the
nearest vertex after tet meshing rather than trusting that vertex numbering
survives it. The positions are placed the same way across a family, on the same
face, so a mode's gain pattern across the twelve is comparable from one model
to the next — which is the whole basis of the alignment stage.

    tapered bar   0.12 m long, 8 mm wide, 4 mm thick at the root; the far end's
                  thickness is the root's times the taper ratio, swept 0.2..1.0.
                  Twelve positions along the top face's centreline, struck down.
    plate         4 mm thick, 0.15 m along x, 0.15/aspect along y; aspect swept
                  1.0..3.0. A 4x3 grid of positions on the top face, struck down.
    tine          a Rhodes tine: a 2 mm steel rod clamped at one end, with the
                  tuning spring as a thicker section 18 mm from the tip; length
                  swept 5 to 15 cm. Twelve positions along the top, struck down.
                  Checked against the clamped-free Euler–Bernoulli rod without
                  the lump: +8% at every length with four layers of linear tets
                  across the section, the same bias everywhere in the sweep.
    bell          a shell of revolution, 0.15 m tall, 30 mm radius at the top,
                  4 mm wall; the radius flares as r0 (1 + f (z/L)^2) with f
                  swept 0.0..1.2, so 0 is a tube and 1.2 is a bell. Four heights
                  by three azimuths (0, 45, 90 degrees) on the outer surface,
                  struck radially inward — three azimuths so that a cos(2 theta)
                  mode reads as (+, 0, -) and not as a constant.
"""
import math
import os
import sys


def write_expos(path, expos):
    with open(path, 'w') as f:
        f.write('# x y z  dx dy dz   (strike position, strike direction)\n')
        for (x, y, z), (dx, dy, dz) in expos:
            f.write('%.7f %.7f %.7f  %.4f %.4f %.4f\n' % (x, y, z, dx, dy, dz))


class Vol:
    """A structured grid of hexahedra, each split into six tetrahedra along a
    shared diagonal so that neighbouring cells agree on their faces. Vertices
    are looked up by integer grid index, so a wrapped direction (the bell's
    azimuth) is just the same index modulo."""
    def __init__(self):
        self.v = []
        self.t = []
        self.idx = {}

    def vertex(self, key, p):
        if key in self.idx:
            return self.idx[key]
        self.idx[key] = len(self.v)
        self.v.append(p)
        return self.idx[key]

    def hex(self, c):
        """c[dx][dy][dz] -> vertex ids of the cell's eight corners"""
        v000, v100, v010, v110 = c[0][0][0], c[1][0][0], c[0][1][0], c[1][1][0]
        v001, v101, v011, v111 = c[0][0][1], c[1][0][1], c[0][1][1], c[1][1][1]
        for tet in ((v000, v100, v110, v111), (v000, v110, v010, v111), (v000, v010, v011, v111),
                    (v000, v011, v001, v111), (v000, v001, v101, v111), (v000, v101, v100, v111)):
            self.t.append(self.positive(tet))

    def positive(self, tet):
        a, b, c, d = (self.v[i] for i in tet)
        ab = [b[k] - a[k] for k in range(3)]
        ac = [c[k] - a[k] for k in range(3)]
        ad = [d[k] - a[k] for k in range(3)]
        vol = (ab[0] * (ac[1] * ad[2] - ac[2] * ad[1]) - ab[1] * (ac[0] * ad[2] - ac[2] * ad[0])
               + ab[2] * (ac[0] * ad[1] - ac[1] * ad[0]))
        return tet if vol > 0 else (tet[0], tet[2], tet[1], tet[3])


def grid(vol, ni, nj, nk, point, wrap_i=False):
    """Fill a structured ni x nj x nk block; `point(i, j, k)` gives the vertex
    position. With wrap_i the i direction closes on itself."""
    ni_v = ni if wrap_i else ni + 1
    for i in range(ni):
        for j in range(nj):
            for k in range(nk):
                c = [[[None, None], [None, None]], [[None, None], [None, None]]]
                for di in (0, 1):
                    for dj in (0, 1):
                        for dk in (0, 1):
                            ii = (i + di) % ni_v
                            c[di][dj][dk] = vol.vertex((ii, j + dj, k + dk), point(ii, j + dj, k + dk))
                vol.hex(c)


def write_tet(path, vol):
    with open(path, 'w') as f:
        f.write('# meshgen — structured tetrahedral volume, metres. nv ne, then vertices, then 0-based tets\n')
        f.write('%d %d\n' % (len(vol.v), len(vol.t)))
        for p in vol.v:
            f.write('%.7f %.7f %.7f\n' % p)
        for t in vol.t:
            f.write('%d %d %d %d\n' % t)


# ── tapered bar ──────────────────────────────────────────────────────────────

def bar(taper, seg):
    L, W, T0 = 0.12, 0.008, 0.004
    nx, ny, nz = seg * 5, 6, 10
    def half(x):
        return 0.5 * T0 * (1.0 + (taper - 1.0) * x / L)
    vol = Vol()
    grid(vol, nx, ny, nz,
         lambda i, j, k: (L * i / nx, W * (j / ny - 0.5), half(L * i / nx) * (2.0 * k / nz - 1.0)))
    expos = [((L * (i + 0.5) / 12.0, 0.0, half(L * (i + 0.5) / 12.0)), (0.0, 0.0, -1.0)) for i in range(12)]
    return vol, expos


# ── plate ────────────────────────────────────────────────────────────────────

def plate(aspect, seg):
    X, T = 0.15, 0.004
    Y = X / aspect
    nx, ny, nz = seg * 5 // 3, max(8, int(round(seg * 5 / 3 / aspect))), 5
    vol = Vol()
    grid(vol, nx, ny, nz, lambda i, j, k: (X * i / nx, Y * j / ny, T * k / nz))
    expos = []
    for j in range(3):
        for i in range(4):
            expos.append(((X * (i + 0.5) / 4.0, Y * (j + 0.5) / 3.0, T), (0.0, 0.0, -1.0)))
    return vol, expos


# ── bell ─────────────────────────────────────────────────────────────────────

def bell(flare, seg):
    L, R0, WALL = 0.15, 0.03, 0.004
    na, nz, nw = seg * 5 // 2, seg * 15 // 8, 5

    def radius(z):
        return R0 * (1.0 + flare * (z / L) ** 2)

    def point(i, j, k):
        z = L * j / nz
        r = radius(z) - WALL * (1.0 - k / nw)
        a = 2 * math.pi * i / na
        return (r * math.cos(a), r * math.sin(a), z)

    vol = Vol()
    grid(vol, na, nz, nw, point, wrap_i=True)
    expos = []
    for hi in range(4):
        z = L * (hi + 0.5) / 4.0
        r = radius(z)
        for ang in (0.0, 45.0, 90.0):
            a = math.radians(ang)
            expos.append(((r * math.cos(a), r * math.sin(a), z), (-math.cos(a), -math.sin(a), 0.0)))
    return vol, expos


# ── tine ─────────────────────────────────────────────────────────────────────

def tine(length, seg, lump=True):
    """A Rhodes tine: a steel rod clamped at x = 0, with the tuning spring — a
    small mass that slides along the tine to set its pitch — as a short thicker
    section near the free end. The rod is square in section, with the side
    chosen so that the bending stiffness per unit mass matches a round rod of
    diameter D (I/A is D^2/16 for the disc and w^2/12 for the square, so
    w = D sqrt(12/16)). The clamp is `--clamp 0 -1e-9 1e-9` in modalfem: every
    vertex at x = 0. Twelve strike positions along the top, struck down, like
    the bar. The lump is a stiffer section as well as a heavier one, which a
    real spring is not; it is labelled as what it is."""
    D = 0.002
    W = D * math.sqrt(12.0 / 16.0)
    ny, nz = 4, 4
    # elements about as long as they are wide: a slender rod meshed with
    # elongated linear tets locks, and the lock grew with length (+10% at 5 cm,
    # +43% at 15 cm with a fixed count along the length)
    nx = max(seg * 2, int(round(length / (W / ny))))
    lump_from, lump_len, lump_scale = length - 0.018, 0.006, 2.2
    def side(x):
        return W * (lump_scale if (lump and lump_from <= x <= lump_from + lump_len) else 1.0)
    vol = Vol()
    grid(vol, nx, ny, nz,
         lambda i, j, k: (length * i / nx, side(length * i / nx) * (j / ny - 0.5), side(length * i / nx) * (k / nz - 0.5)))
    expos = [((length * (i + 0.5) / 12.0, 0.0, 0.5 * side(length * (i + 0.5) / 12.0)), (0.0, 0.0, -1.0)) for i in range(12)]
    return vol, expos


# family: (generator, parameter name, low, high, extra modalfem arguments)
FAMILIES = {
    'bar':   (bar,   'taper',  0.2, 1.0, ''),
    'plate': (plate, 'aspect', 1.0, 3.0, ''),
    'bell':  (bell,  'flare',  0.0, 1.2, ''),
    'tine':  (tine,  'length', 0.05, 0.15, '--clamp 0 -1e-9 1e-9'),
}


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    outdir = sys.argv[1]
    per = 12
    seg = 24
    i = 2
    while i < len(sys.argv):
        if sys.argv[i] == '--per-family':
            per = int(sys.argv[i + 1]); i += 2
        elif sys.argv[i] == '--seg':
            seg = int(sys.argv[i + 1]); i += 2
        else:
            print('unknown option', sys.argv[i]); return 2
    os.makedirs(outdir, exist_ok=True)
    manifest = open(os.path.join(outdir, 'meshes.tsv'), 'w')
    manifest.write('id\tfamily\tparam\tvalue\tvertices\ttets\n')
    for fam, (fn, pname, lo, hi, opts) in FAMILIES.items():
        for k in range(per):
            value = lo + (hi - lo) * k / (per - 1)
            vol, expos = fn(value, seg)
            mid = '%s%02d' % (fam, k)
            write_tet(os.path.join(outdir, mid + '.tet'), vol)
            write_expos(os.path.join(outdir, mid + '.expos'), expos)
            # the extractor's extra arguments for this model: the clamp, for a
            # family that has one. Read by the Makefile rule.
            with open(os.path.join(outdir, mid + '.opts'), 'w') as f:
                f.write(opts + '\n')
            manifest.write('%s\t%s\t%s\t%.4f\t%d\t%d\n' % (mid, fam, pname, value, len(vol.v), len(vol.t)))
            print('  %-8s %s=%.3f  %5d vertices %6d tets' % (mid, pname, value, len(vol.v), len(vol.t)))
    manifest.close()
    return 0


if __name__ == '__main__':
    sys.exit(main())
