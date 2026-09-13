/* serve.h — `kykdesk --serve`: the engine in real time behind HostLink on
 * stdio, so the web page runs headless through tools/bridge/bridge.mjs.
 *
 * Wire: HostLink v1 frames (SDK frame.h) on stdin/stdout. Standard
 * commands answered here: HELLO (0x01) and GET_DESCRIPTOR (0x02); 0x60–0x6F
 * go to the shared KykExt (shell/common/kyk_ext.h). The engine advances by
 * the wall clock at 48 kHz / 24-sample blocks, driven by the script (looping
 * with --loop) until the host sends SET_CONTROL, after which the host owns
 * the controls. Audio is discarded; this is a telemetry source.
 */
#pragma once
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include "alchemy/host_link/frame.h"
#include "alchemy/host_link/wire.h"
#include "kyk_ext.h"
#include "kyk_worldrx.h"
#include "kyk_aim.h"
#include "kyk_telemetry.h"
#include "script.h"
#include "kyk_worlds.h"

namespace kykdesk {

static const char* kIoMapJson =
    "[{\"jack\":\"J1\",\"id\":\"fm\",\"name\":\"FM In\",\"sig\":\"audio-in\"},"
    "{\"jack\":\"J2\",\"id\":\"sync\",\"name\":\"Sync\",\"sig\":\"trig\"},"
    "{\"jack\":\"J3\",\"id\":\"voct\",\"name\":\"V/Oct\",\"sig\":\"voct\"},"
    "{\"jack\":\"J4\",\"id\":\"pos0\",\"name\":\"Position 0\",\"sig\":\"cv-bi\"},"
    "{\"jack\":\"J5\",\"id\":\"pos1\",\"name\":\"Position 1\",\"sig\":\"cv-bi\"},"
    "{\"jack\":\"J6\",\"id\":\"pos2\",\"name\":\"Position 2\",\"sig\":\"cv-bi\"},"
    "{\"jack\":\"J7\",\"id\":\"pos3\",\"name\":\"Position 3\",\"sig\":\"cv-bi\"},"
    "{\"jack\":\"J8\",\"id\":\"cv_a\",\"name\":\"CV Out A\",\"sig\":\"cv-uni\"},"
    "{\"jack\":\"J9\",\"id\":\"out_l\",\"name\":\"Out L\",\"sig\":\"audio-out\"},"
    "{\"jack\":\"J10\",\"id\":\"out_r\",\"name\":\"Out R\",\"sig\":\"audio-out\"}]";

class DesktopSource : public kyk::ExtSource
{
public:
    kyk::StereoEngine* eng  = nullptr;
    kyk::World*          world = nullptr;   /* the live one */
    /* 0xFF: what is live came from --gen or --space and is not one of the
     * built-in worlds. Reporting 0 instead made the page draw the Braids
     * terrain over a lattice it was not built from. */
    static constexpr uint8_t kNoWorld = 0xFFu;
    uint8_t              world_idx = kNoWorld;
    /* Its own buffer for expanding tabulated worlds, sized for the largest,
     * so switching never disturbs whatever space was loaded at startup. */
    std::vector<uint8_t>      scratch;
    kyk::Space                scratch_space;
    kyk::solids::VertexTable  vtable;
    const uint8_t*      blob = nullptr;
    size_t              blob_len = 0;
    kyk::ExtStats      stats;
    bool                host_owns = false;
    float               host_f0 = 110.f, host_c[kyk::kMaxN] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

