#!/usr/bin/env python3
"""threeway.py — today's recorded attack, the synthesised exciter, and the
exciter with the waveguide, rendered and measured note by note against the
note's own recording; and a four-voice passage at four notes a second.

    ~/fmexplorer/bin/python proto/threeway.py [--sets piano-vcsl,wurli] [--workers 6] [--refit]

Writes, all git-ignored:
    bench/threeway/<set>_<Note>_{rec,a-recorded,b-synth,c-synth+waveguide}.wav
    bench/threeway/<set>_passage_{a-recorded,b-synth,c-synth+waveguide}.wav
    bench/threeway/params/<set>_<param>.json     the fitted numbers a point
    bench/threeway/results.json                   every measurement
and prints the tables proto/README.md quotes.

The WAVs are at the engine's own gain (0.23) and float; the recording is at
the level today's burst plays it (the world's scale times the same gain),
lined up on the strike, so each A/B is level-matched as the module plays.
Every render here is velocity 1.0 on a single note — the swing the fit
saw, which is where (b) and (c) are fitted and where (a)'s burst is the
loudest take — and 0.8 in the passage, as the overlap harness plays.
CPU only; a worker is one process with its own copy of the library.
"""
import os
for _v in ('OMP_NUM_THREADS', 'OPENBLAS_NUM_THREADS', 'MKL_NUM_THREADS'):
    os.environ.setdefault(_v, '1')       # a worker is one core: six workers, not six times four threads
import argparse
import json
import math
import subprocess
import sys
from concurrent.futures import ProcessPoolExecutor

import numpy as np
import soundfile as sf

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import kp  # noqa: E402

ROOT = kp.ROOT
OUT = os.path.join(ROOT, 'bench', 'threeway')
CARD = os.path.join(ROOT, 'out', 'card', 'kyklophoria')
FIT = os.path.join(ROOT, 'out', 'fit')
GAIN = 0.23          # the engine's (Engine::gain x PhaseTrim, a resonate world)
SR = kp.SR

NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B']


