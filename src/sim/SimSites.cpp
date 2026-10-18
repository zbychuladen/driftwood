// Building sites: whether a specialist building may go somewhere, how good the
// spot is and why, what each settler thinks of it, and placing it.
//
// Nothing here draws a random number from the behaviour generator: the player
// hovers over many spots before choosing one, and looking must not change the
// game. Opinions use a hash of who, where and what instead.
#include <algorithm>
#include <cmath>
#include "sim/Sim.h"

namespace dw {

namespace {

float hash01(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t x = a * 0x9E3779B1u ^ b * 0x85EBCA77u ^ c * 0xC2B2AE3Du;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
    return (x & 0xffffff) / float(0x1000000);
}

bool homeKey(const std::string& k) { return k == "hut" || k == "longhouse" || k == "leanto" || k == "cottage" || k == "house" || k == "manor"; }
bool quietKey(const std::string& k) { return k == "school" || k == "infirmary" || k == "herbs"; }
bool noisyKey(const std::string& k) { return k == "forge" || k == "mine"; }

} // namespace

bool Sim::siteLegal(const ProjectDef& d, int x, int y) const {
    auto self = const_cast<Sim*>(this);   // footprintOk only reads
    if (d.key == "mine") {
        if (!inb(x, y)) return false;
        int i = idx(x, y);
        return explored[i] && tt[i] == HILL && bOcc[i] == -1;
    }
    if (d.key == "lighthouse") {
        if (!inb(x, y)) return false;
        int i = idx(x, y);
        return explored[i] && tt[i] >= 2 && bOcc[i] == -1 && (self->footprintOk(x, y, 1, 1) || tt[i] == HILL);
    }
    if (d.key == "bridge") {
        // planks over water, with dry land under both ends
        int len = std::max(d.w, d.h), water = 0;
        for (int k = 0; k < len; k++) {
            int xx = d.w > d.h ? x + k : x, yy = d.w > d.h ? y : y + k;
            if (!inb(xx, yy) || !explored[idx(xx, yy)] || bOcc[idx(xx, yy)] != -1) return false;
            bool end = k == 0 || k == len - 1;
            if (end && (isWater(xx, yy) || tt[idx(xx, yy)] == HILL)) return false;
            if (!end && isWater(xx, yy)) water++;
        }
        return water > 0;
    }
    if (!self->footprintOk(x, y, d.w, d.h)) return false;
    if (d.key == "boatyard" || d.key == "ship") {
        for (int yy = y; yy < y + d.h; yy++) for (int xx = x; xx < x + d.w; xx++) if (distSea[idx(xx, yy)] <= 1) return true;
        return false;
    }
    return true;
}

SiteReport Sim::evaluateSite(const ProjectDef& d, int x, int y, int self) const {
    // a building judged where it already stands: its own footprint does not count against it
    if (self >= 0) {
        auto& occ = const_cast<std::vector<int>&>(bOcc);
        std::vector<int> mine;
        for (int i = 0; i < int(occ.size()); i++) if (occ[i] == self) { mine.push_back(i); occ[i] = -1; }
        SiteReport r = evaluateSite(d, x, y);
        for (int i : mine) occ[i] = self;
        return r;
    }
    SiteReport r;
    r.legal = siteLegal(d, x, y);
    if (!r.legal) return r;
    // fresh water distances, worked out once per island
    if (distFresh.size() != size_t(GW * GH)) {
        distFresh.assign(GW * GH, 1e9f);
        std::vector<int> q;
        for (int i = 0; i < GW * GH; i++) if (fresh[i]) { distFresh[i] = 0; q.push_back(i); }
        for (size_t h = 0; h < q.size(); h++) {
            int i = q[h], cx = i % GW, cy = i / GW;
            const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& dd : D) {
                int nx = cx + dd[0], ny = cy + dd[1];
                if (!inb(nx, ny)) continue;
                int j = idx(nx, ny);
                if (distFresh[j] > distFresh[i] + 1) { distFresh[j] = distFresh[i] + 1; q.push_back(j); }
            }
        }
    }
    const std::string& k = d.key;
    float cxm = x + d.w / 2.f, cym = y + d.h / 2.f;
    auto add = [&](const char* id, float w, std::string text) { r.factors.push_back({id, std::move(text), w}); };
    auto distTo = [&](float tx, float ty) { return std::hypot(cxm - tx, cym - ty); };
    // nearest finished (or begun) building of some kinds
    auto nearest = [&](auto pred) {
        float best = 1e9f;
        for (auto& b : plan) if (b.placed && !b.skip && pred(b.key)) best = std::min(best, distTo(b.x + b.w / 2.f, b.y + b.h / 2.f));
        return best;
    };
    float dCamp = distTo(campX + .5f, campY + .5f);
    float dFresh = 1e9f, dSea = 1e9f;
    int grass = 0, poor = 0, cells = 0, trees = 0, hill = 0;
    for (int yy = y; yy < y + d.h; yy++) for (int xx = x; xx < x + d.w; xx++) {
        if (!inb(xx, yy)) continue;
        int i = idx(xx, yy);
        dFresh = std::min(dFresh, distFresh[i]);
        dSea = std::min(dSea, distSea[i]);
        cells++;
        if (tt[i] == GRASS) grass++;
        if (tt[i] == FOREST || tt[i] == SAND) poor++;
        if (tt[i] == HILL) hill++;
        if (objAt[i] >= 0 && objs[objAt[i]].type == "tree") trees++;
    }
    auto countNear = [&](const char* type, int rad) {
        int n = 0;
        for (int yy = int(cym) - rad; yy <= int(cym) + rad; yy++) for (int xx = int(cxm) - rad; xx <= int(cxm) + rad; xx++)
            if (inb(xx, yy) && objAt[idx(xx, yy)] >= 0 && objs[objAt[idx(xx, yy)]].type == type) n++;
        return n;
    };
    float dHome = nearest(homeKey), dForge = nearest([](const std::string& b) { return noisyKey(b); }), dQuiet = nearest(quietKey);
    float dStock = distTo(stockX + 1.f, stockY + .5f), dFields = nearest([](const std::string& b) { return b == "garden" || b == "fields"; });
    float dFire = distTo(campX + .5f, campY + .5f), dStore = nearest([](const std::string& b) { return b == "store"; });

