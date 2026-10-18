// Behaviour: the planner that decides what each person does next, and the
// queued actions (walk, work) that make up every job.
#include <algorithm>
#include <queue>
#include "Sim.h"

namespace dw {

// ---------------------------------------------------------------- path
std::vector<std::pair<float, float>> Sim::findPath(float fx, float fy, float tx, float ty) {
    int sx = std::clamp(int(fx), 0, GW - 1), sy = std::clamp(int(fy), 0, GH - 1);
    int gx = std::clamp(int(tx), 0, GW - 1), gy = std::clamp(int(ty), 0, GH - 1);
    std::vector<std::pair<float, float>> path;
    if (sx == gx && sy == gy) return path;
    const int N = GW * GH;
    std::vector<float> g(N, 1e9f);
    std::vector<int> came(N, -1);
    std::vector<uint8_t> closed(N, 0);
    int s = idx(sx, sy), goal = idx(gx, gy);
    Project* dock = built("dock");
    auto walkable = [&](int x, int y) {
        if (!inb(x, y)) return false;
        int i = idx(x, y);
        if (tt[i] >= 2) return true;
        if (dock && bOcc[i] == dock->id) return true;
        return i == goal;
    };
    using QE = std::pair<float, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
    g[s] = 0;
    open.push({0, s});
    int it = 0;
    while (!open.empty() && it++ < 8000) {
        int cur = open.top().second; open.pop();
        if (cur == goal) break;
        if (closed[cur]) continue;
        closed[cur] = 1;
        int cx = cur % GW, cy = cur / GW;
        for (int dy = -1; dy <= 1; dy++) for (int dx = -1; dx <= 1; dx++) {
            if (!dx && !dy) continue;
            int nx = cx + dx, ny = cy + dy;
            if (!walkable(nx, ny)) continue;
            if (dx && dy && (!walkable(cx + dx, cy) || !walkable(cx, cy + dy))) continue;
            int ni = idx(nx, ny);
            float ng = g[cur] + (dx && dy ? 1.414f : 1.f);
            if (ng < g[ni]) { g[ni] = ng; came[ni] = cur; open.push({ng + std::hypot(float(nx - gx), float(ny - gy)), ni}); }
        }
    }
    if (came[goal] < 0) return path;
    for (int c = goal; c != s && c >= 0; c = came[c]) path.push_back({c % GW + .5f, c / GW + .5f});
    std::reverse(path.begin(), path.end());
    if (!path.empty()) path.pop_back();
    return path;
}

// ---------------------------------------------------------------- helpers
Action Sim::walkA(float x, float y) { Action a; a.type = Action::WALK; a.x = x; a.y = y; return a; }
Action Sim::workA(float dur, const std::string& label, const std::string& anim, std::function<void(Person&)> done) {
    Action a; a.type = Action::WORK; a.dur = dur; a.label = label; a.anim = anim; a.done = std::move(done); return a;
}

float Sim::skillF(Person& p, const std::string& k) {
    float f = (.6f + p.sk[k] * .08f) * (p.has("hardworking") ? 1.15f : 1) * (p.mood < 30 ? .8f : 1)
            * (mods.count("tools") && mods["tools"] >= day() ? 1.25f : 1) * (firsts.count("ruinTools") ? 1.12f : 1) * (p.inspired ? 1.15f : 1);
    if (k == "chop" || k == "mine" || k == "build") f *= (1 - toolWear / 260) * (ironTools ? 1.2f : 1);
    return f;
}

void Sim::clearQ(Person& p) {
    if (p.release) p.release();
    p.release = nullptr;
    p.q.clear();
    p.focus = {};
    p.anim.clear();
    p.sleeping = false;
    p.hidden = false;
}

void Sim::setQ(Person& p, const std::string& kind, const std::string& doing, std::deque<Action> q, Focus f) {
    p.kind = kind; p.doing = doing; p.q = std::move(q); p.focus = f;
    if (p.q.empty()) { p.q = wanderQ(p); p.kind = "idle"; p.doing = "Taking a breather"; p.focus = {}; }
}

// Focus shorthands for the jobs below.
static Focus onProject(const Project& b, bool fromStock = false, bool toStock = false) { Focus f; f.kind = Focus::PROJECT; f.id = b.id; f.fromStock = fromStock; f.toStock = toStock; return f; }
static Focus onObject(int i, bool toStock = false) { Focus f; f.kind = Focus::OBJECT; f.id = i; f.toStock = toStock; return f; }
static Focus onPerson(const Person& q) { Focus f; f.kind = Focus::PERSON; f.id = q.id; return f; }
static Focus onPoi(int i) { Focus f; f.kind = Focus::POI; f.id = i; return f; }
static Focus onStock() { Focus f; f.kind = Focus::STOCK; return f; }
static Focus onSpot(float x, float y, bool toStock = false) { Focus f; f.kind = Focus::SPOT; f.x = x; f.y = y; f.toStock = toStock; return f; }

Obj* Sim::nearestObj(const std::function<bool(const Obj&)>& f, float x, float y) {
    Obj* best = nullptr;
    float bd = 1e9;
    for (auto& o : objs) {
        if (o.dead || !f(o)) continue;
        float d = std::hypot(o.x + .5f - x, o.y + .5f - y);
        if (d < bd) { bd = d; best = &o; }
    }
    return best;
}

std::vector<std::pair<int, int>> Sim::frontier() {
    std::vector<std::pair<int, int>> out;
    // sample inside the known part of the island, so a big map still finds its edges
    int x0 = GW, y0 = GH, x1 = 0, y1 = 0;
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) if (explored[idx(x, y)]) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); }
    x0 = std::max(1, x0 - 2); y0 = std::max(1, y0 - 2); x1 = std::min(GW - 2, x1 + 2); y1 = std::min(GH - 2, y1 + 2);
    if (x1 < x0 || y1 < y0) return out;
    for (int k = 0; k < 600 && out.size() < 40; k++) {
        int x = x0 + ri(x1 - x0 + 1), y = y0 + ri(y1 - y0 + 1), i = idx(x, y);
        if (!explored[i] || tt[i] < 2) continue;
        bool f = false;
        for (int yy = -2; yy <= 2 && !f; yy++) for (int xx = -2; xx <= 2; xx++)
            if (inb(x + xx, y + yy) && !explored[idx(x + xx, y + yy)]) { f = true; break; }
        if (f) out.push_back({x, y});
    }
    return out;
}

