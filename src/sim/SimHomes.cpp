// Households and their homes. A household (a couple, their children, or one
// person on their own) owns a home, and its status is that home's level: no
// home of their own, a hut, a cottage, a house, a manor. A home rises a level
// once its household's needs for the next one have been met for a few days and
// the materials are in store, and falls back a level when the needs of its own
// level lapse for too long.
#include <algorithm>
#include <deque>
#include "Sim.h"

namespace dw {

namespace {
const char* const HOME_KEYS[5] = {"", "hut", "cottage", "house", "manor"};
const char* const STATUS[5] = {"castaway", "settler", "villager", "householder", "notable"};
constexpr int REACH = 14;   // tiles a service covers, walking
} // namespace

int Sim::homeLevel(const std::string& key) {
    for (int i = 1; i < 5; i++) if (key == HOME_KEYS[i]) return i;
    return 0;
}
const char* Sim::statusName(int level) { return STATUS[std::clamp(level, 0, 4)]; }
const char* Sim::levelKey(int level) { return HOME_KEYS[std::clamp(level, 1, 4)]; }

Household* Sim::household(int id) { for (auto& h : households) if (h.id == id) return &h; return nullptr; }
Household* Sim::householdOf(int pid) { for (auto& h : households) for (int m : h.members) if (m == pid) return &h; return nullptr; }
int Sim::status(const Household& h) {
    auto* b = h.home >= 0 ? projectById(h.home) : nullptr;
    return b && b->done ? homeLevel(b->key) : 0;
}
std::string Sim::householdName(const Household& h) {
    std::vector<std::string> names;
    for (int m : h.members) if (auto* p = person(m); p && !p->child) names.push_back(p->name);
    if (names.empty()) for (int m : h.members) if (auto* p = person(m)) names.push_back(p->name);
    if (names.empty()) return "nobody";
    return names.size() == 1 ? names[0] : names[0] + " and " + names[1];
}

void Sim::updateHouseholds() {
    // the dead and departed leave; empty households end and give up their home
    for (auto& h : households) h.members.erase(std::remove_if(h.members.begin(), h.members.end(), [&](int id) { auto* p = person(id); return !p || p->dead || p->status != "member"; }), h.members.end());
    households.erase(std::remove_if(households.begin(), households.end(), [](const Household& h) { return h.members.empty(); }), households.end());
    auto join = [&](Household& h, int pid) { if (std::find(h.members.begin(), h.members.end(), pid) == h.members.end()) h.members.push_back(pid); };
    auto leave = [&](int pid) { for (auto& h : households) h.members.erase(std::remove(h.members.begin(), h.members.end(), pid), h.members.end()); };
    for (auto* p : members()) {
        Household* mine = householdOf(p->id);
        if (p->child) {
            // children live with a parent
            Household* theirs = nullptr;
            for (int par : p->parents) if (auto* h = householdOf(par)) theirs = h;
            if (theirs && theirs != mine) { leave(p->id); join(*household(theirs->id), p->id); }
            else if (!theirs && !mine) { households.push_back({nextHousehold++, {p->id}}); }
            continue;
        }
        if (!mine) { households.push_back({nextHousehold++, {p->id}}); mine = &households.back(); }
        // partners share a household: the better-placed one, and its home
        if (p->partner >= 0) {
            Household* other = householdOf(p->partner);
            if (other && other != mine) {
                Household* keep = status(*other) > status(*mine) || (status(*other) == status(*mine) && other->id < mine->id) ? other : mine;
                Household* drop = keep == mine ? other : mine;
                int keepId = keep->id, dropId = drop->id;
                std::vector<int> moving = drop->members;
                for (int m : moving) join(*household(keepId), m);
                households.erase(std::remove_if(households.begin(), households.end(), [&](const Household& h) { return h.id == dropId; }), households.end());
            }
        }
    }
    // homes: one each, the best free one for those without; homes that are gone are let go
    std::set<int> taken;
    for (auto& h : households) {
        auto* b = h.home >= 0 ? projectById(h.home) : nullptr;
        if (!b || !b->done || !homeLevel(b->key)) h.home = -1;
        else taken.insert(h.home);
    }
    std::vector<Household*> homeless;
    for (auto& h : households) if (h.home < 0) homeless.push_back(&h);
    std::stable_sort(homeless.begin(), homeless.end(), [](Household* a, Household* b) { return a->members.size() > b->members.size(); });
    for (auto* h : homeless) {
        Project* best = nullptr;
        for (auto& b : plan) if (b.done && homeLevel(b.key) && !taken.count(b.id) && (!best || homeLevel(b.key) > homeLevel(best->key))) best = &b;
        if (best) { h->home = best->id; taken.insert(best->id); h->rising = h->falling = 0; }
    }
}

// Path distance in tiles from a home to the nearest built `key`, across land,
// up to REACH (more means out of reach). Worked out once per key while the
// buildings stay the same.
int Sim::reach(const std::string& key, int x, int y) {
    int built = 0;
    for (auto& b : plan) built += b.done;
    auto& c = reachCache[key];
    if (c.built != built || c.dist.size() != size_t(GW * GH)) {
        c.built = built;
        c.dist.assign(GW * GH, uint8_t(255));
        std::deque<int> q;
        for (auto* b : builtAll(key))
            for (int yy = b->y; yy < b->y + std::max(1, b->h); yy++) for (int xx = b->x; xx < b->x + std::max(1, b->w); xx++)
                if (inb(xx, yy)) { c.dist[idx(xx, yy)] = 0; q.push_back(idx(xx, yy)); }
        while (!q.empty()) {
            int i = q.front(); q.pop_front();
            int d = c.dist[i];
            if (d >= REACH + 1) continue;
            int cx = i % GW, cy = i / GW;
            const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& o : D) {
                int nx = cx + o[0], ny = cy + o[1];
                if (!inb(nx, ny) || isWater(nx, ny)) continue;
                int j = idx(nx, ny);
                if (c.dist[j] > d + 1) { c.dist[j] = uint8_t(d + 1); q.push_back(j); }
            }
        }
    }
    int best = 255;
    for (int yy = y - 1; yy <= y + 2; yy++) for (int xx = x - 1; xx <= x + 2; xx++) if (inb(xx, yy)) best = std::min(best, int(c.dist[idx(xx, yy)]));
    return best;
}

