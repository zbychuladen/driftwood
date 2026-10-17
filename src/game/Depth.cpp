#include "game/Depth.h"
#include <algorithm>
#include <queue>

namespace depth {

static const float EPS = 1e-4f;

int compare(const Item& a, const Item& b) {
    // A footprint entirely on the far side of the other along either ground
    // axis is behind it. (When the two axes disagree the shapes cannot overlap
    // on screen, so either answer is fine.)
    if (a.x1 <= b.x0 + EPS || a.y1 <= b.y0 + EPS) return -1;
    if (b.x1 <= a.x0 + EPS || b.y1 <= a.y0 + EPS) return 1;
    return 0;
}

static float key(const Item& it) { return it.x0 + it.x1 + it.y0 + it.y1; }

std::vector<int> order(const std::vector<Item>& items) {
    const int n = int(items.size());
    std::vector<std::vector<int>> after(n);   // edges: i must be drawn before each of after[i]
    std::vector<int> inDeg(n, 0);
    auto edge = [&](int from, int to) { after[from].push_back(to); inDeg[to]++; };

    // sweep across screen x so only boxes that can overlap are compared
    std::vector<int> byX(n);
    for (int i = 0; i < n; i++) byX[i] = i;
    std::sort(byX.begin(), byX.end(), [&](int a, int b) { return items[a].sx0 < items[b].sx0; });
    for (int ii = 0; ii < n; ii++) {
        const Item& a = items[byX[ii]];
        for (int jj = ii + 1; jj < n; jj++) {
            const Item& b = items[byX[jj]];
            if (b.sx0 >= a.sx1) break;
            if (b.sy0 >= a.sy1 || a.sy0 >= b.sy1) continue;
            int c = compare(a, b);
            if (c == 0) c = key(a) < key(b) ? -1 : key(a) > key(b) ? 1 : (byX[ii] < byX[jj] ? -1 : 1);
            if (c < 0) edge(byX[ii], byX[jj]);
            else edge(byX[jj], byX[ii]);
        }
    }

    // Kahn's algorithm; among items free to go next, the one furthest back goes first
    auto cmp = [&](int a, int b) { float ka = key(items[a]), kb = key(items[b]); return ka != kb ? ka > kb : a > b; };
    std::priority_queue<int, std::vector<int>, decltype(cmp)> ready(cmp);
    for (int i = 0; i < n; i++) if (!inDeg[i]) ready.push(i);
    std::vector<int> out;
    out.reserve(n);
    std::vector<char> done(n, 0);
    while (int(out.size()) < n) {
        if (ready.empty()) {
            // a loop: release the remaining item that sits furthest back
            int best = -1;
            for (int i = 0; i < n; i++) if (!done[i] && (best < 0 || cmp(best, i))) best = i;
            inDeg[best] = 0;
            ready.push(best);
        }
        int i = ready.top();
        ready.pop();
        if (done[i]) continue;
        done[i] = 1;
        out.push_back(i);
        for (int j : after[i]) if (!done[j] && --inDeg[j] == 0) ready.push(j);
    }
    return out;
}

} // namespace depth
