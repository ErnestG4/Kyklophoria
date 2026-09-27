/* regions_check — the resonate world's SDRAM region policy
 * (shell/common/kyk_regions.h): four of 4 MB and twenty of 2 MB, as
 * shell/alchemy/main.cpp has them, the
 * smallest free one a world fits, the slot's own first. It was eight of 4 MB,
 * and a ninth world did not load whichever slot it went to. */
#include "kyk_regions.h"
#include <cstdio>
#include <vector>

using namespace kyk;
static int fails = 0;
#define CHECK(cond, ...) do { if(!(cond)) { fails++; printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); } } while(0)

int main()
{
    constexpr uint32_t MB = 1u << 20;
    std::vector<uint32_t> caps; for(int i = 0; i < 4; i++) caps.push_back(4 * MB); for(int i = 0; i < 20; i++) caps.push_back(2 * MB);
    const int n = (int)caps.size();
    std::vector<uint8_t> owner(n, 0);
    /* the card as it is: 24 worlds — the families and the layered pianos over
       2 MB, the rest under. Load them in card order until nothing fits */
    const double sizes[24] = {0.47, 1.51, 2.48, 1.23, 2.42, 0.57, 0.10, 1.33, 0.97, 1.08, 1.28, 0.57, 1.89, 0.52, 0.35, 0.16, 0.87, 0.88, 0.80, 0.96, 1.19, 0.56, 0.39, 0.14};
    int loaded = 0;
    for(double s : sizes)
    {
        const int r = PickRegion((size_t)(s * MB), caps.data(), owner.data(), n, -1);
        if(r < 0) break;
        owner[r] = 1; loaded++;
    }
    CHECK(loaded == 24, "only %d of the card's 24 worlds loaded at once (it was 8)", loaded);
    /* a small world takes a small region while one is free */
    std::vector<uint8_t> o2(n, 0);
    const int r1 = PickRegion(100 * 1024, caps.data(), o2.data(), n, -1);
    CHECK(r1 >= 4, "a 100 KB world took a 4 MB region (%d) with 2 MB ones free", r1);
    /* a big one a big region; one bigger than any region, none */
    const int r2 = PickRegion(3 * MB, caps.data(), o2.data(), n, -1);
    CHECK(r2 >= 0 && r2 < 4, "a 3 MB world got region %d", r2);
    CHECK(PickRegion(5 * MB, caps.data(), o2.data(), n, -1) < 0, "a 5 MB world was given a region");
    /* the slot's own region first, when the world fits it */
    o2[10] = 1;
    CHECK(PickRegion(1 * MB, caps.data(), o2.data(), n, 10) == 10, "a reload did not keep the slot's own region");
    /* and a world that has outgrown it goes elsewhere */
    CHECK(PickRegion(3 * MB, caps.data(), o2.data(), n, 10) < 4, "a world that outgrew its 2 MB region was put back in it");
    /* the small regions full: a small world takes a big one */
    std::vector<uint8_t> o3(n, 0); for(int r = 4; r < n; r++) o3[r] = 1;
    CHECK(PickRegion(100 * 1024, caps.data(), o3.data(), n, -1) < 4, "a small world found no room with every big region free");
    printf(fails ? "regions_check: %d FAILED\n" : "regions_check: ok — the card's 24 worlds, %d of them loaded at once (it was 8)\n", fails ? fails : loaded);
    return fails ? 1 : 0;
}
