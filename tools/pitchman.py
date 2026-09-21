#!/usr/bin/env python3
"""pitchman.py <family> <sample dir> [--copy <dir>] [--out fits.tsv] [--lo 21 --hi 108]

A manifest for a set whose files carry no note name: every file's pitch,
found by a harmonic product spectrum over the first half second past the
onset (the fundamental is the bin that most of the first five harmonics
agree on, which survives a piano's weak fundamental and a plucked string's
strong second), written as tools/fitset.py's `manifest` layout — id, family,
param=midi, value, dynamic — beside the files. Printed as a table so the
eye can check it: an octave error shows up as a note out of the run, and a
sampler set is a run."""
import argparse, glob, os, sys
import numpy as np, soundfile as sf

NAMES = ['C', 'Cs', 'D', 'Ds', 'E', 'F', 'Fs', 'G', 'Gs', 'A', 'As', 'B']


def pitch(x, sr, lo, hi):
    """The lowest line with a harmonic series over it. Candidates are the
    lines within 25 dB of the strongest in the first half second past the
    onset; each is scored by how many of its first six harmonics have a
    line within 30 dB of the strongest (within 3%, since a piano's are
    stretched); the lowest candidate whose score is within one of the best
    wins. The strongest line alone is not it — a B2's seventh harmonic was
    louder than its fundamental — and a harmonic product spectrum over the
    stand rumble read every note above C5 as A#0. Below 25 Hz is nothing."""
    from scipy import signal
    x = signal.sosfiltfilt(signal.butter(8, 25.0, 'highpass', fs=sr, output='sos'), x)
    env = np.abs(x); pk = int(env.argmax())
    seg = x[max(0, pk - int(0.01 * sr)):][:int(0.5 * sr)]
    n = 1 << 16
    S = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    fr = np.fft.rfftfreq(n, 1.0 / sr)
    flo, fhi = 440 * 2 ** ((lo - 69) / 12), 440 * 2 ** ((hi - 69) / 12)
    ok = (fr >= flo) & (fr <= fhi)
    ref = S[ok].max()
    pk_i, _ = signal.find_peaks(S, height=ref * 10 ** (-30 / 20))

    def sharp(i):
        # a line stands 15 dB over the median of its own neighbourhood (a
        # fifth either side); rumble does not, whatever its height
        a, b = int(i / 1.25), int(i * 1.25) + 1
        return S[i] > 10 ** (15 / 20) * np.median(S[a:b])
    cands = [fr[i] for i in pk_i if flo <= fr[i] <= fhi and sharp(i)]

    def line(g):
        i = int(round(g * n / sr)); w = max(3, int(0.03 * g * n / sr))
        return S[max(0, i - w):i + w + 1].max()
    # a candidate is scored by the energy its harmonic series explains, the
    # first twelve, each line counted once: the note's own fundamental
    # explains nearly all of it, the soundboard's thump or a rumble line
    # explains its own line and little else, and a harmonic mistaken for
    # the fundamental explains every other line at best
    def explained(c, ks=range(1, 13)):
        return sum(line(k * c) ** 2 for k in ks if k * c < sr / 2)
    # half the fundamental explains every even line and so everything the
    # fundamental does; what it cannot explain is an odd one, so a
    # candidate must carry a twentieth of its energy on its odd harmonics
    scored = [(explained(c), c) for c in cands if explained(c, range(1, 13, 2)) >= 0.05 * explained(c)]
    if not scored:
        return fr[ok][int(np.argmax(S[ok]))]
    best = max(e for e, _ in scored)
    f = min(c for e, c in scored if e >= 0.8 * best)
    i = int(round(f * n / sr)); w = max(3, int(0.03 * f * n / sr))
    return fr[max(0, i - w) + int(np.argmax(S[max(0, i - w):i + w + 1]))]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('family'); ap.add_argument('indir')
    ap.add_argument('--out', default='fits.tsv'); ap.add_argument('--lo', type=int, default=21); ap.add_argument('--hi', type=int, default=108)
    ap.add_argument('--copy', help='a directory to fill with the files renamed by id, and the manifest: what fitset.py --layout manifest reads')
    a = ap.parse_args()
    if a.copy:
        os.makedirs(a.copy, exist_ok=True)
    rows = []
    for i, p in enumerate(sorted(glob.glob(os.path.join(a.indir, '*')))):
        if not p.lower().endswith(('.wav', '.flac', '.mp3', '.aif', '.aiff')):
            continue
        x, sr = sf.read(p, always_2d=True); x = x.mean(axis=1)
        f = pitch(x, sr, a.lo, a.hi)
        midi = 69 + 12 * np.log2(f / 440)
        m = int(round(midi))
        rows.append(('%s%03d' % (a.family, i), m, f, midi - m, os.path.basename(p)))
        if a.copy:
            import shutil
            shutil.copy(p, os.path.join(a.copy, rows[-1][0] + os.path.splitext(p)[1].lower()))
        print('  %-10s %7.1f Hz  midi %3d %-4s (%+.2f)  %s' % (rows[-1][0], f, m, NAMES[m % 12] + str(m // 12 - 1), midi - m, os.path.basename(p)))
    with open(os.path.join(a.copy or a.indir, a.out), 'w') as o:
        o.write('id\tfamily\tparam\tvalue\tdynamic\n')
        for r in rows:
            o.write('%s\t%s\tmidi\t%d\t-\n' % (r[0], a.family, r[1]))
    print('%d files -> %s' % (len(rows), os.path.join(a.copy or a.indir, a.out)))


if __name__ == '__main__':
    main()
