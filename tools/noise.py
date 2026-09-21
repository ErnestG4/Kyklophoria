#!/usr/bin/env python3
"""noise.py — the wash: what a dense body leaves after its modes and its burst.

    noise.py out/fit/perc [--bands 8]

A tam-tam is hundreds of modes; a bank of 48 with a burst fits a quarter of
its sustain and three times the modes buys five per cent (measured, the
findings). What is left is lines too dense to name, which is noise to the
ear, so it is modelled as noise: the residual after the modes and the burst,
the energy the target has and the model does not, per octave band
(62.5 Hz to 16 kHz, eight bands) over 100 ms frames from 0.1 s on, a line
through the log of each — a level at the strike and a T60 a band. The
deficit and not the residual, because a well-fitted partial with its phase
a little off leaves a residual as loud as itself. Sixteen numbers a note. The runtime plays white
noise through eight second-order band-passes under those envelopes, ~64
instructions a sample, scaled by the strike. Written into the record as
`noise L0 T0 L1 T1 ...`; bands the residual does not have (level under the
burst's floor or a T60 under 50 ms) are written as zero. Run after
bursts.py, on acoustic bodies: an electric world has no body, its deficit
is fit error, and a wash on the Wurlitzer measured worse (2.4 -> 3.0 dB).
Acoustic sets measured: guitar 4.5 -> 3.4 dB of sustain band error, viola
6.7 -> 5.9, mandolin 3.2 -> 2.8, banjo and violin slightly.
"""
import argparse
import math
import os
import sys

import numpy as np
import soundfile as sf

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import modalfit  # noqa: E402

EDGES = [62.5 * 2 ** k for k in range(9)]   # 62.5 .. 16000


