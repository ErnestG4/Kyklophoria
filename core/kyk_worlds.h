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
#include "kyk_solids.h"
#include "kyk_shapes.h"

namespace kyk {
namespace worlds {

enum : uint8_t { kBraids = 0, kCell24 = 1, kCell16 = 2, kTesseract = 3, kStack = 4,
                 kFieldCalm = 5, kFieldWild = 6, kFieldTorus = 7, kHarmonic = 8, kFm = 9, kVowel = 10, kShapes = 11, kShapesRing = 12, kLock = 13, kUnison = 14, kPlate = 15, kBar = 16, kDrum = 17, kCount = 18 };

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
        {"24-cell",  "a waveform on each vertex of the 4-D solid; Morph tightens the lock", World::Kind::Vertices},
        {"16-cell",  "only 8 vertices, on the axes: wide open, mostly mire", World::Kind::Vertices},
        {"Tesseract","16 vertices on the cube corners, so the axes aim straight at them", World::Kind::Vertices},
        {"Stack",    "one waveform idea per axis: stacking, tilt, width, parity", World::Kind::Lattice},
        {"Field",    "correlated noise, even in every direction",   World::Kind::Lattice},
        {"Field II", "the same, rougher and less correlated",       World::Kind::Lattice},
        {"Torus",    "a field with no edges: wraps on every axis, and gravity wraps with it", World::Kind::Lattice},
        {"Harmonic", "one parameter per axis, legible but lopsided", World::Kind::Lattice},
        {"FM",       "index, ratio, carrier and a second carrier that interferes", World::Kind::Fm},
        {"Vowel",    "three resonances over a falling source; the peaks ride the pitch", World::Kind::Formant},
        {"Shapes",   "the real saw, square, triangle and pulse on a grid; fold and phase distortion", World::Kind::Table},
        {"Shapes R", "the same grid, with wavefolding and ring modulation instead", World::Kind::Table},
        {"Lock",     "real waveforms on the 24-cell, one family per rotation plane", World::Kind::Lock},
        {"Unison",   "one wave stacked on itself; interval, detune, comb", World::Kind::Unison},
        {"Plate",     "a struck rectangular plate; the side ratio reorders its modes", World::Kind::Modal},
        {"Bar",       "a struck bar, tuned onto the harmonic grid", World::Kind::Modal},
        {"Drum",      "a struck membrane, tuned; dense and low-ordered", World::Kind::Modal},
    };
    return kEntries[i < kCount ? i : 0];
}

inline bool IsAnalytic(uint8_t i) { return Get(i).kind != World::Kind::Lattice; }

/* The 24-cell's vertex table, built once and kept for the life of the app. */
/* Which solid a world is built on, or -1 if it is not a vertex world. */
inline int SolidOf(uint8_t i)
{
    if(i == kCell24) return (int)solids::Solid::Cell24;
    if(i == kCell16) return (int)solids::Solid::Cell16;
    if(i == kTesseract) return (int)solids::Solid::Tesseract;
    return -1;
}

/* Analytic worlds: point the World at a formula. Nothing to expand and
 * nothing to wait for, so switching is a pointer write.
 *
 * A vertex world needs somewhere to keep its table, and that is the caller's
 * to provide rather than a static in here: at K = 128 a table is 17 KB, and
 * three of them as file statics overflowed the module's DTCM. The shell knows
 * which memory it can afford; this header does not. Pass the table that
 * belongs to the World being built, so a world still playing out of the other
 * buffer keeps its own. */
inline bool Point(uint8_t i, World& w, int p, const uint8_t* topo, solids::VertexTable* table = nullptr)
{
    if(i == kBraids) { w.UseAnalytic(BraidsBasis(), p, topo); return true; }
    if(i == kFm) { FmField f; f.n = 4; f.k = 64; w.UseFm(f, p, topo); return true; }
    if(i == kVowel) { FormantField f; f.n = 4; f.k = 64; w.UseFormant(f, p, topo); return true; }
    if(i == kShapes || i == kShapesRing)
    {
        w.UseShapes(4, kShapeK, Shaper::Fold,
                    (i == kShapesRing) ? Shaper::Ring : Shaper::Warp,
                    0.08f, p, topo);
        return true;
    }
    if(i == kLock) { w.UseLock(4, kShapeK, 0.17f, p, topo); return true; }
    if(i == kUnison) { w.UseUnison(4, kShapeK, p, topo); return true; }
    if(i == kPlate) { w.UseModal(Body::Plate, 4, kShapeK, p, topo); return true; }
    if(i == kBar) { w.UseModal(Body::Bar, 4, kShapeK, p, topo); return true; }
    if(i == kDrum) { w.UseModal(Body::Drum, 4, kShapeK, p, topo); return true; }
    const int sol = SolidOf(i);
    if(sol >= 0)
    {
        if(!table) return false;
        solids::Build((solids::Solid)sol, 64, *table);
        solids::Use(*table, w, p, topo);
        return true;
    }
    return false;
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
        case kStack:
            g.family = Family::Stack; g.name = "stack";
            break;
        case kFieldCalm:
            g.family = Family::Field; g.rough = 0.9f; g.smooth = 1; g.name = "field";
            break;
        case kFieldWild:
            g.family = Family::Field; g.rough = 1.5f; g.smooth = 0; g.seed = 7; g.name = "field II";
            break;
        case kFieldTorus:
            /* Every axis wraps, so the space has no edges at all: a sweep
             * that runs off one side arrives back on the other, the field
             * generator smooths across the seam so there is no join, and a
             * Kepler orbit here is toroidal. */
            g.family = Family::Field; g.rough = 1.0f; g.smooth = 1; g.seed = 3; g.name = "torus";
            for(int a = 0; a < kMaxN; a++) g.topo[a] = (uint8_t)Topo::Wrap;
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