// ---------------------------------------------------------------- simple queues
std::deque<Action> Sim::wanderQ(Person& p, bool play) {
    float a = rnd() * 6.283f, r = 1.5f + rnd() * 3.5f;
    float x = campX + .5f + std::cos(a) * r, y = campY + .5f + std::sin(a) * r;
    if (isWater(int(x), int(y))) { x = campX + 2.f; y = campY + .5f; }
    return {walkA(x, y), workA(20 + rnd() * 25, play ? "Playing" : "Resting", "idle")};
}

std::deque<Action> Sim::drinkQ(Person& p, Focus* where) {
    struct O { float x, y, d; int well; };
    std::vector<O> opts;
    for (auto& s : pondShore) opts.push_back({s.x + .5f + s.dx * .35f, s.y + .5f + s.dy * .35f, std::hypot(s.x - p.x, s.y - p.y), -1});
    if (auto* w = built("well")) opts.push_back({w->x + 1.3f, w->y + .5f, std::hypot(w->x - p.x, w->y - p.y) - 4, w->id});
    if (opts.empty()) return {};
    auto o = *std::min_element(opts.begin(), opts.end(), [](const O& a, const O& b) { return a.d < b.d; });
    if (where) *where = o.well >= 0 ? onProject(*projectById(o.well)) : onSpot(o.x, o.y);
    // a well dug in a good spot gives sweet water; a poor one leaves people thirsty sooner
    float left = o.well >= 0 ? std::max(0.f, (1.1f - siteQ("well")) * 40) : 0;
    return {walkA(o.x, o.y), workA(12, "Drinking", "drink", [this, left](Person& p) { p.needs.thirst = left; if (rnd() < .12f) bubble(p, "ahh"); })};
}

std::deque<Action> Sim::eatQ(Person& p) {
    if (auto* ck = built("cookhouse")) {
        // a cooked meal at the cookhouse; how much it fills depends on where it was put
        float q = ck->siteQuality;
        auto [sx, sy] = workSpot(*ck);
        return {walkA(stockX + 1 + (rnd() - .5f), stockY + 1.3f),
                workA(4, "Taking food", "take", [this](Person& p) { if (stock.food >= 1) { stock.food -= 1; p.carry = Carry{"meal", 1}; } }),
                walkA(sx, sy),
                workA(25, "Eating at the cookhouse", "eat", [this, q](Person& p) {
                    if (p.carry && p.carry->t == "meal") {
                        p.needs.hunger = std::max(0.f, p.needs.hunger - 60 * q);
                        if (q > 1.05f) { p.mood += 2; if (rnd() < .15f) bubble(p, "now that's a meal"); }
                        else if (q < .85f && rnd() < .2f) bubble(p, "it's gone cold again");
                    }
                    p.carry.reset();
                })};
    }
    auto* f = fire();
    float cx = (f ? f->x : campX) + .5f, cy = (f ? f->y : campY) + .5f, a = rnd() * 6.283f;
    return {walkA(stockX + 1 + (rnd() - .5f), stockY + 1.3f),
            workA(4, "Taking food", "take", [this](Person& p) { if (stock.food >= 1) { stock.food -= 1; p.carry = Carry{"meal", 1}; } }),
            walkA(cx + std::cos(a) * 1.6f, cy + std::sin(a) * 1.6f),
            workA(25, "Eating", "eat", [this](Person& p) {
                if (p.carry && p.carry->t == "meal") {
                    p.needs.hunger = std::max(0.f, p.needs.hunger - 60);
                    if (fireLit()) { p.mood += 2; if (rnd() < .15f) bubble(p, "mm, warm"); }
                }
                p.carry.reset();
            })};
}

Sim::BedSpot Sim::bedSpot(Person& p) {
    if (auto* b = projectById(p.bed)) {
        if (b->key == "leanto") {
            int k = 0;
            for (auto* q : members()) { if (q == &p) break; if (q->bed == b->id) k++; }
            return {b->x + .4f + k * .55f, b->y + 1.3f, false, b};
        }
        return {b->x + b->w + .15f, b->y + b->h / 2.f, true, b};
    }
    auto* f = fire();
    float a = std::fmod(p.id * 1.7f, 6.283f);
    return {(f ? f->x : campX) + .5f + std::cos(a) * 1.8f, (f ? f->y : campY) + .5f + std::sin(a) * 1.8f, false, nullptr};
}

std::deque<Action> Sim::sleepQ(Person& p, bool sick) {
    auto s = bedSpot(p);
    double h = hour();
    float dur = h >= 20 ? float((24 - h + 6.5) * 60) : h < 6.5 ? float((6.5 - h) * 60) : sick ? (built("herbs") ? 150.f : 240.f) : 150.f;
    auto w = workA(dur, sick ? "Resting" : "Sleeping", "sleep", [](Person& p) { p.sleeping = false; p.hidden = false; });
    std::string where = s.b ? s.b->key : "ground";
    bool inside = s.inside;
    w.start = [where, inside](Person& p) { p.sleeping = true; p.hidden = inside; p.sleepIn = where; };
    return {walkA(s.x, s.y), w};
}

std::deque<Action> Sim::warmQ(Person& p) {
    auto* f = fire();
    float a = rnd() * 6.283f;
    return {walkA(f->x + .5f + std::cos(a) * 1.4f, f->y + .5f + std::sin(a) * 1.4f), workA(45, "Warming hands", "sit")};
}