    // the walk from camp, for everything (the mine, boatyard and lighthouse are tied to their ground)
    bool tied = k == "mine" || k == "boatyard" || k == "lighthouse";
    float walkFree = tied ? 18 : 12;
    if (dCamp > walkFree) add("far", -std::min(.25f, (dCamp - walkFree) * .02f), "a long walk from camp");
    if (trees >= 2) add("grove", -std::min(.12f, trees * .03f), "means felling " + std::to_string(trees) + " trees");

    if (k == "garden" || k == "fields" || k == "herbs") {
        float good = cells ? float(grass) / cells : 0, bad = cells ? float(poor) / cells : 0;
        if (good > .6f) add("soil", .15f * good, "good, open soil");
        if (bad > .3f) add("poorsoil", -.2f * bad, "poor soil, roots and sand");
        if (dFresh <= 6) add("water", .1f, "water close by");
        else add("dry", -.1f * std::min(1.f, (dFresh - 6) / 10), "far from water");
        if (dForge < 6) add("smoke", -.12f, "beside the forge's smoke");
    }
    if (k == "well") {
        if (dFresh <= 3) add("easydig", .2f, "fresh water just below, an easy dig");
        else if (dFresh > 10) add("harddig", -.1f, "a long way down to water");
        if (dSea <= 2) add("brackish", -.25f, "too near the sea, the water will taste of salt");
        if (dHome <= 6) add("homes", .08f, "close to where people sleep");
        else if (dHome > 12 && dHome < 1e8f) add("farhomes", -.08f, "far from the huts");
    }
    if (k == "store") {
        if (dStock <= 5) add("stock", .12f, "right by the stockpile");
        else if (dStock > 10) add("farstock", -.1f, "a long haul from the stockpile");
        if (dFields <= 8) add("fields", .06f, "close to the fields");
        if (dSea <= 1) add("damp", -.12f, "low by the shore, the damp will get in");
    }
    if (k == "forge") {
        int rocks = countNear("rock", 8);
        bool mine = nearest([](const std::string& b) { return b == "mine"; }) <= 10;
        if (rocks >= 2 || mine) add("stone", .15f, "stone close at hand");
        else add("nostone", -.12f, "far from any stone");
        if (dHome < 4 || dQuiet < 4) add("homes", -.2f, "right beside the homes, all that smoke and hammering");
    }
    if (k == "pottery") {
        if (dFresh <= 5) add("clay", .15f, "clay by the water's edge");
        else add("noclay", -.1f, "no clay nearby");
        if (countNear("tree", 8) >= 5) add("wood", .05f, "firewood all around");
        if (dHome < 3) add("homes", -.08f, "kiln smoke over the huts");
    }
    if (k == "workshop") {
        // timber in, furniture out to the homes
        if (countNear("tree", 8) >= 5) add("timber", .1f, "timber close at hand");
        else add("notimber", -.08f, "a long carry for timber");
        if (dStock <= 6 || dStore <= 6) add("stores", .08f, "handy for the stockpile");
        if (dHome < 3) add("homes", -.08f, "sawdust and hammering beside the homes");
    }
    if (k == "loomhouse") {
        if (dFields <= 8) add("fiber", .1f, "near the fiber in the fields");
        if (dForge < 4) add("soot", -.08f, "soot from the forge on the cloth");
    }
    if (k == "infirmary" || k == "herbs") {
        if (dForge >= 8) add("quiet", .08f, "quiet, away from the hammering");
        else if (dForge < 5) add("noise", -.2f, "right beside the forge");
        if (dFresh <= 6 || nearest([](const std::string& b) { return b == "well"; }) <= 6) add("cleanwater", .08f, "clean water near");
    }
    if (k == "school") {
        if (dHome <= 8) add("homes", .1f, "close to the children's homes");
        if (dForge < 5) add("noise", -.18f, "too noisy beside the forge");
    }
    if (k == "bath") {
        if (dFresh <= 3) add("pool", .2f, "by water that can be warmed");
        else add("nopool", -.12f, "no water to fill it from");
    }
    if (k == "longhouse") {
        if (dFire <= 6) add("fire", .12f, "close to the fire");
        if (dFresh <= 8 || nearest([](const std::string& b) { return b == "well"; }) <= 8) add("water", .06f, "water within reach");
        if (dCamp > 10) add("apart", -.1f, "set apart from everyone");
    }
    if (k == "cookhouse") {
        if (dStock <= 5 || dStore <= 5) add("stores", .12f, "right by the food stores");
        if (dFields <= 8) add("fields", .06f, "near the fields");
        if (dForge < 4) add("smoke", -.15f, "forge smoke in the food");
        if (dCamp > 8) add("farwork", -.12f, "a long way from where people work");
    }
    if (k == "mine") {
        int hills = 0;
        for (int yy = y - 2; yy <= y + 2; yy++) for (int xx = x - 2; xx <= x + 2; xx++) if (inb(xx, yy) && tt[idx(xx, yy)] == HILL) hills++;
        if (hills >= 10) add("deep", .12f, "deep into a big hill");
        else if (hills <= 4) add("shallow", -.1f, "a thin hill, the seam will run out");
    }
    if (k == "boatyard") {
        if (nearest([](const std::string& b) { return b == "dock"; }) <= 6) add("dock", .08f, "beside the dock");
        if (hill) add("steep", -.08f, "a steep launch");
    }
    if (k == "lighthouse") {
        if (dSea <= 1) add("coast", .2f, "right on the coast, seen far out at sea");
        else add("inland", -.2f, "too far inland to be seen from the sea");
        if (tt[idx(x, y)] == HILL) add("high", .1f, "high ground");
    }
    float sum = 0;
    for (auto& f : r.factors) sum += f.weight;
    r.quality = std::clamp(1 + sum, .6f, 1.25f);
    // worst first, so the first reason is the one people mention
    std::stable_sort(r.factors.begin(), r.factors.end(), [](const SiteFactor& a, const SiteFactor& b) { return a.weight < b.weight; });
    return r;
}

