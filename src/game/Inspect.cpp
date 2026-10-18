#include "game/Inspect.h"
#include <cmath>
#include <map>
#include "game/Art.h"
#include "game/Scale.h"

using namespace dw;

static std::string names(Sim& s, const std::vector<int>& ids) {
    std::string out;
    for (int id : ids)
        if (auto* p = s.person(id); p && !p->dead) out += (out.empty() ? "" : ", ") + p->name;
    return out;
}

static std::string metres(float m) { return cat(int(std::lround(m)), " m"); }

// What each structure is for, in the journal's words. Proposals carry their
// own text in the project table; these cover the ones the camp plans itself.
static std::string purpose(const Project& b) {
    static const std::map<std::string, std::string> d = {
        {"fire", "Warmth, light and cooked meals. Someone has to keep feeding it."},
        {"leanto", "A slanted roof of branches. Five can sleep under it, out of the rain."},
        {"rack", "Fish and meat dry here, so food keeps for longer."},
        {"hut", "A round hut of wattle and thatch, with room for two."},
        {"cottage", "A snug cottage with a chimney and proper windows, for a household that is getting on."},
        {"house", "A two-storey house, the home of a household that has done well."},
        {"manor", "A manor of stone and slate. Whoever lives here is somebody."},
        {"dock", "Planks out over the water, for fishing beyond the surf."},
        {"tower", "A lookout over the island and the first place to spot trouble."},
        {"garden", "Turned earth for vegetables, tended by whoever has the knack."},
        {"fields", "Rows of grain. Hard work at sowing and harvest, bread all winter."},
        {"well", "Clean water close to camp. No more trips to the pond."},
        {"store", "Dry storage, so less food spoils."},
        {"longhouse", "A long hall with beds for six and a hearth for everyone."},
        {"forge", "A fire hot enough to work metal, for better tools."},
        {"mine", "A shaft into the hill for good stone."},
        {"herbs", "Healing plants, grown on purpose instead of hunted for."},
        {"pottery", "A kiln for pots, so food and water keep."},
        {"loomhouse", "Looms for cloth, so the cold bites less."},
        {"ship", "The ship home, if they ever finish it."},
        {"workshop", "Saws, planes and a bench, where a carpenter makes the furniture better homes need."},
        {"cookhouse", "A hearth and a long table. Meals here fill people up better, if it is in a good spot."}};
    if (!b.desc.empty()) return b.desc;
    auto it = d.find(b.key);
    return it != d.end() ? it->second : "Something the camp built together.";
}

// Settlers whose current work points at this thing, with what they are doing.
static std::string workingOn(Sim& s, Focus::Kind kind, int id) {
    std::string out;
    for (auto* m : s.members())
        if (m->focus.kind == kind && m->focus.id == id && !m->q.empty()) out += (out.empty() ? "" : "; ") + m->name + ", " + lower(m->doing);
    return out;
}