float Sim::beauty(int x, int y) {
    float b = 0;
    int trees = 0;
    for (auto& o : objs) if (!o.dead && o.type == "tree" && !o.sapling && std::abs(o.x - x) <= 4 && std::abs(o.y - y) <= 4) trees++;
    b += std::min(4, trees) * .5f;
    auto near = [&](const char* k, int r) { for (auto* p : builtAll(k)) if (std::abs(p->x - x) <= r && std::abs(p->y - y) <= r) return true; return false; };
    if (near("circle", 6)) b += 2;
    if (near("bath", 6)) b += 1;
    if (near("garden", 4)) b += .5f;
    if (near("forge", 5)) b -= 3;
    if (near("mine", 5)) b -= 2;
    return b;
}

std::vector<HomeNeed> Sim::homeNeeds(const Household& h, int level) {
    std::vector<HomeNeed> v;
    auto* home = h.home >= 0 ? projectById(h.home) : nullptr;
    if (!home || level < 2 || level > 4) return v;
    int x = home->x, y = home->y;
    float fd = foodDays();
    auto add = [&](const std::string& id, bool met, const std::string& text) { v.push_back({id, text, met}); };
    bool fresh = false;
    for (int yy = y - 5; yy <= y + 6 && !fresh; yy++) for (int xx = x - 5; xx <= x + 6; xx++) if (inb(xx, yy) && this->fresh[idx(xx, yy)]) { fresh = true; break; }
    int services = 0;
    for (auto k : {"bath", "school", "infirmary", "circle"}) services += reach(k, x, y) <= REACH;
    float bt = beauty(x, y);
    bool standing = false;
    for (int m : h.members) if (auto* p = person(m)) { for (auto& [g, lv] : p->gifts) standing |= lv >= 5; standing |= !p->calling.empty(); }
    if (level == 2) {
        add("food", fd >= 4, "food for four days in store");
        add("pots", pots >= 2, "pots in the village");
        add("water", reach("well", x, y) <= REACH || fresh, "water close by");
    } else if (level == 3) {
        add("food", fd >= 7, "food for a week in store");
        add("cloth", cloth >= 2, "cloth in the village");
        add("water", reach("well", x, y) <= REACH, "a well within reach");
        add("service", services >= 1, "a bath, school, infirmary or story circle within reach");
        add("beauty", bt >= 3, "pleasant surroundings");
    } else {
        add("food", fd >= 10, "food for ten days in store");
        add("cloth", cloth >= 4, "plenty of cloth");
        add("water", reach("well", x, y) <= REACH, "a well within reach");
        add("service", services >= 2, "two of a bath, school, infirmary or story circle within reach");
        add("beauty", bt >= 5, "beautiful surroundings");
        add("standing", standing, "a gifted or called member");
    }
    return v;
}