std::deque<Action> Sim::fuelQ(Person& p, Project& f) {
    fuelRes = p.id;
    p.release = [this, id = p.id] { if (fuelRes == id) fuelRes = -1; };
    Project* fp = &f;
    return {walkA(stockX + .5f, stockY + 1.3f),
            workA(4, "Grabbing firewood", "take", [this](Person& p) { if (stock.wood >= 1) { stock.wood -= 1; p.carry = Carry{"wood", 1}; } }),
            walkA(f.x + 1.4f, f.y + .5f),
            workA(5, "Feeding the fire", "take", [this, fp](Person& p) {
                if (p.carry) { fp->fuel += 3; p.carry.reset(); }
                if (fuelRes == p.id) fuelRes = -1;
                p.release = nullptr;
            })};
}

std::deque<Action> Sim::socialQ(Person& p) {
    auto* f = fire();
    float cx = (f ? f->x : campX) + .5f, cy = (f ? f->y : campY) + .5f, a = rnd() * 6.283f, r = 1.5f + rnd() * .6f;
    return {walkA(cx + std::cos(a) * r, cy + std::sin(a) * r),
            workA(60, "Talking", "sit", [](Person& p) { p.needs.social = std::max(0.f, p.needs.social - 35); })};
}

std::deque<Action> Sim::hermitQ(Person& p) {
    for (auto& hp : pois) if (hp.type == "hermit") {
        float a = rnd() * 6.283f;
        return {walkA(hp.x + .5f + std::cos(a) * 2, hp.y + .5f + std::sin(a) * 2), workA(60 + rnd() * 60, "Whittling", "sit")};
    }
    return {};
}

std::deque<Action> Sim::deliver(const std::string& label) {
    return {walkA(stockX + .5f + rnd(), stockY + 1.3f), workA(5, label, "take", [this](Person& p) {
        if (p.carry) {
            auto& c = *p.carry;
            if (c.t == "wood") stock.wood += c.n;
            else if (c.t == "stone") stock.stone += c.n;
            else if (c.t == "food" || c.t == "fish") stock.food += c.n;
        }
        p.carry.reset();
    })};
}

// ---------------------------------------------------------------- planner
void Sim::plan_(Person& p) {
    p.sleeping = false; p.hidden = false;
    if (p.status == "hermit") return setQ(p, "idle", "Keeping to themself", hermitQ(p));
    if (p.status == "arriving") {
        auto w = workA(10, "Asking to join", "sit", [this](Person& q) { decideNewcomer(q); });
        return setQ(p, "arrive", "Walking toward the camp", {walkA(campX + 1.5f, campY + 1.5f), w});
    }
    if (p.status == "leaving")
        return setQ(p, "leave", "Leaving", {walkA(boatX + .5f, boatY + .5f), workA(5, "Leaving", "idle", [this](Person& q) { q.dead = true; q.gone = true; lostThisSeason++; })});
    auto& n = p.needs;
    double h = hour();
    bool night = h >= 22 || h < 6;
    Focus bed = projectById(p.bed) ? onProject(*projectById(p.bed)) : Focus{};
    Focus atFire = fire() ? onProject(*fire()) : Focus{};
    Focus mealAt = fire() ? onProject(*fire(), true) : onStock();
    if (p.sick > 0) return setQ(p, "rest", "Resting, feverish", sleepQ(p, true), bed);
    if (n.thirst > 62) { Focus water; auto q = drinkQ(p, &water); return setQ(p, "drink", "Going for water", std::move(q), water); }
    if (n.hunger > 58 && stock.food >= 1) return setQ(p, "eat", "Having a meal", eatQ(p), mealAt);
    if (n.fatigue > 90) return setQ(p, "sleep", "Collapsing into sleep", sleepQ(p, false), bed);
    if (n.cold > 58 && fireLit()) return setQ(p, "warm", "Warming up by the fire", warmQ(p), atFire);
    if (night) {
        if (n.fatigue > 20 || h < 5) return setQ(p, "sleep", "Sleeping", sleepQ(p, false), bed);
        return setQ(p, "social", "Sitting up by the fire", socialQ(p), atFire);
    }
    bool meal = (h >= 12 && h < 13.5) || (h >= 18 && h < 19.5);
    if (meal && n.hunger > 38 && stock.food >= 1) return setQ(p, "eat", "Sharing a meal", eatQ(p), mealAt);
    auto* f = fire();
    if (f && f->done && f->fuel < 2.5f && stock.wood >= 1 && (h >= 16 || temp() < 10) && fuelRes < 0)
        return setQ(p, "fuel", "Feeding the fire", fuelQ(p, *f), onProject(*f, true));
    if (h >= 19.5 && h < 22) return setQ(p, "social", "Talking by the fire", socialQ(p), atFire);
    if (!p.child && !p.calling.empty() && p.callingNeed > 55 && h >= 9 && h < 18 && p.gates[3] > .45f && foodDays() > 3) return callingJob(p);
    if (p.child) return setQ(p, "play", "Playing near camp", wanderQ(p, true));
    if (p.told && p.told->until > tMin && p.told->from <= tMin) {
        auto w = p.told->job;
        if (w == "rest") { auto q = wanderQ(p); q.push_back(workA(90, "Resting", "sit", [](Person& p) { p.mood = std::min(100.f, p.mood + 3); })); return setQ(p, "idle", p.told->contrary ? "Taking the day off, on principle" : "Resting, as narrated", q); }
        if (w == "calling" && !p.calling.empty()) return callingJob(p);
        if (w == "hermit") for (auto& hp : pois) if (hp.type == "hermit" && hp.found && !hp.joined) return visitJob(p, hp);
        if (w == "explore") return exploreJob(p);
        if (w == "food") return foodJob(p);
        if (w == "wood") return chopJob(p);
        if (w == "build") if (auto* pr = currentProject()) return buildJob(p, *pr);
    }
    workQ(p);
}

std::string Sim::gardenJob(Project& g) {
    int s = season();
    if (g.state == "ripe") return "harvest";
    if (g.state == "empty" && (s <= 1 || (s == 2 && (day() - 1) % 10 < 3))) return "plant";
    return "";
}

