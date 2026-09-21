/* script.h — the desktop parameter script: parsing and playback.
 *
 * One event per line, `<time_s> <command> <args…>`; `#` comments.
 *   f0 <hz>                      set pitch
 *   pos <x0> [x1 …]              set position (control frame)
 *   glide f0 <hz> <secs>         linear glide to a pitch
 *   glide pos <x0…> <secs>       linear glide to a position (N values)
 *   angle <plane> <turns>        rotation angle of a plane
 *   rate <plane> <turns/sec>     orbit rate of a plane, signed
 *   couple <K> [reach]           Kuramoto ratio coupling; 0 is independent
 *   spread <turns>               stereo spread ±δ in the stereo plane
 *   kepler <G> <ecc> <plane>     launch a Kepler orbit; G 0 stops it
 *   plane <idx>                  the stereo plane
 *   <secs> dur                   total length (default: last event + 1 s)
 * Events apply at the first block whose start time ≥ their time.
 */
#pragma once
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include <sstream>
#include "kyk_stereo.h"

namespace kykdesk {

struct Event
{
    double             t;
    std::string        cmd;      /* f0 | pos | glide_f0 | glide_pos | angle | spread | plane */
    std::vector<float> v;
    double             dur = 0;
};

struct Script
{
    std::vector<Event> ev;
    double             total = 1.0;
    bool               uses_stereo = false;   /* any angle/spread event */

    bool Load(const std::string& path)
    {
        std::ifstream in(path);
        if(!in) return false;
        std::string line;
        int         ln = 0;
        total          = -1;
        ev.clear();
        while(std::getline(in, line))
        {
            ln++;
            const size_t h = line.find('#');
            if(h != std::string::npos) line.erase(h);
            std::istringstream ss(line);
            double             t;
            std::string        cmd;
            if(!(ss >> t >> cmd)) continue;
            if(cmd == "dur") { total = t; continue; }
            Event e;
            e.t   = t;
            e.cmd = cmd;
            if(cmd == "glide")
            {
                std::string what;
                ss >> what;
                e.cmd = "glide_" + what;
            }
            std::vector<float> vals;
            float              x;
            while(ss >> x) vals.push_back(x);
            if(e.cmd.rfind("glide_", 0) == 0)
            {
                if(vals.size() < 2) { fprintf(stderr, "%s:%d: glide needs values and a duration\n", path.c_str(), ln); return false; }
                e.dur = vals.back();
                vals.pop_back();
            }
            if(vals.empty()) { fprintf(stderr, "%s:%d: %s needs a value\n", path.c_str(), ln, cmd.c_str()); return false; }
            if(e.cmd == "angle" || e.cmd == "spread" || e.cmd == "rate" || e.cmd == "kepler"
               || e.cmd == "couple") uses_stereo = true;
            e.v = vals;
            ev.push_back(e);
        }
        std::stable_sort(ev.begin(), ev.end(), [](const Event& a, const Event& b) { return a.t < b.t; });
        if(total < 0) total = (ev.empty() ? 0.0 : ev.back().t) + 1.0;
        return true;
    }
};

/* Plays a Script into a StereoEngine block by block. */
class Player
{
public:
    void Reset(const Script* s)
    {
        script_ = s;
        next_   = 0;
        f0_     = 110.f;
        for(int a = 0; a < kyk::kMaxN; a++) pos_[a] = 0.5f;
        gf0_ = gpos_ = false;
    }

