/* modalfem — mesh2faust's pipeline, keeping the eigenvectors signed.
 *
 *   modalfem --tet m.tet --expos m.expos --out m.mmr
 *            [--nmodes 48] [--fem 140] [--minfreq 20] [--maxfreq 20000]
 *            [--material E nu rho alpha beta] [--quiet]
 *
 * This is mesh2faust's `mesh2modal()` — Vega's StVK stiffness and mass
 * assembly, Spectra's shift-invert generalised eigensolver — with a different
 * front and a different back. The front takes a tetrahedral volume from
 * tools/meshgen.py rather than a surface for Vega's tet mesher, because the
 * mesher refines for element quality and not for size: a 4 mm bar came out one
 * layer of linear tets thick, and one layer is twice too stiff in bending
 * (2942 Hz against the Euler–Bernoulli 1441 for the uniform bar). The back is
 * ten lines instead of mesh2faust's. mesh2faust reports a mode's gain at a vertex as the *norm* of the
 * eigenvector there, which throws the sign away and with it half of what a
 * gain pattern says about a shape: a first bending mode and a third look alike
 * in magnitude across a few points and opposite in sign. The alignment stage
 * lives on those signs. So here a gain is the eigenvector at the vertex nearest
 * a canonical strike position, projected on the strike direction, signed —
 * which is also the physically honest number, since a strike has a direction.
 *
 * Damping is Rayleigh, alpha/omega + beta*omega, exactly as mesh2faust computes
 * it and for the same reason it is called unreliable in the brief: it is a
 * two-parameter model, not a measurement. It is stored because the format has a
 * slot for it; grading uses uniform damping and says so.
 *
 * Output is a text record, one line per quantity, so it can be read by a
 * stdlib Python script and by eye. The corpus packer turns a directory of these
 * into the binary the C++ tools read.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>

#include "tetMesh.h"
#include "StVKElementABCDLoader.h"
#include "StVKStiffnessMatrix.h"
#include "generateMassMatrix.h"
#include "sparseMatrix.h"

#include <Eigen/SparseCore>
#include <Spectra/MatOp/SparseSymMatProd.h>
#include <Spectra/MatOp/SymShiftInvert.h>
#include <Spectra/SymGEigsShiftSolver.h>

struct Expo { double p[3], d[3]; };

static bool ReadExpos(const std::string& path, std::vector<Expo>& out)
{
    std::ifstream f(path);
    if(!f) return false;
    std::string line;
    while(std::getline(f, line))
    {
        if(line.empty() || line[0] == '#') continue;
        std::istringstream ss(line);
        Expo e;
        if(!(ss >> e.p[0] >> e.p[1] >> e.p[2] >> e.d[0] >> e.d[1] >> e.d[2])) return false;
        out.push_back(e);
    }
    return !out.empty();
}

int main(int argc, char** argv)
{
    std::string obj, expos, out;
    int    nmodes = 48, fem = 140;
    double minfreq = 20.0, maxfreq = 20000.0;
    double E = 2e11, nu = 0.29, rho = 7850.0, alpha = 5.0, beta = 3e-8;
    bool   quiet = false;
    for(int i = 1; i < argc; i++)
    {
        const std::string a = argv[i];
        auto next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : "0"; };
        if(a == "--tet") obj = next();
        else if(a == "--expos") expos = next();
        else if(a == "--out") out = next();
        else if(a == "--nmodes") nmodes = atoi(next());
        else if(a == "--fem") fem = atoi(next());
        else if(a == "--minfreq") minfreq = atof(next());
        else if(a == "--maxfreq") maxfreq = atof(next());
        else if(a == "--quiet") quiet = true;
        else if(a == "--material")
        {
            E = atof(next()); nu = atof(next()); rho = atof(next()); alpha = atof(next()); beta = atof(next());
        }
        else { fprintf(stderr, "unknown option %s\n", a.c_str()); return 2; }
    }
    if(obj.empty() || expos.empty() || out.empty()) { fprintf(stderr, "need --tet --expos --out\n"); return 2; }

    std::vector<Expo> ex;
    if(!ReadExpos(expos, ex)) { fprintf(stderr, "cannot read %s\n", expos.c_str()); return 1; }

    /* ── the volume ─────────────────────────────────────────────────────── */
    std::vector<double> verts;
    std::vector<int>    elems;
    {
        std::ifstream f(obj);
        std::string   line;
        int nvv = 0, nee = 0, state = 0;
        while(std::getline(f, line))
        {
            if(line.empty() || line[0] == '#') continue;
            std::istringstream ss(line);
            if(state == 0) { ss >> nvv >> nee; state = 1; continue; }
            if((int)verts.size() < 3 * nvv) { double x, y, z; ss >> x >> y >> z; verts.push_back(x); verts.push_back(y); verts.push_back(z); continue; }
            int a, b, c, d; ss >> a >> b >> c >> d;
            elems.push_back(a); elems.push_back(b); elems.push_back(c); elems.push_back(d);
        }
        if(nvv <= 0 || (int)verts.size() != 3 * nvv || (int)elems.size() != 4 * nee)
        { fprintf(stderr, "cannot read %s\n", obj.c_str()); return 1; }
    }
    TetMesh* vol = new TetMesh((int)verts.size() / 3, verts.data(), (int)elems.size() / 4, elems.data(), E, nu, rho);
    const int nv = vol->getNumVertices();
    if(!quiet) fprintf(stderr, "  %s: %d vertices, %d tets\n", obj.c_str(), nv, vol->getNumElements());

    /* ── K and M, exactly as mesh2faust assembles them ─────────────────── */
    SparseMatrix* Mv = nullptr;
    GenerateMassMatrix::computeMassMatrix(vol, &Mv, true);
    StVKElementABCD*   abcd = StVKElementABCDLoader::load(vol);
    StVKInternalForces forces{vol, abcd};
    SparseMatrix*      Kv = nullptr;
    StVKStiffnessMatrix kmat{&forces};
    kmat.GetStiffnessMatrixTopology(&Kv);
    std::vector<double> zero((size_t)nv * 3, 0.0);
    kmat.ComputeStiffnessMatrix(zero.data(), Kv);
    delete abcd;

    const int n = Kv->Getn();
    std::vector<Eigen::Triplet<double>> kt, mt;
    for(int i = 0; i < Kv->GetNumRows(); i++)
        for(int j = 0; j < Kv->GetRowLength(i); j++) kt.push_back({i, Kv->GetColumnIndex(i, j), Kv->GetEntry(i, j)});
    for(int i = 0; i < Mv->GetNumRows(); i++)
        for(int j = 0; j < Mv->GetRowLength(i); j++) mt.push_back({i, Mv->GetColumnIndex(i, j), Mv->GetEntry(i, j)});
    Eigen::SparseMatrix<double> K(n, n), M(n, n);
    K.setFromTriplets(kt.begin(), kt.end());
    M.setFromTriplets(mt.begin(), mt.end());
    delete Kv; delete Mv;

    /* ── the eigenproblem, as mesh2faust poses it ───────────────────────── */
    const int femN  = std::min(fem, n - 1);
    const int ncv   = std::min(std::max(2 * femN + 1, 20), n);
    const double sigma = std::pow(2 * M_PI * minfreq, 2);
    using OpType  = Spectra::SymShiftInvert<double, Eigen::Sparse, Eigen::Sparse>;
    using BOpType = Spectra::SparseSymMatProd<double>;
    OpType  op(K, M);
    BOpType bop(M);
    Spectra::SymGEigsShiftSolver<OpType, BOpType, Spectra::GEigsMode::ShiftInvert> eigs(op, bop, femN, ncv, sigma);
    eigs.init();
    eigs.compute(Spectra::SortRule::LargestMagn, 1000, 1e-10, Spectra::SortRule::SmallestAlge);
    if(eigs.info() != Spectra::CompInfo::Successful) { fprintf(stderr, "eigensolver failed on %s\n", obj.c_str()); return 1; }
    const auto lam = eigs.eigenvalues();
    const auto vec = eigs.eigenvectors();

    /* ── the canonical positions, as tet-mesh vertices ──────────────────── */
    std::vector<int> vid(ex.size(), -1);
    for(size_t p = 0; p < ex.size(); p++)
    {
        double best = 1e30;
        for(int v = 0; v < nv; v++)
        {
            const Vec3d& q = vol->getVertex(v);
            const double dx = q[0] - ex[p].p[0], dy = q[1] - ex[p].p[1], dz = q[2] - ex[p].p[2];
            const double d2 = dx * dx + dy * dy + dz * dz;
            if(d2 < best) { best = d2; vid[p] = v; }
        }
        if(!quiet && best > 1e-5) fprintf(stderr, "  position %zu is %.2f mm from its nearest vertex\n", p, 1e3 * std::sqrt(best));
    }

    /* ── modes: real ones in range, lowest first ────────────────────────── */
    struct Mode { double hz, zeta; std::vector<double> g; };
    std::vector<Mode> modes;
    const double wmin = 2 * M_PI * minfreq, wmax = 2 * M_PI * maxfreq;
    int rigid = 0;
    for(int m = 0; m < femN; m++)
    {
        if(lam[m] < wmin * wmin) { rigid++; continue; }     /* rigid-body and anything below the floor */
        const double w = std::sqrt(lam[m]);
        if(w > wmax) break;
        Mode md;
        md.hz   = w / (2 * M_PI);
        md.zeta = 0.5 * (alpha / w + beta * w);
        md.g.resize(ex.size());
        for(size_t p = 0; p < ex.size(); p++)
        {
            const int base = 3 * vid[p];
            md.g[p] = vec(base, m) * ex[p].d[0] + vec(base + 1, m) * ex[p].d[1] + vec(base + 2, m) * ex[p].d[2];
        }
        modes.push_back(md);
        if((int)modes.size() >= nmodes) break;
    }
    if(!quiet) fprintf(stderr, "  %d modes below the floor (rigid), %zu kept of %d asked, lowest %.1f Hz highest %.1f Hz\n",
                       rigid, modes.size(), nmodes, modes.empty() ? 0.0 : modes.front().hz,
                       modes.empty() ? 0.0 : modes.back().hz);

    /* ── the record ─────────────────────────────────────────────────────── */
    FILE* f = fopen(out.c_str(), "w");
    if(!f) { fprintf(stderr, "cannot write %s\n", out.c_str()); return 1; }
    fprintf(f, "# modalfem record. Gains are the mass-normalised eigenvector at the nearest\n");
    fprintf(f, "# tet vertex to each canonical position, projected on the strike direction,\n");
    fprintf(f, "# signed. zeta is Rayleigh alpha/(2w) + beta*w/2 and is a model, not a measurement.\n");
    fprintf(f, "mesh %s\n", obj.c_str());
    fprintf(f, "material %g %g %g %g %g\n", E, nu, rho, alpha, beta);
    fprintf(f, "tetverts %d elements %d femmodes %d rigid %d\n", nv, vol->getNumElements(), femN, rigid);
    fprintf(f, "positions %zu\n", ex.size());
    for(size_t p = 0; p < ex.size(); p++)
    {
        const Vec3d& q = vol->getVertex(vid[p]);
        fprintf(f, "pos %zu vertex %d at %.6f %.6f %.6f strike %.4f %.4f %.4f\n", p, vid[p], q[0], q[1], q[2],
                ex[p].d[0], ex[p].d[1], ex[p].d[2]);
    }
    fprintf(f, "modes %zu\n", modes.size());
    for(size_t m = 0; m < modes.size(); m++)
    {
        fprintf(f, "mode %zu hz %.6f zeta %.9g gains", m, modes[m].hz, modes[m].zeta);
        for(double g : modes[m].g) fprintf(f, " %.9g", g);
        fprintf(f, "\n");
    }
    fclose(f);
    delete vol;
    return 0;
}