void Sim::workQ(Person& p) {
    auto* proj = currentProject();
    auto alive = members();
    float days = foodDays();
    int sea_ = season();
    float target = sea_ == 2 ? 22.f : sea_ == 3 ? 10.f : sea_ == 1 ? 12.f : 8.f;
    float needW = proj ? std::max(0.f, proj->wood - proj->dw) : 0, needS = proj ? std::max(0.f, proj->stone - proj->ds) : 0;
    float fuelReserve = sea_ >= 2 ? 14.f : 5.f;
    float dFood = clampf(1 - days / target, 0, 1) * (sea_ >= 2 ? 1.9f : 1.4f) + (days > target * 2.5f ? 0 : .12f);
    float dWood = (needW > stock.wood ? .95f : 0) + (stock.wood < fuelReserve ? .6f : 0) + .06f;
    float dStone = (needS > stock.stone ? .95f : 0) + .03f;
    if (needS > stock.stone) mods["stoneShort"] = day();
    bool canHaul = proj && ((needW > 0 && stock.wood > 0) || (needS > 0 && stock.stone > 0));
    float dBuild = proj && (canHaul || (needW == 0 && needS == 0)) ? 1.05f : 0;
    Project* g = built("garden");
    std::string gw = g ? gardenJob(*g) : "";
    if (auto* fl = built("fields")) { auto fw = gardenJob(*fl); if (!fw.empty() && (gw.empty() || fw == "harvest")) { g = fl; gw = fw; } }
    float dFarm = gw.empty() ? 0 : 1.5f;
    float dCraft = 0;
    std::string craftKind;
    static const std::map<std::string, std::string> WORKSHOP = {{"smith", "forge"}, {"potter", "pottery"}, {"weaver", "loomhouse"}, {"herbalist", "infirmary"}};
    for (auto& [gk, lv] : p.gifts) {
        auto it = WORKSHOP.find(gk);
        if (it == WORKSHOP.end() || !built(it->second)) continue;
        float d = gk == "smith" ? toolWear / 80 : gk == "potter" ? (pots < 14 ? .45f : .05f) : gk == "weaver" ? ((cloth < 14 ? .4f : .05f) + (season() >= 2 ? .25f : 0)) : (herbs < 8 ? .35f : .05f);
        if (d > dCraft) { dCraft = d; craftKind = gk; }
    }
    int unexplored = 0;
    for (auto e : explored) if (!e) unexplored++;
    float dExplore = day() >= 2 && unexplored > GW * GH * .03f ? .28f + (p.has("curious") ? .15f : 0) : 0;
    Poi* hp = nullptr;
    for (auto& q : pois) if (q.type == "hermit" && q.found && !q.joined) hp = &q;
    float dVisit = hp ? .32f + (p.has("social") ? .15f : 0) : 0;
    float dFriend = !p.child ? (100 - p.tiers[BELONGING]) / 100 * .65f * p.gates[2] + (p.needs.social > 65 ? .2f : 0) : 0;
    if (agenda && !p.child) {
        bool own = agenda->by == p.id;
        float fol = own ? .55f : clampf(affOf(p, agenda->by) / 60, .3f, 1.4f) * agenda->str * .7f;
        float sat = agenda->job == "stone" ? std::min(1.f, 70 / std::max(1.f, stock.stone)) : agenda->job == "wood" ? std::min(1.f, 70 / std::max(1.f, stock.wood)) : std::min(1.f, 18 / std::max(1.f, days));
        fol *= sat;
        if (agenda->job == "stone") dStone += fol;
        else if (agenda->job == "wood") dWood += fol;
        else if (agenda->job == "farm") { if (dFarm > 0) dFarm += fol; else dFood += fol * .6f; }
        else dFood += fol;
    }
    if (boosted("food")) dFood += .6f;
    if (boosted("build")) dBuild *= 1.5f;
    if (boosted("explore")) dExplore += .7f;
    if (boosted("wood")) dWood += .5f;
    std::map<std::string, int> cnt;
    for (auto* q : alive) if (q != &p) cnt[q->kind]++;
    float fs = std::max(p.sk["forage"], p.sk["fish"]);
    struct Opt { const char* k; float d, sk; };
    Opt opts[] = {{"food", dFood, fs}, {"wood", dWood, p.sk["chop"]}, {"stone", dStone, p.sk["mine"]}, {"build", dBuild, p.sk["build"]},
                  {"farm", cnt["farm"] ? 0 : dFarm, p.sk["farm"]}, {"explore", cnt["explore"] ? dExplore * .2f : dExplore, 3},
                  {"visit", cnt["visit"] ? 0 : dVisit, 3}, {"friend", cnt["friend"] > 1 ? 0 : dFriend, 3}, {"craft", dCraft, 6}};
    std::string best;
    float bs = .12f;
    for (auto& o : opts) {
        float s = o.d / (1 + cnt[o.k] * .85f) * (.78f + o.sk * .045f * (1 + (100 - p.tiers[ESTEEM]) / 150)) + rnd() * .07f;
        if (s > bs) { bs = s; best = o.k; }
    }
    if (best == "food") return foodJob(p);
    if (best == "wood") return chopJob(p);
    if (best == "stone") return mineJob(p);
    if (best == "build") return buildJob(p, *proj);
    if (best == "farm") return farmJob(p, *g, gw);
    if (best == "explore") return exploreJob(p);
    if (best == "visit") return visitJob(p, *hp);
    if (best == "friend") return friendJob(p);
    if (best == "craft") return craftJob(p, craftKind);
    setQ(p, "idle", "Taking a breather", wanderQ(p));
}

