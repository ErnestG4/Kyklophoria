#!/usr/bin/env python3
"""modalfit — a recording of a struck thing, fitted as a modal set on the GPU.

    modalfit.py in.wav out.mmr [--resynth out.wav] [--modes 48] [--seconds 2.0]
                [--steps 600] [--onset -30] [--device cuda]

The FEM supplies parametric freedom; a recording supplies the truth. This is
the truth half: take a strike, find its partials, and fit (frequency, decay,
amplitude, phase) for every mode by gradient so that the resynthesis matches
the recording. The result is a corpus record — the same .mmr as modalfem
writes — with a measured decay per mode instead of a Rayleigh model's, which
is where a Rhodes stops being plausible and starts being one.

How it fits:

  1. The onset is the first sample above --onset dBFS. Everything before it is
     discarded; --seconds after it are the target.
  2. Initialisation is classical modal analysis: an STFT, the mean magnitude
     over the ring, its highest peaks as candidate modes; for each, the
     frequency from the phase advance of its bin between hops (the phase
     vocoder's estimate, good to a fraction of a hertz where a bin is twelve)
     and the decay rate from a linear fit of log magnitude against time.
  3. Refinement is gradient descent on a differentiable renderer — a sum of
     decaying sines — against a multi-resolution log-magnitude STFT loss (three
     window sizes), which does not care about phase and does care about
     frequency and decay at every scale. Adam, --steps steps, on the GPU if
     there is one; a fit is a few seconds. Frequencies move at a fortieth of
     the rate of everything else: the loss in frequency is a comb, and a step
     of a bin lands in the wrong tooth. The first version stepped 2% at a time
     and oscillated at 100 cents; the phase-vocoder start plus a slow
     frequency is what recovers a synthetic strike to under a cent.
  4. The record: hz, zeta = rate / omega, and the fitted amplitude as the gain
     at every one of P positions — a recording is one position, and the record
     says so with `fitted 1` in its header. A corpus that mixes fitted and FEM
     models should treat the fitted ones' gain patterns as flat, which is what
     they are.

Checked on a render whose modes are known (tools/modalfit_check.py): the
four modes above -20 dB come back within a cent and within 2% in T60; the two
at -26 dB that ring for 80 ms are dropped rather than misfitted.

Needs torch, numpy, soundfile: the fmexplorer venv on this machine.
"""
import argparse
import math
import sys

import numpy as np
import soundfile as sf
import torch


def load(path, seconds, onset_db):
    x, sr = sf.read(path, always_2d=True)
    x = x.mean(axis=1).astype(np.float64)
    thr = 10 ** (onset_db / 20)
    idx = np.argmax(np.abs(x) > thr)
    if not (np.abs(x) > thr).any():
        sys.exit('no onset above %g dBFS' % onset_db)
    x = x[idx: idx + int(seconds * sr)]
    x = x / (np.max(np.abs(x)) or 1.0)
    return x, sr


def initialise(x, sr, nmodes, nfft=8192, hop=512):
    """Peaks of the mean spectrum, and a decay per peak from its bin's track."""
    win = np.hanning(nfft)
    frames = []
    for s in range(0, len(x) - nfft, hop):
        frames.append(np.fft.rfft(x[s:s + nfft] * win))
    C = np.array(frames)                       # complex, frames x bins
    S = np.abs(C)
    mean = S.mean(axis=0)
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    # peaks: local maxima above 40 Hz, ranked by height
    cand = []
    for b in range(2, len(mean) - 2):
        if freqs[b] < 40 or freqs[b] > 20000:
            continue
        if mean[b] > mean[b - 1] and mean[b] >= mean[b + 1] and mean[b] > mean[b - 2] and mean[b] >= mean[b + 2]:
            cand.append((mean[b], b))
    cand.sort(reverse=True)
    modes = []
    t = np.arange(len(S)) * hop / sr
    for h, b in cand[:nmodes]:
        # the phase advance between hops, unwrapped about the bin's own
        # frequency, over the frames where the partial is well above the floor
        track = np.log(S[:, b] + 1e-9)
        ok = track > track.max() - 6.0
        dphi = np.angle(C[1:, b] * np.conj(C[:-1, b]))
        expect = 2 * math.pi * b * hop / nfft
        dev = np.angle(np.exp(1j * (dphi - expect)))
        good = ok[1:] & ok[:-1]
        f = (expect + (np.median(dev[good]) if good.any() else 0.0)) * sr / (2 * math.pi * hop)
        # fit over the frames where the track is above the floor
        if ok.sum() >= 3:
            p = np.polyfit(t[ok], track[ok], 1)
            rate = max(0.5, -p[0])
        else:
            rate = 5.0
        amp = S[0, b] / (nfft / 4)
        modes.append((f, rate, amp))
    return modes


def stft_mag(y, n, hop):
    win = torch.hann_window(n, device=y.device)
    return torch.stft(y, n, hop, window=win, return_complex=True).abs()