    int Telemetry(uint8_t flags, uint8_t* out, int cap) override
    {
        return eng ? kyk::EncodeTelemetry(*eng, flags, out, cap, 0u, morphIdx, mute,
                                          nullptr, world_idx) : 0;
    }
    bool SpaceInfo(kyk::SpaceHeader& h, uint32_t& crc, uint16_t& stride) override
    {
        if(!eng || !eng->SpacePtr()) return false;
        h      = eng->SpacePtr()->Header();
        crc    = kyk::Crc32(blob, blob_len);
        stride = (uint16_t)eng->SpacePtr()->Stride();
        return true;
    }
    bool Cell(uint32_t idx, uint8_t* mags, float* payload, int& k, int& p) override
    {
        const kyk::Space* s = eng ? eng->SpacePtr() : nullptr;
        if(!s || idx >= s->PointCount()) return false;
        k = s->K(); p = s->P();
        for(int i = 0; i < k; i++) mags[i] = kyk::detail::MagToU8(s->Mags(idx)[i]);
        for(int j = 0; j < p; j++) payload[j] = s->Payload(idx)[j];
        return true;
    }
    void Stats(kyk::ExtStats& s) override { s = stats; s.render_div = (uint8_t)(eng ? eng->L.render_div : 1); }
    /* A user world arriving from the page. The desktop shell keeps it in a
     * World of its own and switches to it, which is what makes the whole path
     * testable without hardware. */
    kyk::World                morphWorld;
    kyk::solids::VertexTable  morphTable;
    /* A third, for answering GET_BASIS without disturbing either of the two
       that have a world pointing into them. */
    kyk::solids::VertexTable  basisTable;
    uint8_t                   morphIdx = 0xFFu;
    uint16_t                  mute = 0u;
    float                     rateWas[kyk::kMaxPlanes] = {0.f};
    /* How far this shell blends when a morph target is set.
     *
     * Not the module's default and not pretending to be: the module's is a
     * knob, stored at zero so that arming a target does not move the sound on
     * its own (shell/alchemy/main.cpp). This shell has no knobs at all, so a
     * fixed half-turn is what makes the blend reachable from a test — with a
     * zero here the morph path could not be exercised without hardware. */
    float                     morphAmt = 0.5f;
    kyk::WorldReceiver rx;
    kyk::World         userWorld;
    char               userName[kyk::kUserNameLen + 1] = {0};

    uint8_t PutWorld(uint32_t total, uint32_t off, const uint8_t* data, int len) override
    {
        const uint8_t st = rx.Take(total, off, data, len);
        if(st != 0u || !rx.Done()) return st;
        /* Parse into scratch first, then copy over the live world.
         *
         * Two reasons, both learned the hard way. Repointing the engine at a
         * different World object races the audio thread — kActSelectWorld
         * rebuilds *world in place for exactly that reason, and doing
         * otherwise made the very next telemetry request come back empty. And
         * UseUserWorld leaves the destination None when it refuses, so parsing
         * straight into the live world would let a corrupt file silence a
         * module that was playing perfectly well. */
        const kyk::UserError e =
            userWorld.UseUserWorld(rx.Blob(), rx.Size(), 8, nullptr, userName);
        rx.Reset();
        if(e != kyk::UserError::Ok) return 1u;
        if(!eng) return 3u;
        *world = userWorld;
        /* And tell the engine, which is the whole point of the call.
         *
         * This was missing, and it is the fifth time this codebase has grown
         * the same bug: state derived from a world, invalidated on everything
         * except the world's *contents* changing. The pointer does not move
         * here, so nothing looked wrong — but the phase convention, the
         * rendered-position cache, the band-limit hold and the morph aim
         * offset all still described the world that had just been replaced.
         * A sine-phase world sent from the page rendered at whatever
         * convention was live before it, which is exactly the defect
         * kyk_engine.h's SetWorld comment was written about.
         *
         * kActSelectWorld has always done this (it rebuilds *world in place
         * and then calls SetWorld with the same pointer); the send path simply
         * never did. shell/alchemy/main.cpp gets it right too, so this was the
         * desktop shell alone — which is worse than it sounds, because the
         * desktop shell is what the whole suite drives, so the send path was
         * being tested against an engine that was ignoring the send. */
        eng->SetWorld(world);
        world_idx = kNoWorld;
        return 0u;
    }