// ---------------------------------------------------------------- jobs
void Sim::chopJob(Person& p) {
    Obj* t = nearestObj([&](const Obj& o) { return o.type == "tree" && !o.palm && !o.sapling && o.res < 0 && explored[idx(o.x, o.y)]; }, p.x, p.y);
    if (!t) return setQ(p, "idle", "No trees nearby", wanderQ(p));
    int ti = t->i;
    t->res = p.id;
    p.release = [this, ti, id = p.id] { if (objs[ti].res == id) objs[ti].res = -1; };
    auto q = std::deque<Action>{walkA(t->x + 1.1f, t->y + .6f), workA(55 / skillF(p, "chop"), "Chopping a tree", "chop", [this, ti](Person& p) {
        Obj& t = objs[ti];
        p.carry = Carry{"wood", 3 + int(std::lround(rnd() * 2)) + (p.gifts.count("carpenter") ? 1 : 0)};
        toolWear = std::min(100.f, toolWear + .5f);
        removeObj(t);
        p.release = nullptr;
        gain(p, "chop");
        Obj st; st.type = "stump"; st.x = t.x; st.y = t.y; st.regrow = day() + 8 + ri(6); st.conifer = t.conifer; st.size = t.size;
        addObj(st);
        if (!firsts.count("chop")) { firsts.insert("chop"); note(cat(p.name, " felled the first tree with our only hatchet. Slow work.")); }
    })};
    for (auto& a : deliver("Stacking logs")) q.push_back(a);
    setQ(p, "wood", "Chopping wood", q, onObject(ti, true));
}

void Sim::mineJob(Person& p) {
    auto stoneN = [this](Person& p, int base) { return base + (p.gifts.count("mason") || p.gifts.count("smith") ? 1 : 0) + (inventions.count("sledge") ? 1 : 0); };
    if (auto* mine = built("mine"); mine && rnd() < .7f) {
        auto q = std::deque<Action>{walkA(mine->x + 1.3f, mine->y + .7f), workA(80 / skillF(p, "mine"), "Hewing stone in the mine", "mine", [this, stoneN](Person& p) {
            p.carry = Carry{"stone", stoneN(p, 4)};
            gain(p, "mine");
            toolWear = std::min(100.f, toolWear + .5f);
        })};
        for (auto& a : deliver("Piling stones")) q.push_back(a);
        return setQ(p, "stone", "Working the mine", q, onProject(*mine, false, true));
    }
    Obj* r = nearestObj([&](const Obj& o) { return o.type == "rock" && o.res < 0 && explored[idx(o.x, o.y)]; }, p.x, p.y);
    if (!r) return chopJob(p);
    int ri_ = r->i;
    r->res = p.id;
    p.release = [this, ri_, id = p.id] { if (objs[ri_].res == id) objs[ri_].res = -1; };
    auto q = std::deque<Action>{walkA(r->x + 1.1f, r->y + .6f), workA(70 / skillF(p, "mine"), "Prying loose stones", "mine", [this, ri_, stoneN](Person& p) {
        Obj& r = objs[ri_];
        r.stone -= 2;
        p.carry = Carry{"stone", stoneN(p, 2)};
        toolWear = std::min(100.f, toolWear + .5f);
        r.res = -1;
        p.release = nullptr;
        gain(p, "mine");
        if (r.stone <= 0) removeObj(r); else r.size = std::max(.45f, r.size * .88f);
    })};
    for (auto& a : deliver("Piling stones")) q.push_back(a);
    setQ(p, "stone", "Breaking stone", q, onObject(ri_, true));
}

void Sim::foodJob(Person& p) {
    int s = season();
    Obj* b = s == 3 ? nullptr : nearestObj([&](const Obj& o) { return o.type == "bush" && o.berries >= 2 && o.res < 0 && explored[idx(o.x, o.y)]; }, p.x, p.y);
    bool preferFish = !b || p.sk["fish"] > p.sk["forage"] + 1 || (b && std::hypot(b->x - p.x, b->y - p.y) > 16) || rnd() < .35f;
    if (!preferFish && b) {
        int bi = b->i;
        b->res = p.id;
        p.release = [this, bi, id = p.id] { if (objs[bi].res == id) objs[bi].res = -1; };
        auto q = std::deque<Action>{walkA(b->x + 1.05f, b->y + .5f), workA(30 / skillF(p, "forage"), "Picking berries", "forage", [this, bi](Person& p) {
            Obj& b = objs[bi];
            int n = int(b.berries);
            b.berries -= n;
            p.carry = Carry{"food", n};
            b.res = -1;
            p.release = nullptr;
            gain(p, "forage");
        })};
        for (auto& a : deliver("Storing berries")) q.push_back(a);
        return setQ(p, "food", "Picking berries", q, onObject(bi, true));
    }
    Project* dock = built("dock");
    float sx, sy; int face = 1;
    bool onDock = false;
    if (dock && dockRes < 0) {
        sx = dock->x + dock->dx * 2 + .5f; sy = dock->y + dock->dy * 2 + .5f;
        dockRes = p.id; onDock = true;
        p.release = [this, id = p.id] { if (dockRes == id) dockRes = -1; };
    } else {
        std::vector<Shore> c;
        for (auto& sh : seaShore) if (explored[idx(sh.x, sh.y)]) c.push_back(sh);
        std::sort(c.begin(), c.end(), [&](const Shore& a, const Shore& b) { return std::hypot(float(a.x - campX), float(a.y - campY)) < std::hypot(float(b.x - campX), float(b.y - campY)); });
        if (c.size() > 10) c.resize(10);
        Shore sh = c.empty() ? (seaShore.empty() ? Shore{campX, campY, 1, 0} : seaShore[0]) : c[ri(int(c.size()))];
        sx = sh.x + .5f + sh.dx * .3f; sy = sh.y + .5f + sh.dy * .3f;
        face = sh.dx - sh.dy >= 0 ? 1 : -1;
    }
    auto w = workA(80, "Fishing", "fish", [this, dock, onDock](Person& p) {
        float n = 1.5f + p.sk["fish"] * .35f + rnd() * 2.5f;
        if (season() == 3) n *= .75f;
        if (weather == "storm") n *= .4f;
        if (dock && onDock) n *= 1.6f;
        if (built("boatyard")) n *= 1.4f;
        if (built("smoke")) n *= 1 + .15f * siteQ("smoke");
        if (inventions.count("trap")) n *= 1.25f;
        if (p.gifts.count("sailor")) n *= 1 + p.gifts["sailor"] * .05f;
        if (mods.count("calm") && mods["calm"] >= day()) n *= 1.3f;
        int ni = int(std::lround(n));
        if (dockRes == p.id) dockRes = -1;
        p.release = nullptr;
        gain(p, "fish");
        if (ni <= 0) { bubble(p, "nothing..."); while (p.q.size() > 1) p.q.pop_back(); return; }
        p.carry = Carry{"fish", ni};
        if (ni >= 6) p.esteemPts = std::min(100.f, p.esteemPts + 2);
        if (!firsts.count("fish")) { firsts.insert("fish"); note(cat(p.name, " caught fish with braided bark and a bone hook. Dinner!")); }
    });
    w.start = [face](Person& p) { p.face = face; };
    auto q = std::deque<Action>{walkA(sx, sy), w};
    for (auto& a : deliver("Storing fish")) q.push_back(a);
    setQ(p, "food", "Fishing", q, onDock ? onProject(*dock, false, true) : onSpot(sx, sy, true));
}

