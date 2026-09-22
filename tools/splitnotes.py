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


def onsets(x, sr, floor_db=-50.0, rise_db=2.5, min_gap=0.18):
    """Where the notes start: every rise in the envelope above the floor,
    at least min_gap apart. Generous on purpose — the labels come from the
    pitch of each segment, not from counting these."""
    hop = int(0.01 * sr)
    e = np.array([np.sqrt((x[i:i + hop] ** 2).mean()) for i in range(0, len(x) - hop, hop)])
    db = 20 * np.log10(e + 1e-9); db -= db.max()
    out = []
    gap = int(min_gap / 0.01)
    for i in range(1, len(db)):
        if db[i] < floor_db:
            continue
        if db[i] - db[i - 1] < rise_db:
            continue
        if out and i - out[-1] < gap:
            if db[i] > db[out[-1]]:
                out[-1] = i
            continue
        out.append(i)
    return [i * hop for i in out]


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
        on = onsets(x, sr)
        if not on:
            print('  %-44s no onsets' % os.path.basename(path)); bad += 1; continue
        # the name says the range; the detector says which note each
        # segment is. Take the segments whose pitch lands within the range,
        # in an ascending run with no repeats — a run of chromatic notes is
        # exactly that — and let the rest go. Counting onsets and trusting
        # the order put every label a semitone out when the first note's
        # onset was missed.
        d = os.path.join(a.outdir, string.lower() if (a.per_string and string) else '')
        os.makedirs(d, exist_ok=True)
        picked = []
        for k, s0 in enumerate(on):
            e0 = on[k + 1] if k + 1 < len(on) else len(x)
            seg = x[max(0, s0 - int(0.01 * sr)):min(e0, s0 + int(a.max_seconds * sr))]
            if len(seg) < int(0.25 * sr):
                continue
            f = pitch(seg, sr, 21, 108)
            m = int(round(69 + 12 * math.log2(f / 440)))
            cents = abs(69 + 12 * math.log2(f / 440) - m)
            if m < lo or m > hi or cents > 0.35:
                continue
            if picked and m <= picked[-1][0]:
                continue
            picked.append((m, seg))
        off = []
        if len(picked) != want:
            off.append('%d of %d notes found' % (len(picked), want))
        for m, seg in picked:
            nm = '%s%s.%s.wav' % (NAMES[m % 12].replace('s', '#'), m // 12 - 1, dyn or 'x')
            out = os.path.join(d, nm)
            if os.path.exists(out):
                continue
            sf.write(out, seg, sr)
            made += 1
        if off:
            print('  %-44s %s' % (os.path.basename(path), '; '.join(off)))
            bad += 1
    print('%s: %d notes written, %d files skipped' % (a.outdir, made, bad))


if __name__ == '__main__':
    main()
