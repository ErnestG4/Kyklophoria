/* kyk_worlds.h — the built-in worlds.
 *
 * A world is a corpus, a representation and an extrapolation method
 * (docs/worlds.md). Each entry here says which backend answers for it: a
 * formula evaluated live, or a lattice that has to be expanded first because
 * the world has no formula. See kyk_world.h for why that distinction is the
 * whole design rather than an optimisation.
 */
#pragma once
#include "kyk_world.h"
#include "kyk_gen.h"

namespace kyk {
namespace worlds {

enum : uint8_t { kBraids = 0, kFieldCalm = 1, kFieldWild = 2, kHarmonic = 3, kCount = 4 };

struct Entry
{
    const char*  name;
    const char*  note;        /* one line, for the page's list */
    World::Kind  kind;        /* Analytic needs no memory and no wait */
};

inline const Entry& Get(uint8_t i)
{
    static const Entry kEntries[kCount] = {
        {"Braids",   "eigenspace of Emilie Gillet's 256-wave bank", World::Kind::Analytic},
        {"Field",    "correlated noise, even in every direction",   World::Kind::Lattice},
        {"Field II", "the same, rougher and less correlated",       World::Kind::Lattice},
        {"Harmonic", "one parameter per axis, legible but lopsided", World::Kind::Lattice},
    };
    return kEntries[i < kCount ? i : 0];
}

inline bool IsAnalytic(uint8_t i) { return Get(i).kind == World::Kind::Analytic; }

/* Analytic worlds: point the World at a formula. Nothing to expand, nothing
 * to allocate, and switching is a pointer write. */
inline bool Point(uint8_t i, World& w, int p, const uint8_t* topo)
{
    if(i != kBraids) return false;
    w.UseAnalytic(BraidsBasis(), p, topo);
    return true;
}

/* Tabulated worlds: expand into caller memory, then attach. Slow enough that
 * the shell should do it off the audio thread. Returns bytes written, 0 on
 * refusal. */
inline size_t Expand(uint8_t i, int n, int side, int k, int p, uint8_t* out, size_t cap)
{
    if(IsAnalytic(i)) return 0;
    GenParams g;
    g.n = n; g.side = side; g.k = k; g.p = p; g.seed = 1;
    switch(i)
    {
        case kFieldCalm:
            g.family = Family::Field; g.rough = 0.9f; g.smooth = 1; g.name = "field";
            break;
        case kFieldWild:
            g.family = Family::Field; g.rough = 1.5f; g.smooth = 0; g.seed = 7; g.name = "field II";
            break;
        case kHarmonic:
        default:
            g.family = Family::Harmonic; g.name = "harmonic";
            break;
    }
    return BuildLattice(g, out, cap);
}

} // namespace worlds
} // namespace kyk
