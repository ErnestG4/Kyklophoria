/* kyk_tour.h — N worlds and a clock.
 *
 * From the bench: "you can't set a loop of N worlds and constantly morph
 * between them based on a clock and clock divisions." Everything needed for it
 * already existed and none of it was joined up — the engine blends two worlds
 * continuously, the library holds thirty-two of yours beside the twenty-two
 * that ship, and J2 has been labelled Sync since the I/O map was agreed and
 * read by nothing. This is the joining up, and it is deliberately the smallest
 * thing that can be: a sequence, a division, and the arithmetic that says where
 * between two worlds the blend should be right now.
 *
 * ── the ring, which is about time rather than arithmetic ────────────────
 *
 * The engine blends a live world with a morph target. A tour of more than two
 * worlds therefore has to replace one of the two at every step, and the
 * question is not whether the *numbers* line up at the moment of the swap —
 * they do, because at full blend the sound is the target's spectrum, so making
 * that target the new live world and resetting the blend to zero renders the
 * same spectrum. The question is how long the shell has to *load* the world it
 * needs next, because loading is not free: expanding one of the lattice worlds
 * measures about 9 ms on x86 and the M7 is an order of magnitude slower than
 * that on this kind of loop, so a load lands tens of milliseconds after it was
 * asked for.
 *
 * Two roles give it no time at all. Whichever of them you overwrite is either
 * audible now (the live world, weight 1 at full blend) or becomes audible over
 * the coming interval (the far end, weight rising from 0) — measured, a load
 * landing halfway through the interval moves the rendered cycle by 1.9 on a
 * frame that spans about ±2, which is a glitch and not a morph.
 *
 * So three, in a ring: live, target, and a spare nobody is listening to.
 *
 *   step        live        target      spare (being loaded)
 *   ----        ----        ------      --------------------
 *   0           e0          e1          e2
 *   1           e1          e2          e3
 *   2           e2          e3          e4
 *
 * Every step is a pointer rotation. The blend always runs 0 → 1, the world that
 * becomes live is one that finished loading a whole clock interval ago, and the
 * buffer being written is inaudible from the moment it is chosen until the step
 * after next. Nothing has to be fast.
 *
 * A tour of exactly two worlds is the one case that needs no loading ever, so
 * it uses two slots and alternates the blend's direction instead — the same
 * ping-pong, reached as a special case rather than as the general design.
 *
 * ── what a step is *not* continuous in ──────────────────────────────────
 *
 * The magnitudes are exact across a step. The phase spectrum and the frame
 * shapers are not, because the engine takes both from the live world: two
 * worlds that render the frame the same way step seamlessly (measured at
 * 0.000000), and two that do not step audibly — about 1.9 for a shaped world
 * against an unshaped one, 4.6 for sine phase against random. So a tour of
 * worlds that share a phase convention and a shaper set is a morph, and a tour
 * that mixes them is a sequencer. That is a property of the representation
 * rather than of this file: shaping the blend instead of the live world means
 * rendering both ends of it, which is a second inverse FFT per render, and the
 * CPU does not have one. The pair table is in docs/checklist.md.
 *
 * ── pacing ──────────────────────────────────────────────────────────────
 *
 * The blend is paced by the *previous* interval between divided edges, which is
 * exact on a steady clock and one step behind on a changing one. The
 * alternative is not moving at all until the next edge says how long the last
 * one was, which is worse: the whole point is that the sound is in motion
 * between the ticks. A tempo that changes sharply inside a step arrives early
 * or late once and is right again after.
 *
 * `glide` is how much of the interval the travel takes. At 1 the blend never
 * stops moving; at 0.1 it arrives in the first tenth and holds, which is a
 * sequencer with a crossfade rather than a morph. It is floored at a few
 * blocks, because a frame swap at full amplitude is a click and this instrument
 * does not do clicks — the steppiest setting still crossfades over about four
 * milliseconds.
 *
 * Everything here is integer bookkeeping and one divide. No time units: the
 * caller counts blocks, because the shells already do and core does not get to
 * know what a second is.
 */
#pragma once
#include "kyk_types.h"

namespace kyk {

/* Eight. A tour is a phrase, not a song: the wire carries the sequence in one
 * frame, the panel has to be able to say where in it you are, and the page
 * draws it as a row. Somebody who wants sixteen worlds in a loop wants a
 * sequencer, and the module has a clock input rather than a screen. */
constexpr int kTourMax = 8;
/* Live, target, spare. */
constexpr int kTourSlots = 3;

/* What a step asks the shell to do. `world` is named the way the wire names a
 * world everywhere else: an index below worlds::kCount is a built-in, and
 * 0x80 | slot is one of yours. 0xFF means there is nothing to load, which is
 * the two-world case. */
struct TourStep
{
    bool    step  = false;
    uint8_t world = 0xFFu;
    int     slot  = -1;    /* which of the shell's three buffers to load it into */
};

class Tour
{
  public:
    void Clear()
    {
        len_ = 0; at_ = 0; ring_ = 0; up_ = true; count_ = 0; interval_ = 0; last_ = 0; primes_ = 0;
    }
    /* Program it. A sequence shorter than two worlds is not a tour and turns it
     * off, which is also how the page clears one. */
    bool Set(const uint8_t* e, int n, uint8_t div)
    {
        if(n < 0 || n > kTourMax) return false;
        for(int i = 0; i < n; i++) entry_[i] = e[i];
        len_      = (uint8_t)n;
        div_      = div < 1u ? 1u : (div > 64u ? 64u : div);
        at_       = 0;
        ring_     = 0;
        up_       = true;
        count_    = 0;
        interval_ = 0;
        last_     = 0;
        primes_   = 0;
        return true;
    }