    uint8_t Action(uint8_t op, const uint8_t* args, int len) override
    {
        if(!eng) return 3u;
        switch(op)
        {
            case kyk::kActResetPhase: eng->L.ResetPhase(); eng->R.ResetPhase(); return 0u;
            case kyk::kActRenderDiv: if(len < 1 || args[0] < 1) return 2u; eng->SetRenderDiv(args[0]); return 0u;
            case kyk::kActAimMorph:
            {
                if(len >= 1 && args[0] == 0u)
                {
                    const float zero[kyk::kMaxN] = {0.f, 0.f, 0.f, 0.f, 0.f, 0.f};
                    eng->L.SetMorphOffset(zero, kyk::kMaxN);
                    eng->R.SetMorphOffset(zero, kyk::kMaxN);
                    return 0u;
                }
                if(morphIdx == 0xFFu) return 1u;
                const kyk::World* live = eng->L.WorldPtr();
                if(!live || !live->Ready() || !morphWorld.Ready()) return 1u;
                float p0[kyk::kMaxN], off[kyk::kMaxN];
                live->Fold(eng->Control(), p0);
                AimSearch(*live, morphWorld, p0, eng->L.sharp, off);
                eng->L.SetMorphOffset(off, kyk::kMaxN);
                eng->R.SetMorphOffset(off, kyk::kMaxN);
                return 0u;
            }
            case kyk::kActMotionMute:
            {
                if(len < 2) return 2u;
                const uint16_t was = mute;
                mute = (uint16_t)(args[0] | (args[1] << 8));
                /* Applied, not merely recorded. The desktop shell has no knobs
                 * to re-read, so it keeps the rates it was told and re-applies
                 * them through the mask — otherwise the mute would be a number
                 * the module reports and does not act on, which is exactly the
                 * kind of thing a test passes and a player notices. */
                const int planes = kyk::Rotation::PlaneCount(eng->L.WorldPtr()
                                                             ? eng->L.WorldPtr()->N() : 4);
                for(int p = 0; p < planes; p++)
                {
                    if(mute & (1u << p)) { if(!(was & (1u << p))) rateWas[p] = eng->rot.Rate(p);
                                           eng->rot.SetRate(p, 0.f); }
                    else if(was & (1u << p)) eng->rot.SetRate(p, rateWas[p]);
                }
                if(mute & kyk::kMuteKepler) eng->kepler.Stop();
                return 0u;
            }
            case kyk::kActPhase:
                if(len < 1 || args[0] > 2) return 2u;
                eng->L.SetPhaseOverride(args[0] == 2 ? kyk::World::Phase::Cosine : kyk::World::Phase::Sine,
                                        args[0] != 0);
                eng->R.SetPhaseOverride(args[0] == 2 ? kyk::World::Phase::Cosine : kyk::World::Phase::Sine,
                                        args[0] != 0);
                return 0u;
            case kyk::kActMorphWorld:
            {
                if(len < 1) return 2u;
                if(args[0] == 0xFFu) { eng->SetMorph(nullptr, 0.f); morphIdx = 0xFFu; return 0u; }
                if(args[0] >= kyk::worlds::kCount) return 2u;
                if(!kyk::worlds::IsAnalytic(args[0])) return 2u;   /* formula worlds only here */
                if(!kyk::worlds::Point(args[0], morphWorld, 8, nullptr, &morphTable)) return 3u;
                eng->SetMorph(&morphWorld, morphAmt);
                morphIdx = args[0];
                return 0u;
            }
            case kyk::kActSelectWorld:
            {
                if(len < 1 || args[0] >= kyk::worlds::kCount) return 2u;
                const uint8_t i = args[0];
                if(kyk::worlds::IsAnalytic(i))
                {
                    if(!kyk::worlds::Point(i, *world, 8, nullptr, &vtable)) return 2u;
                }
                else
                {
                    const size_t need = kyk::Space::BlobSize(4, 64, 8, 8, false);
                    if(scratch.size() < need) scratch.resize(need);
                    const size_t n = kyk::worlds::Expand(i, 4, 8, 64, 8, scratch.data(), scratch.size());
                    if(n == 0 || scratch_space.Attach(scratch.data(), n) != kyk::SpaceError::Ok) return 3u;
                    world->UseLattice(&scratch_space);
                }
                eng->SetWorld(world);
                world_idx = i;
                return 0u;
            }
            default: return 1u;
        }
    }
    int Worlds(uint8_t& count, uint8_t& current, const char** names, const char** notes,
               uint8_t* kinds, int max) override
    {
        count   = (uint8_t)(kyk::worlds::kCount < max ? kyk::worlds::kCount : max);
        current = world_idx;
        for(int i = 0; i < (int)count; i++)
        {
            names[i] = kyk::worlds::Get((uint8_t)i).name;
            notes[i] = kyk::worlds::Get((uint8_t)i).note;
            kinds[i] = (uint8_t)kyk::worlds::Get((uint8_t)i).kind;
        }
        return count;
    }

