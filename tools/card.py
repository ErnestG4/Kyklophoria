#!/usr/bin/env python3
"""card.py [--force world,...] — the SD card's worlds, assembled in one folder

What goes on the module's card is decided in manifests/card.tsv, one world a
row with the fitted sets it was built from, and nothing else: this copies
exactly those worlds into out/card/kyklophoria/, fresh each run, and writes
out/card/CONTENTS.txt saying what each one is, when it was built and what
the gate made of it. Copy the folder's contents into the card's
/kyklophoria (the module reads 0:/kyklophoria, not the root), replacing the
.kykm files there; your own .kykw worlds can stay.

Why a folder and not "copy what's new": the module lists the FIRST 32 world
files it finds in /kyklophoria (kMaxCardWorlds, shell/alchemy/main.cpp), in
the order the card's directory holds them, which is the order they were
written. A card that has grown past 32 drops the newest copies without a
word, and after a night of re-exports nobody could say which bass was on it.
So the card gets at most --max (24) resonator worlds, leaving room for eight
of the player's own, and every world is checked before it is copied:

  - it exists and was built after every set it came from was fitted
  - its name, the file name less .kykm, is at most 16 characters
  - it fits a 4 MB region (kResRegionBytes)
  - every set it came from passes tools/gate.py; a failing world is left
    off unless named in --force, and the reason is printed

    python3 tools/card.py
    python3 tools/card.py --force xylophone-iowa
"""
import argparse, glob, os, shutil, sys, time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gate

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
REGION = 4 << 20


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--manifest', default=os.path.join(ROOT, 'manifests', 'card.tsv'))
    ap.add_argument('--out', default=os.path.join(ROOT, 'out', 'card'))
    ap.add_argument('--force', default='', help='comma-separated worlds to copy even if a source fails the gate')
    ap.add_argument('--max', type=int, default=24)
    a = ap.parse_args()
    force = set(w for w in a.force.split(',') if w)
    g = argparse.Namespace(loss=1.5, ring=3.0, modes=8, share=0.10)

    rows = []
    for l in open(a.manifest).read().splitlines():
        if not l.strip() or l.startswith('#'):
            continue
        f = l.split('\t')
        rows.append((f[0], f[1].split(',') if len(f) > 1 and f[1] else [], f[2] if len(f) > 2 else ''))

    dest = os.path.join(a.out, 'kyklophoria')
    if os.path.isdir(dest):
        shutil.rmtree(dest)
    os.makedirs(dest)
    kept, lines = [], []
    for world, srcs, note in rows:
        path = os.path.join(ROOT, 'out', 'worlds', world + '.kykm')
        why = []
        if not os.path.exists(path):
            print('  %-18s not built yet' % world)
            lines.append('%-18s --  not built yet' % world)
            continue
        if len(world) > 16:
            why.append('name over 16 characters')
        size = os.path.getsize(path)
        if size > REGION:
            why.append('%.1f MB, over a 4 MB region' % (size / 1e6))
        dirs = sorted(set(d for s in srcs for d in glob.glob(os.path.join(ROOT, 'out', 'fit', s))
                          if os.path.exists(os.path.join(d, 'fits.tsv'))))
        if srcs and not dirs:
            why.append('no fitted set matches %s' % ','.join(srcs))
        grades = []
        for d in dirs:
            n, bad, graded = gate.grade(d, g)
            if graded is None:
                why.append('%s still being fitted' % os.path.basename(d)); continue
            if not graded:
                grades.append('%s shaped' % os.path.basename(d)); continue
            nbad = sum(1 for b in bad if b[3])
            grades.append('%s %d/%d bad' % (os.path.basename(d), nbad, n))
            if nbad > g.share * n:
                why.append('%s fails the gate (%d of %d points bad)' % (os.path.basename(d), nbad, n))
            if os.path.getmtime(os.path.join(d, 'fits.tsv')) > os.path.getmtime(path):
                why.append('%s refitted after this world was built' % os.path.basename(d))
        built = time.strftime('%m-%d %H:%M', time.localtime(os.path.getmtime(path)))
        if why and world not in force:
            print('  %-18s LEFT OFF: %s' % (world, '; '.join(why)))
            lines.append('%-18s --  left off: %s' % (world, '; '.join(why)))
            continue
        if len(kept) >= a.max:
            print('  %-18s LEFT OFF: the card already has %d' % (world, a.max))
            continue
        shutil.copy2(path, os.path.join(dest, world + '.kykm'))
        kept.append(world)
        flag = '  (forced past: %s)' % '; '.join(why) if why else ''
        print('  %-18s %7.0f KB  built %s  %s%s' % (world, size / 1e3, built, note, flag))
        lines.append('%-18s %7.0f KB  built %s  %s  [%s]%s' % (world, size / 1e3, built, note, ', '.join(grades) or 'no sets named', flag))

    with open(os.path.join(a.out, 'CONTENTS.txt'), 'w') as c:
        c.write('Kyklophoria card worlds, assembled %s by tools/card.py from manifests/card.tsv\n' % time.strftime('%Y-%m-%d %H:%M'))
        c.write('Copy kyklophoria/*.kykm into the card\'s /kyklophoria, replacing the .kykm files there.\n\n')
        c.write('\n'.join(lines) + '\n')
    print('%d worlds in %s' % (len(kept), dest))


if __name__ == '__main__':
    main()
