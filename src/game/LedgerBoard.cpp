#include "game/LedgerBoard.h"
#include <algorithm>

void LedgerBoard::update(const std::vector<dw::Gap>& now, double t, bool frozen) {
    if (frozen) return;
    clock = t;
    for (auto& e : entries) e.sorted = true;
    for (auto& g : now) {
        auto it = std::find_if(entries.begin(), entries.end(), [&](const Shown& e) { return e.gap.key == g.key; });
        if (it == entries.end()) { entries.push_back({g, t, t, t, false}); continue; }
        it->last = t;
        it->sorted = false;
        // the words follow the moment; who is suggested only changes once an hour
        auto keep = it->gap.candidates;
        it->gap = g;
        if (t - it->candidatesAt < REFRESH && !keep.empty()) it->gap.candidates = keep;
        else it->candidatesAt = t;
    }
    entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const Shown& e) { return t - e.last > LINGER || (e.sorted && e.last - e.first < CONFIRM); }), entries.end());
}

std::vector<LedgerBoard::Shown> LedgerBoard::visible() const {
    std::vector<Shown> v;
    for (auto& e : entries) if (e.last - e.first >= CONFIRM || clock - e.first >= CONFIRM) if (!(e.sorted && e.last - e.first < CONFIRM)) v.push_back(e);
    std::stable_sort(v.begin(), v.end(), [](const Shown& a, const Shown& b) { return !a.sorted && b.sorted; });
    return v;
}
