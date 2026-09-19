/* spectrum.h — one point in the space is one spectrum.
 *
 * The grader needs a fixed-length vector per point, comparable between points
 * by Euclidean distance the way kykworlds compares harmonic magnitude vectors.
 * A modal model at a point is a list of (frequency, amplitude) pairs, and its
 * frequencies are not on any shared grid, so:
 *
 *   strike at canonical position p_s, listen at p_l (both 0 by default);
 *   the amplitude of mode i is G[i][p_s] G[i][p_l] / omega_i — the impulse
 *   response of a mass-normalised mode, displacement picked up at a point;
 *   the 1/omega is the displacement, and --no-omega gives the velocity;
 *
 *   damping is uniform: one decay rate for every mode, so the elapsed time
 *   scales the whole spectrum and changes nothing about its shape. The brief
 *   asks for this because the FEA damping is a two-parameter model and the
 *   grade is about spectral variety, not decay; --damping-rate and --elapsed
 *   are flags so the choice is stated rather than buried;
 *
 *   the amplitudes are laid onto bands, --bands-per-octave of them (6) from
 *   40 Hz to 20 kHz, each mode as a Gaussian of --kernel octaves (1/12) so
 *   that a mode gliding across a band edge moves energy between bands
 *   continuously instead of flipping bins — a bin flip would read as a cliff
 *   in the continuity sweep and it would be the grid's cliff, not the space's;
 *
 *   the band vector is normalised to unit norm, so distance is timbre and not
 *   loudness, as in kykworlds.
 *
 * A frequency that is not positive contributes nothing; the `linear` variant
 * produces those off the corpus. A frequency above the top band is not cut off
 * — it contributes through the kernel's tail, which is nothing by 22 kHz — so
 * a padding mode being interpolated down into hearing arrives smoothly rather
 * than appearing all at once. The first version cut at 20 kHz and the
 * continuity sweep measured that cut as a cliff on every axis: the grid's
 * cliff, not the space's.
 *
 * --normalise-pitch divides every frequency by the model's lowest audible one
 * before banding. The brief's spectrum counts pitch motion as variety, and in
 * this corpus that is most of the variety — the bars sit an octave below the
 * bells — so the number is reported both ways: as defined, and with the
 * fundamental held still so that what is left is the ratio structure and the
 * gain pattern, which is what Kyklophoria's variety measures.
 */
#pragma once
#include <cmath>
#include <vector>
#include "space.h"

namespace mb {

struct Spectrograph
{
    int    bands_per_octave = 6;
    double f_lo = 40.0, f_hi = 20000.0;
    double kernel = 1.0 / 12.0;      /* octaves, Gaussian sigma */
    int    strike = 0, listen = 0;
    bool   omega = true;
    double damping_rate = 5.0;       /* 1/s, uniform */
    double elapsed = 0.2;            /* s */
    bool   normalise_pitch = false;
    double pitch_ref = 440.0;        /* where the lowest mode goes when normalised */

    int Bands() const { return (int)std::ceil(std::log2(f_hi / f_lo) * bands_per_octave); }
    double BandCentre(int b) const { return f_lo * std::pow(2.0, (b + 0.5) / bands_per_octave); }

    /* Modal amplitudes at the elapsed time; skipped modes report -1. */
    void Amplitudes(const Rep& r, std::vector<double>& amp, int* skipped = nullptr) const
    {
        const int N = (int)r.hz.size();
        amp.assign(N, 0.0);
        int sk = 0;
        const double env = std::exp(-damping_rate * elapsed);
        for(int i = 0; i < N; i++)
        {
            if(!(r.hz[i] > 0.0)) { sk++; continue; }
            double a = r.G(i, strike) * r.G(i, listen);
            if(omega) a /= 2.0 * M_PI * r.hz[i];
            amp[i] = std::fabs(a) * env;
        }
        if(skipped) *skipped = sk;
    }

    /* The unit band vector. */
    void Bands(const Rep& r, std::vector<double>& out, int* skipped = nullptr) const
    {
        const int B = Bands();
        std::vector<double> amp;
        Amplitudes(r, amp, skipped);
        out.assign(B, 0.0);
        const double inv2s2 = 1.0 / (2.0 * kernel * kernel);
        double shift = 1.0;
        if(normalise_pitch)
        {
            double lowest = 0.0;
            for(size_t i = 0; i < amp.size(); i++)
                if(amp[i] > 0.0 && r.hz[i] <= f_hi && (lowest == 0.0 || r.hz[i] < lowest)) lowest = r.hz[i];
            if(lowest > 0.0) shift = pitch_ref / lowest;
        }
        for(size_t i = 0; i < amp.size(); i++)
        {
            if(amp[i] <= 0.0) continue;
            const double lf = std::log2(r.hz[i] * shift / f_lo) * bands_per_octave;   /* in band units */
            for(int b = 0; b < B; b++)
            {
                const double d = (lf - (b + 0.5)) / bands_per_octave;      /* octaves */
                if(std::fabs(d) > 4 * kernel) continue;
                out[b] += amp[i] * std::exp(-d * d * inv2s2);
            }
        }
        double n2 = 0;
        for(double v : out) n2 += v * v;
        if(n2 > 0) { const double inv = 1.0 / std::sqrt(n2); for(double& v : out) v *= inv; }
    }
};

} // namespace mb
