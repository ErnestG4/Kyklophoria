/* kykdesk — the desktop main(): drives the core from a parameter script and
 * renders a WAV, or serves HostLink on stdio for the web page. This is how
 * the core is tested; anything that only works on the module is a bug.
 *
 *   kykdesk [--space file.kyk | --gen [--seed S --N n --side s --K k --P p --wrap axis]]
 *          --script params.txt --out out.wav
 *          [--sr 48000] [--block 24] [--render-div 1] [--rolloff 0] [--gain 0.5]
 *          [--telemetry blocks.csv] [--dur seconds] [--stereo]
 *   kykdesk --serve [--gen … | --space f] [--script s.txt] [--loop]
 *
 * Script format: shell/desktop/script.h. Output is mono (the left voice)
 * unless --stereo, or any angle/spread event, is given. Prints the CRC32 of
 * the rendered float bytes: bit-identical runs match.
 */
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <fstream>
#include "kyk_stereo.h"
#include "kyk_world.h"
#include "kyk_worlds.h"
#include "wavio.h"
#include "script.h"
#include "serve.h"

using namespace kyk;
using namespace kykdesk;

int main(int argc, char** argv)
{
    std::string card_dir;   /* a directory standing in for the SD card */
    std::string space_path, script_path, out_path, telem_path;
    bool        gen = false, stereo = false, serve = false, loop = false;
    GenParams   gp;
    int         sr = 48000, block = 24, render_div = 1, rolloff = 0;
    float       gain = 0.23f, slew_ms = 5.f;   /* headroom for the crest factor, see kyk_engine.h */
    double      dur_override = -1;
    float       sharp = 0.f, deadband = 5e-4f;
    int         world_sel = -1;   /* --world N: use a built-in world instead of a lattice */
    std::string resonate_path;    /* --resonate f.kykm: a fitted world (ModalBake), struck by the script */
    for(int i = 1; i < argc; i++)
    {
        std::string a = argv[i];
        auto        next = [&]() -> const char* { return i + 1 < argc ? argv[++i] : ""; };
        if(a == "--card") card_dir = next();
        else if(a == "--space") space_path = next();
        else if(a == "--gen") gen = true;
        else if(a == "--seed") gp.seed = (uint32_t)strtoul(next(), nullptr, 0);
        else if(a == "--N") gp.n = atoi(next());
        else if(a == "--side") gp.side = atoi(next());
        else if(a == "--K") gp.k = atoi(next());
        else if(a == "--P") gp.p = atoi(next());
        else if(a == "--wrap") { int ax = atoi(next()); if(ax >= 0 && ax < kMaxN) gp.topo[ax] = (uint8_t)Topo::Wrap; }
        else if(a == "--world") world_sel = atoi(next());
        else if(a == "--resonate") resonate_path = next();
        else if(a == "--family")
        {
            const std::string f = next();
            gp.family = (f == "field") ? Family::Field : Family::Harmonic;
            if(gp.family == Family::Field) gp.name = "field";   /* the header names the space */
        }
        else if(a == "--rough") gp.rough = (float)atof(next());
        else if(a == "--smooth") gp.smooth = atoi(next());
        else if(a == "--slew") slew_ms = (float)atof(next());
        else if(a == "--sharp") sharp = (float)atof(next());
        else if(a == "--deadband") deadband = (float)atof(next());
        else if(a == "--script") script_path = next();
        else if(a == "--out") out_path = next();
        else if(a == "--telemetry") telem_path = next();
        else if(a == "--sr") sr = atoi(next());
        else if(a == "--block") block = atoi(next());
        else if(a == "--render-div") render_div = atoi(next());
        else if(a == "--rolloff") rolloff = atoi(next());
        else if(a == "--gain") gain = (float)atof(next());
        else if(a == "--dur") dur_override = atof(next());
        else if(a == "--stereo") stereo = true;
        else if(a == "--serve") serve = true;
        else if(a == "--loop") loop = true;
        else { fprintf(stderr, "unknown arg %s\n", a.c_str()); return 2; }
    }
    if(!resonate_path.empty() && space_path.empty()) gen = true;   /* a resonate world needs no lattice; --gen gives the engine one to hold */
    if((!serve && (script_path.empty() || out_path.empty())) || (space_path.empty() && !gen))
    {
        fprintf(stderr, "usage: kykdesk (--space f.kyk | --gen | --resonate f.kykm) --script s.txt --out o.wav [options]\n"
                        "       kykdesk --serve (--space f.kyk | --gen) [--script s.txt] [--loop]\n");
        return 2;
    }

    /* the space blob — owned here, viewed by the core */
    std::vector<uint8_t> blob;
    if(gen)
    {
        blob.resize(Space::BlobSize(gp.n, gp.k, gp.p, gp.side, false));
        if(!BuildLattice(gp, blob.data(), blob.size())) { fprintf(stderr, "generator refused the params\n"); return 1; }
    }
    else
    {
        std::ifstream in(space_path, std::ios::binary);
        if(!in) { fprintf(stderr, "cannot open %s\n", space_path.c_str()); return 1; }
        blob.assign(std::istreambuf_iterator<char>(in), {});
    }
    Space            space;
    const SpaceError se = space.Attach(blob.data(), blob.size());
    if(se != SpaceError::Ok) { fprintf(stderr, "space: %s\n", Space::ErrorName(se)); return 1; }

    Script script;
    bool   have_script = false;
    if(!script_path.empty())
    {
        if(!script.Load(script_path)) { fprintf(stderr, "cannot read %s\n", script_path.c_str()); return 1; }
        have_script = true;
    }
    double total = have_script ? script.total : 1.0;
    if(dur_override > 0) total = dur_override;
    if(script.uses_stereo) stereo = true;

    static StereoEngine eng;   /* two voices, ~40 KB with 1024-sample frames: static, as on the module */
    static World world;
    world.UseLattice(&space);
    if(world_sel >= 0 && world_sel < (int)worlds::kCount)
    {
        static solids::VertexTable vtable;
        if(!worlds::Point((uint8_t)world_sel, world, gp.p, gp.topo, &vtable))
        {
            const size_t n = worlds::Expand((uint8_t)world_sel, gp.n, gp.side, gp.k, gp.p, blob.data(), blob.size());
            if(n && space.Attach(blob.data(), n) == SpaceError::Ok) world.UseLattice(&space);
        }
    }
    static std::vector<uint8_t> resonate_blob;
    if(!resonate_path.empty())
    {
        FILE* rf = fopen(resonate_path.c_str(), "rb");
        if(!rf) { fprintf(stderr, "cannot read %s\n", resonate_path.c_str()); return 1; }
        fseek(rf, 0, SEEK_END); const long n = ftell(rf); fseek(rf, 0, SEEK_SET);
        resonate_blob.resize((size_t)n);
        if(fread(resonate_blob.data(), 1, (size_t)n, rf) != (size_t)n) { fclose(rf); return 1; }
        fclose(rf);
        world.UseResonate(resonate_blob.data(), (uint32_t)resonate_blob.size());
        if(!world.Ready()) { fprintf(stderr, "%s is not a resonate world\n", resonate_path.c_str()); return 1; }
    }
    eng.Init(&world, (float)sr);
    static ResonatorVoice morph_voices[2]; static ResonatorWorld morph_worlds[2];
    eng.L.SetMorphScratch(morph_voices, morph_worlds);
    /* a resonate world's axes — body, velocity, decay, coil — are its
       spin here as on the module, so a script's `pos` and the bridge's
       control frame are the same hands; ACTION 18 still sets the spin
       directly for a block, which is what the wire check needs of it */
    eng.TuneFromControl(true);
    eng.SetGain(gain);
    eng.SetRenderDiv(render_div);
    eng.SetRolloff(rolloff);
    eng.slew_ms = slew_ms;
    eng.sharp   = sharp;
    eng.L.move_eps = deadband;
    eng.R.move_eps = deadband;

    if(serve) return Serve(eng, world, blob, have_script ? &script : nullptr, loop, sr, block, card_dir);

    Player player;
    player.Reset(&script);
    const int    N = space.N();
    const size_t total_samples = (size_t)(total * sr);
    std::vector<float> out(total_samples + (size_t)block, 0.f), outR(total_samples + (size_t)block, 0.f);
    FILE*              telem = telem_path.empty() ? nullptr : fopen(telem_path.c_str(), "w");
    if(telem)
    {
        fprintf(telem, "block,t,f0,kcut");
        for(int a = 0; a < N; a++) fprintf(telem, ",p%d", a);
        for(int j = 0; j < space.P(); j++) fprintf(telem, ",pl%d", j);
        fprintf(telem, "\n");
    }
    for(size_t s = 0; s < total_samples; s += (size_t)block)
    {
        const double t = (double)s / sr;
        player.At(t, eng);
        eng.Process(&out[s], &outR[s], block);
        if(telem)
        {
            fprintf(telem, "%u,%.6f,%.3f,%d", eng.L.Block() - 1, t, eng.L.F0(), eng.L.Kcut());
            for(int a = 0; a < N; a++) fprintf(telem, ",%.5f", eng.L.Position()[a]);
            for(int j = 0; j < space.P(); j++) fprintf(telem, ",%.5f", eng.Payload()[j]);
            fprintf(telem, "\n");
        }
    }
    if(telem) fclose(telem);
    out.resize(total_samples);
    outR.resize(total_samples);
    std::vector<float> wav;
    if(stereo)
    {
        wav.resize(total_samples * 2);
        for(size_t i = 0; i < total_samples; i++) { wav[2 * i] = out[i]; wav[2 * i + 1] = outR[i]; }
    }
    else wav = out;
    if(!kykdesk::WriteWavFloat(out_path, wav.data(), wav.size(), sr, stereo ? 2 : 1)) { fprintf(stderr, "cannot write %s\n", out_path.c_str()); return 1; }
    float peak = 0.f;
    for(float v : wav) peak = std::max(peak, std::abs(v));
    printf("%s: %zu samples @ %d Hz %s, %d-sample blocks, N=%d side=%d K=%d P=%d, peak %.3f, crc32 %08x\n",
           out_path.c_str(), total_samples, sr, stereo ? "stereo" : "mono", block, N, space.Side(), space.K(), space.P(), peak,
           Crc32(reinterpret_cast<const uint8_t*>(wav.data()), wav.size() * 4));
    return 0;
}