void Sim::buildJob(Person& p, Project& b) {
    auto [sx, sy] = workSpot(b);
    Project* bp = &b;
    auto cancel = [bp] { return bp->done; };
    float needW = std::max(0.f, b.wood - b.dw), needS = std::max(0.f, b.stone - b.ds);
    if ((needW > 0 && stock.wood > 0) || (needS > 0 && stock.stone > 0)) {
        std::string t = needW > 0 && stock.wood > 0 ? "wood" : "stone";
        auto load = workA(4, "Loading up", "take", [this, bp, t](Person& p) {
            float rem = t == "wood" ? bp->wood - bp->dw : bp->stone - bp->ds;
            int cap = t == "wood" ? (inventions.count("barrow") ? 12 : 6) : (inventions.count("barrow") ? 8 : 4);
            int n = int(std::min({float(cap), rem, t == "wood" ? stock.wood : stock.stone}));
            if (n > 0) { (t == "wood" ? stock.wood : stock.stone) -= n; p.carry = Carry{t, n}; }
        });
        auto drop = workA(4, "Dropping materials", "take", [bp](Person& p) {
            if (p.carry) { if (p.carry->t == "wood") bp->dw += p.carry->n; else bp->ds += p.carry->n; p.carry.reset(); }
        });
        drop.cancel = cancel;
        return setQ(p, "build", cat("Hauling ", t, " to the ", lower(b.name)), {walkA(stockX + .5f + rnd(), stockY + 1.3f), load, walkA(sx, sy), drop}, onProject(b, true));
    }
    auto w = workA(60, cat("Building the ", lower(b.name)), "build", [this, bp](Person& p) {
        if (bp->done) return;
        bp->progress += 60 * skillF(p, "build") * giftBuild(p, *bp);
        toolWear = std::min(100.f, toolWear + .3f);
        bp->workers.insert(p.id);
        gain(p, "build");
        if (bp->progress >= bp->work) finish(*bp);
    });
    w.cancel = cancel;
    w.start = [](Person& p) { p.face = -1; };
    setQ(p, "build", cat("Building the ", lower(b.name)), {walkA(sx, sy), w}, onProject(b));
}

void Sim::farmJob(Person& p, Project& g, const std::string& job) {
    float sx = g.x + g.w + .2f, sy = g.y + g.h / 2.f + (rnd() - .5f);
    Project* gp = &g;
    if (job == "plant")
        return setQ(p, "farm", "Planting the field", {walkA(sx, sy), workA(80 / skillF(p, "farm"), "Sowing seeds", "farm", [this, gp](Person& p) {
            gp->state = "growing"; gp->growth = 0; gain(p, "farm");
            if (!firsts.count("plant")) { firsts.insert("plant"); note(cat(p.name, " sowed the garden: beans, squash and something nobody can name.")); }
        })}, onProject(g));
    auto q = std::deque<Action>{walkA(sx, sy), workA(70 / skillF(p, "farm"), "Harvesting", "farm", [this, gp](Person& p) {
        int n = int(std::lround(((gp->key == "fields" ? 30 : 14) + p.sk["farm"] * .8f) * (1 + (p.gifts.count("farmer") ? p.gifts["farmer"] : 0) * .06f) * gp->siteQuality));
        gp->state = "empty"; gp->growth = 0;
        p.carry = Carry{"food", n};
        gain(p, "farm");
        if (!firsts.count("harvest")) { firsts.insert("harvest"); note(cat("First harvest! ", p.name, " carried in ", n, " baskets of squash and beans."), "big"); }
    })};
    for (auto& a : deliver("Storing the harvest")) q.push_back(a);
    setQ(p, "farm", "Harvesting", q, onProject(g, false, true));
}

void Sim::exploreJob(Person& p) {
    auto fr = frontier();
    if (fr.empty()) return setQ(p, "idle", "Taking a breather", wanderQ(p));
    std::pair<int, int> best = fr[0];
    float bs = -1e9;
    for (auto& f : fr) { float s = -std::hypot(f.first - p.x, f.second - p.y) * .35f + rnd() * 6; if (s > bs) { bs = s; best = f; } }
    std::string dir = dirWord(best.first - campX, best.second - campY);
    setQ(p, "explore", "Exploring the " + dir, {walkA(best.first + .5f, best.second + .5f), workA(30, "Surveying the land", "idle", [this, dir](Person& p) {
        reveal(p.x, p.y, inventions.count("compass") ? 8.5f : 6);
        if (!firsts.count("explore")) { firsts.insert("explore"); note(cat(p.name, " went off to explore the ", dir, " of the island and came back with scratched legs and stories.")); }
    })}, onSpot(best.first + .5f, best.second + .5f));
}

