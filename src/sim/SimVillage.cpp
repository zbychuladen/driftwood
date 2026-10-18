// The village by hand: paths that wear in where people walk, clearings,
// orchards and footbridges drawn up by the narrator, and the names and looks
// the narrator gives its buildings.
#include <algorithm>
#include "Sim.h"

namespace dw {

void Sim::villageDaily() {
    // paths fade where nobody walks any more
    for (auto& w : wear) w = uint8_t(w * 95 / 100);
}

void Sim::finishByHand(Project& b) {
    if (b.key == "clearing") {
        int trees = 0, rocks = 0;
        for (int yy = b.y; yy < b.y + b.h; yy++) for (int xx = b.x; xx < b.x + b.w; xx++) {
            int i = idx(xx, yy);
            if (objAt[i] < 0) continue;
            auto& o = objs[objAt[i]];
            if (o.type == "tree" || o.type == "stump" || o.type == "bush") trees += o.type == "tree";
            else if (o.type == "rock") rocks++;
            else continue;
            removeObj(o);
        }
        stock.wood += trees * 4;
        stock.stone += rocks * 2;
        for (auto& o : bOcc) if (o == b.id) o = -1;
        b.skip = true;   // nothing stands there; the ground is simply open now
        note(cat("The grove is cleared: ", trees, trees == 1 ? " tree" : " trees", " felled, and ", trees * 4, " logs stacked for the camp."));
    }
    if (b.key == "orchard") { b.state = "growing"; b.growth = 0; note("The orchard is planted: young fruit trees in rows. They will bear by summer."); }
    if (b.key == "bridge") note("The footbridge is done. The far side is a short walk now.");
}

void Sim::rotatePlacement() {
    if (!pendingPlacement) return;
    auto* b = projectById(pendingPlacement->project);
    if (b && b->key == "bridge") std::swap(b->w, b->h);
}

bool Sim::rename(int id, const std::string& raw) {
    auto* b = projectById(id);
    if (!b) return false;
    std::string n = raw.substr(0, 24);
    while (!n.empty() && n.back() == ' ') n.pop_back();
    while (!n.empty() && n.front() == ' ') n.erase(n.begin());
    b->customName = n;
    if (!n.empty()) note(cat("The ", lower(b->name), " has a name now: ", n, "."));
    return true;
}

bool Sim::restyle(int id) {
    auto* b = projectById(id);
    if (!b) return false;
    b->style = (b->style + 1) % 4;
    return true;
}

} // namespace dw