std::vector<Opinion> Sim::opinions(const ProjectDef& d, int x, int y, int self) const {
    std::vector<Opinion> out;
    SiteReport r = evaluateSite(d, x, y, self);
    if (!r.legal) return out;
    uint32_t keyHash = uint32_t(std::hash<std::string>{}(d.key));
    for (auto& p : people) {
        if (p.dead || p.child || p.status != "member") continue;
        auto g = p.gifts.find(d.gift);
        float gift = g == p.gifts.end() ? 0.f : g->second / 10.f;
        auto bs = p.sk.find("build");
        float build = bs == p.sk.end() ? 0.f : bs->second / 10.f;
        float e = std::clamp(gift * .85f + build * .15f, 0.f, 1.f);
        // everyone's eye is off by some amount; the specialist's barely at all
        float noise = (hash01(uint32_t(p.id), uint32_t(x * 131 + y), keyHash) - .5f) * .6f;
        float temper = (p.has("cheerful") ? .08f : 0) - (p.has("gloomy") ? .08f : 0) - (p.has("obstinate") ? .05f : 0);
        float perceived = r.quality + (noise + temper) * (1 - e);
        std::string say;
        const SiteFactor* worst = r.factors.empty() || r.factors.front().weight >= 0 ? nullptr : &r.factors.front();
        const SiteFactor* best = r.factors.empty() || r.factors.back().weight <= 0 ? nullptr : &r.factors.back();
        if (e >= .45f) {
            // the specialist names the real reason
            if (perceived < .9f && worst) say = "No good: " + worst->text + ".";
            else if (perceived > 1.08f && best) say = "A fine spot: " + best->text + ".";
            else say = worst ? "It will do, though it is " + worst->text + "." : "It will do.";
        } else {
            static const char* happy[] = {"Looks fine to me.", "Why not here?", "I like it."};
            static const char* unsure[] = {"I am not sure about this spot.", "Hm. Somewhere else, maybe?", "Feels wrong, somehow."};
            int pickN = int(hash01(uint32_t(p.id), keyHash, uint32_t(x + y * 7)) * 3) % 3;
            say = perceived >= .95f ? happy[pickN] : unsure[pickN];
        }
        out.push_back({p.id, perceived, e, say});
    }
    // the specialists first, then whoever feels most strongly
    std::stable_sort(out.begin(), out.end(), [](const Opinion& a, const Opinion& b) {
        if ((a.expertise >= .45f) != (b.expertise >= .45f)) return a.expertise >= .45f;
        return std::abs(a.perceived - 1) > std::abs(b.perceived - 1);
    });
    return out;
}