    int Basis(uint8_t w, uint32_t offset, uint8_t* out, int max, uint32_t& total) override
    {
        total = 0;
        if(!kyk::worlds::IsAnalytic(w)) return 0;
        kyk::World tmp;
        /* Its own table, not the live world's.
         *
         * VertexField holds raw pointers into a VertexTable, so building a
         * world into `vtable` rewrites the waveforms of whatever world is
         * already pointing at it — and the live world built by
         * kActSelectWorld points at exactly that. A *read* of another world's
         * formula silently changed the sound of the one playing: measured over
         * the wire with the 24-cell live, a GET_BASIS for the tesseract moved
         * partials by up to 22 magnitude steps, about 8.8 dB.
         *
         * There has been a separate `morphTable` beside this for the same
         * reason since morph targets were added. This one was missed. */
        if(!kyk::worlds::Point(w, tmp, 8, nullptr, &basisTable)) return 0;
        /* A vertex world has no formula to send, and falling through to the
         * eigen branch reads an EigenBasis whose mean and comp pointers were
         * never set. The module answers UNSUPPORTED here; so should this. */
        if(tmp.Which() == kyk::World::Kind::Vertices) return 0;
        if(tmp.Which() == kyk::World::Kind::Bend)
        {
            uint8_t blob[kyk::kBendBlobBytes];
            total = (uint32_t)kyk::BendBlob(tmp.Bend(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(tmp.Which() == kyk::World::Kind::Modal)
        {
            uint8_t blob[kyk::kModalBlobBytes];
            total = (uint32_t)kyk::ModalBlob(tmp.Modal(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(tmp.Which() == kyk::World::Kind::Lock)
        {
            uint8_t blob[kyk::kLockBlobBytes];
            total = (uint32_t)kyk::LockBlob(tmp.Lock(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(tmp.Which() == kyk::World::Kind::Unison)
        {
            uint8_t blob[kyk::kUnisonBlobBytes];
            total = (uint32_t)kyk::UnisonBlob(tmp.Unison(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(tmp.Which() == kyk::World::Kind::Table)
        {
            uint8_t blob[kyk::kShapeBlobBytes];
            total = (uint32_t)kyk::ShapeBlob(tmp.Shapes(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(tmp.Which() == kyk::World::Kind::Formant)
        {
            uint8_t blob[kyk::kFormantBlobBytes];
            total = (uint32_t)kyk::FormantBlob(tmp.Formant(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        if(tmp.Which() == kyk::World::Kind::Fm)
        {
            uint8_t blob[kyk::kFmBlobBytes];
            total = (uint32_t)kyk::FmBlob(tmp.Fm(), blob);
            if(offset >= total) return 0;
            uint32_t n = total - offset;
            if(n > (uint32_t)max) n = (uint32_t)max;
            std::memcpy(out, blob + offset, n);
            return (int)n;
        }
        const kyk::EigenBasis& b = tmp.Basis();
        std::vector<uint8_t>   raw;
        raw.push_back((uint8_t)b.n);
        raw.push_back((uint8_t)b.k);
        auto put = [&](float f) { uint8_t t[4]; std::memcpy(t, &f, 4); for(int i = 0; i < 4; i++) raw.push_back(t[i]); };
        put(b.extent);
        put(b.floor_);
        for(int i = 0; i < b.k; i++) put(b.mean[i]);
        for(int a = 0; a < b.n; a++)
            for(int i = 0; i < b.k; i++) put(b.comp[(size_t)a * b.k + i]);
        total = (uint32_t)raw.size();
        if(offset >= total) return 0;
        uint32_t n = total - offset;
        if(n > (uint32_t)max) n = (uint32_t)max;
        std::memcpy(out, raw.data() + offset, n);
        return (int)n;
    }

    uint8_t SetControl(float f0, const float* c, int n, const float* angles, int planes, float spread) override
    {
        if(!eng) return 3u;
        host_owns = true;
        host_f0   = f0;
        for(int a = 0; a < n; a++) host_c[a] = c[a];
        for(int p = 0; p < planes && p < eng->rot.Planes(); p++) eng->rot.SetAngle(p, angles[p]);
        eng->spread = spread;
        return 0u;
    }
};

inline double Now()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

inline int Serve(kyk::StereoEngine& eng, kyk::World& world, std::vector<uint8_t>& blob,
                 const Script* script, bool loop, int sr, int block)
{
    using namespace alchemy::hostlink;
    DesktopSource src;
    src.eng = &eng; src.blob = blob.data(); src.blob_len = blob.size();
    src.world = &world;
    src.stats.cycles_budget = (uint32_t)(1e9 * block / sr);   /* desktop "cycles" are nanoseconds */
    kyk::KykExt ext(src);

    /* descriptor */
    char desc[4096];
    kyk::SpaceHeader h{};
    char name[33] = "analytic";
    if(eng.SpacePtr()) { h = eng.SpacePtr()->Header(); std::memcpy(name, h.name, 32); name[32] = 0; }
    else { h.n = (uint8_t)eng.WorldPtr()->N(); h.side = 0; h.k = (uint8_t)eng.WorldPtr()->K(); h.p = (uint8_t)eng.WorldPtr()->P(); }
    const int dlen = snprintf(desc, sizeof(desc),
        "{\"dv\":1,\"module\":{\"id\":\"kyk\",\"name\":\"kyklophoria\",\"fw\":\"" KYK_FW_VERSION "\",\"git\":\"desktop\",\"sdk\":\"bridge\",\"board\":\"desktop\"},"
        "\"schemaHash\":0,\"size\":0,\"components\":[],%s,\"space\":{\"name\":\"%s\",\"n\":%d,\"side\":%d,\"k\":%d,\"p\":%d},\"iomap\":%s}",
        ext.DescriptorRootJson(), name, h.n, h.side, h.k, h.p, kIoMapJson);
    const uint32_t dcrc = kyk::Crc32(reinterpret_cast<const uint8_t*>(desc), (size_t)dlen);

    Player player;
    player.Reset(script);
    fcntl(0, F_SETFL, fcntl(0, F_GETFL) | O_NONBLOCK);
    FrameParser parser;
    static uint8_t dec[kMaxDecoded], wire[kMaxWire];
    FrameWriter    w(dec);
    static float   outL[4096], outR[4096];
    const double   t0     = Now();
    double         t_done = 0;           /* engine time rendered so far (absolute) */
    double         script_t0 = 0;        /* engine time at which the current script pass began */
    double         acc_ns = 0; uint32_t acc_n = 0; double win_t = t0;
    fprintf(stderr, "kykdesk --serve: %s N=%d side=%d K=%d P=%d, %d Hz / %d, script %s%s\n", name, h.n, h.side, h.k, h.p, sr, block,
            script ? "yes" : "none", loop ? " (loop)" : "");
    for(;;)
    {
        /* advance the engine to the wall clock */
        const double now = Now() - t0;
        int guard = 0;
        while(t_done < now && guard++ < 4000)
        {
            double st = t_done - script_t0;
            if(script && loop && script->total > 0 && st >= script->total)
            {
                /* wrap: restart the script, engine state carries over */
                player.Reset(script);
                script_t0 += script->total;
                st = t_done - script_t0;
            }
            if(src.host_owns) { eng.SetF0(src.host_f0); eng.SetControl(src.host_c, kyk::kMaxN); }
            else player.At(st, eng);
            const double b0 = Now();
            eng.Process(outL, outR, block);
            const double ns = (Now() - b0) * 1e9;
            src.stats.cycles_last = (uint32_t)ns;
            if(ns > src.stats.cycles_max) src.stats.cycles_max = (uint32_t)ns;
            acc_ns += ns; acc_n++;
            if(src.stats.cycles_avg == 0) src.stats.cycles_avg = (uint32_t)ns;   /* before the first window closes */
            if(ns > 1e9 * block / sr) src.stats.overruns++;
            t_done += (double)block / sr;
        }
        if(Now() - win_t >= 1.0 && acc_n) { src.stats.cycles_avg = (uint32_t)(acc_ns / acc_n); acc_ns = 0; acc_n = 0; win_t = Now(); src.stats.cycles_max = 0; }

        /* serve the link */
        uint8_t inbuf[512];
        ssize_t nr;
        while((nr = read(0, inbuf, sizeof(inbuf))) > 0)
        {
            for(ssize_t i = 0; i < nr; i++)
            {
                ParsedFrame f;
                if(!parser.Push(inbuf[i], f)) continue;
                if(!f.ok)
                {
                    w.Begin(kErrType, f.seq); w.U8(10u);   /* FRAME_ERROR */
                }
                else
                {
                    w.Begin((uint8_t)(f.type | kRespFlag), f.seq);
                    if(f.type == 0x01u)
                    {
                        w.U8(0u); w.U8(1u); w.U8(0u); w.U8(0u); w.U8(0u);
                        for(int k = 0; k < 12; k++) w.U8(0u);
                        w.U32(0u); w.U32(0u); w.U32((uint32_t)dlen); w.U32(dcrc);
                        w.U16(kMaxBody); w.U16(0u);
                        w.Str("kyk"); w.Str("Kyklophoria"); w.Str(KYK_FW_VERSION); w.Str("desktop"); w.Str("bridge");
                    }
                    else if(f.type == 0x02u)
                    {
                        if(f.len < 6) { w.U8(2u); }
                        else
                        {
                            uint32_t off; uint16_t maxlen;
                            std::memcpy(&off, f.body, 4); std::memcpy(&maxlen, f.body + 4, 2);
                            if(off > (uint32_t)dlen) { w.U8(2u); }
                            else
                            {
                                uint32_t n = (uint32_t)dlen - off;
                                if(n > maxlen) n = maxlen;
                                if(n > kMaxBody - 7u) n = kMaxBody - 7u;
                                w.U8(0u); w.U32(off); w.U16((uint16_t)n);
                                w.Bytes(reinterpret_cast<const uint8_t*>(desc) + off, n);
                            }
                        }
                    }
                    else if(f.type >= ext.FirstCmd() && f.type <= ext.LastCmd()) ext.Handle(f, w, (uint32_t)(now * 1000));
                    else w.U8(1u);   /* UNSUPPORTED */
                }
                const size_t wl = w.Encode(wire);
                size_t       done = 0;
                while(done < wl) { const ssize_t k = write(1, wire + done, wl - done); if(k <= 0) return 0; done += (size_t)k; }
            }
        }
        if(nr == 0) return 0;   /* stdin closed: the bridge went away */
        pollfd pfd = {0, POLLIN, 0};
        poll(&pfd, 1, 1);
    }
}

} // namespace kykdesk