static Info building(Sim& s, Project& b) {
    Info in{s.nameOf(b), b.customName.empty() ? "Building" : "Building, the " + lower(b.name), purpose(b), {}};
    float pct = b.work > 0 ? b.progress / b.work : 0;
    in.rows.push_back({"Status", b.done ? "Built" : b.progress > 0 ? cat("Being built, ", int(pct * 100), "%") : "Planned"});
    in.rows.push_back({"Size", cat(int(b.w * scale::TILE_M), " by ", int(b.h * scale::TILE_M), " m")});
    if (Sim::specialist(b) && b.placed) {
        // how good its spot is, and the main reason, as the specialist would put it
        SiteReport r = s.evaluateSite(b, b.x, b.y, b.id);
        float q = b.siteQuality;
        std::string word = q >= 1.15f ? "Excellent" : q >= 1.03f ? "Good" : q >= .92f ? "Fair" : q >= .8f ? "Poor" : "Bad";
        std::string why = r.factors.empty() ? "" : ", " + (q < 1 ? r.factors.front().text : r.factors.back().text);
        in.rows.push_back({"Spot", cat(word, why, b.sited ? " (your choice)" : "")});
    }
    if (b.done && b.craftW > 0) in.rows.push_back({"Workmanship", cat(Sim::craftWord(b.craft), b.craft >= 1.12f ? ", built with real care" : b.craft < .85f ? ", it does not work as well as it might" : "")});
    if (b.done && Sim::homeLevel(b.key)) {
        // whose home it is, their standing, and what the next level asks of it
        Household* hh = nullptr;
        for (auto& h : s.households) if (h.home == b.id) hh = &h;
        if (!hh) in.rows.push_back({"Home of", "nobody yet"});
        else {
            int lv = Sim::homeLevel(b.key);
            in.rows.push_back({"Home of", cat(s.householdName(*hh), ", ", Sim::statusName(lv), "s")});
            std::string lacking;
            for (auto& n : s.homeNeeds(*hh, lv)) if (!n.met) lacking += (lacking.empty() ? "" : ", ") + n.text;
            if (!lacking.empty()) in.rows.push_back({"Slipping", cat("short of ", lacking, hh->falling ? cat(" (", Sim::FALL_DAYS - hh->falling, " days before it falls back)") : "")});
            if (lv < 4) {
                std::string next;
                for (auto& n : s.homeNeeds(*hh, lv + 1)) next += (next.empty() ? "" : "; ") + std::string(n.met ? "has " : "needs ") + n.text;
                HomeCost c = Sim::upgradeCost(lv + 1);
                next += cat("; ", c.wood, " wood and ", c.stone, " stone");
                in.rows.push_back({cat("To a ", Sim::levelKey(lv + 1)), next});
            }
        }
    }
    if (const bld::Design* d = art::designFor(s, b)) {
        // what this one was built from: its own materials, not just its type's
        std::string made = !d->made.empty() ? d->made : d->walls + " walls, " + d->roof + " roof";
        if (d->windows) made += cat(", ", d->windows, d->windows == 1 ? " window" : " windows");
        if (d->porch) made += ", a porch";
        in.rows.push_back({"Built of", made});
    }
    if (!b.done) {
        if (b.wood > 0) in.rows.push_back({"Wood", cat(int(std::floor(b.dw)), " of ", b.wood, " delivered")});
        if (b.stone > 0) in.rows.push_back({"Stone", cat(int(std::floor(b.ds)), " of ", b.stone, " delivered")});
        if (!b.gift.empty() && giftInfo().count(b.gift)) in.rows.push_back({"Needs", cat("a ", lower(giftInfo().at(b.gift).n), " (skill ", b.min, ")")});
    }
    std::string who = names(s, std::vector<int>(b.workers.begin(), b.workers.end()));
    if (!who.empty()) in.rows.push_back({b.done ? "Built by" : "Builders", who});
    if (b.beds > 0) {
        std::vector<int> sleepers;
        for (auto* m : s.members()) if (m->bed == b.id) sleepers.push_back(m->id);
        std::string sl = names(s, sleepers);
        in.rows.push_back({"Beds", cat(int(sleepers.size()), " of ", b.beds, " taken", sl.empty() ? "" : ": " + sl)});
    }
    if (std::string now = workingOn(s, Focus::PROJECT, b.id); !now.empty()) in.rows.push_back({"Here now", now});
    if (b.key == "fire" && b.done) in.rows.push_back({"Fire", b.fuel > 0 ? cat("Burning, ", int(std::ceil(b.fuel)), " logs left") : "Gone out"});
    if ((b.key == "garden" || b.key == "fields" || b.key == "herbs") && b.done) {
        std::string st = b.state == "ripe" ? "Ripe, ready to harvest" : b.state == "growing" ? cat("Growing, ", int(b.growth * 100), "%") : "Waiting to be sown";
        in.rows.push_back({"Crop", st});
    }
    return in;
}

