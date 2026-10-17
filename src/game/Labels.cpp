#include "game/Labels.h"
#include <algorithm>

namespace labels {

bool overlap(const Box& a, const Box& b) { return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h; }

std::vector<Box> layout(const std::vector<Want>& wants) {
    const int n = int(wants.size());
    std::vector<Box> out(n, Box{0, 0, 0, 0});
    std::vector<int> idx(n);
    for (int i = 0; i < n; i++) idx[i] = i;
    // pinned first, then top to bottom so nudges cascade one way
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        if (wants[a].pinned != wants[b].pinned) return wants[a].pinned;
        return wants[a].at.y < wants[b].at.y;
    });
    std::vector<Box> placed;
    for (int i : idx) {
        const Box& w = wants[i].at;
        const float step = w.h + 2;
        const float tries[7] = {0, step, -step, 2 * step, -2 * step, 3 * step, -3 * step};
        for (float dy : tries) {
            Box b{w.x, w.y + dy, w.w, w.h};
            bool free = std::none_of(placed.begin(), placed.end(), [&](const Box& p) { return overlap(p, b); });
            if (free || (wants[i].pinned && dy == 0)) { out[i] = b; placed.push_back(b); break; }
        }
    }
    return out;
}

} // namespace labels