void Sim::visitJob(Person& p, Poi& hp) {
    Person* h = person(hp.who);
    if (!h) return setQ(p, "idle", "Taking a breather", wanderQ(p));
    Poi* hpp = &hp;
    setQ(p, "visit", cat("Visiting ", h->name, " the hermit"), {walkA(hp.x + 1.4f, hp.y + 1), workA(70, "Sharing a meal with the hermit", "sit", [this, h, hpp](Person& p) {
        h->trust += 18 + (p.has("social") ? 10 : 0) + (p.has("cheerful") ? 5 : 0);
        static const std::vector<std::string> lines = {"hm.", "you again", "sit, then", "the fish run north in spring"};
        bubble(*h, pick(lines));
        if (h->trust >= 100 && !hpp->joined) {
            hpp->joined = true;
            h->status = "member"; h->q.clear(); h->needs.social = 50;
            for (auto* q : members()) if (q != h) { q->aff[h->id] = 30; h->aff[q->id] = 30; }
            assignBeds();
            note(cat("After many visits, ", h->name, " the hermit packed a bundle and walked into camp with ", p.name, ". \"I suppose I could use the company,\" they said."), "big");
            announceGifts(*h);
        } else if (!hpp->mentioned) {
            hpp->mentioned = true;
            note(cat(p.name, " shared a meal with ", h->name, ". The hermit barely spoke, but did not chase them off."));
        }
    })}, onPoi(int(hpp - pois.data())));
}

void Sim::friendJob(Person& p) {
    std::vector<Person*> fr;
    for (auto* q : members()) if (q != &p && !q->sleeping && !q->hidden && !q->child) fr.push_back(q);
    std::sort(fr.begin(), fr.end(), [&](Person* a, Person* b) { return affOf(p, a->id) > affOf(p, b->id); });
    if (fr.size() > 3) fr.resize(3);
    if (fr.empty()) return setQ(p, "idle", "Taking a breather", wanderQ(p));
    Person* q = pick(fr);
    setQ(p, "friend", "Spending time with " + q->name, {walkA(q->x + .7f, q->y + .4f), workA(40, "Chatting", "sit", [this, q](Person& p) {
        p.needs.social = std::max(0.f, p.needs.social - 25);
        q->needs.social = std::max(0.f, q->needs.social - 15);
        affAdd(p, q->id, 4); affAdd(*q, p.id, 3);
        static const std::vector<std::string> lines = {"how are you, really?", "I had the strangest dream", "you look tired", "thanks for yesterday", "do you miss home?"};
        if (rnd() < .35f) bubble(p, pick(lines));
    })}, onPerson(*q));
}

void Sim::craftJob(Person& p, const std::string& kind) {
    static const std::map<std::string, std::string> WORKSHOP = {{"smith", "forge"}, {"potter", "pottery"}, {"weaver", "loomhouse"}, {"herbalist", "infirmary"}};
    static const std::map<std::string, std::string> LAB = {{"smith", "Forging tools"}, {"potter", "Throwing pots"}, {"weaver", "Weaving blankets"}, {"herbalist", "Preparing remedies"}};
    Project* b = built(WORKSHOP.at(kind));
    if (!b) return setQ(p, "idle", "Taking a breather", wanderQ(p));
    auto w = workA(120, LAB.at(kind), kind == "smith" ? "build" : "sit", [this, kind](Person& p) {
        int lv = p.gifts.count(kind) ? p.gifts[kind] : 1;
        if (kind == "smith") {
            toolWear = std::max(0.f, toolWear - (30 + lv * 3) * siteQ("forge"));
            if (!ironTools) { ironTools = true; note(cat(p.name, " forged the first iron tools on the island. Axes that hold an edge! Everyone chops, digs and builds faster now."), "big"); }
        }
        if (kind == "potter") { pots = std::min(20.f, pots + (1 + lv / 5.f) * siteQ("pottery")); if (!firsts.count("pots")) { firsts.insert("pots"); note(cat(p.name, " fired the first batch of clay pots. Sealed with wax, they keep food from spoiling."), "big"); } }
        if (kind == "weaver") { cloth = std::min(20.f, cloth + (1 + lv / 5.f) * siteQ("loomhouse")); if (!firsts.count("cloth")) { firsts.insert("cloth"); note(cat(p.name, " wove the first blankets from grass fiber. Nights are warmer already."), "big"); } }
        if (kind == "herbalist") herbs = std::min(20.f, herbs + 3);
        p.esteemPts = std::min(100.f, p.esteemPts + 3);
    });
    w.start = [](Person& p) { p.face = -1; };
    setQ(p, "craft", LAB.at(kind), {walkA(b->x + b->w + .3f, b->y + b->h / 2.f), w}, onProject(*b));
}

std::optional<std::pair<int, int>> Sim::totemSpot() {
    if (totems >= 10) return std::nullopt;
    for (int k = 0; k < 80; k++) {
        float a = rnd() * 6.283f, r = 8 + rnd() * 4;
        int x = int(std::round(campX + std::cos(a) * r)), y = int(std::round(campY + std::sin(a) * r));
        if (!inb(x, y)) continue;
        int i = idx(x, y);
        if (tt[i] >= 3 && tt[i] != HILL && explored[i] && objAt[i] < 0 && bOcc[i] == -1) return std::pair{x, y};
    }
    return std::nullopt;
}

void Sim::invent(Person& p) {
    const Invention* next = nullptr;
    for (auto& i : inventionList()) if (!inventions.count(i.k)) { next = &i; break; }
    if (!next) { p.esteemPts = std::min(100.f, p.esteemPts + 2); return; }
    if (++p.inventProg < 4) { bubble(p, p.inventProg == 1 ? "hmm, what if..." : "almost..."); return; }
    p.inventProg = 0;
    inventions.insert(next->k);
    p.esteemPts = std::min(100.f, p.esteemPts + 20);
    note(cat(p.name, " invented ", next->n, "! ", next->d), "big");
}