HomeCost Sim::upgradeCost(int level) {
    return level == 2 ? HomeCost{20, 8} : level == 3 ? HomeCost{30, 20} : level == 4 ? HomeCost{50, 40} : HomeCost{0, 0};
}

// Changes a home to another level in place. A manor needs a 3 by 3 footprint
// and grows from the house's corner if there is room; false if there is not.
bool Sim::setHomeLevel(Project& b, int level) {
    const ProjectDef* d = projectDef(levelKey(level));
    if (!d) return false;
    int ox = b.x, oy = b.y;
    if (d->w != b.w || d->h != b.h) {
        bool found = false;
        int tries[4][2] = {{b.x, b.y}, {b.x - (d->w - b.w), b.y}, {b.x, b.y - (d->h - b.h)}, {b.x - (d->w - b.w), b.y - (d->h - b.h)}};
        for (auto& t : tries) {
            bool ok = true;
            for (int yy = t[1]; yy < t[1] + d->h && ok; yy++) for (int xx = t[0]; xx < t[0] + d->w && ok; xx++) {
                if (!inb(xx, yy) || isWater(xx, yy)) { ok = false; break; }
                int i = idx(xx, yy);
                if ((bOcc[i] != -1 && bOcc[i] != b.id) || objAt[i] >= 0) ok = false;
            }
            if (ok) { ox = t[0]; oy = t[1]; found = true; break; }
        }
        if (!found) return false;
        for (auto& o : bOcc) if (o == b.id) o = -1;
        for (int yy = oy; yy < oy + d->h; yy++) for (int xx = ox; xx < ox + d->w; xx++) bOcc[idx(xx, yy)] = b.id;
    }
    b.key = d->key;
    b.name = d->name;
    b.w = d->w; b.h = d->h; b.beds = d->beds;
    b.x = ox; b.y = oy;
    reachCache.clear();
    return true;
}

void Sim::homesDaily() {
    updateHouseholds();
    for (auto& h : households) {
        auto* b = h.home >= 0 ? projectById(h.home) : nullptr;
        if (!b || !b->done) continue;
        int lv = homeLevel(b->key);
        std::string who = householdName(h);
        // keeping what they have
        auto keep = homeNeeds(h, lv);
        bool kept = std::all_of(keep.begin(), keep.end(), [](const HomeNeed& n) { return n.met; });
        h.falling = kept ? 0 : h.falling + 1;
        if (h.falling >= FALL_DAYS && lv >= 2) {
            std::string lack;
            for (auto& n : keep) if (!n.met) { lack = n.text; break; }
            if (setHomeLevel(*b, lv - 1)) note(cat(who, " could not keep up their ", lower(projectDef(levelKey(lv))->name), " without ", lack, ". It is a ", lower(b->name), " again."), "sad");
            h.falling = 0;
            continue;
        }
        // rising to the next level
        if (lv >= 4) { h.rising = 0; continue; }
        auto next = homeNeeds(h, lv + 1);
        HomeCost c = upgradeCost(lv + 1);
        bool ready = std::all_of(next.begin(), next.end(), [](const HomeNeed& n) { return n.met; }) && stock.wood >= c.wood && stock.stone >= c.stone;
        h.rising = ready ? h.rising + 1 : 0;
        if (h.rising >= RISE_DAYS) {
            h.rising = 0;
            std::string from = lower(b->name);
            if (setHomeLevel(*b, lv + 1)) {
                stock.wood -= c.wood;
                stock.stone -= c.stone;
                note(cat(who, " turned their ", from, " into a ", lower(b->name), ". They are ", statusName(lv + 1), "s now."), "big");
            } else if (!h.noRoom) {
                h.noRoom = true;
                note(cat(who, " would make a manor of their house, but there is no room around it to grow."));
            }
        }
    }
    assignBeds();
}

} // namespace dw
