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
    # the onset is found from the strike, not from the start: the envelope's
    # peak, then back to the last 5 ms that sat 20 dB below it, less 10 ms for
    # the ramp. A struck or plucked thing goes from nothing to its peak inside
    # that; a file that opens with a hand on the strings at 22 dB below the
    # note (the Philharmonia guitar's A3, for a second and a half) would
    # trigger any threshold taken from the front, and did — the fit started
    # in the noise and starved. onset_db is the floor a file must reach at all
    hop = max(1, int(0.005 * sr))
    env = np.sqrt(np.array([np.mean(x[i:i + hop] ** 2) for i in range(0, len(x) - hop + 1, hop)]))
    if not len(env) or env.max() < 10 ** (onset_db / 20):
        raise ValueError('no onset above %g dBFS' % onset_db)
    pk = int(env.argmax())
    quiet = np.where(env[:pk] < env[pk] * 10 ** (-20 / 20))[0]
    idx = max(0, (int(quiet[-1]) - 1) * hop) if len(quiet) else 0
    x = x[idx: idx + int(seconds * sr)]
    x = x / (np.max(np.abs(x)) or 1.0)
    return x, sr


def initialise(x, sr, nmodes, nfft=8192, hop=512, report=None):
    """Peaks of the mean spectrum, verified, with a decay per peak from its
    bin's track.

    Verification is what the first version lacked, and what let a whistle into
    the guitars: a peak of the *mean* spectrum can be a partial, or it can be a
    noise ridge, an mp3 artefact, a room resonance or mains hum, and every one
    of those fits perfectly well as a sinusoid that never decays. Three tests
    from the sinusoidal-modelling literature, each a number a reader can check:

      prominence   the peak stands at least 8 dB above the median of the two
                   octaves' worth of bins around it, in the mean spectrum —
                   a ridge in the noise does not
      coherence    the phase advance of its bin between hops is steady: the
                   deviation from the bin's own frequency has a standard
                   deviation under a third of a radian over the frames where
                   the partial is above its floor — noise wanders, a sinusoid
                   does not
      decay        its log magnitude over time is a line going down: the fit's
                   r^2 is at least 0.5 and the slope negative — or, for a
                   partial that beats and so is no line, its first quarter
                   stands 3 dB above its last — a partial dies, hum and a
                   room mode do not

    A candidate that fails any test is reported, not fitted."""
    win = np.hanning(nfft)
    frames = []
    for s in range(0, len(x) - nfft, hop):
        frames.append(np.fft.rfft(x[s:s + nfft] * win))
    C = np.array(frames)                       # complex, frames x bins
    S = np.abs(C)
    mean = S.mean(axis=0)
    logmean = np.log(mean + 1e-12)
    freqs = np.fft.rfftfreq(nfft, 1.0 / sr)
    cand = []
    for b in range(2, len(mean) - 2):
        if freqs[b] < 40 or freqs[b] > 20000:
            continue
        if mean[b] > mean[b - 1] and mean[b] >= mean[b + 1] and mean[b] > mean[b - 2] and mean[b] >= mean[b + 2]:
            cand.append((mean[b], b))
    cand.sort(reverse=True)
    modes = []
    rejected = {'prominence': 0, 'coherence': 0, 'decay': 0}
    t = np.arange(len(S)) * hop / sr
    for h, b in cand:
        if len(modes) >= nmodes:
            break
        # prominence over the local floor: the median over an octave each side
        lo, hi = max(1, b // 2), min(len(mean) - 1, b * 2)
        floor = np.median(logmean[lo:hi])
        if logmean[b] - floor < 8.0 / 8.686:          # 8 dB, in nepers
            rejected['prominence'] += 1
            continue
        track = np.log(S[:, b] + 1e-9)
        ok = track > track.max() - 6.0
        dphi = np.angle(C[1:, b] * np.conj(C[:-1, b]))
        expect = 2 * math.pi * b * hop / nfft
        dev = np.angle(np.exp(1j * (dphi - expect)))
        good = ok[1:] & ok[:-1]
        if good.sum() >= 2 and np.std(dev[good]) > 0.33:
            rejected['coherence'] += 1
            continue
        f = (expect + (np.median(dev[good]) if good.any() else 0.0)) * sr / (2 * math.pi * hop)
        if ok.sum() >= 3:
            p = np.polyfit(t[ok], track[ok], 1)
            pred = np.polyval(p, t[ok])
            ss = np.sum((track[ok] - track[ok].mean()) ** 2)
            r2 = 1.0 - np.sum((track[ok] - pred) ** 2) / ss if ss > 0 else 0.0
            # a partial that beats — a string's two polarisations — is not
            # a line, but it still falls: the first quarter of its track
            # against the last, 3 dB, is a drop that hum and a room never make
            q = max(1, ok.sum() // 4)
            drop = track[ok][:q].mean() - track[ok][-q:].mean()
            if p[0] > 0 or (r2 < 0.5 and ok.sum() > 6 and drop < 3.0 / 8.686):
                rejected['decay'] += 1
                continue
            rate = max(0.5, -p[0])
        else:
            rate = 5.0
        amp = S[0, b] / (nfft / 4)
        modes.append((f, rate, amp))
    if report is not None:
        report.update(rejected)
    return modes


def excess_db(y, x, sr, n=2048, hop=512):
    """The whistle detector: how much energy the resynthesis has where the
    recording has none. Over the time-frequency cells where the target sits at
    or below its own floor (20th percentile of log magnitude), the mean of the
    model's excess above that floor, in dB. A clean fit is under 1; a whistle
    is several."""
    tx = torch.tensor(x, dtype=torch.float32)
    ty = torch.tensor(y, dtype=torch.float32)
    X = torch.log(stft_mag(tx, n, hop) + 1e-4)
    Y = torch.log(stft_mag(ty, n, hop) + 1e-4)
    floor = torch.quantile(X.flatten(), 0.2)
    quiet = X <= floor
    if not quiet.any():
        return 0.0
    return float(8.686 * torch.relu(Y[quiet] - floor).mean())


def validate(f, r, amp, x, sr, nfft=4096, hop=256, margin_db=6.0):
    """Drop a fitted mode the recording does not show: over the mode's own
    first half-second (or its T60, if shorter), the target's magnitude at its
    frequency has to sit `margin_db` above the target's floor at that time. A
    mode that passed the fit but not this is one the optimiser invented to fill
    a hole in the loss, and it is the kind that whistles."""
    win = np.hanning(nfft)
    frames = [np.abs(np.fft.rfft(x[s:s + nfft] * win)) for s in range(0, len(x) - nfft, hop)]
    S = np.log(np.array(frames) + 1e-9)
    floor = np.quantile(S, 0.2)
    keep = np.ones(len(f), dtype=bool)
    for i in range(len(f)):
        b = int(round(f[i] * nfft / sr))
        if b < 1 or b >= S.shape[1]:
            keep[i] = False
            continue
        n = max(1, min(len(S), int(min(0.5, 6.91 / r[i]) * sr / hop)))
        seen = S[:n, max(0, b - 1):b + 2].max()
        if seen - floor < margin_db / 8.686:
            keep[i] = False
    return keep


def stft_mag(y, n, hop):
    win = torch.hann_window(n, device=y.device)
    return torch.stft(y, n, hop, window=win, return_complex=True).abs()


def fit(x, sr, init, steps, device, verbose=True):
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
    # the target's own floor, per scale: the 20th percentile of its log
    # magnitude. Below it there is nothing to match, only noise to imitate.
    tfloor = [torch.quantile(tl.flatten(), 0.2) for tl in tlog]

    def render():
        f, r, a = torch.exp(logf), torch.exp(logr), torch.exp(loga)
        env = torch.exp(-r[:, None] * t[None, :])
        return (a[:, None] * env * torch.sin(2 * math.pi * f[:, None] * t[None, :] + phase[:, None])).sum(0)

    sched = torch.optim.lr_scheduler.CosineAnnealingLR(opt, T_max=steps, eta_min=0.0)
    for step in range(steps):
        opt.zero_grad()
        y = render()
        # three terms a scale. Spectral convergence, relative and so dominated
        # by the loud partials. Log-magnitude L1 at a tenth of the weight —
        # Diaz et al. (ICASSP 2023) use 1.0 linear against 0.1 log for the same
        # fit, and equal weights here let the floor's two thousand bins outvote
        # the partials: the loudest modes rang four times too long. And an
        # asymmetric term, the model's log magnitude above the target's where
        # the target is at its own floor — energy the instrument never made,
        # which is exactly what a whistle is. The log terms sit on a floor at
        # the target's 20th percentile, so imitating noise buys nothing.
        loss = 0.0
        for (n, h), tm, tl, tn, tf in zip(scales, tmag, tlog, tnorm, tfloor):
            ym = stft_mag(y, n, h)
            yl = torch.log(ym + 1e-4)
            sc = (ym - tm).norm() / tn
            lg = (torch.clamp(yl, min=tf) - torch.clamp(tl, min=tf)).abs().mean()
            excess = torch.relu(yl - torch.clamp(tl, min=tf))
            ex = excess[tl <= tf].mean() if (tl <= tf).any() else excess.mean()
            loss = loss + sc + 0.1 * lg + 1.0 * ex
        loss.backward()
        opt.step()
        sched.step()
        if verbose and (step % 100 == 0 or step == steps - 1):
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
    rep = {}
    init = initialise(x, sr, a.modes, report=rep)
    print('  %d candidate modes from the spectrum, %.1f .. %.1f Hz; rejected %s' % (
        len(init), min(m[0] for m in init), max(m[0] for m in init), rep))
    f, r, amp, y, loss = fit(x, sr, init, a.steps, a.device)
    # a mode the fit has turned down to nothing is a mode it could not place,
    # not a quiet one: keep it out of the record rather than in with a decay
    # nobody measured
    cap = a.max_t60 if a.max_t60 > 0 else 3.0 * a.seconds
    r = np.maximum(r, 6.91 / cap)
    keep = (amp > amp.max() * 10 ** (a.floor / 20)) & validate(f, r, amp, x, sr)
    dropped = int((~keep).sum())
    f, r, amp = f[keep], r[keep], amp[keep]
    order = np.argsort(f)
    # resynthesise what survived, for the file and for the whistle detector
    t = np.arange(len(x)) / sr
    y = np.zeros_like(x)
    for i in range(len(f)):
        y += amp[i] * np.exp(-r[i] * t) * np.sin(2 * math.pi * f[i] * t)
    ex = excess_db(y, x, sr)
    with open(a.out, 'w') as o:
        o.write('# modalfit record: a strike fitted as decaying sines on %s. zeta is measured,\n' % a.device)
        o.write('# gains are the fitted amplitude at the one position the recording is, repeated.\n')
        o.write('source %s\nfitted 1\nloss %.5f\nexcess_db %.3f\n' % (a.wav, loss, ex))
        o.write('positions %d\n' % a.positions)
        o.write('modes %d\n' % len(order))
        for k, i in enumerate(order):
            w = 2 * math.pi * f[i]
            o.write('mode %d hz %.6f zeta %.9g gains %s\n' % (k, f[i], r[i] / w, ' '.join('%.9g' % amp[i] for _ in range(a.positions))))
    if a.resynth:
        sf.write(a.resynth, np.clip(y / (np.max(np.abs(y)) or 1.0) * 0.5, -1, 1), sr)
    if a.target:
        sf.write(a.target, np.clip(x * 0.5, -1, 1), sr)
    print('  fitted %d modes (%d dropped), final loss %.4f, excess %.2f dB -> %s' % (len(order), dropped, loss, ex, a.out))
    for i in order[:8]:
        print('    %8.1f Hz  T60 %.2fs  amp %.3g' % (f[i], 6.91 / r[i], amp[i]))


if __name__ == '__main__':
    main()
