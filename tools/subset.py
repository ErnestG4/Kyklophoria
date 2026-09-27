#!/usr/bin/env python3
"""subset.py <fit dir> <out dir> <n> — a few of a set's points, for a fixture

A test fixture wants a handful of notes, not a keyboard: the suite's
worlds live in a public repository and every byte of them is stored
recording. Takes n points spread evenly across the set (and everything
they name — the record, its bursts, its target) into a new fit directory
that export.py reads like any other.

    ~/fmexplorer/bin/python tools/subset.py out/fit/piano-iowa out/fit/piano-small 11
"""
import os, shutil, sys

def main():
    src, dst, n = sys.argv[1], sys.argv[2], int(sys.argv[3])
    rows = open(os.path.join(src, 'fits.tsv')).read().splitlines()
    hdr, rows = rows[0], rows[1:]
    if len(rows) > n:
        step = (len(rows) - 1) / float(n - 1) if n > 1 else 1
        rows = [rows[int(round(i * step))] for i in range(n)]
    os.makedirs(dst, exist_ok=True)
    for f in os.listdir(dst):
        os.remove(os.path.join(dst, f))
    open(os.path.join(dst, 'fits.tsv'), 'w').write('\n'.join([hdr] + rows) + '\n')
    for r in rows:
        rid = r.split('\t')[0]
        for f in os.listdir(src):
            if f.startswith(rid + '.') or f.startswith(rid + '-'):
                shutil.copy(os.path.join(src, f), os.path.join(dst, f))
    print('%s: %d of %d points -> %s' % (src, len(rows), len(rows), dst))

if __name__ == '__main__':
    main()
