/* slot_check — the morph target's slot is never read while it is rewritten
 * (shell/common/kyk_slot.h). The audio callback is simulated as an interrupt
 * between every write of the rebuild: whenever it finds the index published,
 * the slot must be whole. Before, the index stayed valid throughout and the
 * callback blended towards a half-written world for the 10 ms of a tabulated
 * world's expansion. */
#include "kyk_slot.h"
#include <cstdio>

using namespace kyk;
static int fails = 0;
#define CHECK(cond, ...) do { if(!(cond)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while(0)

int main()
{
    /* the slot: eight words that are whole when all carry the same world's mark */
    volatile uint8_t idx = 0xFFu;
    int slot[8];
    for(int& v : slot) v = 3;
    idx = 3;
    int torn = 0, reads = 0;
    auto isr = [&]() {
        if(idx == 0xFFu) return;
        reads++;
        for(int v : slot) if(v != (int)idx) { torn++; return; }
    };
    /* a rebuild to world 7, interrupted after every word, and a failed one */
    const bool ok = RebuildSlot(idx, 7, [&]() { for(int& v : slot) { v = 7; isr(); } return true; });
    isr();
    CHECK(ok && idx == 7, "the rebuild did not publish world 7 (%d)", idx);
    CHECK(torn == 0, "the callback read a half-written slot %d times", torn);
    const bool bad = RebuildSlot(idx, 9, [&]() { slot[0] = 9; isr(); return false; });
    isr();
    CHECK(!bad && idx == 0xFFu, "a failed rebuild published something (%d)", idx);
    CHECK(torn == 0, "the callback read a half-written slot %d times", torn);
    printf("  the morph target's slot: withdrawn while it is rewritten, published whole (%d reads, %d torn)\n", reads, torn);
    if(fails) { printf("slot_check: %d FAILED\n", fails); return 1; }
    printf("slot_check: ok\n");
    return 0;
}