def nn(m):
    m = int(round(m))
    return '%s%d' % (NAMES[m % 12], m // 12 - 1)


# set -> world, member per note, fitted sets, class, notes, the passage's middle
SETS = {
    'piano-vcsl': dict(world='piano-vcsl.kykm', fits=['piano-vcsl'], cls='piano',
                       notes=[34, 48, 60, 72, 84, 96], mid=60),
    'piano-iowa': dict(world='piano-iowa.kykm', fits=['piano-iowa'], cls='piano',
                       notes=[33, 48, 60, 72, 84, 96], mid=60),
    # the neck's strings (guitar-strings is built from guitar2-sul*): the low
    # E string's lowest note is F2, G3 is the open G string, E4 the open high
    # e, B4 the high e at the seventh fret
    'guitar2': dict(world='guitar-strings.kykm', fits=['guitar2-sule', 'guitar2-sula', 'guitar2-suld', 'guitar2-sulg', 'guitar2-sulb', 'guitar2-sul_e'],
                    cls='guitar', notes=[(41, 0), (55, 3), (64, 5), (71, 5)], mid=(64, 3)),
    'wurli': dict(world='wurli.kykm', fits=['wurli'], cls='wurli', notes=[36, 48, 60, 72], mid=60),
}


def members(cfg):
    """[(note, member)] for the notes, and the passage's member"""
    ns = [(n, 0) if not isinstance(n, tuple) else n for n in cfg['notes']]
    mid = cfg['mid'] if isinstance(cfg['mid'], tuple) else (cfg['mid'], 0)
    return ns, mid


def passage_notes(mid, seed=7):
    """overlap.cpp's rand4hz: 28 strikes 0.25 s apart from 0.1 s, a random
    walk over two octaves around mid, never the same note twice running"""
    r = seed; last = 0; t, n = [], []
    for i in range(28):
        while True:
            r = (r * 1664525 + 1013904223) & 0xFFFFFFFF
            v = int((r >> 16) % 25) - 12
            if v != last:
                break
        last = v
        t.append(0.1 + 0.25 * i); n.append(mid + v)
    return t, n


# ── one point's fit (a worker) ─────────────────────────────────────────
def fit_job(args):
    setname, member, i, refit = args
    import fit
    cfg = SETS[setname]
    pj = os.path.join(OUT, 'params', '%s_m%d_p%d.json' % (setname, member, i))
    if os.path.exists(pj) and not refit:
        return pj
    W = kp.World(os.path.join(CARD, cfg['world']), member)
    note = W.params[i]
    idx, rec, k = kp.note_record(W, note, [os.path.join(FIT, f) for f in cfg['fits']])
    x, g, lag = kp.recording_aligned(W, idx, rec, k)
    pb, pc, info = fit.fit_point(W, idx, x, cfg['cls'])
    info.update(rec=rec['id'], k_cents=1200 * math.log2(k), scale=g, lag=lag, member=member, point=i)
    json.dump(dict(pb=pb.tolist(), pc=pc.tolist(), info=info), open(pj, 'w'), default=float, indent=1)
    return pj


def load_params(W, setname, member, only=None):
    """every point this set has params for, into the library"""
    got = {}
    for i in range(W.P):
        if only is not None and i not in only:
            continue
        pj = os.path.join(OUT, 'params', '%s_m%d_p%d.json' % (setname, member, i))
        if os.path.exists(pj):
            d = json.load(open(pj))
            got[i] = d
    return got


# ── one note's renders and numbers ─────────────────────────────────────
def note_job(args):
    setname, note, member = args
    import measure as ms
    cfg = SETS[setname]
    W = kp.World(os.path.join(CARD, cfg['world']), member)
    i, rec, k = kp.note_record(W, note, [os.path.join(FIT, f) for f in cfg['fits']])
    d = json.load(open(os.path.join(OUT, 'params', '%s_m%d_p%d.json' % (setname, member, i))))
    pb, pc, info = np.array(d['pb'], np.float32), np.array(d['pc'], np.float32), d['info']
    x, g, lag = kp.recording_aligned(W, i, rec, k)
    dur = min(len(x), int(6.0 * SR))
    x = x[:dur]
    pnote = W.params[i]
    pt = W.parsed['points'][i]
    sw, sc, ln, fade = pt['bursts'][-1] if pt['bursts'] else (1, 1, 0, 0)
    hz, z, gg = W.modes(pnote)
    ftop = float(hz[gg > gg.max() * 1e-3].max())
    f0 = 440.0 * 2 ** ((pnote - 69) / 12)
    tag = '%s_%s' % (setname, nn(pnote))
    res = dict(set=setname, note=pnote, name=nn(pnote), member=member, rec=rec['id'], burst_ms=ln / 48.0, fade_ms=fade / 48.0,
               ftop=ftop, info=info, variants={})
    sf.write(os.path.join(OUT, tag + '_rec.wav'), (GAIN * x).astype(np.float32), SR, subtype='FLOAT')
    for v, name in ((0, 'a-recorded'), (1, 'b-synth'), (2, 'c-synth+waveguide')):
        W.set(i, pb if v < 2 else pc)
        y, m, nz, wg = W.render(pnote, 1.0, v, dur, parts=True)
        sf.write(os.path.join(OUT, '%s_%s.wav' % (tag, name)), (GAIN * y).astype(np.float32), SR, subtype='FLOAT')
        em, per_band = ms.early_match(y, x)
        r = dict(early=em, early_bands=per_band, env=ms.env_diff(y, x),
                 seam=ms.seam(y, x, (ln - fade) / SR, ln / SR, ftop),
                 high=ms.high_band(y, x, ftop, ((0.0, 0.05), (0.05, 0.3), (0.3, 1.0), (1.0, 2.0))),
                 windows=ms.windows(y, x, f0), peak=float(np.abs(GAIN * y).max()))
        if v == 2:
            r['double'] = ms.doubling(m, wg, hz)
            r['wg_rms_db'] = float(10 * math.log10(np.mean(wg ** 2) + 1e-30) - 10 * math.log10(np.mean(m ** 2) + 1e-30))
        if v >= 1:
            r['noise_rms_db'] = float(10 * math.log10(np.mean(nz ** 2) + 1e-30) - 10 * math.log10(np.mean(m ** 2) + 1e-30))
        res['variants'][name[0]] = r
    return res


# ── the passage ────────────────────────────────────────────────────────
def passage_job(args):
    setname, = args
    cfg = SETS[setname]
    _, (mid, member) = members(cfg)
    W = kp.World(os.path.join(CARD, cfg['world']), member)
    got = load_params(W, setname, member)
    t, notes = passage_notes(mid)
    need = sorted(set(W.near(n) for n in notes))
    missing = [i for i in need if i not in got]
    out = dict(set=setname, mid=mid, member=member, notes=notes, points=need, missing=missing)
    n = int(8.0 * SR)
    vel = [0.8] * len(t)
    renders = {}
    for v, name in ((0, 'a-recorded'), (1, 'b-synth'), (2, 'c-synth+waveguide')):
        for i in need:
            if i in got:
                W.set(i, np.array(got[i]['pb' if v < 2 else 'pc'], np.float32))
        y = W.passage(t, notes, vel, v, 4, n)
        renders[name] = y
        sf.write(os.path.join(OUT, '%s_passage_%s.wav' % (setname, name)), (GAIN * y).astype(np.float32), SR, subtype='FLOAT')
    out['pops'] = {k: pops(y, t) for k, y in renders.items()}
    out['peak'] = {k: float(np.abs(GAIN * y).max()) for k, y in renders.items()}
    # the real engine for (a), through kykdesk: the harness is the engine
    out['kykdesk_a'] = kykdesk_check(os.path.join(CARD, cfg['world']), t, notes, member, W, renders['a-recorded'])
    return out


def pops(y, times, sr=SR):
    """popcheck's measure: per strike, the largest second difference in the
    3 ms after it over the median second difference of the 20 ms before
    (dB); strikes land on the first 24-sample block at or after their time"""
    d2 = np.abs(np.diff(y, 2))
    r = []
    for t in times:
        i = int(math.ceil(t * sr / 24.0)) * 24
        pre = d2[max(0, i - int(0.02 * sr)):i]; post = d2[i:i + int(0.003 * sr)]
        if len(pre) < 10 or len(post) < 5:
            continue
        if np.sqrt(np.mean(y[max(0, i - int(0.02 * sr)):i] ** 2)) < 1e-4:
            continue                 # a strike out of silence has no "before" to compare with
        r.append(20 * math.log10((post.max() + 1e-12) / (np.median(pre) + 1e-9)))
    r = np.array(r)
    return dict(n=len(r), over30=int((r > 30).sum()), worst=float(r.max()), median=float(np.median(r)))


def kykdesk_check(world, t, notes, member, W, ours):
    """the same passage through Kyklophoria's own engine (kykdesk): how far
    this harness's (a) is from the module's"""
    exe = os.path.normpath(os.path.join(ROOT, '..', 'Kyklophoria', 'build', 'host', 'kykdesk'))
    if not os.path.exists(exe):
        return None
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        sp = os.path.join(tmp, 's.txt'); op = os.path.join(tmp, 'o.wav')
        lines = ['0 poly 4']
        if W.mname is not None:
            M = len(kp.sa.parse_world(open(world, 'rb').read())['members'])
            lines.append('0 pos %.6f 0.5 0.5 0.5' % (member / max(1, M - 1)))
        for tt, no in zip(t, notes):
            lines.append('%.6f f0 %.6f' % (tt, 440.0 * 2 ** ((no - 69) / 12)))
            lines.append('%.6f strike 0.8' % tt)
        lines.append('8.0 dur')
        open(sp, 'w').write('\n'.join(lines) + '\n')
        r = subprocess.run([exe, '--resonate', world, '--script', sp, '--out', op], capture_output=True, text=True)
        if r.returncode:
            return dict(error=r.stderr[-200:])
        y, _ = sf.read(op)
    n = min(len(y), len(ours))
    a = ours[:n] * GAIN
    # best lag within a block
    best = None
    for lag in range(-48, 49):
        seg = y[max(0, lag):n + min(0, lag)]; ref = a[max(0, -lag):n - max(0, lag)]
        e = float(np.sqrt(np.mean((seg - ref) ** 2)) / (np.sqrt(np.mean(seg ** 2)) + 1e-20))
        if best is None or e < best[0]:
            best = (e, lag)
    return dict(rel_err=best[0], lag=best[1])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--sets', default=','.join(SETS))
    ap.add_argument('--workers', type=int, default=6)
    ap.add_argument('--refit', action='store_true')
    ap.add_argument('--no-passage', action='store_true')
    a = ap.parse_args()
    os.makedirs(os.path.join(OUT, 'params'), exist_ok=True)
    sets = [s for s in a.sets.split(',') if s]
    # the points to fit: every requested note's, and every point the passage plays
    jobs = []
    for s in sets:
        cfg = SETS[s]
        ns, (mid, pm) = members(cfg)
        byw = {}
        for note, m in ns:
            W = byw.setdefault(m, kp.World(os.path.join(CARD, cfg['world']), m))
            jobs.append((s, m, W.near(note), a.refit))
        if not a.no_passage:
            W = byw.setdefault(pm, kp.World(os.path.join(CARD, cfg['world']), pm))
            t, notes = passage_notes(mid)
            for i in sorted(set(W.near(n) for n in notes)):
                jobs.append((s, pm, i, a.refit))
    jobs = sorted(set(jobs))
    print('fitting %d points on %d workers' % (len(jobs), a.workers), flush=True)
    with ProcessPoolExecutor(a.workers) as ex:
        for j, _ in zip(jobs, ex.map(fit_job, jobs)):
            pass
    print('rendering', flush=True)
    njobs = [(s, n, m) for s in sets for n, m in members(SETS[s])[0]]
    with ProcessPoolExecutor(a.workers) as ex:
        notes = list(ex.map(note_job, njobs))
        passages = [] if a.no_passage else list(ex.map(passage_job, [(s,) for s in sets]))
    rp = os.path.join(OUT, 'results.json')
    old = json.load(open(rp)) if os.path.exists(rp) else dict(notes=[], passages=[])
    keep_n = [r for r in old.get('notes', []) if r['set'] not in sets]
    keep_p = [r for r in old.get('passages', []) if r['set'] not in sets]
    json.dump(dict(notes=keep_n + notes, passages=keep_p + passages), open(rp, 'w'), indent=1, default=float)
    import report
    report.show(json.load(open(rp)))


if __name__ == '__main__':
    main()
