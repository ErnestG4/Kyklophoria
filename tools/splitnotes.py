#!/usr/bin/env python3
"""splitnotes.py <out dir> <file> [...] — Iowa's chromatic runs cut into notes

The University of Iowa's strings, guitar and mallets are recorded a few
notes to a file — `Violin.arco.pp.sulG.G3B3.aiff` is G3 to B3 played one
after another — and the filename says exactly which notes and in what
order. So the cut is checkable: find the onsets, and if their number
matches the semitones the name claims, each segment's note is known
without guessing. Every segment is then pitched by tools/pitchman.py's
detector and disagreements are reported; a file whose onsets do not match
its name is left alone and named.

The string is kept: `sulG` is the G string, and a set split this way
becomes one directory per string, which is what makes the same note on
two strings two different points.

    ~/fmexplorer/bin/python tools/splitnotes.py out/gen/violin samples/foss/iowa/Strings/violin/*.aiff
"""
import argparse, math, os, re, sys
import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pitchman import pitch, NAMES

NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}


def midi_of(tok):
    m = re.fullmatch(r'([A-Ga-g])([b#]?)(-?\d)', tok)
    if not m:
        return None
    v = NOTE[m.group(1).upper()] + (1 if m.group(2) == '#' else -1 if m.group(2) == 'b' else 0)
    return v + 12 * (int(m.group(3)) + 1)


def parse(name):
    """(first midi, last midi, string or None, dynamic or None) from an Iowa name"""
    base = os.path.basename(name)
    base = re.sub(r'\.(aif|aiff|wav|flac)$', '', base, flags=re.I)
    base = base.replace('.stereo', '').replace('.mono', '')
    parts = base.split('.')
    string = next((p for p in parts if p.lower().startswith('sul')), None)
    dyn = next((p for p in parts if p.lower() in ('pp', 'mf', 'ff', 'mp', 'mezzo', 'p', 'f')), None)
    lo = hi = None
    for p in parts:
        m = re.fullmatch(r'([A-Ga-g][b#]?-?\d)([A-Ga-g][b#]?-?\d)', p)
        if m:
            lo, hi = midi_of(m.group(1)), midi_of(m.group(2))
            break
        if lo is None and midi_of(p) is not None:
            lo = hi = midi_of(p)
    return lo, hi, string, dyn


def onsets(x, sr, want, floor_db=-45.0):
    """Where the notes start: the envelope's rises, the `want` strongest,
    at least 200 ms apart."""
    hop = int(0.01 * sr)
    e = np.array([np.sqrt((x[i:i + hop] ** 2).mean()) for i in range(0, len(x) - hop, hop)])
    db = 20 * np.log10(e + 1e-9)
    db -= db.max()
    rise = np.concatenate([[0], np.diff(db)])
    cand = [(rise[i], i) for i in range(2, len(db)) if db[i] > floor_db and rise[i] > 3.0]
    cand.sort(reverse=True)
    keep = []
    for r, i in cand:
        if all(abs(i - j) > 20 for j in keep):
            keep.append(i)
        if len(keep) >= want * 3:
            break
    keep.sort()
    if len(keep) > want:                      # the loudest `want` rises, in order
        by = sorted(keep, key=lambda i: -rise[i])[:want]
        keep = sorted(by)
    return [i * hop for i in keep]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('outdir'); ap.add_argument('files', nargs='+')
    ap.add_argument('--per-string', action='store_true', help='a directory per string')
    ap.add_argument('--dynamic', default='', help='only this dynamic (pp, mf, ff)')
    ap.add_argument('--max-seconds', type=float, default=6.0)
    a = ap.parse_args()
    made = bad = 0
    for path in a.files:
        lo, hi, string, dyn = parse(path)
        if lo is None:
            print('  %-44s no note range in the name' % os.path.basename(path)); bad += 1; continue
        if a.dynamic and (dyn or '').lower() != a.dynamic.lower():
            continue
        want = hi - lo + 1
        x, sr = sf.read(path, always_2d=True); x = x.mean(axis=1)
        on = onsets(x, sr, want)
        if len(on) != want:
            print('  %-44s %d onsets for %d notes (%s..%s) — skipped' % (
                os.path.basename(path), len(on), want, NAMES[lo % 12] + str(lo // 12 - 1), NAMES[hi % 12] + str(hi // 12 - 1)))
            bad += 1; continue
        d = os.path.join(a.outdir, string.lower() if (a.per_string and string) else '')
        os.makedirs(d, exist_ok=True)
        off = []
        for k, s0 in enumerate(on):
            e0 = on[k + 1] if k + 1 < len(on) else len(x)
            seg = x[max(0, s0 - int(0.01 * sr)):min(e0, s0 + int(a.max_seconds * sr))]
            if len(seg) < int(0.2 * sr):
                continue
            m = lo + k
            f = pitch(seg, sr, 21, 108)
            heard = 69 + 12 * math.log2(f / 440)
            if abs(heard - m) > 0.6:
                off.append('%s heard %s' % (NAMES[m % 12] + str(m // 12 - 1), NAMES[int(round(heard)) % 12] + str(int(round(heard)) // 12 - 1)))
            nm = '%s%s.%s.wav' % (NAMES[m % 12].replace('s', '#'), m // 12 - 1, dyn or 'x')
            sf.write(os.path.join(d, nm), seg, sr)
            made += 1
        if off:
            print('  %-44s %d of %d disagree: %s' % (os.path.basename(path), len(off), want, '; '.join(off[:3])))
    print('%s: %d notes written, %d files skipped' % (a.outdir, made, bad))


if __name__ == '__main__':
    main()
