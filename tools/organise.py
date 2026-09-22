#!/usr/bin/env python3
"""organise.py <out dir> <file> [...] — sample files sorted into the layout the fitter reads

Every library names its files its own way and all of them carry the same
two facts: which note, and how hard. Iowa writes `Piano.ff.D4.aiff`, VCSL
writes `JHPiano_Sus_Close_A#0_vl2_rr1.wav`. This pulls the note and the
dynamic out of the name and writes one directory per dynamic, softest
first, with the note as the file name — which is what
`fitset.py --layout folders` reads, and what export.py's layer() needs to
build a world with velocity layers.

    ~/fmexplorer/bin/python tools/organise.py out/gen/piano-iowa samples/foss/iowa/Piano_Other/piano/*.aiff
"""
import argparse, os, re, shutil, sys
import soundfile as sf

NOTE = {'C': 0, 'D': 2, 'E': 4, 'F': 5, 'G': 7, 'A': 9, 'B': 11}
NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']
# every dynamic word these libraries use, softest first
ORDER = ['ppp', 'pp', 'p', 'mp', 'mf', 'f', 'ff', 'fff', 'vl1', 'vl2', 'vl3', 'vl4', 'vl5', 'vl6'] + \
        ['v%02d' % k for k in range(1, 17)] + [
         'pianissimo', 'piano', 'mezzo-piano', 'mezzo-forte', 'forte', 'fortissimo']


def midi_of(tok):
    m = re.fullmatch(r'([A-Ga-g])([b#s]?)(-?\d)', tok)
    if not m:
        return None
    # not `in '#s'`: the empty string is a substring of everything, so every
    # natural came out a semitone sharp and then collided with the flat above
    # it — 36 of every 88 Iowa piano files were dropped without a word
    acc = m.group(2)
    v = NOTE[m.group(1).upper()] + (1 if acc in ('#', 's') else -1 if acc == 'b' else 0)
    return v + 12 * (int(m.group(3)) + 1)


def parse(name):
    base = re.sub(r'\.(aif|aiff|wav|flac|mp3)$', '', os.path.basename(name), flags=re.I)
    toks = re.split(r'[._\- ]+', base)
    midi = next((midi_of(t) for t in toks if midi_of(t) is not None), None)
    # the first token is the instrument in every library seen so far, and
    # "Piano.ff.A0.aiff" reads its own name as a dynamic if you let it
    dyn = next((t.lower() for t in toks[1:] if t.lower() in ORDER), None)
    if midi is None or dyn is None:
        # Salamander writes the note and the velocity layer as one word,
        # "F#3v2"; its `rel*` files are key releases and carry no note
        for t in toks:
            m = re.fullmatch(r'([A-Ga-g][b#s]?-?\d)v(\d+)', t)
            if m:
                midi = midi if midi is not None else midi_of(m.group(1))
                dyn = dyn or ('v%02d' % int(m.group(2)))
                break
    return midi, dyn


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('outdir'); ap.add_argument('files', nargs='+')
    ap.add_argument('--octave', type=int, default=0, help='shift every parsed note by this many octaves')
    ap.add_argument('--only-dynamic', default='')
    a = ap.parse_args()
    seen = {}
    for p in a.files:
        midi, dyn = parse(p)
        if midi is None:
            print('  %-50s no note in the name' % os.path.basename(p)); continue
        midi += 12 * a.octave
        dyn = dyn or 'x'
        if a.only_dynamic and dyn != a.only_dynamic:
            continue
        d = os.path.join(a.outdir, dyn)
        os.makedirs(d, exist_ok=True)
        out = os.path.join(d, '%s%d.wav' % (NAMES[midi % 12], midi // 12 - 1))
        if os.path.exists(out):                      # a round robin: keep the first
            continue
        x, sr = sf.read(p, always_2d=True)
        sf.write(out, x.mean(axis=1), sr)
        seen.setdefault(dyn, []).append(midi)
    for dyn in sorted(seen, key=lambda d: ORDER.index(d) if d in ORDER else 99):
        v = sorted(seen[dyn])
        print('  %-6s %3d notes, %s..%s' % (dyn, len(v), NAMES[v[0] % 12] + str(v[0] // 12 - 1), NAMES[v[-1] % 12] + str(v[-1] // 12 - 1)))
    print('%s: %d dynamics' % (a.outdir, len(seen)))


if __name__ == '__main__':
    main()