std::optional<std::pair<int, int>> Sim::suggestSite(const ProjectDef& d) {
    // The settlers look over sites around camp (anywhere explored for buildings
    // tied to hills or shores) and pick the best one, with a little taste of
    // their own so villages differ.
    bool tied = d.key == "mine" || d.key == "boatyard" || d.key == "lighthouse";
    float r1 = (d.r1 > 0 ? d.r1 : 8) + (tied ? 18 : 6);
    float bs = -1e9f;
    int bx = -1, by = -1;
    for (int y = 1; y < GH - d.h - 1; y++) for (int x = 1; x < GW - d.w - 1; x++) {
        float dc = std::hypot(x + d.w / 2.f - campX - .5f, y + d.h / 2.f - campY - .5f);
        if (dc > r1 || dc < d.r0 * .6f) continue;
        if (!siteLegal(d, x, y)) continue;
        float s = evaluateSite(d, x, y).quality * 3 - dc * .02f + hash01(uint32_t(x), uint32_t(y), seed + uint32_t(d.key.size())) * .15f;
        if (s > bs) { bs = s; bx = x; by = y; }
    }
    if (bx < 0) return std::nullopt;
    return std::make_pair(bx, by);
}

float Sim::siteQ(const std::string& key) {
    auto* b = built(key);
    return b ? std::clamp(b->siteQuality * b->craft, .5f, 1.45f) : 1.f;
}

