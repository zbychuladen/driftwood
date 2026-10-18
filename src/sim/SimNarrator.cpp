// The player's tools as the narrator everyone knows is there and takes in
// their stride: narrating a settler, words in the sand, project proposals with
// votes, fate cards. Also newcomers and discoveries.
#include <algorithm>
#include <regex>
#include "Sim.h"

namespace dw {

float Sim::avgBuyIn() {
    float s = 0; int n = 0;
    for (auto* p : members()) if (!p->child) { s += p->buyIn; n++; }
    return n ? s / n : 0;
}

std::string Sim::writeSand(const std::string& raw) {
    std::string txt = raw.substr(0, 120);
    if (txt.find_first_not_of(" \t") == std::string::npos) return "";
    if (lastSand == day()) return "The tide has not yet washed away your last words. Try again tomorrow.";
    lastSand = day();
    note("\"" + txt + "\"", "narrator");
    std::vector<Person*> adults;
    for (auto* p : members()) if (!p->child) adults.push_back(p);
    Person* finder = adults.empty() ? members()[0] : pick(adults);
    struct Key { std::regex re; std::string b, r; };
    static const std::vector<Key> KEYS = {
        {std::regex("winter|cold|hungry|hunger|food|starv|store|harvest|fish|eat", std::regex::icase), "food", "They took it as a warning and spent the day gathering food."},
        {std::regex("build|home|roof|shelter|house|hut|raise|wall", std::regex::icase), "build", "They took it as a call to build and worked with new energy."},
        {std::regex("explore|beyond|north|south|east|west|hill|forest|cave|island|seek|find|horizon", std::regex::icase), "explore", "Several of them stared at the horizon, wondering what else is out there."},
        {std::regex("wood|fire|warm|timber|tree", std::regex::icase), "wood", "They hauled extra wood to the pile, just in case."},
        {std::regex("love|friend|peace|together|forgive|kind|family|heart", std::regex::icase), "peace", "There was a softness in camp all day. Old grudges seemed smaller."},
        {std::regex("wolf|wolves|danger|beware|watch|guard|night", std::regex::icase), "guard", "Nobody slept well, but everyone kept one eye open."},
        {std::regex("joy|laugh|sing|dance|feast|celebrat|happy|sun", std::regex::icase), "joy", "Someone started singing at the fire, and soon everyone joined."},
        {std::regex("purpose|dream|create|art|music|song|story|meaning|calling|inspir|become|soul", std::regex::icase), "purpose", "That night they talked for hours about what each of them wants their life here to mean."}};
    std::vector<const Key*> hits;
    for (auto& k : KEYS) if (std::regex_search(txt, k.re)) hits.push_back(&k);
    if (hits.empty()) {
        for (auto* p : members()) p->buyIn = std::min(100.f, p->buyIn + .5f);
        note(cat(finder->name, " found words in the sand at dawn. Nobody could agree on what they meant."));
        return "They found it, and argued about what it meant.";
    }
    for (size_t i = 0; i < hits.size() && i < 2; i++) {
        auto& b = hits[i]->b;
        boost[b] = tMin + 30 * 60;
        if (b == "peace") for (auto* p : members()) for (auto* q : members()) if (p != q) affAdd(*p, q->id, 6);
        if (b == "joy") for (auto* p : members()) p->mood = std::min(100.f, p->mood + 12);
        if (b == "guard") mods["guard"] = day() + 3;
        if (b == "purpose") for (auto* p : members()) { if (!p->calling.empty()) p->callingNeed = std::min(100.f, p->callingNeed + 30); else if (!p->child) p->goodDays++; }
    }
    for (auto* p : members()) p->buyIn = std::min(100.f, p->buyIn + (p->has("obstinate") ? .5f : 2.5f));
    note(cat(finder->name, " found words in the sand at dawn and read them aloud. ", hits[0]->r));
    return "They found your words. Watch how they respond.";
}

std::vector<const ProjectDef*> Sim::proposalList() {
    std::vector<const ProjectDef*> v;
    for (auto& d : allProjects()) {
        if (d.autoPlan || d.upgrade || d.commissioned) continue;
        bool have = false;
        for (auto& b : plan) if (b.key == d.key && !b.skip) have = true;
        if (!have && prereqOk(d)) v.push_back(&d);
    }
    return v;
}

void Sim::propose(const std::string& key) {
    if (lastProposal == day()) return;
    const ProjectDef* o = projectDef(key);
    if (!o || !giftOk(*o)) return;
    lastProposal = day();
    std::vector<Person*> voters;
    for (auto* p : members()) if (!p->child) voters.push_back(p);
    float days = foodDays();
    Vote v; v.name = o->name;
    auto hasAny = [](Person* p, const std::vector<std::string>& ts) -> std::string { for (auto& t : ts) if (p->has(t.c_str())) return t; return ""; };
    for (auto* p : voters) {
        float s = .38f + (p->buyIn - 40) / 110 + (p->has("hardworking") ? .08f : 0);
        std::string why = "likes the idea";
        auto lk = hasAny(p, o->like), dl = hasAny(p, o->dislike);
        if (!lk.empty()) { s += .28f; why = "is " + traitNames().at(lk); }
        if (!dl.empty()) s -= .35f;
        if (!o->gift.empty() && p->gifts.count(o->gift)) { s += .4f; why = "is our " + lower(giftInfo().at(o->gift).n); }
        if (days < 4 && !o->food) s -= .35f;
        if (p->mood < 30) s -= .15f;
        bool yes = rnd() < clampf(s, .05f, .95f);
        if (!yes) why = days < 4 && !o->food ? "says food comes first" : !dl.empty() ? "is " + traitNames().at(dl) : p->buyIn < 25 ? "is tired of the narrator" : p->mood < 30 ? "is too tired" : "thinks it is a waste";
        else if (p->buyIn > 65) why = "likes where the story is going";
        v.rows.push_back({p->id, yes, why});
        if (yes) v.y++; else v.n++;
    }
    v.pass = v.y > v.n || (v.y == v.n && avgBuyIn() > 50);
    Person* proposer = voters.empty() ? nullptr : pick(voters);
    note(cat(proposer ? proposer->name : "Someone", " heard the narrator float an idea over breakfast: a ", lower(o->name), ". The group voted ", v.y, " to ", v.n, ". ", v.pass ? "It will be built." : "The idea was dropped."), "narrator");
    if (v.pass) {
        mkProj(*o, true);
        for (auto& r : v.rows) if (r.yes) if (auto* p = person(r.pid)) p->buyIn = std::min(100.f, p->buyIn + 2);
    } else {
        for (auto& r : v.rows) if (auto* p = person(r.pid)) p->buyIn = std::max(0.f, p->buyIn - 1);
    }
    vote = v;
}

std::vector<Fate> Sim::drawFates() {
    std::vector<Fate> pool = {
        {"stranger", "A stranger at the tide line", "A survivor of another wreck crawls ashore. One more mouth, one more pair of hands."},
        {"whale", "A whale beaches itself", "Food for weeks. The smell will sour moods for a while."},
        {"bounty", "A bountiful season", "Berries and crops grow twice as fast until the season turns."},
        {"tools", "A crate of tools washes up", "Iron axes and a saw. Everyone works faster this season."},
        {"fever", "A fever sweeps the camp", "Two will fall sick. Nursing them will draw everyone closer to the story."},
        {"dream", "A shared dream", "They all dream of the same far place. Part of the island is revealed."},
        {"driftwood", "A storm of driftwood", "A terrible night of wind. Spirits drop, but the beach is covered in timber."},
        {"love", "A season of warmth", "Friendships deepen quickly. Someone may fall in love."},
        {"wolves", "The wolves grow bold", "More wolf raids this season. But the camp learns to stand watch together."},
        {"calm", "A season of calm seas", "No storms this season. The fish come close to shore."}};
    for (auto* p : members()) if (p->partner >= 0) { pool.push_back({"twins", "Twins", "A couple will welcome two babies at once."}); break; }
    for (auto& h : pois) if (h.type == "hermit" && !h.found) pool.push_back({"smoke", "Smoke rising in the forest", "Someone else lives on this island. The settlers will learn where."});
    std::shuffle(pool.begin(), pool.end(), gen);
    pool.resize(std::min<size_t>(3, pool.size()));
    return pool;
}

void Sim::applyFate(const Fate& f) {
    auto m = members();
    int endOfSeason = ((day() - 1) / 10 + 1) * 10;
    for (auto k : {"bounty", "tools", "love", "wolves", "calm"}) mods.erase(k);
    note(cat("The story turned: ", lower(f.title), "."), "narrator");
    const auto& id = f.id;
    if (id == "stranger") spawnNewcomers(1, "tide");
    else if (id == "whale") { stock.food += 70; for (auto* p : m) p->mood = std::max(0.f, p->mood - 8); note("A whale lay dead on the beach at sunrise. We will eat for weeks, if we can stand the smell."); }
    else if (id == "bounty" || id == "love" || id == "calm") mods[id] = endOfSeason;
    else if (id == "tools") { mods["tools"] = endOfSeason; note("A crate washed ashore with iron axes, a saw and a hammer. It felt like a gift.", "big"); }
    else if (id == "fever") {
        std::vector<Person*> adults;
        for (auto* p : m) if (!p->child) adults.push_back(p);
        std::shuffle(adults.begin(), adults.end(), gen);
        if (adults.size() > 2) adults.resize(2);
        for (auto* p : adults) { p->sick = 30; clearQ(*p); }
        for (auto* p : m) p->buyIn = std::min(100.f, p->buyIn + 8);
        lastSick = day();
        note(cat(listNames(adults), " fell sick with fever. The others sat up with them and asked the narrator, half joking, how this part ends."), "sad");
    } else if (id == "dream") {
        std::vector<Poi*> t;
        for (auto& p : pois) if (!p.found) t.push_back(&p);
        if (!t.empty()) { auto* p = pick(t); reveal(p->x, p->y, 6); }
        else for (int k = 0; k < 4; k++) reveal(ri(GW), ri(GH), 6);
        for (auto* q : m) q->buyIn = std::min(100.f, q->buyIn + 4);
        note("Everyone woke having dreamed of the same distant place. The narrator had described it in the night, and now they know it is real.", "narrator");
    } else if (id == "driftwood") {
        stock.wood += 45;
        for (auto* p : m) p->mood = std::max(0.f, p->mood - 6);
        weather = "storm";
        note("A night of howling wind. By morning the beach was littered with timber.");
    } else if (id == "wolves") { mods["wolves"] = endOfSeason; mods["guard"] = day() + 5; }
    else if (id == "twins") {
        for (auto* a : m) if (a->partner >= 0 && !a->child) { if (auto* b = person(a->partner)) { birth(*a, *b); birth(*a, *b, true); } break; }
    } else if (id == "smoke") {
        for (auto& h : pois) if (h.type == "hermit") reveal(h.x, h.y, 4);
    }
    for (auto* p : m) p->buyIn = std::min(100.f, p->buyIn + 1);
}

void Sim::spawnNewcomers(int n, const std::string& how) {
    std::vector<Shore> shore;
    for (auto& s : seaShore) if (std::hypot(float(s.x - campX), float(s.y - campY)) > 8) shore.push_back(s);
    if (shore.empty()) shore = seaShore;
    if (shore.empty()) return;
    Shore s = pick(shore);
    static const std::vector<uint32_t> TUN = {0xc8553d,0x3d7ec8,0xe0b13a,0x5a9a5b,0x8d5bb5,0xd47fa6,0x2fa39b,0xb07a3c,0x6a7fd0,0xc97a2c,0x4d8c7a,0xa04f6a};
    std::vector<Person*> group;
    for (int i = 0; i < n; i++) {
        auto& p = makePerson(newName(), pick(TUN), s.x + .5f + i * .4f, s.y + .5f);
        p.status = "arriving"; p.needs.hunger = 55; p.buyIn = 20 + rnd() * 30;
        group.push_back(&p);
    }
    reveal(s.x, s.y, 3);
    std::string dir = dirWord(s.x - campX, s.y - campY);
    if (how == "tide") note(cat("Someone spotted a figure stumbling along the ", dir, "ern shore: ", listNames(group), ", half-drowned, survivor of another wreck."), "big");
    else if (how == "wreck") note(cat("After the storm, wreckage drifted onto the ", dir, "ern beach, and ", n > 1 ? "survivors" : "a survivor", " with it: ", listNames(group), "."), "big");
    else note(cat("A ship saw our ", how == "light" ? "lighthouse" : "signal fire", "! A rowboat brought ", listNames(group), " ashore on the ", dir, "ern beach before the ship sailed on."), "big");
}

void Sim::decideNewcomer(Person& p) {
    std::vector<Person*> voters;
    for (auto* q : members()) if (!q->child) voters.push_back(q);
    float days = stock.food / std::max(1.f, (members().size() + 1) * 2.f);
    int y = 0;
    for (auto* q : voters) {
        float s = .6f + (days > 5 ? .2f : days < 2 ? -.35f : 0) + (q->has("social") ? .2f : 0) + (q->has("loner") ? -.25f : 0) + (q->has("cheerful") ? .1f : 0);
        if (rnd() < s) y++;
    }
    int n = int(voters.size()) - y;
    if (y >= n) {
        p.status = "member"; p.q.clear();
        for (auto* q : members()) if (q != &p) { q->aff[p.id] = 20 + rnd() * 15; p.aff[q->id] = 25 + rnd() * 15; }
        assignBeds();
        note(cat(p.name, " asked to stay. The group voted ", y, " to ", n, " to take them in."), "big");
        announceGifts(p);
    } else {
        p.status = "leaving";
        note(cat(p.name, " asked to stay, but the group voted ", y, " to ", n, " against it. Food is too short. They walked away down the beach."), "sad");
    }
}

void Sim::spawnFromCave(Poi& poi) {
    static const std::vector<uint32_t> TUN = {0xc8553d,0x3d7ec8,0xe0b13a,0x5a9a5b,0x8d5bb5,0xd47fa6};
    auto& p = makePerson(newName(), pick(TUN), poi.x + .5f, poi.y + 1.2f);
    p.status = "arriving"; p.needs.hunger = 80;
    note(cat("Deep in a cave, someone called out. ", p.name, " had been living there alone since their own ship went down. They followed us back toward camp."), "big");
}

void Sim::discover(Poi& poi) {
    poi.found = true;
    Person* finder = nullptr;
    float bd = 1e9;
    for (auto* p : members()) { float d = std::hypot(p->x - poi.x, p->y - poi.y); if (d < bd) { bd = d; finder = p; } }
    if (finder) finder->esteemPts = std::min(100.f, finder->esteemPts + 15);
    std::string nm = finder ? finder->name : "Someone";
    if (poi.type == "hermit") {
        Person* h = person(poi.who);
        note(cat(nm, " found a hut deep in the forest. An old hermit named ", h ? h->name : "?", " lives there, and wants nothing to do with us. Yet."), "big");
    }
    if (poi.type == "cave") spawnFromCave(poi);
    if (poi.type == "ruins") { firsts.insert("ruinTools"); note(cat(nm, " found ruins overgrown with vines: old walls and a cache of stone tools. Someone was here long before us. We all work a little faster with them."), "big"); }
    if (poi.type == "wreck") { stock.wood += 30; note(cat(nm, " found an old shipwreck on a far beach. We salvaged 30 planks and a rusted lantern."), "big"); }
}

} // namespace dw