void Sim::callingJob(Person& p) {
    const std::string c = p.calling;
    auto* f = fire();
    float cx = (f ? f->x : campX) + .5f, cy = (f ? f->y : campY) + .5f;
    auto done = [this](std::function<void(Person&)> extra) {
        return [this, extra](Person& q) {
            q.callingNeed = std::max(0.f, q.callingNeed - 70);
            q.esteemPts = std::min(100.f, q.esteemPts + 4);
            q.mood = std::min(100.f, q.mood + 4);
            if (extra) extra(q);
        };
    };
    if (c == "carver") {
        auto sp = totemSpot();
        if (!sp) return setQ(p, "calling", "Whittling small figures", {walkA(cx + 1.8f, cy + .3f), workA(100, "Whittling", "build", done({}))});
        int tx = sp->first, ty = sp->second;
        auto w = workA(150, "Carving a totem", "build", done([this, tx, ty](Person& q) {
            if (objAt[idx(tx, ty)] >= 0 || bOcc[idx(tx, ty)] != -1) return;
            Obj o; o.type = "totem"; o.x = tx; o.y = ty; o.v = ri(4);
            addObj(o);
            bOcc[idx(tx, ty)] = -4;
            totems++;
            if (totems == 1 || totems % 4 == 0) note(cat(q.name, " finished a carved totem. People touch it for luck as they pass. There are ", totems, " around camp now."));
        }));
        w.start = [](Person& q) { q.face = -1; };
        return setQ(p, "calling", "Carving a totem", {walkA(tx + 1.2f, ty + .6f), w}, onSpot(tx + .5f, ty + .6f));
    }
    if (c == "musician")
        return setQ(p, "calling", "Practicing the flute", {walkA(cx + 1.6f, cy + .4f), workA(90, "Playing music", "music", done([this](Person& q) {
            for (auto* o : members()) if (std::hypot(o->x - q.x, o->y - q.y) < 7) { o->mood = std::min(100.f, o->mood + 3); o->needs.social = std::max(0.f, o->needs.social - 8); }
        }))});
    if (c == "storyteller") {
        bool kids = false;
        for (auto* q : members()) if (q->child) kids = true;
        return setQ(p, "calling", kids ? "Telling stories to the children" : "Composing a saga", {walkA(cx - 1.4f, cy + .8f), workA(100, "Telling stories", "sit", done([this](Person& q) {
            for (auto* o : members()) if (o != &q && std::hypot(o->x - q.x, o->y - q.y) < 6) affAdd(*o, q.id, 3);
            static const std::vector<std::string> L = {" told the story of the wreck again. Each time the waves get taller.", " has started a saga about our first winter. The children know whole verses by heart.", " made up a tale about how the island was born from a sleeping turtle."};
            if (rnd() < .25f) note(q.name + pick(L));
        }))});
    }
    if (c == "explorer") {
        exploreJob(p);
        p.kind = "calling"; p.doing = "Charting the island";
        p.q.push_back(workA(15, "Drawing the map", "sit", done({})));
        return;
    }
    if (c == "inventor")
        return setQ(p, "calling", "Tinkering with an idea", {walkA(stockX + 2.4f, stockY + .5f), workA(150, "Tinkering", "build", done([this](Person& q) { invent(q); }))}, onStock());
    if (c == "healer") {
        Person* sick = nullptr;
        for (auto* q : members()) if (q->sick > 0 && q != &p) sick = q;
        if (sick)
            return setQ(p, "calling", "Tending to " + sick->name, {walkA(sick->x + .8f, sick->y + .3f), workA(60, "Tending the sick", "farm", done([this, sick](Person& q) {
                sick->sick = std::max(0.f, sick->sick - 14); bubble(*sick, "thank you"); affAdd(*sick, q.id, 8);
            }))}, onPerson(*sick));
        if (Obj* b = nearestObj([&](const Obj& o) { return o.type == "bush" && explored[idx(o.x, o.y)]; }, p.x, p.y))
            return setQ(p, "calling", "Gathering healing herbs", {walkA(b->x + 1.05f, b->y + .5f), workA(70, "Gathering herbs", "forage", done([this](Person&) { herbs = std::min(12.f, herbs + 2); }))}, onObject(b->i));
    }
    if (c == "teacher") {
        std::vector<Person*> kids;
        for (auto* q : members()) if (q->child) kids.push_back(q);
        if (!kids.empty()) {
            Person* k = pick(kids);
            return setQ(p, "calling", "Teaching " + k->name, {walkA(cx + .8f, cy - 1.4f), workA(100, "Teaching", "sit", done([this](Person&) { for (auto* q : members()) if (q->child) q->bornDay -= 1; }))});
        }
        std::string best = "build";
        float bv = -1;
        for (auto& [k, v] : p.sk) if (v > bv) { bv = v; best = k; }
        Person* st = nullptr;
        for (auto* q : members()) if (q != &p && !q->child && (!st || q->sk[best] < st->sk[best])) st = q;
        if (st)
            return setQ(p, "calling", cat("Teaching ", st->name, " ", skillNames().at(best)), {walkA(st->x + .8f, st->y + .3f), workA(80, "Teaching", "sit", done([this, st, best](Person& q) {
                st->sk[best] = std::min(10.f, st->sk[best] + .35f); affAdd(*st, q.id, 4);
            }))}, onPerson(*st));
    }
    if (c == "philosopher") {
        auto* sh = built("circle");
        float sx = sh ? sh->x + 2.3f : pondX + 2.6f, sy = sh ? sh->y + 1.f : pondY + .5f;
        return setQ(p, "calling", "Turning the story over", {walkA(sx, sy), workA(120, "Thinking", "sit", done([this](Person& q) {
            q.rapport = std::min(100.f, q.rapport + 5);
            for (auto* o : members()) o->rapport = std::min(100.f, o->rapport + .8f);
            if (rnd() < .2f) {
                auto fr = frontier();
                if (!fr.empty()) {
                    auto f2 = pick(fr);
                    reveal(f2.first, f2.second, 5);
                    note(cat(q.name, " came back from a long think by the water sure of a place to the ", dirWord(f2.first - campX, f2.second - campY), ". It turned out to be real."), "narrator");
                }
            }
        }))});
    }
    auto q = wanderQ(p);
    q.push_back(workA(60, "Daydreaming", "sit", done({})));
    setQ(p, "calling", "Daydreaming", q);
}

} // namespace dw
