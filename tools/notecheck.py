#!/usr/bin/env python3
"""notecheck.py <world.kykm> [lo hi] [--vel v]   — every semitone through the engine
(--vel: the strike's velocity, 0.9 by default; a soft strike tells a pickup's
hard-hit octave "bark" from a fit that is an octave wrong at every velocity)

Combust, on the module: "wurli's notes are OFF badly. A2 to B2 and C2 in
particular is an octave wrong." The records were the cause (a C2 labelled
C3; two bass fits without their fundamental), and this is the check that
would have said so before the card did: every semitone from lo to hi
struck once in isolation through Kyklophoria's own engine (kykdesk
--resonate, the same runtime the module runs), and its pitch read back by
tools/pitchman.py's detector.

WHAT IT GRADES AGAINST, and why that changed. Asking whether the render
reads the note that was asked for is the wrong question on an instrument
whose fundamental is weak. A viola's C string at D3 radiates its second
harmonic some 21 dB ABOVE its fundamental — measured on the Iowa recording,
not assumed — and a detector given a clean sum of decaying sinusoids hears
the octave, where on the recording it finds the period anyway from the bow
noise and the inharmonicity. That put 34 of 405 semitones of the string
sets "off" while the fits were reproducing the recordings to within a
fraction of a dB (F#3: recording -24.7, resynth -24.7).

So the target is the grade. For each note the record's own -target.wav is
read through the same detector, and a note is only a failure when THE
RECORDING READS RIGHT AND OURS DOES NOT — which is the only case where we
introduced something. Where the recording reads the octave too, it is
reported as agreed, not as a failure: that is the instrument. Where no
target is on disk the ask is the grade, as before, which is what the
Wurlitzer needed and where fundcheck.py agreed the fundamentals really
were missing.

    ~/fmexplorer/bin/python tools/notecheck.py out/worlds/wurli.kykm 36 96
"""
import os, re, subprocess, sys, tempfile
import numpy as np, soundfile as sf
sys.path.insert(0, os.path.dirname(__file__))
from pitchman import pitch, NAMES

KYK = os.path.join(os.path.dirname(__file__), '..', '..', 'Kyklophoria')


def harmonics(x, sr, f0, n=4):
    """each of the first n harmonics in dB over half a second of sustain (the
    attack skipped, since a strike's broadband front swamps the balance), and
    the spectrum's own median as a floor. A harmonic under floor + 12 dB is
    not there to be measured, and comparing two numbers that are both the
    floor produced 140 dB "balance shifts" that meant only that one of the
    two signals was silent at that frequency."""
    s = x[int(0.15 * sr):int(0.15 * sr) + int(0.5 * sr)]
    if len(s) < int(0.1 * sr):
        s = x[:max(1, len(x))]
    X = np.abs(np.fft.rfft(s * np.hanning(len(s)))); fr = np.fft.rfftfreq(len(s), 1 / sr)
    floor = 20 * np.log10(np.median(X) + 1e-12)
    out = []
    for k in range(1, n + 1):
        f = f0 * k
        m = (fr > f * 0.97) & (fr < f * 1.03)
        v = 20 * np.log10(X[m].max() + 1e-12) if m.any() else -120.0
        out.append(v if v > floor + 12 else None)
    return out


# a velocity-layered record's takes, soft to hard, by the names the sets use
LAYER_RANK = {'min': 0.0, 'pp': 0.0, 'p': 0.2, 'mp': 0.4, 'med': 0.5, 'mf': 0.6, 'f': 0.8, 'max': 1.0, 'ff': 1.0}


def layer_rank(name):
    """a take's loudness 0..1 from its layer's name: pp/mf/ff and MIN/MED/MAX
    by LAYER_RANK, Epi's v015/v030/... as the velocity they were rendered at"""
    n = name.lower()
    if n in LAYER_RANK:
        return LAYER_RANK[n]
    m = re.fullmatch(r'v(\d{3})', n)
    return int(m.group(1)) / 100.0 if m else None


def fit_dir(world):
    """where a card world was fitted: the card manifests name it (piano-iowa-vel
    is out/fit/piano-iowa3), else out/fit/<its own name>"""
    name = os.path.basename(world)[:-5]
    for man in ('manifests/card.tsv', 'manifests/card-extra.tsv'):
        if os.path.exists(man):
            for line in open(man).read().splitlines():
                f = line.split('\t')
                if len(f) >= 2 and f[0] == name:
                    return os.path.join('out', 'fit', f[1])
    return os.path.join('out', 'fit', name)


def targets(world, vel=0.9):
    """midi -> the recording that record was fitted from, when it is on disk,
    the take nearest the strike's velocity where there are several: a
    layered record's (ep-vel's epv007-MED-target.wav, Epi's tine000-v050-),
    or one row a take (bells-vel's pp, mf and ff rows at one note). Without
    the layers every layered world was graded on the ask, and an EP's bass,
    whose recordings carry the second harmonic 6-14 dB over the first even
    soft, read as an octave wrong (28 September)."""
    d = fit_dir(world)
    f = os.path.join(d, 'fits.tsv')
    if not os.path.exists(f):
        return {}
    rows = [l.split('\t') for l in open(f).read().splitlines()]
    hdr = rows[0]
    if 'param' not in hdr or 'value' not in hdr:
        return {}
    ip, iv = hdr.index('param'), hdr.index('value')
    idyn = hdr.index('dynamic') if 'dynamic' in hdr else None
    files = os.listdir(d)
    best = {}                                  # midi -> (distance, path)
    for r in rows[1:]:
        if len(r) <= max(ip, iv) or r[ip] != 'midi':
            continue
        midi = int(round(float(r[iv])))
        cands = []
        t = r[0] + '-target.wav'
        if t in files:
            rk = layer_rank(r[idyn]) if idyn is not None and len(r) > idyn else None
            cands.append((rk, t))
        pre = r[0] + '-'
        for fn in files:
            if fn.startswith(pre) and fn.endswith('-target.wav'):
                rk = layer_rank(fn[len(pre):-len('-target.wav')])
                if rk is not None:
                    cands.append((rk, fn))
        for rk, fn in cands:
            dist = abs(rk - vel) if rk is not None else 0.5
            if midi not in best or dist < best[midi][0]:
                best[midi] = (dist, os.path.join(d, fn))
    return {m: p for m, (_, p) in best.items()}