    /* Apply everything due at time t (seconds) to the engine's controls. */
    void At(double t, kyk::StereoEngine& eng)
    {
        if(script_)
        {
            const auto& ev = script_->ev;
            while(next_ < ev.size() && ev[next_].t <= t + 1e-9)
            {
                const Event& e = ev[next_++];
                if(e.cmd == "f0") { f0_ = e.v[0]; gf0_ = false; }
                else if(e.cmd == "pos") { for(size_t a = 0; a < e.v.size() && a < (size_t)kyk::kMaxN; a++) pos_[a] = e.v[a]; gpos_ = false; }
                else if(e.cmd == "angle") { if(e.v.size() >= 2) eng.rot.SetAngle((int)e.v[0], e.v[1]); }
                else if(e.cmd == "rate") { if(e.v.size() >= 2) eng.rot.SetRate((int)e.v[0], e.v[1]); }
                else if(e.cmd == "couple")
                {
                    eng.rot.SetCouple(e.v[0]);
                    if(e.v.size() > 1) eng.rot.SetReach((int)e.v[1]);
                }
                else if(e.cmd == "spread") eng.spread = e.v[0];
                else if(e.cmd == "kepler")
                {
                    if(e.v[0] <= 0.f) eng.kepler.Stop();
                    else
                    {
                        eng.kepler.gravity = e.v[0];
                        eng.kepler.plane   = e.v.size() > 2 ? (int)e.v[2] : 0;
                        eng.kepler.Reset(0.35f, e.v.size() > 1 ? e.v[1] : 0.5f);
                    }
                }
                else if(e.cmd == "plane") eng.spread_plane = (int)e.v[0];
                /* a resonate world's strike, at a velocity in [0,1]; the
                   pitch is whatever f0 is at that moment. Nothing on any
                   other kind of world (docs/modal-mode.md) */
                else if(e.cmd == "strike") { eng.SetF0(f0_); eng.Strike(e.v.empty() ? 0.8f : e.v[0]); }
                else if(e.cmd == "glide_f0") { gf0_ = true; f0_from_ = f0_; f0_to_ = e.v[0]; f0_t0_ = t; f0_t1_ = t + e.dur; }
                /* the pitch lock (1, the default: the semitone, taken at
                   the strike) and the polyphony (1, 2 or 4) */
                else if(e.cmd == "lock") eng.SetPitchLock(e.v.empty() || e.v[0] != 0.f);
                else if(e.cmd == "poly") eng.SetPolyphony(e.v.empty() ? 1 : (int)e.v[0]);
                else if(e.cmd == "glide_pos")
                {
                    gpos_ = true; pos_t0_ = t; pos_t1_ = t + e.dur;
                    for(int a = 0; a < kyk::kMaxN; a++) { pos_from_[a] = pos_[a]; pos_to_[a] = a < (int)e.v.size() ? e.v[a] : pos_[a]; }
                }
            }
        }
        if(gf0_)
        {
            float u = f0_t1_ > f0_t0_ ? (float)((t - f0_t0_) / (f0_t1_ - f0_t0_)) : 1.f;
            if(u >= 1.f) { u = 1.f; gf0_ = false; }
            f0_ = f0_from_ + (f0_to_ - f0_from_) * u;
        }
        if(gpos_)
        {
            float u = pos_t1_ > pos_t0_ ? (float)((t - pos_t0_) / (pos_t1_ - pos_t0_)) : 1.f;
            if(u >= 1.f) { u = 1.f; gpos_ = false; }
            for(int a = 0; a < kyk::kMaxN; a++) pos_[a] = pos_from_[a] + (pos_to_[a] - pos_from_[a]) * u;
        }
        eng.SetF0(f0_);
        eng.SetControl(pos_, kyk::kMaxN);
    }

    float        F0() const { return f0_; }
    const float* Pos() const { return pos_; }

private:
    const Script* script_ = nullptr;
    size_t        next_   = 0;
    float         f0_     = 110.f;
    float         pos_[kyk::kMaxN];
    bool          gf0_ = false, gpos_ = false;
    float         f0_from_ = 0, f0_to_ = 0;
    double        f0_t0_ = 0, f0_t1_ = 0;
    float         pos_from_[kyk::kMaxN], pos_to_[kyk::kMaxN];
    double        pos_t0_ = 0, pos_t1_ = 0;
};

} // namespace kykdesk