def fit(x, sr, init, steps, device):
    t = torch.arange(len(x), device=device, dtype=torch.float32) / sr
    target = torch.tensor(x, device=device, dtype=torch.float32)
    f0 = torch.tensor([m[0] for m in init], device=device)
    r0 = torch.tensor([m[1] for m in init], device=device)
    a0 = torch.tensor([m[2] for m in init], device=device)
    logf = torch.log(f0).clone().requires_grad_(True)
    logr = torch.log(r0).clone().requires_grad_(True)
    loga = torch.log(a0 + 1e-6).clone().requires_grad_(True)
    phase = torch.zeros(len(init), device=device, requires_grad=True)
    opt = torch.optim.Adam([{'params': [logr, loga, phase], 'lr': 0.02},
                            {'params': [logf], 'lr': 0.0005}])
    scales = [(4096, 1024), (1024, 256), (256, 64)]
    tmag = [stft_mag(target, n, h) for n, h in scales]
    tlog = [torch.log(m + 1e-4) for m in tmag]
    tnorm = [m.norm() for m in tmag]

    def render():
        f, r, a = torch.exp(logf), torch.exp(logr), torch.exp(loga)
        env = torch.exp(-r[:, None] * t[None, :])
        return (a[:, None] * env * torch.sin(2 * math.pi * f[:, None] * t[None, :] + phase[:, None])).sum(0)

    for step in range(steps):
        opt.zero_grad()
        y = render()
        # two terms a scale: spectral convergence, which is relative and so is
        # dominated by the loud partials, and log-magnitude L1, which is what
        # hears the decay tails. Log L1 alone let the two loudest modes ring
        # four times too long — two loud bins out of two thousand hardly move
        # a mean over the floor.
        loss = 0.0
        for (n, h), tm, tl, tn in zip(scales, tmag, tlog, tnorm):
            ym = stft_mag(y, n, h)
            loss = loss + (ym - tm).norm() / tn + (torch.log(ym + 1e-4) - tl).abs().mean()
        loss.backward()
        opt.step()
        if step % 100 == 0 or step == steps - 1:
            print('  step %4d  loss %.4f' % (step, loss.item()))
    with torch.no_grad():
        y = render()
        f, r, a = torch.exp(logf), torch.exp(logr), torch.exp(loga)
    return f.cpu().numpy(), r.cpu().numpy(), a.cpu().numpy(), y.cpu().numpy(), loss.item()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wav')
    ap.add_argument('out')
    ap.add_argument('--resynth', help='write the resynthesis here')
    ap.add_argument('--target', help='write the analysed excerpt here, at the same level, for an A/B')
    ap.add_argument('--max-t60', type=float, default=0.0,
                    help='cap a fitted T60 at this many seconds (0: three times the analysed length). A decay longer '
                         'than the window was not measured; on recordings it is room rumble and mp3 floor')
    ap.add_argument('--modes', type=int, default=48)
    ap.add_argument('--seconds', type=float, default=2.0)
    ap.add_argument('--steps', type=int, default=600)
    ap.add_argument('--onset', type=float, default=-30.0)
    ap.add_argument('--positions', type=int, default=12)
    ap.add_argument('--floor', type=float, default=-60.0, help='drop fitted modes this far below the loudest, dB')
    ap.add_argument('--device', default='cuda' if torch.cuda.is_available() else 'cpu')
    a = ap.parse_args()
    x, sr = load(a.wav, a.seconds, a.onset)
    init = initialise(x, sr, a.modes)
    print('  %d candidate modes from the spectrum, %.1f .. %.1f Hz' % (len(init), min(m[0] for m in init), max(m[0] for m in init)))
    f, r, amp, y, loss = fit(x, sr, init, a.steps, a.device)
    # a mode the fit has turned down to nothing is a mode it could not place,
    # not a quiet one: keep it out of the record rather than in with a decay
    # nobody measured
    cap = a.max_t60 if a.max_t60 > 0 else 3.0 * a.seconds
    r = np.maximum(r, 6.91 / cap)
    keep = amp > amp.max() * 10 ** (a.floor / 20)
    dropped = int((~keep).sum())
    f, r, amp = f[keep], r[keep], amp[keep]
    order = np.argsort(f)
    with open(a.out, 'w') as o:
        o.write('# modalfit record: a strike fitted as decaying sines on %s. zeta is measured,\n' % a.device)
        o.write('# gains are the fitted amplitude at the one position the recording is, repeated.\n')
        o.write('source %s\nfitted 1\nloss %.5f\n' % (a.wav, loss))
        o.write('positions %d\n' % a.positions)
        o.write('modes %d\n' % len(order))
        for k, i in enumerate(order):
            w = 2 * math.pi * f[i]
            o.write('mode %d hz %.6f zeta %.9g gains %s\n' % (k, f[i], r[i] / w, ' '.join('%.9g' % amp[i] for _ in range(a.positions))))
    if a.resynth:
        sf.write(a.resynth, np.clip(y / (np.max(np.abs(y)) or 1.0) * 0.5, -1, 1), sr)
    if a.target:
        sf.write(a.target, np.clip(x * 0.5, -1, 1), sr)
    print('  fitted %d modes (%d dropped below %.0f dB), final loss %.4f -> %s' % (len(order), dropped, a.floor, loss, a.out))
    for i in order[:8]:
        print('    %8.1f Hz  T60 %.2fs  amp %.3g' % (f[i], 6.91 / r[i], amp[i]))


if __name__ == '__main__':
    main()
