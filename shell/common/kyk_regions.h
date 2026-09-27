/* kyk_regions.h — which SDRAM region a resonate world is read into.
 *
 * A resonate world (.kykm) is read off the card into a region of SDRAM and
 * played where it lies. The regions were eight of 4 MB each, and the ninth
 * world would not load whichever slot it went to (Combust, 2026-09-23: "Only 8
 * world slots can be filled then others fail. It doesn't matter which
 * numerically"). Most worlds are far smaller — a Wurlitzer 100 KB, a grand
 * 1.2 MB — and only the families and the layered pianos need 4, so the
 * regions come in two sizes (shell/alchemy/main.cpp: eight of 4 MB, twelve
 * of 2 MB, 56 MB of the 64) and a world takes the smallest free one it fits.
 *
 * Pure, so the suite can hold it to that (tests/regions_check.cpp): the
 * module's card path is the only other caller.
 *
 *   caps[r]   the bytes region r holds
 *   owner[r]  nonzero if region r is taken
 *   current   the region the slot already holds, or -1
 *
 * Returns the region to read into, or -1 if none fits. The slot's own region
 * first if the world fits it (a reload changes nothing else); else the
 * smallest free region that fits, the lowest-numbered among equals. The
 * caller frees `current` only once the new world has loaded, so a load that
 * fails leaves the slot as it was. */
#pragma once
#include <cstdint>
#include <cstddef>

namespace kyk {

inline int PickRegion(size_t bytes, const uint32_t* caps, const uint8_t* owner, int n, int current)
{
    if(bytes == 0) return -1;
    if(current >= 0 && current < n && caps[current] >= bytes) return current;
    int best = -1;
    for(int r = 0; r < n; r++)
    {
        if(owner[r] || caps[r] < bytes) continue;
        if(best < 0 || caps[r] < caps[best]) best = r;
    }
    return best;
}

} // namespace kyk