def main():
    vel = 0.9
    if '--vel' in sys.argv:
        i = sys.argv.index('--vel'); vel = float(sys.argv[i + 1]); del sys.argv[i:i + 2]
    world = sys.argv[1]
    lo = int(sys.argv[2]) if len(sys.argv) > 2 else 36
    hi = int(sys.argv[3]) if len(sys.argv) > 3 else 96
    tgt = targets(world, vel)
    kykdesk = os.path.join(KYK, 'build', 'host', 'kykdesk')
    if not os.path.exists(kykdesk):
        print('no %s: make -C %s build/host/kykdesk' % (kykdesk, KYK)); return 2
    bad, agreed, ear = [], [], []
    with tempfile.TemporaryDirectory() as d:
        for m in range(lo, hi + 1):
            f0 = 440.0 * 2 ** ((m - 69) / 12)
            sc = os.path.join(d, 'n.txt'); wav = os.path.join(d, 'n.wav')
            open(sc, 'w').write('0.8 dur\n0.0 f0 %.4f\n0.05 strike %.3f\n' % (f0, vel))
            subprocess.run([kykdesk, '--gen', '--seed', '1', '--resonate', world, '--script', sc, '--out', wav],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, check=True)
            x, sr = sf.read(wav, always_2d=True); x = x.mean(axis=1)
            f = pitch(x[int(0.05 * sr):], sr, 21, 108)
            got = 69 + 12 * np.log2(f / 440)
            name = lambda v: NAMES[int(round(v)) % 12] + str(int(round(v)) // 12 - 1)
            if abs(got - m) <= 0.5:
                continue
            # ours disagrees with the ask. Does the recording agree with the
            # ask? If it does not, this is the instrument and not a fault
            if m in tgt:
                xt, srt = sf.read(tgt[m], always_2d=True); xt = xt.mean(axis=1)
                ft = pitch(xt, srt, 21, 108)
                gt = 69 + 12 * np.log2(ft / 440)
                if abs(gt - m) > 0.5:
                    agreed.append('  %-4s reads %-4s in the recording too (%+.1f st), ours %-4s (%+.1f st)'
                                  % (name(m), name(gt), gt - m, name(got), got - m))
                    continue
                # the recording reads right and we do not. Did we move the
                # harmonic balance, or is ours faithful and merely sparse?
                # A clean sum of decaying sinusoids gives a period detector
                # much less to work with than a bow does, so the read alone
                # cannot tell those apart and the balance can
                ht = harmonics(xt, srt, f0)
                ho = harmonics(x, sr, f0)
                if ho[0] is None:
                    bad.append('  %-4s asked, %-4s heard (%+.1f st) — OUR RENDER HAS NOTHING AT THE FUNDAMENTAL'
                               % (name(m), name(got), got - m))
                    continue
                if ht[0] is None:
                    agreed.append('  %-4s the recording itself has nothing at the fundamental; ours reads %-4s (%+.1f st)'
                                  % (name(m), name(got), got - m))
                    continue
                dt = [(ho[k] - ho[0]) - (ht[k] - ht[0]) for k in range(1, 4)
                      if ho[k] is not None and ht[k] is not None]
                if not dt:
                    ear.append('  %-4s reads %-4s (%+.1f st); no harmonic above h1 measurable in both — for the ear'
                               % (name(m), name(got), got - m))
                elif abs(max(dt, key=abs)) > 6.0:
                    bad.append('  %-4s asked, %-4s heard (%+.1f st) — WE MOVED THE BALANCE: re h1 out by %s dB'
                               % (name(m), name(got), got - m, ', '.join('%+.1f' % v for v in dt)))
                else:
                    ear.append('  %-4s reads %-4s (%+.1f st) but the balance matches the recording (re h1 within %+.1f dB) — for the ear'
                               % (name(m), name(got), got - m, max(dt, key=abs)))
                continue
            bad.append('  %-4s asked, %-4s heard (%+.1f st, %.1f Hz) — no target on disk, graded on the ask'
                       % (name(m), name(got), got - m, f))
    tail = ''
    if ear: tail += ', %d faithful but read the octave' % len(ear)
    if agreed: tail += ', %d the recording reads the same way' % len(agreed)
    print('%s: %d semitones %s..%s, %d off%s' % (world, hi - lo + 1, NAMES[lo % 12] + str(lo // 12 - 1),
          NAMES[hi % 12] + str(hi // 12 - 1), len(bad), tail))
    for b in bad: print(b)
    for b in ear: print(b)
    for b in agreed: print(b)
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
