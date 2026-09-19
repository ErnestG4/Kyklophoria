/* hungarian.h — minimum-cost assignment, O(n^3), the standard potentials form.
 *
 * Square cost matrix, row-major. Returns assignment[row] = column. Used to
 * match modes between two models by minimising 1 - MAC, which is the same as
 * maximising the total MAC of the matching.
 */
#pragma once
#include <vector>
#include <limits>

namespace mb {

inline std::vector<int> Hungarian(const std::vector<double>& cost, int n)
{
    const double INF = std::numeric_limits<double>::infinity();
    std::vector<double> u(n + 1, 0.0), v(n + 1, 0.0);
    std::vector<int>    p(n + 1, 0), way(n + 1, 0);
    for(int i = 1; i <= n; i++)
    {
        p[0] = i;
        int j0 = 0;
        std::vector<double> minv(n + 1, INF);
        std::vector<char>   used(n + 1, 0);
        do
        {
            used[j0] = 1;
            const int i0 = p[j0];
            double    delta = INF;
            int       j1 = 0;
            for(int j = 1; j <= n; j++)
                if(!used[j])
                {
                    const double cur = cost[(size_t)(i0 - 1) * n + (j - 1)] - u[i0] - v[j];
                    if(cur < minv[j]) { minv[j] = cur; way[j] = j0; }
                    if(minv[j] < delta) { delta = minv[j]; j1 = j; }
                }
            for(int j = 0; j <= n; j++)
                if(used[j]) { u[p[j]] += delta; v[j] -= delta; }
                else minv[j] -= delta;
            j0 = j1;
        } while(p[j0] != 0);
        do { const int j1 = way[j0]; p[j0] = p[j1]; j0 = j1; } while(j0);
    }
    std::vector<int> assign(n, -1);
    for(int j = 1; j <= n; j++) if(p[j]) assign[p[j] - 1] = j - 1;
    return assign;
}

} // namespace mb
