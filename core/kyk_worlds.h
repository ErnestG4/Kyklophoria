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

enum : uint8_t { kCrop = 0, kCell24 = 1, kCell16 = 2, kTesseract = 3, kStack = 4,
                 kFieldCalm = 5, kFieldWild = 6, kFieldTorus = 7, kHarmonic = 8, kFm = 9, kVowel = 10, kShapes = 11, kShapesRing = 12, kLock = 13, kUnison = 14, kPlate = 15, kBar = 16, kDrum = 17, kSaw = 18, kPulse = 19, kEdge = 20, kGrit = 21, kCount = 22 };

struct Entry
{
    const char*  name;
    const char*  note;        /* one line, for the page's list */
    World::Kind  kind;        /* Analytic needs no memory and no wait */
};

inline const Entry& Get(uint8_t i)
{
    static const Entry kEntries[kCount] = {
        /* Named for what it is rather than for where its corpus came from.
         * The bank is Emilie Gillet's and the credit belongs in the note, in
         * THIRD_PARTY.md and in the README — not in the name of a world that
         * is four principal components of it and sounds like neither.
         *
         * The notes say what the four axes do, and the formula where there is
         * one. A world is a space you steer by hand and the page is the only
         * place that can tell you what you are steering — "eigenspace of a
         * 256-wave bank" says where it came from and nothing about what
         * turning a knob will do. They cost about 2.5 KB across all
         * twenty-one, which the list already pages. */
        {"Crop",     "four principal components of a 256-wave bank (Braids, Emilie Gillet). "
                     "exp(mean log-spectrum + sum of axis x component), so any coordinate is closed form", World::Kind::Analytic},
        {"24-cell",  "a waveform on each of the 24 vertices of (+-1,+-1,0,0), blended exp(-d^2/2s^2); "
                     "Morph narrows s to 0.3x, so arriving at a vertex arrives at that waveform", World::Kind::Vertices},
        {"16-cell",  "8 vertices, one per axis direction, same exp(-d^2/2s^2) blend: "
                     "wide open, and mostly the mire between them", World::Kind::Vertices},
        {"Tesseract","16 vertices on the cube corners, so axis-aligned motion aims straight at one "
                     "rather than between two", World::Kind::Vertices},
        {"Stack",    "one waveform idea per axis: stack count on a log axis (n x2 and n /2 are equal "
                     "and opposite steps), spectral tilt, pulse width, parity", World::Kind::Lattice},
        {"Field",    "correlated noise, sampled rather than derived. No legible axis anywhere and the "
                     "most even space here: 1.5x direction spread against 2.1x to 6.2x", World::Kind::Lattice},
        {"Field II", "the same, rougher and less correlated: shorter correlation length, so a step "
                     "of the same size travels further", World::Kind::Lattice},
        {"Torus",    "a field with no edges: every axis wraps, and the falling body wraps with it - "
                     "23 laps in 200 s on a clamped world, 166 here", World::Kind::Lattice},
        {"Harmonic", "one parameter per axis: saw to square, tilt to h^-2, a formant sweeping "
                     "harmonic 1 to K, even harmonics 1 to 0. Legible, and the most lopsided here", World::Kind::Lattice},
        {"FM",       "Bessel sidebands J_k(I): index 0 to 16 (0 is a bare sine), ratio 0.5 to 2.5 warped "
                     "to linger on whole numbers, carrier harmonic 1 to 8, second carrier 0 to 9 that interferes", World::Kind::Fm},
        {"Vowel",    "three resonances, chained - the second a multiple of the first, the third of the "
                     "second - over a falling source. Peaks ride the pitch: a world is told nothing about f0", World::Kind::Formant},
        {"Shapes",   "saw, square, triangle and pulse on a grid at sine phase, then wavefolding and "
                     "phase distortion. Aliasing at full fold -32 dB, at full PM -62", World::Kind::Table},
        {"Shapes R", "the same grid, with wavefolding and ring modulation instead: "
                     "ring mod aliases at -59 dB against -67 dry", World::Kind::Table},
        {"Lock",     "the 24-cell at sine phase, one family per Givens plane: (0,1) pulses, (0,2) saws, "
                     "(0,3) triangles, (1,2) and (1,3) stacks of partials, (2,3) combs", World::Kind::Lock},
        {"Unison",   "voices 1 to 7, roots at 1 + j.s for s from 1 (dense buzz) to 2.6 (wide and hollow), "
                     "detune spreading each root across two harmonics, and the copied wave saw to pulse", World::Kind::Unison},
        {"Plate",     "a struck plate as a formula, never an integrator: strike point, shape, how long "
                      "ago, how damped. Modes are tuned onto the grid, so what survives is which are loud", World::Kind::Modal},
        {"Bar",       "a struck bar, tuned onto the grid the same way: sparse and ringing, "
                      "and 8.9% off a real bar at its worst", World::Kind::Modal},
        {"Drum",      "a struck membrane, tuned: packed low and dying fast. A real one sits 25.5% off "
                      "the harmonic grid, over four semitones, so this is a drum in amplitude only", World::Kind::Modal},
        {"Saw",       "nothing but a saw, bent four ways: tilt (h^-1.6 through h^-0.75), parity, "
                      "comb, fold point", World::Kind::Bend},
        {"Pulse",     "nothing but a pulse: duty, tilt, comb, fold point. "
                      "One shape, four ways to bend it, and no axis fighting another", World::Kind::Bend},
        {"Edge",      "saw against pulse and nothing else: the blend between them, duty, comb, fold point", World::Kind::Bend},
        {"Grit",      "the Shapes grid put through bit reduction (8 bits to 1) and sample-rate reduction "
                      "(a 1024-point cycle down to 32 values). Aliases on purpose; the figure is in the README", World::Kind::Table},
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
    if(i == kCrop) { w.UseAnalytic(BraidsBasis(), p, topo); return true; }
    if(i == kFm) { FmField f; f.n = 4; f.k = 64; w.UseFm(f, p, topo); return true; }
    if(i == kVowel) { FormantField f; f.n = 4; f.k = 64; w.UseFormant(f, p, topo); return true; }
    if(i == kShapes || i == kShapesRing)
    {
        w.UseShapes(4, kShapeK, Shaper::Fold,
                    (i == kShapesRing) ? Shaper::Ring : Shaper::Warp,
                    0.08f, p, topo);
        return true;
    }
    /* The same grid of real waveforms as Shapes, with the two harsh shapers on
       its manipulation axes instead of the polite ones. Reported from the bench
       as the gap: "there's no really glitchy harsh worlds (shapes are close)" —
       the base was right and fold and phase modulation are simply too clean. */
    if(i == kGrit)
    {
        w.UseShapes(4, kShapeK, Shaper::Crush, Shaper::Drop, 0.08f, p, topo);
        return true;
    }
    if(i == kLock) { w.UseLock(4, kShapeK, 0.17f, p, topo); return true; }
    if(i == kUnison) { w.UseUnison(4, kShapeK, p, topo); return true; }
    if(i == kPlate) { w.UseModal(Body::Plate, 4, kShapeK, p, topo); return true; }
    if(i == kBar) { w.UseModal(Body::Bar, 4, kShapeK, p, topo); return true; }
    if(i == kDrum) { w.UseModal(Body::Drum, 4, kShapeK, p, topo); return true; }
    if(i == kSaw) { w.UseBend(Base::Saw, 4, kShapeK, p, topo); return true; }
    if(i == kPulse) { w.UseBend(Base::Pulse, 4, kShapeK, p, topo); return true; }
    if(i == kEdge) { w.UseBend(Base::Edge, 4, kShapeK, p, topo); return true; }
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
