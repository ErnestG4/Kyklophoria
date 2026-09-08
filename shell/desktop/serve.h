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
#include "kyk_telemetry.h"
#include "script.h"

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
    const uint8_t*      blob = nullptr;
    size_t              blob_len = 0;
    kyk::ExtStats      stats;
    bool                host_owns = false;
    float               host_f0 = 110.f, host_c[kyk::kMaxN] = {0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f};

    int Telemetry(uint8_t flags, uint8_t* out, int cap) override
    {
        return eng ? kyk::EncodeTelemetry(*eng, flags, out, cap) : 0;
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
    uint8_t Action(uint8_t op, const uint8_t* args, int len) override
    {
        if(!eng) return 3u;
        switch(op)
        {
            case kyk::kActResetPhase: eng->L.ResetPhase(); eng->R.ResetPhase(); return 0u;
            case kyk::kActRenderDiv: if(len < 1 || args[0] < 1) return 2u; eng->SetRenderDiv(args[0]); return 0u;
            default: return 1u;
        }
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

inline int Serve(kyk::StereoEngine& eng, const uint8_t* blob, size_t blob_len, const Script* script, bool loop,
                 int sr, int block)
{
    using namespace alchemy::hostlink;
    DesktopSource src;
    src.eng = &eng; src.blob = blob; src.blob_len = blob_len;
    src.stats.cycles_budget = (uint32_t)(1e9 * block / sr);   /* desktop "cycles" are nanoseconds */
    kyk::KykExt ext(src);

    /* descriptor */
    char desc[4096];
    const kyk::SpaceHeader& h = eng.SpacePtr()->Header();
    char name[33]; std::memcpy(name, h.name, 32); name[32] = 0;
    const int dlen = snprintf(desc, sizeof(desc),
        "{\"dv\":1,\"module\":{\"id\":\"kyk\",\"name\":\"kyklophoria\",\"fw\":\"0.1.0-m1\",\"git\":\"desktop\",\"sdk\":\"bridge\",\"board\":\"desktop\"},"
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
                        w.Str("kyk"); w.Str("Kyklophoria"); w.Str("0.1.0-m1"); w.Str("desktop"); w.Str("bridge");
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
