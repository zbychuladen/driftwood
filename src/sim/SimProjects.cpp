// Projects: what gets built next (need vs. persuasion), placement, completion,
// specialist gifts and the camp agenda set by charismatic people.
#include <algorithm>
#include "Sim.h"

namespace dw {

Project& Sim::mkProj(const ProjectDef& d, bool voiced) {
    Project p;
    static_cast<ProjectDef&>(p) = d;
    p.id = nextProjId++;
    p.voiced = voiced;
    plan.push_back(p);
    return plan.back();
}

float Sim::foodDays() { return stock.food / std::max(1.f, float(members().size()) * 2); }

float Sim::needOf(const std::string& k) {
    auto m = members();
    int pop = int(m.size());
    if (k == "fire") return 5;
    if (k == "leanto") return totalBeds(false) == 0 ? 2.2f : 0;
    if (k == "hut") {
        // every household wants a home of its own, beyond a bed in the longhouse
        int homeless = 0;
        for (auto& h : households) homeless += h.home < 0;
        int bd = totalBeds(false);
        return bd < pop ? 1.f : homeless > 0 ? .6f : bd < pop + 2 ? .35f : .05f;
    }
    if (k == "rack") return .45f;
    if (k == "garden") return foodDays() < 8 ? .75f : .35f;
    if (k == "fields") return (pop > 8 ? .5f : .15f) + (foodDays() < 6 ? .35f : 0);
    if (k == "well") return .45f;
    if (k == "store") return stock.food > 40 ? .6f : .3f;
    if (k == "dock") return .35f;
    if (k == "longhouse") return pop >= 7 ? .75f : .3f;
    if (k == "tower") return lastRaid && day() - lastRaid < 10 ? .9f : .2f;
    if (k == "forge") return std::min(1.f, .1f + toolWear / 90);
    if (k == "mine") return mods.count("stoneShort") && day() - mods["stoneShort"] < 3 ? .6f : .15f;
    if (k == "herbs") { bool sick = false; for (auto* p : m) if (p->sick > 0) sick = true; return sick || (lastSick && day() - lastSick < 8) ? .7f : .2f; }
    if (k == "pottery") return stock.food > 30 ? .5f : .25f;
    if (k == "loomhouse") return season() >= 2 ? .7f : .2f;
    if (k == "circle") return avgBuyIn() / 120;
    if (k == "school") { for (auto* p : m) if (p->child) return .6f; return .1f; }
    if (k == "infirmary") return pop > 10 ? .5f : .2f;
    if (k == "signal" || k == "boatyard") return .3f;
    if (k == "smoke") return .35f;
    if (k == "cookhouse") return pop >= 6 ? .55f : .2f;
    if (k == "monument") return .6f;
    if (k == "clearing" || k == "orchard" || k == "bridge" || k == "flowers" || k == "lantern" || k == "bench" || k == "banner") return .3f;
    if (k == "workshop") { int better = 0; for (auto& h : households) better += h.home >= 0 && projectById(h.home) && homeLevel(projectById(h.home)->key) >= 2; return better ? .55f : .2f; }
    return .25f;
}

bool Sim::prereqOk(const ProjectDef& b) {
    if (b.req.empty()) return true;
    if (b.req == "child") { for (auto* p : members()) if (p->child) return true; return false; }
    return built(b.req) != nullptr;
}

Person* Sim::giftHolder(const std::string& g, int min) {
    Person* best = nullptr;
    for (auto* p : members()) {
        if (p->child) continue;
        auto it = p->gifts.find(g);
        if (it == p->gifts.end() || it->second < std::max(1, min)) continue;
        if (!best || it->second > best->gifts[g]) best = p;
    }
    return best;
}

bool Sim::giftOk(const ProjectDef& b) { return b.gift.empty() || giftHolder(b.gift, b.min); }

float Sim::influence(Person& p) {
    float s = 0; int c = 0;
    for (auto* q : members()) if (q != &p && !q->child) { s += affOf(*q, p.id); c++; }
    float resp = c ? s / c : 30;
    return p.charisma / 10.f * (.55f + resp / 100 * .9f);
}

std::optional<Sim::Champion> Sim::champion(Project& b) {
    if (b.gift.empty()) return std::nullopt;
    Person* h = giftHolder(b.gift, b.min);
    if (!h) return std::nullopt;
    return Champion{h, influence(*h) * 1.6f * (.75f + h->gifts[b.gift] / 20.f)};
}

float Sim::projScore(Project& b, int i) {
    if (b.done || b.skip || (b.wait && b.wait > day()) || !prereqOk(b) || !giftOk(b)) return -1e9f;
    if (askPlacement && !overruled() && !b.placed && !settlersBuild(b)) return -1e9f;   // waits for the narrator's blueprint (unless the rival plans)
    float s = 3 - i * .03f + needOf(b.key) * 2 + (b.voiced ? .6f : 0);
    if (b.sited && askPlacement) s += 1.4f;   // the narrator marked it out: they take that as wanted
    if (auto ch = champion(b)) s += ch->push;
    return s;
}

Project* Sim::activeProject() {
    auto* a = active >= 0 ? projectById(active) : nullptr;
    return a && !a->done && !a->skip ? a : nullptr;
}

Project* Sim::currentProject() {
    if (auto* a = activeProject()) return a;
    for (int tries = 0; tries < 6; tries++) {
        Project* best = nullptr;
        float bs = -1e8f;
        int i = 0;
        for (auto& b : plan) { float sc = projScore(b, i++); if (sc > bs) { bs = sc; best = &b; } }
        if (!best) { active = -1; return nullptr; }
        if (!best->placed && specialist(*best)) {
            // a specialist building: the narrator chooses the spot when asked to, otherwise the settlers do
            auto s = suggestSite(*best);
            if (!s) {
                if (!best->waitNoted) { best->waitNoted = true; note(cat("There is no good spot for the ", lower(best->name), " yet. It will have to wait until more land is cleared or explored.")); }
                best->wait = day() + 3;
                continue;
            }
            if (askPlacement && !overruled()) {
                if (!pendingPlacement) pendingPlacement = PendingPlacement{best->id, s->first, s->second};
                return nullptr;   // nothing to build until the spot is chosen
            }
            placeAt(best->id, s->first, s->second);
            return best;
        }
        if (!best->placed && !place(*best)) {
            if (!best->waitNoted) { best->waitNoted = true; note(cat("There is no good spot for the ", lower(best->name), " yet. It will have to wait until more land is cleared or explored.")); }
            best->wait = day() + 3;
            continue;
        }
        startProject(*best);
        return best;
    }
    return nullptr;
}

bool Sim::footprintOk(int x, int y, int w, int h) {
    for (int yy = y - 1; yy <= y + h; yy++) for (int xx = x - 1; xx <= x + w; xx++) {
        if (!inb(xx, yy)) return false;
        int i = idx(xx, yy);
        bool inside = xx >= x && xx < x + w && yy >= y && yy < y + h;
        if (inside && (tt[i] < 2 || !explored[i] || tt[i] == HILL)) return false;
        if (bOcc[i] != -1) return false;
    }
    return true;
}

bool Sim::claim(Project& b, int x, int y) {
    b.x = x; b.y = y; b.placed = true;
    for (int yy = y; yy < y + b.h; yy++) for (int xx = x; xx < x + b.w; xx++) {
        int i = idx(xx, yy);
        bOcc[i] = b.id;
        if (b.key == "clearing") continue;   // its trees come down as the work is done
        if (objAt[i] >= 0) {
            auto& o = objs[objAt[i]];
            if (o.type == "tree") stock.wood += 2;
            if (o.type == "rock") stock.stone += 2;
            removeObj(o);
        }
    }
    if (b.key == "garden" || b.key == "herbs" || b.key == "fields") { b.state = "empty"; b.growth = 0; }
    return true;
}

bool Sim::place(Project& b) {
    float cx = campX + .5f, cy = campY + .5f;
    if (b.key == "fire") { b.x = campX; b.y = campY; b.placed = true; b.fuel = 0; return true; }
    if (b.key == "dock") {
        float bd = 1e9; bool found = false;
        const int D[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        for (int y = 1; y < GH - 1; y++) for (int x = 1; x < GW - 1; x++) {
            int i = idx(x, y);
            if (tt[i] < 2 || !explored[i]) continue;
            for (auto& d : D) {
                bool ok = true;
                for (int k = 1; k <= 3; k++) { int nx = x + d[0] * k, ny = y + d[1] * k; if (!inb(nx, ny) || !sea[idx(nx, ny)] || bOcc[idx(nx, ny)] != -1) ok = false; }
                float dist = std::hypot(float(x - campX), float(y - campY));
                if (ok && dist < bd) { bd = dist; found = true; b.x = x + d[0]; b.y = y + d[1]; b.dx = d[0]; b.dy = d[1]; b.lx = x; b.ly = y; }
            }
        }
        if (!found) return false;
        b.placed = true;
        for (int k = 0; k < 3; k++) bOcc[idx(b.x + b.dx * k, b.y + b.dy * k)] = b.id;
        return true;
    }
    if (b.key == "mine") {
        float bd = 1e9; int bx = -1, by = -1;
        for (int y = 1; y < GH - 1; y++) for (int x = 1; x < GW - 1; x++) {
            int i = idx(x, y);
            if (!explored[i] || tt[i] != HILL || bOcc[i] != -1) continue;
            float d = std::hypot(float(x - campX), float(y - campY));
            if (d < bd && d < 24) { bd = d; bx = x; by = y; }
        }
        if (bx < 0) if (auto* r = nearestObj([&](const Obj& o) { return o.type == "rock" && explored[idx(o.x, o.y)]; }, campX, campY)) { bx = r->x; by = r->y; }
        if (bx < 0) return false;
        b.x = bx; b.y = by; b.placed = true;
        int i = idx(bx, by);
        bOcc[i] = b.id;
        if (objAt[i] >= 0) removeObj(objs[objAt[i]]);
        return true;
    }
    if (b.key == "signal" || b.key == "lighthouse") {
        float bs = -1e9; int bx = -1, by = -1;
        for (int y = 2; y < GH - 2; y++) for (int x = 2; x < GW - 2; x++) {
            int i = idx(x, y);
            if (!explored[i] || tt[i] < 2 || bOcc[i] != -1) continue;
            if (!footprintOk(x, y, 1, 1) && tt[i] != HILL) continue;
            float d = std::hypot(float(x - campX), float(y - campY));
            if (d < 5 || d > 20) continue;
            float s = (b.key == "signal" ? (tt[i] == HILL ? 6.f : 0.f) : (distSea[i] <= 1 ? 6.f : -distSea[i])) - std::abs(d - 10) * .3f + rnd();
            if (s > bs) { bs = s; bx = x; by = y; }
        }
        if (bx < 0) return false;
        b.x = bx; b.y = by; b.placed = true;
        int i = idx(bx, by);
        bOcc[i] = b.id;
        if (objAt[i] >= 0) removeObj(objs[objAt[i]]);
        return true;
    }
    if (b.key == "boatyard") {
        float bd = 1e9; int bx = -1, by = -1;
        for (int y = 2; y < GH - 3; y++) for (int x = 2; x < GW - 3; x++) {
            if (!footprintOk(x, y, 2, 2)) continue;
            bool s = false;
            for (int yy = y; yy < y + 2; yy++) for (int xx = x; xx < x + 2; xx++) if (distSea[idx(xx, yy)] <= 1) s = true;
            float d = std::hypot(float(x - campX), float(y - campY));
            if (s && d < bd) { bd = d; bx = x; by = y; }
        }
        return bx >= 0 && claim(b, bx, by);
    }
    float r0 = b.r0, r1 = b.r1 > 0 ? b.r1 : 8, mid = (r0 + r1) / 2, bs = -1e9;
    int bx = -1, by = -1;
    for (int pass = 0; pass < 2 && bx < 0; pass++)
        for (int y = 1; y < GH - b.h - 1; y++) for (int x = 1; x < GW - b.w - 1; x++) {
            float d = std::hypot(x + b.w / 2.f - cx, y + b.h / 2.f - cy);
            if (d < r0 || d > r1 + (pass ? 12 : 3)) continue;
            if (!footprintOk(x, y, b.w, b.h)) continue;
            float clutter = 0;
            for (int yy = y; yy < y + b.h; yy++) for (int xx = x; xx < x + b.w; xx++) {
                if (objAt[idx(xx, yy)] >= 0) clutter++;
                if (tt[idx(xx, yy)] == SAND) clutter += .5f;
            }
            float s = -std::abs(d - mid) - clutter * .6f - (d > r1 ? 4 : 0) + rnd() * 1.2f;
            if (s > bs) { bs = s; bx = x; by = y; }
        }
    return bx >= 0 && claim(b, bx, by);
}

std::pair<float, float> Sim::workSpot(Project& b) {
    if (b.key == "dock") return {b.lx + .5f, b.ly + .5f};
    if (b.key == "mine") return {b.x + 1.3f, b.y + .7f};
    return {b.x + b.w + .25f, b.y + b.h / 2.f + (rnd() - .5f) * (b.h - .3f)};
}

float Sim::giftBuild(Person& p, Project& b) {
    float f = 1;
    if (!b.gift.empty() && p.gifts.count(b.gift)) f *= 1.2f + p.gifts[b.gift] * .04f;
    if (p.gifts.count("carpenter") && b.wood >= b.stone) f *= 1.15f;
    if (p.gifts.count("mason") && b.stone > b.wood) f *= 1.15f;
    return f;
}

void Sim::startProject(Project& b) {
    active = b.id;
    auto ch = champion(b);
    float nd = needOf(b.key) * 2;
    if (ch && ch->push > nd && ch->push > .9f)
        note(cat(ch->p->name, ", our ", lower(giftInfo().at(b.gift).n), ", talked everyone into starting a ", lower(b.name), " next. When ", ch->p->name, " speaks, people listen."));
    else if (ch)
        note(cat("Work began on a ", lower(b.name), ", with ", ch->p->name, " leading the way."));
}

void Sim::finish(Project& b) {
    b.done = true;
    if (b.byHand) finishByHand(b);
    if (b.craftW > 0 && !settlersBuild(b) && (b.craft >= 1.12f || b.craft < .85f))
        note(b.craft >= 1.12f ? cat("The ", lower(b.name), " is finely made. Whoever built it took real care.") : cat("The ", lower(b.name), " went up roughly. It will do, but it will not do as well."));
    if (specialist(b)) siteJudged(b, true);
    if (active == b.id) active = -1;
    b.holderAtFinish = giftOk(b);
    std::vector<Person*> ws;
    for (int id : b.workers) if (auto* p = person(id)) ws.push_back(p);
    std::string who = ws.empty() ? "Everyone" : listNames(ws);
    static const std::map<std::string, std::string> L = {
        {"fire", " got the campfire going. The first warm meal in days. Someone laughed for the first time since the wreck."},
        {"leanto", " finished a lean-to of branches and ferns. It leaks, but it is ours."},
        {"rack", " built a drying rack. Strips of fish now hang in the smoke and keep for longer."},
        {"hut", " raised a round hut with a thatched roof. Two of us will sleep dry tonight."},
        {"garden", " cleared and turned a garden plot for the seeds from the sack."},
        {"well", " lined a well with stone. No more walking to the pond."},
        {"store", " finished the storehouse. Food keeps much longer behind its thick walls."},
        {"dock", " built a dock out over the water. The fish bite better past the shallows."},
        {"tower", " raised a watchtower. From the top you can see the whole island."},
        {"signal", " piled a great signal fire on the high ground. We keep it smoking day and night, hoping a ship will see."},
        {"smoke", " finished the smokehouse. The whole camp smells of fish and alder smoke."},
        {"herbs", " planted an herb garden: yarrow, mint and a bitter leaf that soothes fever."},
        {"circle", " laid log seats round a fire pit at the edge of camp. Most evenings someone tells a story there, and the narrator gets a few lines in."},
        {"boatyard", " launched our new boat. They sailed all the way around the island and drew the coastline on bark."},
        {"lighthouse", " finished the lighthouse. Its fire can be seen from far out at sea."},
        {"school", " built a schoolhouse. The children sit in a circle and learn letters scratched in clay."},
        {"bath", " dug a pool over a warm spring and lined it with stone. Pure bliss after a cold day."},
        {"fields", " cleared a wide stretch of meadow and sowed it with grain. If it takes, we will never starve again."},
        {"forge", " built a forge of stone and clay. The bellows wheeze, the coals glow, and the hammer rings across camp."},
        {"mine", " opened a mine into the hillside, propped up with timbers. Good stone, and plenty of it."},
        {"pottery", " built a beehive kiln. Soon there will be pots, jars and bowls."},
        {"loomhouse", " finished the loom house. Bundles of fiber hang from every rafter."},
        {"infirmary", " finished the infirmary: clean beds, dried herbs, and quiet."},
        {"cookhouse", " finished the cookhouse. A proper hearth, a long table, and something always simmering."}};
    if (b.key == "fire") b.fuel = 3;
    if (b.key == "signal" || b.key == "lighthouse") b.fuel = 99;
    if (b.key == "longhouse") {
        static const std::vector<std::string> A = {"Driftwood", "Saltmarsh", "Ember", "Gull", "Pine", "Lantern", "Fern", "Otter", "Kelp", "Hearth", "Heron", "Cinder"};
        static const std::vector<std::string> B = {"Hollow", "Rest", "Point", "Haven", "Reach", "Landing", "Cove", "Green", "Hold"};
        villageName = pick(A) + " " + pick(B);
        note(cat(who, " finished the longhouse. We feasted inside it and gave our home a name: ", villageName, "."), "big");
    } else {
        auto it = L.find(b.key);
        note(it != L.end() ? who + it->second : cat(who, " finished the ", lower(b.name), "."), "big");
    }
    if (b.key == "boatyard")
        for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) if (tt[idx(x, y)] >= 2 && distSea[idx(x, y)] <= 2) reveal(x, y, .8f);
    if (b.voiced) for (auto* p : members()) p->buyIn = std::min(100.f, p->buyIn + 5);
    for (auto* p : ws) {
        p->esteemPts = std::min(100.f, p->esteemPts + 12);
        p->mood = std::min(100.f, p->mood + 8);
        for (auto* q : ws) if (q != p) affAdd(*p, q->id, 3);
    }
    assignBeds();
}

float Sim::giftNeed(const std::string& g) {
    float n = 0;
    for (auto& b : plan) if (!b.done && !b.skip && b.gift == g && prereqOk(b)) n = std::max(n, needOf(b.key));
    if (g == "smith" && built("forge")) n = std::max(n, toolWear / 100);
    if (g == "farmer" && (built("garden") || built("fields"))) n = std::max(n, foodDays() < 8 ? .6f : .25f);
    if (g == "sailor") n = std::max(n, foodDays() < 6 ? .5f : .2f);
    return n;
}

static bool hasWorkshop(const std::string& g) { return g == "smith" || g == "potter" || g == "weaver" || g == "herbalist"; }

void Sim::updateAgenda() {
    struct Best { Person* p; std::string g; float inf, nd; };
    std::optional<Best> best;
    float bs = 0;
    for (auto* p : members()) {
        if (p->child) continue;
        for (auto& [g, lv] : p->gifts) {
            if (lv < 3) continue;
            float inf = influence(*p), nd = giftNeed(g);
            if (nd <= .05f && !hasWorkshop(g)) continue;
            bool ok = inf >= .62f || nd >= .6f;
            if (!ok) {
                if (nd > .1f && rnd() < .06f && p->charisma <= 4)
                    for (auto& b : plan) if (!b.done && !b.skip && b.gift == g && prereqOk(b)) {
                        note(cat(p->name, " keeps quietly suggesting a ", lower(b.name), ", but nobody really listens. Maybe when we truly need it."));
                        break;
                    }
                continue;
            }
            if (inf + nd > bs) { bs = inf + nd; best = Best{p, g, inf, nd}; }
        }
    }
    auto old = agenda;
    if (!best) {
        if (old) note("Without anyone pushing, work drifted back to whatever needed doing most.");
        agenda.reset();
        return;
    }
    auto& gi = giftInfo().at(best->g);
    agenda = Agenda{best->p->id, best->g, gi.job, clampf(std::max(best->inf, best->nd), .3f, 1.3f)};
    if (!old || old->by != best->p->id || old->gift != best->g) {
        static const std::map<std::string, std::string> verb = {{"stone", "breaking stone"}, {"wood", "hauling timber"}, {"farm", "working the soil"}, {"food", "gathering food"}};
        if (best->inf >= .62f && best->nd < .5f)
            note(cat(best->p->name, ", our ", lower(gi.n), ", has talked of nothing but ", gi.v, " for days. Half the camp is ", verb.at(gi.job), " now, even though it is not what we need most. That is what a persuasive neighbour can do."));
        else
            note(cat(best->p->name, " finally has everyone's ear: we really do need ", gi.v, ". The camp is ", verb.at(gi.job), "."));
    }
}

} // namespace dw