    int     Len() const { return len_; }
    uint8_t Div() const { return div_; }
    int     At() const { return at_; }
    bool    Running() const { return len_ >= 2; }
    uint8_t Entry(int i) const { return (uint8_t)(len_ ? entry_[Wrap(i)] : 0xFFu); }
    /* Two worlds need no spare, and a spare they did not need would be reloaded
     * every step for nothing. */
    int Slots() const { return len_ >= 3 ? kTourSlots : 2; }

    /* Which buffer is which, and what each should hold. A shell setting up from
     * scratch loads WorldForSlot(s) into every slot below Slots(), then points
     * the engine at LiveSlot() and TargetSlot(). */
    int     LiveSlot() const { return ring_; }
    int     TargetSlot() const { return (ring_ + 1) % Slots(); }
    int     SpareSlot() const { return Slots() == 3 ? (ring_ + 2) % 3 : -1; }
    uint8_t WorldForSlot(int s) const
    {
        const int n = Slots();
        if(s < 0 || s >= n) return 0xFFu;
        /* Two worlds never move: the buffers hold them for the life of the tour
         * and the travel turns around instead. */
        if(n == 2) return Entry(s);
        /* slot ring_+i holds the world i steps ahead */
        return Entry(at_ + ((s - ring_) + n) % n);
    }
    /* The two ends of the travel, by world rather than by buffer. */
    uint8_t From() const { return up_ ? WorldForSlot(LiveSlot()) : WorldForSlot(TargetSlot()); }
    uint8_t To() const { return up_ ? WorldForSlot(TargetSlot()) : WorldForSlot(LiveSlot()); }
    /* Which end of the blend is which: true when it runs live → target. With
     * three slots it always does; with two it alternates, because there is no
     * spare to rotate through. */
    bool Up() const { return up_; }

    /* A rising edge on the clock. The division is counted here, so a shell hands
     * over every edge it sees and does not have to keep a counter of its own. */
    TourStep Edge(uint32_t block)
    {
        TourStep s;
        if(!Running()) return s;
        if(++count_ < div_) return s;
        count_ = 0;
        return Fire(block);
    }

    /* A step without a clock: free-run, or a host driving the tour by hand.
     * Skips the division, because dividing a rate you set yourself is a knob
     * fighting another knob. */
    TourStep Fire(uint32_t block)
    {
        TourStep s;
        if(!Running()) return s;
        /* Two edges to catch the clock before anything moves. The first says
         * when, the second says how long, and until the interval is known the
         * blend cannot be paced — so a step taken before then is a switch rather
         * than an arrival. Stepping on the first edge would make every tour click
         * the moment it was switched on, which is a worse trade than waiting one
         * beat. Measured: without this the first step moves the rendered cycle by
         * 1.9 on a frame that spans about ±2, and every step after it by nothing
         * at all. */
        if(primes_ < 2u)
        {
            if(primes_ == 1u) interval_ = block - last_;
            last_ = block;
            primes_++;
            return s;
        }
        /* Two edges inside one block is not a tempo, it is a host or a noisy
         * jack, and taking it would freeze the blend at an end — an interval of
         * zero means "not known" everywhere else here. Keep the last estimate
         * that made sense. */
        if(block != last_) interval_ = block - last_;
        last_ = block;
        at_       = Wrap(at_ + 1);
        s.step    = true;
        if(Slots() == 3)
        {
            ring_    = (uint8_t)((ring_ + 1) % 3);
            s.slot   = SpareSlot();
            s.world  = WorldForSlot(s.slot);
        }
        else
        {
            /* Two worlds: the roles swap and nothing is loaded. The blend turns
             * around instead, which keeps the end it is sitting on still. */
            up_ = !up_;
        }
        return s;
    }

    /* Where the blend should be, given the block the caller is on. Rests at the
     * end it has arrived at when the clock stops or before the first edge. */
    float Blend(uint32_t block, float glide) const
    {
        if(!Running() || interval_ == 0u) return up_ ? 0.f : 1.f;
        if(glide < 0.f) glide = 0.f;
        if(glide > 1.f) glide = 1.f;
        uint32_t ramp = (uint32_t)(glide * (float)interval_);
        if(ramp < kMinRamp) ramp = kMinRamp;
        const uint32_t since = block - last_;
        const float    u     = since >= ramp ? 1.f : (float)since / (float)ramp;
        return up_ ? u : 1.f - u;
    }

    /* Blocks. Eight of them is 4 ms at 48 kHz and 24 samples, which is the
     * shortest crossfade that is not a click. */
    static constexpr uint32_t kMinRamp = 8u;

  private:
    int Wrap(int i) const { return len_ ? (i % (int)len_) : 0; }

    uint8_t  entry_[kTourMax] = {0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu, 0xFFu};
    uint8_t  len_             = 0u;
    uint8_t  div_             = 1u;
    uint8_t  at_              = 0u;   /* which entry is live */
    uint8_t  ring_            = 0u;   /* which buffer is live */
    uint8_t  count_           = 0u;   /* edges seen within the division */
    bool     up_              = true;
    uint8_t  primes_          = 0u;   /* clock edges seen: two to catch it */
    uint32_t last_            = 0u;
    uint32_t interval_        = 0u;
};

} // namespace kyk
