/* tour_check — N worlds, a clock, and the claim that a step is not a click.
 *
 * Two halves. The first is the state machine: who is in which role, which role a
 * step is allowed to overwrite, what the division counts, and where the blend is
 * between edges. The second is the one that matters — drive a real Engine
 * through a tour the way a shell drives it and measure the rendered cycle
 * *across* the steps. The ping-pong exists so that number is zero, so the naive
 * alternative is measured here too rather than described.
 */
#include "kyk_tour.h"
#include "kyk_worlds.h"
#include "kyk_engine.h"
#include <cstdio>
#include <cstring>
#include <cmath>

using namespace kyk;

static int  bad = 0;
static void ck(const char* m, bool ok) { printf("  %s %s\n", ok ? "ok  " : "FAIL", m); if(!ok) bad++; }

int main()
{
    char m[160];

    /* ── the state machine ──────────────────────────────────────────────── */
    {
        Tour t;
        ck("nothing programmed is not running", !t.Running() && t.Len() == 0);
        const uint8_t one[1] = {5u};
        t.Set(one, 1, 1u);
        ck("one world is not a tour either", !t.Running());
        TourStep s = t.Edge(100u);
        ck("and an edge on it does nothing", !s.step);

        const uint8_t four[4] = {11u, 13u, 21u, 0x80u | 3u};
        t.Set(four, 4, 1u);
        ck("four worlds is a tour", t.Running() && t.Len() == 4);
        ck("three buffers, and each starts with a world of its own",
           t.Slots() == 3 && t.LiveSlot() == 0 && t.TargetSlot() == 1 && t.SpareSlot() == 2
               && t.WorldForSlot(0) == 11u && t.WorldForSlot(1) == 13u && t.WorldForSlot(2) == 21u);
        ck("and the travel runs from the live world to the target", t.From() == 11u && t.To() == 13u && t.Up());

        /* Twelve steps of a four-world tour: three laps. Each step arrives at the
         * world it was travelling to, aims at the next, and loads the one after
         * that into the buffer nobody is listening to. */
        bool     arrived = true, safe = true, walked = true;
        uint32_t blk = 1000u;
        t.Fire(blk); blk += 500u; t.Fire(blk);   /* two edges to catch the clock */
        for(int i = 0; i < 12; i++)
        {
            const uint8_t going_to = t.To();
            blk += 500u;
            s = t.Fire(blk);
            if(!s.step) arrived = false;
            if(t.From() != going_to) arrived = false;
            if(t.From() != four[(i + 1) % 4]) walked = false;
            /* the buffer being written is neither of the two being heard */
            if(s.slot != t.SpareSlot() || s.slot == t.LiveSlot() || s.slot == t.TargetSlot()) safe = false;
            /* and it is being given the world two steps ahead, which is what
               buys it a whole interval to arrive in */
            if(s.world != t.WorldForSlot(s.slot) || s.world != t.Entry(t.At() + 2)) safe = false;
        }
        ck("twelve steps each arrive where the blend was heading", arrived);
        ck("and walk the sequence, wrapping", walked);
        ck("every step loads the spare, never a buffer being heard, and loads it two steps ahead", safe);

        /* Two worlds is the case that needs no loading at all. */
        const uint8_t two[2] = {13u, 21u};
        Tour p2;
        p2.Set(two, 2, 1u);
        bool noload = true, flips = true;
        p2.Fire(1000u); p2.Fire(1100u);      /* two edges to catch the clock */
        bool up = p2.Up();
        for(int i = 0; i < 6; i++)
        {
            const uint8_t going_to = p2.To();
            const TourStep s2 = p2.Fire(1200u + 100u * (uint32_t)i);
            if(!s2.step || s2.world != 0xFFu || s2.slot != -1) noload = false;
            if(p2.Up() == up) flips = false;
            up = p2.Up();
            if(p2.From() != going_to) flips = false;
        }
        ck("a two-world tour has two buffers and nothing to load, ever", p2.Slots() == 2 && noload);
        ck("it turns the blend around instead, which keeps the end it is sitting on still", flips);

        /* The division counts raw edges. Twenty-four of them at a division of
         * four is six steps, less the two the tour spends catching the clock. */
        t.Set(four, 4, 4u);
        int fired = 0;
        for(int i = 0; i < 24; i++) { blk += 100u; if(t.Edge(blk).step) fired++; }
        std::snprintf(m, sizeof m, "a division of four steps %d times in twenty-four edges", fired);
        ck(m, fired == 4);

        /* Pacing. With a steady 500-block interval and full glide the blend is
         * halfway across at 250 blocks and arrived at 500. */
        t.Set(four, 4, 1u);
        blk = 10000u;
        t.Fire(blk);            /* the first edge only says when */
        blk += 500u;
        t.Fire(blk);            /* the second says how long, and now it can pace */
        const float b0    = t.Blend(blk, 1.f);
        const float bmid  = t.Blend(blk + 250u, 1.f);
        const float b1    = t.Blend(blk + 500u, 1.f);
        const float bwait = t.Blend(blk + 5000u, 1.f);
        std::snprintf(m, sizeof m, "full glide travels evenly: %.3f, %.3f, %.3f and rests at %.3f", b0, bmid, b1, bwait);
        ck(m, b0 == 0.f && std::fabs(bmid - 0.5f) < 0.01 && b1 == 1.f && bwait == 1.f);
        /* A short glide arrives early and holds — a sequencer with a crossfade. */
        const float g0 = t.Blend(blk + 40u, 0.1f), g1 = t.Blend(blk + 60u, 0.1f);
        std::snprintf(m, sizeof m, "a tenth of the interval has arrived by 50 blocks (%.3f at 40, %.3f at 60)", g0, g1);
        ck(m, g0 < 1.f && g1 == 1.f);
        /* And zero glide is still a crossfade, not a switch. */
        const float z0 = t.Blend(blk + 4u, 0.f), z1 = t.Blend(blk + 8u, 0.f);
        std::snprintf(m, sizeof m, "zero glide still crossfades over %u blocks (%.3f at half of it, %.3f at the end)",
                      Tour::kMinRamp, z0, z1);
        ck(m, std::fabs(z0 - 0.5f) < 0.01 && z1 == 1.f);
        /* Before any edge, and after the clock stops, it rests on an end rather
         * than sitting in the middle of nowhere. */
        Tour fresh;
        fresh.Set(four, 4, 1u);
        ck("before the first edge the blend rests at one end", fresh.Blend(0u, 1.f) == 0.f);
    }

    /* ── the rendered cycle across a step ──────────────────────────────── */
    /*
     * Two things have to be true for a clocked tour to be usable, and they are
     * different things.
     *
     * The first is arithmetic: at a step, the end of the blend the sound is
     * sitting on must not move. At full blend the sound *is* the target's
     * spectrum, so making that the live world and resetting the blend to zero
     * renders the same spectrum — for two worlds that render the frame the same
     * way, exactly.
     *
     * The second is time, and it is the one that decides the design. Loading a
     * world takes tens of milliseconds on the module, so a step's load lands
     * well after the step asked for it. The ring gives it a whole clock interval
     * by keeping a spare buffer nobody is listening to; two buffers give it none,
     * because whichever one you overwrite is either audible now or becomes
     * audible as the blend travels.
     *
     * Both are measured, with the load deliberately landing halfway through the
     * interval — which is about what 100 ms of lattice expansion against a
     * sixteenth at 120 bpm looks like.
     */
    enum Mode { kRing, kTwo };
    auto run = [&](Mode mode, uint32_t delay, double* worst_step, double* worst_mid) {
        /* Four worlds that render the frame the same way — sine phase, no frame
         * shapers — which is the case a step is seamless for. The pairs it is not
         * seamless for are measured in docs/checklist.md. */
        const uint8_t seq[4] = {worlds::kLock, worlds::kUnison, worlds::kPlate, worlds::kBar};
        static World  buf[kTourSlots];
        static solids::VertexTable vt[kTourSlots];
        Tour t;
        t.Set(seq, 4, 1u);
        const int slots = mode == kRing ? t.Slots() : 2;
        for(int q = 0; q < slots; q++)
            worlds::Point(mode == kRing ? t.WorldForSlot(q) : seq[q], buf[q], 8, nullptr, &vt[q]);

        static Engine eng;
        int live = 0, targ = 1;
        eng.Init(&buf[live], 48000.f);
        eng.render_div = 1;
        eng.gain       = 1.f;
        eng.SetF0(110.f);
        const float p[kMaxN] = {0.37f, 0.62f, 0.28f, 0.71f, 0.f, 0.f};
        eng.SetPosition(p, 4);
        eng.SetMorph(&buf[targ], 0.f);
        float out[24];

        const uint32_t per = 40u;   /* blocks between steps */
        uint32_t       blk = 0u;
        double         prev[kFrame];
        bool           have = false;
        *worst_step = 0; *worst_mid = 0;
        /* loads in flight, at most one per buffer */
        int      pend_slot[kTourSlots] = {-1, -1, -1};
        uint8_t  pend_world[kTourSlots] = {0xFFu, 0xFFu, 0xFFu};
        uint32_t pend_at[kTourSlots] = {0u, 0u, 0u};
        for(int step = 0; step < 24; step++)
        {
            for(uint32_t i = 0; i < per; i++)
            {
                for(int q = 0; q < kTourSlots; q++)
                    if(pend_slot[q] >= 0 && blk >= pend_at[q])
                    {
                        worlds::Point(pend_world[q], buf[pend_slot[q]], 8, nullptr, &vt[pend_slot[q]]);
                        if(pend_slot[q] == live) eng.SetWorld(&buf[live]);
                        pend_slot[q] = -1;
                    }
                eng.SetMorph(&buf[targ], t.Blend(blk, 1.f));
                eng.Process(out, 24);
                blk++;
                double cur[kFrame];
                for(int q = 0; q < kFrame; q++) cur[q] = eng.Frame()[q];
                if(have)
                {
                    double d = 0;
                    for(int q = 0; q < kFrame; q++) d = std::fmax(d, std::fabs(cur[q] - prev[q]));
                    /* everything that is not the smooth travel: the step itself,
                       and the block a late load lands in */
                    if(i == 0 || i == delay) *worst_step = std::fmax(*worst_step, d);
                    else *worst_mid = std::fmax(*worst_mid, d);
                }
                std::memcpy(prev, cur, sizeof prev);
                have = true;
            }
            const TourStep s = t.Fire(blk);
            if(mode == kRing)
            {
                live = t.LiveSlot();
                targ = t.TargetSlot();
                eng.SetWorld(&buf[live]);
                eng.SetMorph(&buf[targ], t.Blend(blk, 1.f));
                if(s.world != 0xFFu && s.slot >= 0)
                {
                    pend_slot[s.slot]  = s.slot;
                    pend_world[s.slot] = s.world;
                    pend_at[s.slot]    = blk + delay;
                }
            }
            else
            {
                /* The obvious two-buffer version: the world just arrived at
                   becomes live and the next one becomes the target, and both
                   have to be fetched now. */
                live = 0; targ = 1;
                pend_slot[0] = 0; pend_world[0] = t.Entry(t.At()); pend_at[0] = blk + delay;
                pend_slot[1] = 1; pend_world[1] = t.Entry(t.At() + 1); pend_at[1] = blk + delay;
            }
        }
    };

    double rs = 0, rm = 0, ts = 0, tm = 0;
    run(kRing, 20u, &rs, &rm);
    run(kTwo, 20u, &ts, &tm);
    /* The step is not a special moment: it moves the cycle by no more than the
       blend's own per-block travel, which is what "constantly morphing" means.
       Exactly zero is not the claim and would be the wrong one — the blend is at
       (per-1)/per when the edge arrives and resets to 0 with the roles rotated,
       so one travel step's worth of motion is left over, and that is motion in
       the direction it was already going. */
    std::snprintf(m, sizeof m,
                  "the ring: a step and a late load move the cycle %.4f, no more than the travel's own %.4f",
                  rs, rm);
    ck(m, rs <= rm * 1.05);
    std::snprintf(m, sizeof m,
                  "two buffers instead: %.4f at those moments, %.0fx the travel and %.0fx the ring — the cost of "
                  "overwriting a world somebody is about to hear",
                  ts, tm > 0 ? ts / tm : 0.0, rs > 0 ? ts / rs : 0.0);
    ck(m, ts > 10.0 * rs);

    if(bad) printf("tour_check: %d FAILURES\n", bad);
    else    printf("tour_check: all passed\n");
    return bad ? 1 : 0;
}