static Info object(Sim& s, const Obj& o) {
    Info in;
    Person* by = o.res >= 0 ? s.person(o.res) : nullptr;
    if (o.type == "tree") {
        in.title = o.palm ? "Palm" : o.conifer ? "Pine" : "Broadleaf tree";
        in.kind = "Tree";
        in.desc = o.palm ? "A palm. Its trunk is too stringy for timber, so nobody fells it." : "A tree. Good timber when the camp needs wood.";
        float h = (o.palm ? scale::PALM : o.conifer ? scale::CONIFER : scale::BROADLEAF) * o.size * (o.sapling ? .35f + o.grow * .6f : 1);
        in.rows.push_back({"Height", cat("about ", metres(h))});
        if (o.sapling) in.rows.push_back({"Age", cat("A sapling, ", int(std::min(1.f, o.grow) * 100), "% grown")});
        if (by) in.rows.push_back({"Felling", by->name + " has picked this one"});
        if (std::string now = workingOn(s, Focus::OBJECT, o.i); !now.empty() && !by) in.rows.push_back({"Here now", now});
    } else if (o.type == "rock") {
        in = {"Boulder", "Stone", "A boulder. Masons split it for building stone.", {}};
        in.rows.push_back({"Stone left", cat(o.stone, o.stone == 1 ? " load" : " loads")});
        if (by) in.rows.push_back({"Quarrying", by->name + " is working it"});
        if (std::string now = workingOn(s, Focus::OBJECT, o.i); !now.empty() && !by) in.rows.push_back({"Here now", now});
    } else if (o.type == "bush") {
        in = {"Berry bush", "Bush", "Wild berries. Anyone can pick them, in season.", {}};
        if (std::string now = workingOn(s, Focus::OBJECT, o.i); !now.empty()) in.rows.push_back({"Here now", now});
        in.rows.push_back({"Berries", s.season() == 3 ? "None until spring" : int(o.berries) > 0 ? cat(int(o.berries), " handfuls") : "Picked clean for now"});
    } else if (o.type == "stump") {
        in = {"Stump", "Felled tree", "What is left of a tree the camp cut down. A sapling will come up here.", {}};
        if (o.regrow > s.day()) in.rows.push_back({"Sprouts", cat("around day ", o.regrow)});
    } else if (o.type == "totem") {
        in = {"Carved totem", "Carving", "A carved post. Each one marks something the village wanted to remember.", {}};
    } else {
        in = {o.type, "Object", "", {}};
    }
    return in;
}

static Info place(Sim& s, const Poi& p, int index) {
    static const std::map<std::string, std::pair<std::string, std::string>> d = {
        {"hermit", {"Hermit's hut", "Someone lives here alone, and not by accident."}},
        {"cave", {"Cave", "A cool, dark way into the hill. Things live in places like this."}},
        {"ruins", {"Ruins", "Old stones laid by people who were here first."}},
        {"wreck", {"Shipwreck", "The bones of a ship. There may be something worth taking."}}};
    auto it = d.find(p.type);
    Info in{it != d.end() ? it->second.first : p.type, "Place", it != d.end() ? it->second.second : "", {}};
    in.rows.push_back({"Visited", p.found ? "Yes" : "Not yet"});
    if (p.type == "hermit" && p.joined) in.rows.push_back({"Hermit", "Joined the village"});
    if (std::string now = workingOn(s, Focus::POI, index); !now.empty()) in.rows.push_back({"Here now", now});
    return in;
}

Info describe(Sim& s, const Pick& p) {
    if (!p.ok) return {};
    switch (p.kind) {
    case Drawable::PERSON:
        if (auto* q = s.person(p.id)) return {q->name, q->child ? "Child" : "Settler", q->doing, {}};
        return {};
    case Drawable::OBJECT:
        if (p.id >= 0 && p.id < int(s.objs.size())) return object(s, s.objs[p.id]);
        return {};
    case Drawable::BUILDING:
        if (auto* b = s.projectById(p.id)) return building(s, *b);
        return {};
    case Drawable::POI:
        if (p.id >= 0 && p.id < int(s.pois.size())) return place(s, s.pois[p.id], p.id);
        return {};
    case Drawable::STOCKPILE: {
        Info in{"Stockpile", "Camp stores", "Everything the camp has gathered and not yet used.", {}};
        in.rows.push_back({"Wood", cat(int(s.stock.wood))});
        in.rows.push_back({"Stone", cat(int(s.stock.stone))});
        in.rows.push_back({"Food", cat(int(s.stock.food), ", about ", int(s.foodDays()), " days")});
        return in;
    }
    case Drawable::BOAT:
        return {"The boat", "Wreckage", "The little boat they washed ashore in. Too battered to sail again.", {}};
    case Drawable::WOLF: {
        Info in{"Wolf", "Animal", "A lone wolf. It comes for the food when the camp is careless.", {}};
        if (s.wolf) in.rows.push_back({"Doing", s.wolf->state == "scared" ? "Running off" : s.wolf->state == "out" ? "Leaving" : "Prowling"});
        return in;
    }
    }
    return {};
}