bool Sim::placeAt(int id, int x, int y) {
    Project* b = projectById(id);
    if (!b || b->placed || b->done) return false;
    SiteReport r = evaluateSite(*b, x, y);
    if (!r.legal) return false;
    claim(*b, x, y);
    if (b->key == "mine" || b->key == "lighthouse") {
        // tied to a single tile; claim() covered it
    }
    b->siteQuality = r.quality;
    b->sited = pendingPlacement && pendingPlacement->project == id;
    bool chosen = b->sited && pendingPlacement->chosen;
    if (pendingPlacement && pendingPlacement->project == id) pendingPlacement.reset();
    siteJudged(*b, false);
    // a blueprint from the build menu waits its turn: the settlers start it when they see fit
    if (!chosen) startProject(*b);
    else {
        b->waitNoted = false;
        note(cat("The narrator marked out a spot for a ", lower(b->name), ". The camp will get to it when they can."));
        // the rival speaks against it, and their followers listen
        if (Person* rv = rival >= 0 ? person(rival) : nullptr) {
            note(cat(rv->name, " said a ", lower(b->name), " was the last thing we needed. A few nodded."));
            for (auto* p : members()) if (p->follow >= 40) p->follow = std::min(100.f, p->follow + 2);
        }
    }
    return true;
}

bool Sim::placeSuggested(int id) {
    Project* b = projectById(id);
    if (!b) return false;
    int x, y;
    if (pendingPlacement && pendingPlacement->project == id) { x = pendingPlacement->sx; y = pendingPlacement->sy; }
    else if (auto s = suggestSite(*b)) { x = s->first; y = s->second; }
    else return false;
    bool asked = pendingPlacement && pendingPlacement->project == id;
    bool ok = placeAt(id, x, y);
    if (ok && asked) b->sited = false;   // the settlers chose after all
    return ok;
}

// What the camp says about the spot: when it is chosen, and again when the
// building is finished and people live with it.
void Sim::siteJudged(Project& b, bool finished) {
    auto ops = opinions(b, b.x, b.y, b.id);
    if (ops.empty()) return;
    const Opinion& lead = ops.front();
    Person* who = const_cast<Sim*>(this)->person(lead.pid);
    if (!who) return;
    std::string what = lower(b.name);
    if (!finished) {
        if (lead.expertise >= .45f && lead.perceived < .85f)
            note(cat(who->name, " frowned at the spot chosen for the ", what, ". \"", lead.say, "\""));
        else if (lead.expertise >= .45f && lead.perceived > 1.1f)
            note(cat(who->name, " walked the ground for the ", what, " and nodded. \"", lead.say, "\""));
        bubble(*who, lead.perceived < .85f ? "here? really?" : lead.perceived > 1.1f ? "good spot" : "it will do", 90);
        return;
    }
    // the narrator's own spot pays off or backfires
    if (b.sited) earn(std::clamp((b.siteQuality - 1) * 40, -8.f, 8.f));
    // living with it: those who thought it a poor spot are put out, the rest pleased
    for (auto& o : ops) {
        Person* p = person(o.pid);
        if (!p) continue;
        // the narrator chose the spot (b.sited): a good one earns trust, a bad one costs it, most of all with the specialist
        if (o.perceived < .85f) {
            p->mood = std::max(0.f, p->mood - 3 - 6 * o.expertise);
            if (b.sited) p->buyIn = std::max(0.f, p->buyIn - 1 - 3 * o.expertise);
        } else if (o.perceived > 1.1f) {
            p->mood = std::min(100.f, p->mood + 2 + 3 * o.expertise);
            if (b.sited) p->buyIn = std::min(100.f, p->buyIn + 2 + 3 * o.expertise);
        }
    }
    if (lead.expertise >= .45f && lead.perceived < .85f)
        note(cat(who->name, " still grumbles about where the ", what, " went. \"", lead.say, "\""), "sad");
    else if (lead.expertise >= .45f && lead.perceived > 1.1f)
        note(cat(who->name, " is proud of the ", what, ". The spot could not have been better."));
}

} // namespace dw