def parse(line):
    w = line.split()
    return float(w[3]), float(w[5]), float(w[w.index('gains') + 1]), float(w[w.index('phase') + 1]) if 'phase' in w else 0.0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('recdir')
    ap.add_argument('--start', type=float, default=0.1, help='the residual is read from here, past the burst')
    a = ap.parse_args()
    rows = [l.split('\t') for l in open(os.path.join(a.recdir, 'fits.tsv')).read().splitlines()[1:]]
    done = 0
    for r in rows:
        rid = r[0]
        rp = os.path.join(a.recdir, rid + '.mmr')
        tp = os.path.join(a.recdir, rid + '-target.wav')
        if not (os.path.exists(rp) and os.path.exists(tp)):
            continue
        lines = [l for l in open(rp).read().splitlines() if not l.startswith('noise ')]
        modes = [parse(l) for l in lines if l.startswith('mode ')]
        bursts = [l.split() for l in lines if l.startswith('burst ')]
        x, sr = sf.read(tp)
        x = x / 0.5
        f = np.array([m[0] for m in modes]); z = np.array([m[1] for m in modes]); g = np.array([m[2] for m in modes]); ph = np.array([m[3] for m in modes])
        y = modalfit.resynth(f, z * 2 * math.pi * f, g, len(x), sr, ph) if len(f) else np.zeros_like(x)
        if bursts and len(bursts[0]) == 2:
            b, _ = sf.read(os.path.join(a.recdir, bursts[0][1]))
            y[:len(b)] += b[:len(y)]
        nfft, hop = 2048, int(0.1 * sr)
        frames = range(int(a.start * sr), len(x) - nfft, hop)
        if len(frames) < 4:
            continue
        # the deficit, not the residual: a well-fitted partial with its
        # phase a little off leaves a residual as loud as itself, and a wash
        # fitted to that put 13 dB of noise on a cowbell that had none. What
        # is missing is the target's band energy less the model's, where
        # that is positive; the wash fills only that
        St = np.array([np.abs(np.fft.rfft(x[s:s + nfft] * np.hanning(nfft))) ** 2 for s in frames])
        Sy = np.array([np.abs(np.fft.rfft(y[s:s + nfft] * np.hanning(nfft))) ** 2 for s in frames])
        fr = np.fft.rfftfreq(nfft, 1.0 / sr)
        t = np.array([s / sr for s in frames])
        # the note's own fall, broadband, fitted the same way: a wash under
        # a note cannot ring longer than the note. It did — a guitar's
        # 2-4 kHz band at 25 s, a piano bass note's at 22 s — because a
        # band that has reached the sampler's floor is a flat line, and a
        # flat line's slope is a T60 of forever; through the runtime that is
        # a hiss that never stops, and Combust heard it as the chain on a
        # snare drum on some notes of every acoustic set
        ta = St.sum(axis=1) / (3.0 * nfft * nfft / 16)
        la = np.log(ta + 1e-12)
        oka = (ta > 2.0 * ta[-3:].mean()) & (ta > ta.max() * 1e-4)
        note_t60 = 60.0
        if oka.sum() >= 4:
            pa = np.polyfit(t[oka], la[oka], 1)
            note_t60 = 6.91 / max(-pa[0] / 2, 1e-3) if pa[0] < 0 else 60.0
        # and no longer than the partials the fit found in that band: what
        # the wash fills is the body and the strings the bank did not take,
        # and those do not outlast the ones it did. Above the highest mode,
        # the highest mode's; the note's own where the fit found nothing
        mode_t60 = 6.91 / (z * 2 * math.pi * f) if len(f) else np.array([])
        band_cap = []
        for k in range(8):
            inb = (f >= EDGES[k]) & (f < EDGES[k + 1])
            if inb.any():
                band_cap.append(float(mode_t60[inb].max()))
            elif len(f) and EDGES[k] >= f.max():
                band_cap.append(float(mode_t60[np.argmax(f)]))
            else:
                band_cap.append(note_t60)
            # and a prior: nothing noise-like in a body rings above 2 kHz
            # for more than two seconds, nor below it for more than eight
            band_cap[k] = min(band_cap[k], 2.0 if k >= 5 else 8.0)
        # what the deficit is made of. A wash is noise; on a pitched
        # instrument the deficit above the highest fitted partial is the
        # partials the bank's budget did not reach, and played as noise they
        # are a hiss under the note — a piano's E1 (48 modes reach 1.7 kHz)
        # came out with the chain of a snare drum under it. So a note
        # world's wash stops at its highest partial; what is above it needs
        # modes, and the burst carries its attack. An index world — a row of
        # bodies — is washed wherever the deficit is, which is what the
        # wash was made for. Telling harmonics from noise by their spacing
        # was tried and does not survive a piano: three strings a note,
        # detuned, put every line in a doublet and no lag lines up
        pitched = r[2] != 'index' if len(r) > 2 else True
        f_top = float(f.max()) if len(f) else 0.0
        # and no longer than the partials the fit found in that band: what
        # the wash fills is the body and the strings the bank did not take,
        # and those do not outlast the ones it did. Above the highest mode,
        # the highest mode's; the note's own where the fit found nothing
        mode_t60 = 6.91 / (z * 2 * math.pi * f) if len(f) else np.array([])
        band_cap = []
        for k in range(8):
            inb = (f >= EDGES[k]) & (f < EDGES[k + 1])
            if inb.any():
                band_cap.append(float(mode_t60[inb].max()))
            elif len(f) and EDGES[k] >= f.max():
                band_cap.append(float(mode_t60[np.argmax(f)]))
            else:
                band_cap.append(note_t60)
            # and a prior: nothing noise-like in a body rings above 2 kHz
            # for more than two seconds, nor below it for more than eight
            band_cap[k] = min(band_cap[k], 2.0 if k >= 5 else 8.0)
        # what the deficit is made of. A wash is noise; a deficit that is
        # the note's harmonics is partials the bank's budget did not reach,
        # and played as noise they are a hiss under the note — a piano's E1
        # came out with the chain of a snare drum under it. A band's deficit
        # is taken on a long window (16384 points, 3 Hz bins, which
        # resolves a bass note's harmonics where the fit's 2048 cannot),
        # averaged over the frames, and correlated with itself one
        # fundamental along: a harmonic series lines up with itself at that
        # lag, a gong's inharmonic modes and a body's noise do not. Over
        # 0.5 the band is harmonics and gets no wash; what it needs is
        # modes, and the burst carries its attack. Peakiness was tried
        # first and threw the tam-tam's wash out with the piano's, since
        # dense inharmonic lines are as peaky as harmonics
        nl = 16384
        lf = [s0 for s0 in range(int(a.start * sr), len(x) - nl, hop)]
        harmonic = [0.0] * 8
        f0 = float(f.min()) if len(f) else 0.0
        if len(lf) >= 2 and f0 > 20.0:
            Dl = np.zeros(nl // 2 + 1)
            wl = np.hanning(nl)
            for s0 in lf:
                Dl += np.maximum(np.abs(np.fft.rfft(x[s0:s0 + nl] * wl)) ** 2 - np.abs(np.fft.rfft(y[s0:s0 + nl] * wl)) ** 2, 0.0)
            frl = np.fft.rfftfreq(nl, 1.0 / sr)
            fs_ = np.sort(f)
            for k in range(8):
                # the spacing to look for is the fitted partials' own near
                # the band, not f0: a piano's stretch has its partials at
                # 2-4 kHz half again as far apart as its fundamental, and a
                # lag of f0 there finds nothing and lets the hiss through
                near = fs_[fs_ <= EDGES[k + 1]][-6:]
                sp = float(np.median(np.diff(near))) if len(near) >= 3 else f0
                L = int(round(sp * nl / sr))
                v = Dl[(frl >= EDGES[k]) & (frl < EDGES[k + 1])]
                v = v - v.mean()
                # any consistent spacing from that up to twice it counts —
                # the stretch above the fitted partials is not known, but a
                # series at any spacing lines up with itself and noise does
                # not at any
                if len(v) > 6 * L and L >= 2 and (v ** 2).sum() > 0:
                    harmonic[k] = max(float((v[:-l] * v[l:]).sum() / (v ** 2).sum()) for l in range(max(1, int(0.8 * L)), 2 * L + 1))
        out = []
        for k in range(8):
            sel = (fr >= EDGES[k]) & (fr < EDGES[k + 1])
            # band power per sample: Parseval through a Hann window is
            # 3/8 N^2 over all bins, half of that one-sided. It was N^2 / 8,
            # "roughly", which read 1.8 dB high
            tb = St[:, sel].sum(axis=1) / (3.0 * nfft * nfft / 16)
            yb = Sy[:, sel].sum(axis=1) / (3.0 * nfft * nfft / 16)
            eb = np.maximum(tb - yb, 0.0)
            le = np.log(eb + 1e-12)
            # the fall is fitted only where there is a fall to fit: above the
            # recording's own floor (its last frames) and within 40 dB of the
            # band's peak. A line through the floor read a 1 s T60 as 2.7 s
            # and its intercept 25 dB low; a line through a hand-damped
            # cymbal's cliff put the intercept 9 dB above the recording
            floor = 2.0 * tb[-3:].mean()
            ok = (le > np.log(1e-12) + 2) & (eb > floor) & (eb > eb.max() * 1e-4)
            if ok.sum() < 4:
                out += [0.0, 0.0]; continue
            p = np.polyfit(t[ok], le[ok], 1)
            t60 = 6.91 / max(-p[0] / 2, 1e-3) / 1.0 if p[0] < 0 else 60.0   # energy falls at 2r
            level = math.exp(p[1] / 2)                            # amplitude at t = 0
            # a band the residual holds no more of than the target's own tail is nothing
            # a band the model already carries within 3 dB, or that the
            # target holds no more of than its own tail, is nothing
            # and a band whose deficit neither falls 10 dB over the frames it
            # was fitted on nor falls with the note is floor, not a wash (a
            # tam-tam's band falls 6 dB in its six seconds, like the tam-tam)
            fall = le[ok][0] - le[ok][-1] if ok.sum() >= 2 else 0.0
            floorish = fall < math.log(10.0) and t60 > 1.5 * note_t60
            if t60 < 0.05 or level <= 0 or floorish or (pitched and EDGES[k] >= f_top) or eb[:3].mean() < 0.5 * tb[:3].mean() or eb[:3].mean() < 2 * tb[-3:].mean():
                out += [0.0, 0.0]
            else:
                out += [level, min(t60, note_t60, band_cap[k], 60.0)]
        lines.append('noise ' + ' '.join('%.6g %.4g' % (out[2 * k], out[2 * k + 1]) for k in range(8)))
        open(rp, 'w').write('\n'.join(lines) + '\n')
        done += 1
        live = sum(1 for k in range(8) if out[2 * k] > 0)
        print('  %-18s %d bands live: %s' % (rid, live, ' '.join('%.0fHz L%.2g T%.1fs' % (math.sqrt(EDGES[k] * EDGES[k + 1]), out[2 * k], out[2 * k + 1]) for k in range(8) if out[2 * k] > 0)))
    print('  %s: %d records with a noise layer' % (a.recdir, done))


if __name__ == '__main__':
    main()
