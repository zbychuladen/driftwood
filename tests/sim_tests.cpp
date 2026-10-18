// Headless simulation tests. No framework needed: each check prints and
// counts failures; the process exit code is the number of failures.
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <regex>
#include <set>
#include <string>
#include "sim/Sim.h"

using namespace dw;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  ok    %s\n", msg); else { std::printf("  FAIL  %s\n", msg); failures++; } } while (0)

// Run the sim for a number of days, auto-picking fate cards like a passive player.
static void runDays(Sim& s, int days) {
    int target = s.day() + days;
    while (s.day() < target) {
        s.step(60);
        if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); }
    }
}

static void testSurvival(uint32_t seed) {
    std::printf("survival, seed %u\n", seed);
    Sim s;
    s.newWorld(seed);
    CHECK(s.members().size() == 5, "starts with five castaways");
    int start = 0;
    for (auto e : s.explored) start += e;
    bool knownAround = true;
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++)
        if (std::hypot(float(x - s.campX), float(y - s.campY)) < START_REVEAL - .5f && !s.explored[Sim::idx(x, y)]) knownAround = false;
    CHECK(knownAround, "on the first morning they know everything within 20 tiles of camp");
    runDays(s, 100);
    int built = 0;
    for (auto& b : s.plan) if (b.done) built++;
    CHECK(!s.members().empty(), "village survives 100 days");
    CHECK(built >= 6, "at least six structures built");
    CHECK(s.fire() != nullptr, "campfire built");
    int explored = 0;
    for (auto e : s.explored) explored += e;
    CHECK(explored >= start * 2, "they explore at least twice the ground they started with");
    bool giftsRespected = true;
    for (auto& b : s.plan) if (b.done && !b.gift.empty() && !b.holderAtFinish) giftsRespected = false;
    CHECK(giftsRespected, "specialist projects only finish with a gifted person present");
    std::printf("  info  day %d, %zu people, %d buildings, village '%s'\n", s.day(), s.members().size(), built, s.villageName.c_str());
}

static void testNoSmithNoForge() {
    std::printf("no smith, no forge\n");
    Sim s;
    s.newWorld(4242);
    for (auto& p : s.people) p.gifts.erase("smith");
    // keep strangers from bringing a smith: strip the gift whenever someone joins
    int target = s.day() + 60;
    while (s.day() < target) {
        s.step(60);
        for (auto& p : s.people) p.gifts.erase("smith");
        if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); if (f.id != "stranger") s.applyFate(f); }
    }
    CHECK(s.built("forge") == nullptr, "forge is never built without a smith");
    CHECK(s.built("mine") == nullptr, "mine is never built without a smith");
}

static void testCharismaPushes() {
    std::printf("a magnetic smith sets the agenda\n");
    Sim s;
    s.newWorld(777);
    auto m = s.members();
    for (auto* p : m) { p->gifts.clear(); p->charisma = 2; }
    m[0]->gifts["smith"] = 8;
    m[0]->charisma = 10;
    runDays(s, 20);
    CHECK(s.agenda.has_value() && s.agenda->by == m[0]->id, "the smith leads the camp agenda");
}

static void testNarrator() {
    std::printf("narrator tools\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 2);
    auto r = s.writeSand("Winter will be long, gather food");
    CHECK(r.find("found your words") != std::string::npos, "words in the sand are found");
    CHECK(s.writeSand("again") .find("tide") != std::string::npos, "only one message per day");
    auto* p = s.members()[0];
    p->traits = {"cheerful", "curious"};   // neither easygoing nor stubborn
    s.narrate(*p, "explore");
    CHECK(s.narrate(*p, "explore") == "pushed", "narrating someone twice in a day irritates them");
    auto props = s.proposalList();
    CHECK(!props.empty(), "there are projects to propose");
    if (!props.empty()) { s.propose(props[0]->key); CHECK(s.vote.has_value() && s.vote->y + s.vote->n > 0, "settlers vote on proposals"); }
}

// Every hour for 60 days, everyone busy with a job points at what they are
// working on, and that thing exists. Front ends draw this link.
static void testFocus(uint32_t seed) {
    std::printf("people know what they are working on, seed %u\n", seed);
    Sim s;
    s.newWorld(seed);
    const std::set<std::string> jobs = {"wood", "stone", "food", "build", "farm", "fuel", "eat", "drink", "warm", "visit", "friend", "craft", "explore"};
    int checked = 0, missing = 0, dangling = 0;
    std::string firstBad;
    int target = s.day() + 60;
    while (s.day() < target) {
        s.step(60);
        if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); }
        for (auto* p : s.members()) {
            // an empty queue means a narrated line or fever just interrupted them; they get a new job next update
            if (!jobs.count(p->kind) || p->q.empty()) continue;
            checked++;
            const Focus& f = p->focus;
            bool ok = true;
            if (f.kind == Focus::NONE) { missing++; ok = false; }
            else if (f.kind == Focus::PROJECT && !s.projectById(f.id)) { dangling++; ok = false; }
            else if (f.kind == Focus::OBJECT && (f.id < 0 || f.id >= int(s.objs.size()))) { dangling++; ok = false; }
            else if (f.kind == Focus::POI && (f.id < 0 || f.id >= int(s.pois.size()))) { dangling++; ok = false; }
            else if (f.kind == Focus::PERSON && !s.person(f.id)) { dangling++; ok = false; }
            if (!ok && firstBad.empty()) firstBad = cat(" (first: ", p->name, ", '", p->doing, "', day ", s.day(), ")");
        }
    }
    CHECK(checked > 100 && missing == 0, (cat("everyone at work has a focus, ", checked, " checks, ", missing, " without") + firstBad).c_str());
    CHECK(dangling == 0, "every focus points at something that exists");
}

static void testDeterminism() {
    std::printf("determinism\n");
    Sim a, b;
    a.newWorld(99); b.newWorld(99);
    runDays(a, 30); runDays(b, 30);
    CHECK(a.stock.food == b.stock.food && a.members().size() == b.members().size() && a.journal.size() == b.journal.size(), "same seed, same history");
}

// Settle the people a save would reset, so two saves can be compared byte for byte.
static std::string saveLoadSave(const std::string& data) { Sim s; s.load(data); return s.save(); }

static void testSaving() {
    std::printf("saving and loading\n");
    Sim a;
    a.newWorld(20261017);
    runDays(a, 40);
    std::string first = a.save();
    Sim b;
    std::string err;
    CHECK(b.load(first, &err), ("a 40-day game loads" + (err.empty() ? std::string() : ": " + err)).c_str());
    int built = 0, builtB = 0;
    for (auto& p : a.plan) built += p.done;
    for (auto& p : b.plan) builtB += p.done;
    CHECK(b.members().size() == a.members().size() && b.day() == a.day() && b.villageName == a.villageName && b.journal.size() == a.journal.size()
          && b.stock.food == a.stock.food && builtB == built && b.explored == a.explored && b.objs.size() == a.objs.size(), "the loaded game has the same people, day, village, journal, stores, buildings and map");
    bool namesKept = true;
    for (size_t i = 0; i < a.people.size(); i++) if (a.people[i].name != b.people[i].name || a.people[i].x != b.people[i].x || a.people[i].aff != b.people[i].aff) namesKept = false;
    CHECK(namesKept, "every settler keeps their name, place and friendships");
    std::string once = saveLoadSave(first), twice = saveLoadSave(once);
    CHECK(once == twice, cat("saving a loaded game gives the same bytes again (", int(once.size() / 1024), " KB)").c_str());
    // two copies of one save go on the same way
    Sim c, d;
    c.load(once); d.load(once);
    runDays(c, 10); runDays(d, 10);
    CHECK(c.save() == d.save(), "two loads of one save play out identically");
    runDays(c, 30);
    CHECK(!c.members().empty() && c.day() == a.day() + 40, "a loaded game keeps running for another 40 days");
    Sim::SaveInfo info;
    CHECK(Sim::peek(first, info) && info.day == a.day() && info.village == a.villageName && info.people == int(a.members().size()), "the save's summary can be read on its own");
    // anything damaged is refused and leaves the running game alone
    Sim e;
    e.newWorld(7);
    std::string before = e.save();
    bool refused = !e.load("not a save at all") && !e.load(first.substr(0, first.size() / 2)) && !e.load(first + "x");
    std::string future = first;
    future[sizeof("DRIFTWOOD SAVE\n") - 1 + 4] = char(Sim::SAVE_VERSION + 1);   // the version number, after the magic and its length
    std::string why;
    bool newer = !e.load(future, &why) && why.find("newer") != std::string::npos;
    CHECK(refused && e.save() == before, "garbage, a cut-off file or trailing data is refused, and the game is untouched");
    CHECK(newer, "a save from a newer version is refused with a reason");
}

// ---------------------------------------------------------------- building sites
static Project* projectNamed(Sim& s, const char* key) { for (auto& b : s.plan) if (b.key == key) return &b; return nullptr; }

// Every legal spot for a building within a radius of camp, with its report.
static std::vector<std::pair<std::pair<int, int>, SiteReport>> sites(Sim& s, const ProjectDef& d, int radius) {
    std::vector<std::pair<std::pair<int, int>, SiteReport>> out;
    for (int y = s.campY - radius; y <= s.campY + radius; y++) for (int x = s.campX - radius; x <= s.campX + radius; x++) {
        SiteReport r = s.evaluateSite(d, x, y);
        if (r.legal) out.push_back({{x, y}, r});
    }
    return out;
}

static bool mentions(const SiteReport& r, const char* id) { for (auto& f : r.factors) if (f.id == id) return true; return false; }

static void testSites() {
    std::printf("building sites\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 45);
    // the forge: a spot beside the huts against one with stone and no homes near
    Project* forge = projectNamed(s, "forge");
    auto forgeSites = sites(s, *forge, 22);
    // on average, spots by stone and clear of homes against spots beside the homes
    float homesQ = 0, stoneQ = 0;
    int nHomes = 0, nStone = 0;
    for (auto& [at, r] : forgeSites) {
        if (mentions(r, "homes") && !mentions(r, "stone")) { homesQ += r.quality; nHomes++; }
        if (mentions(r, "stone") && !mentions(r, "homes")) { stoneQ += r.quality; nStone++; }
    }
    CHECK(nHomes && nStone && stoneQ / nStone > homesQ / nHomes, cat("a forge with stone at hand beats one beside the homes (", nStone, " and ", nHomes, " spots)").c_str());
    // the well: by fresh water against by the sea
    Project* well = projectNamed(s, "well");
    const SiteReport *sweet = nullptr, *salty = nullptr;
    for (auto& [at, r] : sites(s, *well, 25)) {
        if (!sweet && mentions(r, "easydig") && !mentions(r, "brackish")) sweet = &r;
        if (!salty && mentions(r, "brackish")) salty = &r;
    }
    CHECK(sweet && salty && sweet->quality > salty->quality, "a well by fresh water beats one by the sea");
    // spots that are not allowed: in the sea, on another building, unexplored
    int seaX = 0, seaY = 0, darkX = -1, darkY = -1;
    for (int y = 0; y < GH && darkX < 0; y++) for (int x = 0; x < GW; x++) if (s.tt[Sim::idx(x, y)] >= 3 && !s.explored[Sim::idx(x, y)]) { darkX = x; darkY = y; break; }
    Project* someBuilt = nullptr;
    for (auto& b : s.plan) if (b.done && b.w >= 2) { someBuilt = &b; break; }
    CHECK(!s.evaluateSite(*forge, seaX, seaY).legal && (darkX < 0 || !s.evaluateSite(*forge, darkX, darkY).legal) && someBuilt && !s.evaluateSite(*forge, someBuilt->x, someBuilt->y).legal,
          "the sea, an unexplored tile and another building's ground are refused");
    bool allNamed = true;
    for (auto& [at, r] : forgeSites) for (auto& f : r.factors) if (f.text.size() < 6) allNamed = false;
    CHECK(allNamed && !forgeSites.empty(), "every reason a spot is good or bad is put in words");

    // judgement: a master smith reads forge sites better than someone who knows nothing of smithing
    auto m = s.members();
    Person *expert = m[0], *novice = m[1];
    expert->gifts["smith"] = 9; expert->sk["build"] = 6;
    novice->gifts.erase("smith"); novice->sk["build"] = 1;
    double errE = 0, errN = 0;
    int n = 0;
    for (size_t i = 0; i < forgeSites.size() && n < 200; i += std::max<size_t>(1, forgeSites.size() / 200), n++) {
        auto [x, y] = forgeSites[i].first;
        float truth = forgeSites[i].second.quality;
        for (auto& o : s.opinions(*forge, x, y)) {
            if (o.pid == expert->id) errE += std::abs(o.perceived - truth);
            if (o.pid == novice->id) errN += std::abs(o.perceived - truth);
        }
    }
    CHECK(n > 50 && errE * 3 < errN, cat("a master smith misjudges forge sites far less than a novice (", int(errE / n * 1000), " vs ", int(errN / n * 1000), " thousandths)").c_str());
    // the settlers' own choice is never a poor one
    std::string poor;
    for (const char* key : {"forge", "well", "store", "pottery", "school", "cookhouse", "workshop"}) {
        Project* b = projectNamed(s, key);
        if (!b) continue;
        auto all = sites(s, *b, 20);
        auto pickIt = s.suggestSite(*b);
        if (all.empty() || !pickIt) continue;
        std::vector<float> qs;
        for (auto& [at, r] : all) qs.push_back(r.quality);
        std::sort(qs.begin(), qs.end());
        float chosen = s.evaluateSite(*b, pickIt->first, pickIt->second).quality;
        if (chosen < qs[qs.size() / 2]) poor += std::string(" ") + key;
    }
    CHECK(poor.empty(), ("the settlers' own pick is never below the median spot" + poor).c_str());
}

static void testPlacementPause() {
    std::printf("blueprints and placing a building\n");
    Sim s;
    s.newWorld(20261017);
    s.askPlacement = true;
    runDays(s, 12);
    // with the narrator placing, the settlers build only their own homes and small things
    bool onlyOwn = true;
    for (auto& b : s.plan) if (b.placed && !Sim::settlersBuild(b)) onlyOwn = false;
    CHECK(onlyOwn && !s.pendingPlacement, "the settlers build only homes and small things unless the narrator draws up a blueprint");
    CHECK(!s.wishes().empty(), "they say what they would build next");
    // locked blueprints wait for the village to rise
    std::string why;
    bool locked = !s.unlocked("school", &why) && s.blueprint("school") < 0 && !why.empty();
    CHECK(locked, ("a blueprint above the village's level is locked (" + why + ")").c_str());
    for (auto* p : s.members()) p->tiers = {90, 90, 90, 90, 10};
    CHECK(s.villageLevel() == 4 && s.unlocked("school"), "and unlocks as the village clears the levels of need");
    // the build menu: something the village can build now (a watchtower needs no gift)
    int id = s.blueprint("tower");
    CHECK(id >= 0 && s.pendingPlacement && s.pendingPlacement->chosen, "a blueprint from the build menu waits for its spot");
    if (id < 0) return;
    double t = s.tMin;
    s.step(600);
    CHECK(s.tMin == t, "the game stands still while the spot is chosen");
    Sim copy;
    copy.load(s.save());
    CHECK(copy.pendingPlacement && copy.pendingPlacement->project == id && copy.pendingPlacement->chosen, "a pending placement survives a save");
    // Esc cancels it, and a fresh one leaves nothing behind
    Sim c2;
    c2.load(s.save());
    c2.cancelBlueprint();
    CHECK(!c2.pendingPlacement, "a blueprint can be cancelled");
    CHECK(!s.placeAt(id, 0, 0), "a spot in the sea is refused");
    Project* b = s.projectById(id);
    auto all = sites(s, *b, 20);
    auto worst = std::min_element(all.begin(), all.end(), [](auto& a, auto& c) { return a.second.quality < c.second.quality; });
    CHECK(worst != all.end() && s.placeAt(id, worst->first.first, worst->first.second) && b->placed && b->sited && std::abs(b->siteQuality - worst->second.quality) < 1e-4f,
          "the narrator's spot is used, and its quality kept");
    s.step(120);
    CHECK(!s.pendingPlacement && s.tMin > t, "the game goes on once the spot is chosen");
    for (int h = 0; h < 24 * 40 && !s.projectById(id)->done; h++) { s.step(60); if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); } }
    CHECK(s.projectById(id)->done, "the settlers get round to building it");
}

// Two runs from the same point, one with a building in a good spot and one in a bad spot.
template <class F> static void paired(const char* key, float good, float bad, int days, F measure, float& g, float& b) {
    for (int pass = 0; pass < 2; pass++) {
        Sim s;
        s.newWorld(20261017);
        runDays(s, 25);
        Project* p = projectNamed(s, key);
        if (!p) return;
        if (!p->placed) { auto at = s.suggestSite(*p); if (!at) return; s.placeAt(p->id, at->first, at->second); }
        p->done = true;
        p->progress = float(p->work);
        if (s.active == p->id) s.active = -1;
        p->siteQuality = pass == 0 ? good : bad;
        float total = 0;
        for (int h = 0; h < days * 24; h++) {
            s.step(60);
            if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); }
            total += measure(s);
        }
        (pass == 0 ? g : b) = total / (days * 24);
    }
}

static void testSiteEffects() {
    std::printf("good and bad spots\n");
    float g = 0, b = 0;
    paired("cookhouse", 1.2f, .7f, 20, [](Sim& s) { float h = 0; int n = 0; for (auto* p : s.members()) { h += p->needs.hunger; n++; } return n ? h / n : 0; }, g, b);
    CHECK(g > 0 && g < b, cat("people are less hungry with a well-placed cookhouse (", int(g), " vs ", int(b), ")").c_str());
    // spoilage: one night's loss from the same full store, in a good spot and a bad one
    {
        Sim base;
        base.newWorld(20261017);
        runDays(base, 25);
        Project* st = projectNamed(base, "store");
        if (st && !st->placed) { auto at = base.suggestSite(*st); if (at) base.placeAt(st->id, at->first, at->second); }
        if (st) { st->done = true; st->progress = float(st->work); if (base.active == st->id) base.active = -1; }
        while (base.hour() < 23.8) { base.step(5); base.pendingFate.clear(); }
        std::string at = base.save();
        float lost[2];
        for (int pass = 0; pass < 2; pass++) {
            Sim s;
            s.load(at);
            for (auto& x : s.plan) if (x.key == "store") x.siteQuality = pass == 0 ? 1.2f : .7f;
            s.stock.food = 300;
            int d = s.day();
            while (s.day() == d) s.step(5);
            lost[pass] = 300 - s.stock.food;
        }
        CHECK(st && lost[0] < lost[1], cat("a well-placed store spoils less food overnight (", int(lost[0]), " vs ", int(lost[1]), " lost)").c_str());
    }
    paired("well", 1.2f, .7f, 20, [](Sim& s) { float t = 0; int n = 0; for (auto* p : s.members()) { t += p->needs.thirst; n++; } return n ? t / n : 0; }, g, b);
    CHECK(g < b, cat("people are less thirsty with a well in a good spot (", int(g), " vs ", int(b), ")").c_str());
}

// Narrating one person over and over turns them contrary for a while; how much
// they put up with first depends on who they are.
static void testContrary() {
    std::printf("contrariness\n");
    auto pushesUntilContrary = [](std::vector<std::string> traits) {
        Sim s;
        s.newWorld(20261017);
        runDays(s, 2);
        auto* p = s.members()[1];
        p->traits = traits;
        s.rapport = Sim::RAPPORT_MAX;
        for (int n = 1; n <= 10; n++) if (s.narrate(*p, "wood") == "contrary") return n;
        return 99;
    };
    int easy = pushesUntilContrary({"easygoing", "cheerful"}), plain = pushesUntilContrary({"cheerful", "curious"}), stubborn = pushesUntilContrary({"obstinate", "curious"});
    CHECK(plain == 3, cat("pushing someone three times in a day makes them contrary (took ", plain, ")").c_str());
    CHECK(easy > plain && plain > stubborn, cat("easygoing settlers put up with more than stubborn ones (", easy, " pushes vs ", plain, " vs ", stubborn, ")").c_str());

    Sim s;
    s.newWorld(20261017);
    runDays(s, 2);
    auto* p = s.members()[1];
    p->traits = {"cheerful", "curious"};
    s.rapport = Sim::RAPPORT_MAX;
    for (int i = 0; i < 3; i++) s.narrate(*p, "wood");
    CHECK(s.contrary(*p) && p->told && p->told->contrary && p->told->job == "rest", "a contrary settler asked to fetch wood takes the day off instead");
    CHECK(s.narrate(*p, "rest") == "contrary" && p->told->job == "wood", "and asked to rest, goes and works");
    std::string data = s.save();
    Sim t;
    CHECK(t.load(data) && t.contrary(*t.person(p->id)) && t.person(p->id)->pressure == p->pressure, "contrariness and pressure survive a save");
    runDays(s, 4);   // four pushes, fading by one a day
    bool noted = false;
    for (auto& e : s.journal) if (e.text.find(p->name + " has stopped bristling") != std::string::npos) noted = true;
    CHECK(!s.contrary(*p) && p->pressure < .01f && noted, cat("after a few days it fades, and the journal says so (pressure ", p->pressure, noted ? "" : ", no journal line", ")").c_str());
    CHECK(s.narrate(*p, "wood") != "contrary", "then they hear a line again");
}

// The narrator is part of life, never divine: nothing the game writes calls
// it a voice, a god or something to pray to.
static void testNoGodWords() {
    std::printf("the narrator is not a god\n");
    static const std::regex god("\\b(voices?|faith|pray(s|ed|er|ers|ing)?|shrines?|worship\\w*|divine|gods?|holy|sacred|altars?|mystics?|meditat\\w*|visions?|omens?|belie(f|fs|ve|ves|ved))\\b", std::regex::icase);
    std::string bad;
    auto scan = [&](const std::string& where, const std::string& text) { if (std::regex_search(text, god) && bad.size() < 600) bad += "\n          " + where + ": " + text; };
    for (auto& d : allProjects()) { scan("project", d.name); scan("project", d.desc); }
    for (auto& [k, g] : giftInfo()) { scan("gift", g.n); scan("gift", g.v); }
    for (auto& [k, c] : callingInfo()) scan("calling", c.n);
    for (auto& [k, t] : traitNames()) scan("trait", t);
    for (uint32_t seed : {20261017u, 777u}) {
        Sim s;
        s.newWorld(seed);
        int lastDay = 0;
        while (s.day() < 80) {
            s.step(60);
            if (!s.pendingFate.empty()) {
                for (auto& f : s.pendingFate) { scan("fate", f.title); scan("fate", f.desc); }
                auto f = s.pendingFate[s.day() % s.pendingFate.size()];
                s.pendingFate.clear();
                s.applyFate(f);
            }
            // a busy narrator: a line or three every day, words in the sand, proposals
            if (s.day() != lastDay && s.hour() > 9) {
                lastDay = s.day();
                auto m = s.members();
                for (int i = 0; i < 3 && !m.empty(); i++) {
                    auto* p = m[(s.day() * 7 + i) % m.size()];
                    if (!p->child) s.narrate(*p, i == 2 ? "rest" : "wood", m[0] != p ? m[0] : nullptr);
                }
                s.writeSand(s.day() % 2 ? "Winter is coming, gather food" : "something odd");
                auto props = s.proposalList();
                if (!props.empty() && s.day() % 5 == 0) s.propose(props[0]->key);
            }
        }
        for (auto& e : s.journal) scan("journal", e.text);
        for (auto& p : s.people) scan("bubble", p.bubble ? p.bubble->text : std::string());
    }
    CHECK(bad.empty(), ("no voices, faith, prayers or shrines in anything the game writes" + bad).c_str());
}

// The lines offered always fit the settler, and looking at them changes nothing.
static void testLines() {
    std::printf("narrated lines\n");
    std::string bad;
    int offered = 0, few = 0;
    bool still = true;
    for (uint32_t seed : {20261017u, 777u}) {
        Sim s;
        s.newWorld(seed);
        for (int k = 0; k < 160; k++) {
            runDays(s, 0);
            s.step(6 * 60);
            if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); }
            std::string before = k % 40 == 0 ? s.save() : "";
            for (auto* p : s.members()) {
                if (p->child) continue;
                auto ls = s.linesFor(*p);
                if (ls.size() < 2 || ls.size() > 3) few++;
                for (auto& l : ls) {
                    offered++;
                    Person* t = l.target >= 0 ? s.person(l.target) : nullptr;
                    bool hermit = false;
                    for (auto& h : s.pois) hermit |= h.type == "hermit" && h.found && !h.joined;
                    bool ok = l.text.find(p->name) != std::string::npos
                           && (l.job != "build" || s.activeProject())
                           && (l.job != "calling" || !p->calling.empty())
                           && (l.job != "hermit" || hermit)
                           && ((l.job != "peace" && l.job != "talk") || (t && t != p && !t->dead && !t->child && t->status == "member"));
                    if (!ok && bad.size() < 300) bad += "\n          " + l.job + ": " + l.text;
                }
            }
            if (!before.empty() && s.save() != before) still = false;
        }
    }
    CHECK(few == 0, cat("every adult is offered two or three lines (", offered, " offered)").c_str());
    CHECK(bad.empty(), ("every line fits: a project to help with, a calling to follow, someone to talk to" + bad).c_str());
    CHECK(still, "reading the lines never changes the game");
    // replies follow buy-in against the price, and the narrator pays from its rapport: they go
    // along and it is paid; they barely buy in and haggle (paid, later); they hardly do and refuse (free)
    Sim s;
    s.newWorld(20261017);
    runDays(s, 2);
    std::string got;
    bool paid = true, later = true;
    auto adults = s.members();
    for (int k = 0; k < 3; k++) {
        Person& p = *adults[k];
        p.traits = {"cheerful", "curious"};
        Line l;
        for (auto& o : s.linesFor(p)) if (o.job != "peace" && o.job != "talk") l = o;
        p.buyIn = (k == 0 ? 5.f : k == 1 ? 3.f : 2.f) * l.cost;
        s.rapport = 50;
        got += s.narrate(p, l) + " ";
        if (k < 2 && std::abs(s.rapport - (50 - l.cost)) > .01f) paid = false;
        if (k == 2 && s.rapport != 50) paid = false;
        if (k == 1 && !(p.told && p.told->from > s.tMin)) later = false;
    }
    CHECK(got == "agreed haggled refused ", ("those who buy in go along, those who barely do haggle, those who hardly do refuse (" + got + ")").c_str());
    CHECK(paid, "going along costs the narrator exactly the price shown; a refusal costs nothing");
    Person& poor = *adults[3];
    s.rapport = 1;
    float b0 = poor.buyIn;
    CHECK(s.narrate(poor, s.linesFor(poor)[0]) == "unaffordable" && s.rapport == 1 && poor.buyIn == b0 && poor.pressure < 1, "without the rapport to pay, the narrator cannot ask");
    CHECK(later, "a haggled line starts a few hours later");
}

static int askAndAnswer(Sim& s, Person& p, const char* topic, int reply, int about = -1) {
    s.ask(p, topic, about);
    int id = s.questions.back().id;
    s.answer(id, reply);
    return id;
}

static void testQuestions() {
    std::printf("questions for the narrator\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 3);
    s.questions.clear();
    auto m = s.members();
    Person& a = *m[0];
    a.traits = {"cheerful", "curious"};
    float r0 = a.buyIn;
    askAndAnswer(s, a, "skill", 0);
    CHECK(a.buyIn > r0, "an honest answer builds rapport");
    a.needs.social = 60;
    r0 = a.buyIn;
    askAndAnswer(s, a, "home", 1);
    CHECK(a.needs.social < 60 && a.buyIn >= r0, "teasing makes them feel part of things");
    a.traits = {"gloomy", "curious"};
    r0 = a.buyIn;
    askAndAnswer(s, a, "home", 1);
    CHECK(a.buyIn < r0, "teasing costs rapport with the sensitive");
    // a misleading answer feels good now and costs more when it comes out
    float gained = 0, lost = 0;
    int found = 0;
    for (int k = 0; k < 12 && found < 3; k++) {
        Person& p = *m[k % m.size()];
        if (p.child) continue;
        p.buyIn = 50;
        askAndAnswer(s, p, "winter", 2);
        gained += p.buyIn - 50;
        float before = p.buyIn;
        int v0 = s.journalVersion;
        runDays(s, 4);
        bool out = false;
        // the journal is newest first
        for (size_t i = 0; i < std::min(s.journal.size(), size_t(s.journalVersion - v0)); i++) out |= s.journal[i].text.find(p.name) != std::string::npos && s.journal[i].text.find("the truth about winter") != std::string::npos;
        if (out) { found++; lost += before - p.buyIn; }
    }
    CHECK(found > 0 && lost / found > gained / 12 * 2, cat("a misleading answer that comes out costs more than it gained (", found, " came out)").c_str());
    // questions lapse after a day, and survive a save while they wait
    s.questions.clear();
    s.ask(*m[1], "friend", m[2]->id);
    int id = s.questions.back().id;
    std::string data = s.save();
    Sim t;
    CHECK(t.load(data) && t.questions.size() == 1 && t.questions[0].id == id && t.questions[0].replies.size() == 3, "a waiting question survives a save");
    runDays(t, 2);
    CHECK(std::none_of(t.questions.begin(), t.questions.end(), [&](const Question& q) { return q.id == id; }), "an unanswered question lapses after a day");
    // left alone, settlers do ask now and then
    Sim u;
    u.newWorld(777);
    int asked = 0, last = 0;
    for (int d = 0; d < 20; d++) { runDays(u, 1); for (auto& q : u.questions) if (q.id > last) { asked++; last = q.id; } }
    CHECK(asked >= 3 && asked <= 20, cat("settlers ask the narrator things now and then (", asked, " in 20 days)").c_str());
}

// Rapport is what the narrator spends: lines that fit are cheap, plans that work
// out earn it back.
static void testRapportCurrency() {
    std::printf("rapport as currency\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 2);
    Person& p = *s.members()[0];
    p.traits = {"cheerful", "curious"};
    p.needs.fatigue = 90;
    p.kind = "wood";
    int rest = -1, work = -1;
    for (auto& l : s.linesFor(p)) { if (l.job == "rest") rest = l.cost; else if (l.job == "food" || l.job == "explore" || l.job == "build") work = std::max(work, l.cost); }
    CHECK(rest > 0 && work > 0 && rest < work, cat("asking a tired settler to rest is cheaper than asking them to work (", rest, " vs ", work, ")").c_str());
    Person q = p;
    q.traits = {"obstinate", "curious"};
    Person e = p;
    e.traits = {"easygoing", "curious"};
    CHECK(Sim::price(e, "wood", 1) < Sim::price(p, "wood", 1) && Sim::price(p, "wood", 1) < Sim::price(q, "wood", 1), "the same request costs more with stubborn settlers than easygoing ones");

    // a season with nobody lost earns rapport; the same season with a death does not
    float credit[2], pool[2];
    for (int pass = 0; pass < 2; pass++) {
        Sim a;
        a.newWorld(777);
        while (!(a.day() == 10 && a.hour() >= 23)) { a.step(60); if (!a.pendingFate.empty()) { auto f = a.pendingFate[0]; a.pendingFate.clear(); a.applyFate(f); } }
        a.lostThisSeason = pass;
        a.rapport = 40;
        float before = a.avgBuyIn();
        a.step(120);
        credit[pass] = a.avgBuyIn() - before;
        pool[pass] = a.rapport;
    }
    CHECK(credit[0] > credit[1] + 4 && pool[0] >= pool[1] + 9.9f, cat("a season with nobody lost earns the narrator rapport (", int(pool[0] - pool[1]), " more) and raises buy-in").c_str());
    // the daily income follows the average buy-in
    Sim inc;
    inc.newWorld(4242);
    runDays(inc, 1);
    while (inc.hour() < 23) inc.step(30);
    inc.rapport = 20;
    float expect = inc.rapportIncome();
    inc.step(120);
    CHECK(std::abs(inc.rapport - 20 - expect) < .05f && expect > 1, cat("rapport comes in every day from how much they buy in (", expect, " a day)").c_str());

    // the narrator's own choice of spot: a good one earns rapport when it is finished, a bad one costs it
    Sim base;
    base.newWorld(20261017);
    base.askPlacement = true;
    runDays(base, 6);
    if (base.blueprint("store") < 0) { CHECK(false, "a store can be drawn up"); return; }
    std::string start = base.save();
    float change[2] = {0, 0};
    for (int pass = 0; pass < 2; pass++) {
        Sim b;
        b.load(start);
        int id = b.pendingPlacement->project;
        auto all = sites(b, *b.projectById(id), 20);
        auto pickIt = pass == 0 ? std::max_element(all.begin(), all.end(), [](auto& x, auto& y) { return x.second.quality < y.second.quality; })
                                : std::min_element(all.begin(), all.end(), [](auto& x, auto& y) { return x.second.quality < y.second.quality; });
        b.placeAt(id, pickIt->first.first, pickIt->first.second);
        for (int h = 0; h < 24 * 40 && !b.projectById(id)->done; h++) {
            float before = b.avgBuyIn();
            b.step(60);
            if (!b.pendingFate.empty()) { auto f = b.pendingFate[0]; b.pendingFate.clear(); b.applyFate(f); }
            if (b.projectById(id)->done) change[pass] = b.avgBuyIn() - before;
        }
    }
    CHECK(change[0] > 0 && change[0] > change[1], cat("a well-chosen spot earns rapport when it is finished, a bad one less (", change[0], " vs ", change[1], ")").c_str());
}

// The Narrator's Ledger: each gap shows when it should and clears once fixed;
// candidates are people who could actually do it; the bigger nudges last.
static void testLedger() {
    std::printf("the ledger\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 4);
    while (s.hour() < 10 || s.hour() >= 11) s.step(30);
    s.pendingFate.clear();
    auto adults = s.members();
    auto has = [&](const char* id) { for (auto& g : s.ledger()) if (g.id == id) return true; return false; };
    auto idle = [&] { for (auto* p : adults) { p->kind = "idle"; p->sick = 0; p->needs.fatigue = 30; } };
    std::string bad;
    auto expect = [&](const char* id, bool want, const char* when) { if (has(id) != want && bad.size() < 300) bad += cat("\n          ", id, want ? " missing " : " still there ", when); };
    std::string before = s.save();
    idle();
    s.stock.wood = 0; expect("wood", true, "with no wood and nobody cutting");
    adults[0]->kind = "wood"; expect("wood", false, "once someone cuts wood");
    idle(); s.stock.food = 0; expect("food", true, "with no food");
    adults[0]->kind = "food"; adults[1]->kind = "farm"; expect("food", false, "with two gathering");
    idle(); s.stock.food = 200;
    if (auto* pr = s.currentProject()) {
        pr->progress = std::max(pr->progress, 1.f);
        expect("builder", true, "with a project and no builder");
        adults[2]->kind = "build"; expect("builder", false, "with a builder");
    }
    idle();
    for (auto* p : adults) p->gifts.erase("herbalist");
    adults[3]->sick = 20; expect("herbalist", true, "with someone sick and no herbalist");
    adults[0]->gifts["herbalist"] = 4; expect("herbalist", false, "with a herbalist");
    idle(); for (auto* p : adults) p->needs.fatigue = 90; expect("tired", true, "with everyone worn out");
    idle(); expect("tired", false, "once rested");
    expect("explore", true, "with the island unknown and nobody exploring");
    adults[1]->kind = "explore"; expect("explore", false, "with someone exploring");
    idle(); s.stock.wood = 0;
    double day = s.tMin;
    s.tMin = std::floor(s.tMin / 1440) * 1440 + 22 * 60;
    expect("wood", false, "at night, when everyone sleeps");
    s.tMin = day;
    if (auto* f = s.fire(); f && f->done) {
        s.stock.wood = 50;
        f->fuel = 0;
        s.tMin = 30 * 1440 + 10 * 60;   // a winter morning
        expect("fire", true, "with the fire out in winter");
        f->fuel = 5; expect("fire", false, "once the fire is fed");
        s.tMin = day;
    }
    CHECK(bad.empty(), ("every ledger gap shows when it should and clears once fixed" + bad).c_str());

    // candidates: never someone already at it, contrary or sick; those likely to agree rank above those who would refuse
    idle(); s.stock.wood = 0;
    adults[1]->contraryUntil = s.tMin + 600;
    adults[2]->sick = 10;
    adults[3]->buyIn = 1;   // would refuse anything
    bool sane = true, order = true;
    for (auto& g : s.ledger()) {
        bool seenRefuser = false;
        for (auto& c : g.candidates) {
            Person* p = s.person(c.pid);
            if (!p || p == adults[1] || p == adults[2] || p->kind == g.job) sane = false;
            bool refuses = s.likely(*p, Line{g.job, "", -1, c.cost}) == "refused";
            if (!refuses && seenRefuser) order = false;
            seenRefuser |= refuses;
        }
    }
    CHECK(sane, "candidates are never already at it, contrary or sick");
    CHECK(order, "those likely to agree are listed before those who would refuse");
    adults[1]->contraryUntil = 0; adults[2]->sick = 0; adults[3]->buyIn = 40;
    s.load(before);

    // an appointed role holds until revoked, and costs a little rapport each day
    Sim r;
    r.newWorld(777);
    runDays(r, 3);
    Person* p = nullptr;
    for (auto* q : r.members()) if (!q->child && (!p || q->buyIn > p->buyIn)) p = q;
    p->buyIn = 90;
    r.rapport = 100;
    int cost = r.roleCost(*p, "wood");
    CHECK(r.appoint(*p, "wood") == "agreed" && std::abs(r.rapport - (100 - cost)) < .01f && p->duty && p->duty->role, "a settler who buys in takes a role, and it costs its price");
    int chopping = 0;
    float before2 = r.rapport;
    for (int h = 0; h < 24 * 4; h++) { r.step(60); r.pendingFate.clear(); chopping += p->kind == "wood"; }
    CHECK(p->duty && chopping >= 8, cat("the woodcutter keeps cutting wood day after day (", chopping, " hours in 4 days)").c_str());
    CHECK(r.rapport < before2 + 4 * r.rapportIncome() - 3, "a role costs a little rapport every day");
    r.revoke(*p);
    CHECK(!p->duty, "a role ends when revoked");
    // a season's work lasts until the season turns
    r.rapport = 100;
    CHECK(r.askSeason(*p, "food") == "agreed" && p->duty && !p->duty->role, "a settler agrees to a season's work");
    int turn = ((r.day() - 1) / 10 + 1) * 10 + 1;
    while (r.day() < turn) { r.step(60); r.pendingFate.clear(); }
    r.step(120);
    CHECK(!p->duty, "a season's work ends when the season turns");
    // an appeal to everyone
    r.rapport = 100;
    int ac = r.appealCost("wood");
    auto res = r.appeal("wood");
    int told = 0;
    for (auto* q : r.members()) told += q->told && q->told->job == "wood" && q->told->until > r.tMin;
    CHECK(res.rfind("agreed", 0) == 0 && told >= 1 && std::abs(r.rapport - (100 - ac)) < .01f, cat("an appeal costs its price and moves those who buy in (", res, ")").c_str());
    r.rapport = 1;
    CHECK(r.appeal("wood") == "unaffordable" && r.rapport == 1, "an appeal the narrator cannot afford does nothing");
    // reading the ledger never changes the game
    std::string a1 = r.save();
    r.ledger();
    CHECK(r.save() == a1, "reading the ledger never changes the game");
}

// Households own homes; a home rises a level when its household's needs are met
// and the materials are in, and falls back when they lapse.
static void untilMidnight(Sim& s) { while (s.hour() < 23.5) { s.step(30); if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); } } }
static void pastMidnight(Sim& s) { int d = s.day(); while (s.day() == d) s.step(30); s.pendingFate.clear(); }

static void testHouseholds() {
    std::printf("households and homes\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 60);
    // everyone belongs to exactly one household; partners share one; children live with a parent
    bool once = true, partners = true, kids = true;
    for (auto* p : s.members()) {
        int n = 0;
        for (auto& h : s.households) for (int m : h.members) n += m == p->id;
        if (n != 1) once = false;
        if (p->partner >= 0 && s.person(p->partner) && !s.person(p->partner)->dead && s.householdOf(p->id) != s.householdOf(p->partner)) partners = false;
        if (p->child) { bool withParent = p->parents.empty(); for (int par : p->parents) withParent |= s.householdOf(par) == s.householdOf(p->id); if (!withParent) kids = false; }
    }
    CHECK(once && partners && kids, "everyone is in one household, partners share it, children live with a parent");
    std::set<int> homes;
    bool shared = false;
    for (auto& h : s.households) if (h.home >= 0) { shared |= homes.count(h.home) > 0; homes.insert(h.home); }
    CHECK(!homes.empty() && !shared, "households have homes of their own, one each");

    // a hut whose household has what a cottage needs, and the materials, becomes a cottage
    Household* h = nullptr;
    // a hut household, a couple if there is one (a couple will not merge into another household mid-test)
    for (auto& x : s.households) if (x.home >= 0 && s.projectById(x.home) && s.projectById(x.home)->key == "hut" && (!h || x.members.size() > h->members.size())) h = &x;
    if (!h) { CHECK(false, "a household lives in a hut"); return; }
    int hid = h->id;
    Project* home = s.projectById(h->home);
    for (int yy = home->y; yy < home->y + 2; yy++) s.fresh[Sim::idx(home->x - 1, yy)] = 1;   // a spring beside it
    bool used = false;
    // the household's home, if it still has one
    auto homeOf = [&](int id) -> Project* { auto* hh = s.household(id); return hh ? s.projectById(hh->home) : nullptr; };
    for (int d = 0; d < Sim::RISE_DAYS + 1 && homeOf(hid) && homeOf(hid)->key == "hut"; d++) {
        untilMidnight(s); s.stock.food = 400; s.goods["pots"] = 10; s.foodSeen["fish"] = s.foodSeen["berries"] = s.day() + 9; s.stock.wood = 100; s.stock.stone = 100; pastMidnight(s);
        used = s.stock.stone < 100;
    }
    home = homeOf(hid);
    CHECK(home && home->key == "cottage" && home->beds == 3 && s.status(*s.household(hid)) == 2, "a household with what a cottage needs turns its hut into a cottage");
    CHECK(used, "rising a level uses materials");
    // and when the pots run out for long enough, it falls back to a hut, with a warning first
    bool warned = false;
    for (int d = 0; d < Sim::FALL_DAYS + 1; d++) {
        untilMidnight(s); s.goods["pots"] = 0; s.stock.food = 400; pastMidnight(s);
        for (auto& g : s.ledger()) warned |= g.id == "home";
        while (s.hour() < 10) s.step(30);
    }
    home = homeOf(hid);
    CHECK(home && home->key == "hut" && warned, "a cottage short of pots for days falls back to a hut, and the ledger warns first");

    // services reach homes by walking: water in the way makes the way longer
    Sim a, b;
    a.newWorld(777); b.newWorld(777);
    runDays(a, 3); runDays(b, 3);
    Project* f = a.fire();
    if (!f) { CHECK(false, "a campfire to measure from"); return; }
    int tx = f->x + 6, ty = f->y;
    for (int yy = f->y - 12; yy <= f->y + 12; yy++) if (Sim::inb(f->x + 3, yy)) b.tt[Sim::idx(f->x + 3, yy)] = 0;
    int open = a.reach("fire", tx, ty), walled = b.reach("fire", tx, ty);
    CHECK(open <= 7 && walled > open + 2, cat("a service's reach follows the way people walk (", open, " tiles, or ", walled, " round the water)").c_str());
}

// Lean-to sleepers lie under its roof and step out when they wake; tireless
// settlers tire slower; night owls sleep by day; specialists improve with practice.
static void testWorkers() {
    std::printf("sleepers, stamina, night owls and practice\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 3);
    int d4 = s.day() + 1;
    while (!(s.day() == d4 && s.hour() >= 1.5)) { s.step(30); s.pendingFate.clear(); }
    int under = 0;
    bool outside = true;
    for (auto* p : s.members()) {
        auto* b = s.projectById(p->bed);
        if (!b || b->key != "leanto" || !p->sleeping) continue;
        bool inFoot = p->x >= b->x && p->x <= b->x + b->w && p->y >= b->y && p->y <= b->y + b->h;
        under += inFoot && p->hidden;
    }
    for (int h = 0; h < 10; h++) s.step(60);
    s.pendingFate.clear();
    for (auto* p : s.members()) {
        if (p->sleeping) continue;
        int tx = int(std::floor(p->x)), ty = int(std::floor(p->y));
        if (Sim::inb(tx, ty)) if (auto* b = s.projectById(s.bOcc[Sim::idx(tx, ty)]); b && b->key == "leanto") outside = false;
    }
    CHECK(under >= 2, cat("lean-to sleepers lie under its roof (", under, " at half past one)").c_str());
    CHECK(outside, "once awake, nobody is left standing inside the lean-to");

    // tireless: the same morning's work tires them less
    float rise[2];
    for (int pass = 0; pass < 2; pass++) {
        Sim t;
        t.newWorld(777);
        runDays(t, 2);
        while (t.hour() < 8) t.step(30);
        Person& p = *t.members()[0];
        p.traits = pass ? std::vector<std::string>{"tireless", "curious"} : std::vector<std::string>{"cheerful", "curious"};
        p.needs.fatigue = 20;
        t.step(4 * 60);
        rise[pass] = p.needs.fatigue - 20;
    }
    CHECK(rise[1] < rise[0] * .8f, cat("a tireless settler tires slower (", int(rise[1]), " against ", int(rise[0]), " over a morning)").c_str());

    // a night owl sleeps through the middle of the day and is up in the small hours, unlike the others
    int dayAsleep[2] = {0, 0}, nightAwake[2] = {0, 0}, samples = 0;
    for (int pass = 0; pass < 2; pass++) {
        Sim o;
        o.newWorld(4242);
        runDays(o, 2);
        Person& who = *o.members()[1];
        who.traits = pass ? std::vector<std::string>{"nightowl", "curious"} : std::vector<std::string>{"cheerful", "curious"};
        samples = 0;
        for (int h = 0; h < 24 * 4; h++) {
            o.step(60);
            o.pendingFate.clear();
            double hr = o.hour();
            if (hr >= 11 && hr < 15) dayAsleep[pass] += who.sleeping;
            if (hr >= 0 && hr < 4) { nightAwake[pass] += !who.sleeping; samples++; }
        }
    }
    CHECK(dayAsleep[1] >= samples / 3 && dayAsleep[1] > dayAsleep[0] + 3 && nightAwake[1] >= samples / 2 && nightAwake[1] > nightAwake[0] + 4,
          cat("a night owl sleeps by day and is up at night (asleep at midday ", dayAsleep[1], " of ", samples, " against ", dayAsleep[0], "; awake at night ", nightAwake[1], " against ", nightAwake[0], ")").c_str());

    // practice: a working potter gets better, and better means quicker
    CHECK(Sim::giftPace(10) < Sim::giftPace(5) && Sim::giftPace(5) < Sim::giftPace(1), "gifted work goes quicker at higher levels");
    int rose = 0;
    for (uint32_t seed : {20261017u, 777u, 4242u}) {
        Sim g;
        g.newWorld(seed);
        runDays(g, 100);
        for (auto& e : g.journal) rose += e.text.find("has become a better") != std::string::npos;
    }
    CHECK(rose >= 2, cat("specialists get better with practice (", rose, " gifts rose over three 100-day games)").c_str());
}

// Settlers walk round buildings, not through them.
static void testPathsAroundBuildings() {
    std::printf("paths round buildings\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 40);
    int checked = 0, through = 0, stuck = 0;
    for (auto& b : s.plan) {
        if (!b.done || (b.key != "hut" && b.key != "store" && b.key != "longhouse" && b.key != "forge" && b.key != "cottage")) continue;
        auto inside = [&](float x, float y) { return x >= b.x && x < b.x + b.w && y >= b.y && y < b.y + b.h; };
        // from one side to the other, both ways round
        float pairs[2][4] = {{b.x - 1.5f, b.y + b.h / 2.f, b.x + b.w + 1.5f, b.y + b.h / 2.f}, {b.x + b.w / 2.f, b.y - 1.5f, b.x + b.w / 2.f, b.y + b.h + 1.5f}};
        for (auto& q : pairs) {
            if (s.isWater(int(q[0]), int(q[1])) || s.isWater(int(q[2]), int(q[3])) || s.bOcc[Sim::idx(int(q[0]), int(q[1]))] >= 0 || s.bOcc[Sim::idx(int(q[2]), int(q[3]))] >= 0) continue;
            auto path = s.findPath(q[0], q[1], q[2], q[3]);
            checked++;
            if (path.empty()) stuck++;
            for (auto& [x, y] : path) if (inside(x, y)) { through++; break; }
        }
    }
    CHECK(checked >= 4 && through == 0, cat("walking past a building goes round it (", checked, " routes)").c_str());
    CHECK(stuck == 0, "and there is always a way round");
    // you can still leave the building you are in, and walk into the one you are going to
    Project* hut = nullptr;
    for (auto& b : s.plan) if (b.done && b.key == "hut") hut = &b;
    if (hut) {
        // an open patch of ground a few tiles away
        int fx = -1, fy = -1;
        for (int r = 3; r <= 6 && fx < 0; r++) for (int dy = -r; dy <= r && fx < 0; dy++) for (int dx = -r; dx <= r; dx++) {
            int x = hut->x + dx, y = hut->y + dy;
            if (Sim::inb(x, y) && !s.isWater(x, y) && s.bOcc[Sim::idx(x, y)] < 0 && std::max(std::abs(dx), std::abs(dy)) == r) { fx = x; fy = y; break; }
        }
        auto out = s.findPath(hut->x + .5f, hut->y + .5f, fx + .5f, fy + .5f);
        auto in = s.findPath(fx + .5f, fy + .5f, hut->x + .5f, hut->y + .5f);
        CHECK(fx >= 0 && !out.empty() && !in.empty(), "a settler can leave a building and walk into one");
    }
}

// Goods are made by gifted workers and used up; homes need them, and food of
// several kinds; the ledger says what nobody is making.
static void testGoods() {
    std::printf("goods and food variety\n");
    Sim s;
    s.newWorld(4242);
    runDays(s, 10);
    // with nobody to make them, goods run down
    for (auto* p : s.members()) p->gifts.clear();
    for (auto k : {"pots", "cloth", "furniture", "spices"}) s.goods[k] = 10;
    runDays(s, 10);
    bool down = true;
    for (auto k : {"pots", "cloth", "furniture", "spices"}) down &= s.good(k) < 10;
    CHECK(down, "goods run down when nobody makes them");
    // variety counts the kinds of food brought in over the last five days
    s.foodSeen = {{"fish", s.day()}, {"berries", s.day() - 2}, {"grain", s.day() - 9}};
    CHECK(s.foodVariety() == 2, "food variety counts what came in over the last five days");

    // a longer game: furniture gets made once there is a workshop and a carpenter;
    // a house asks for it, and the ledger names what nobody makes
    int madeFurniture = 0, gapsRight = 0, gapsWanted = 0;
    for (uint32_t seed : {20261017u, 777u}) {
        Sim g;
        g.newWorld(seed);
        runDays(g, 160);
        Project* ws = g.built("workshop");
        if (ws && g.good("furniture") > 0) madeFurniture++;
        bool anyMaker = ws && g.giftHolder("carpenter", 1);
        bool wanted = false;
        for (auto& h : g.households) {
            auto* b = h.home >= 0 ? g.projectById(h.home) : nullptr;
            if (!b) continue;
            int lv = Sim::homeLevel(b->key);
            for (int want : {lv, lv + 1}) for (auto& n : g.homeNeeds(h, want)) wanted |= n.id == "furniture" && !n.met;
        }
        bool gap = false;
        for (auto& x : g.ledger()) gap |= x.id == "goods" && x.text.find("furniture") != std::string::npos;
        if (wanted && !anyMaker) { gapsWanted++; gapsRight += gap; }
        if (anyMaker) gapsRight += !gap, gapsWanted++;
    }
    CHECK(madeFurniture >= 1, "a carpenter at the workshop makes furniture");
    CHECK(gapsRight == gapsWanted, cat("the ledger names a good homes need that nobody makes, and only then (", gapsRight, " of ", gapsWanted, ")").c_str());
    // any standing building will do as the home being asked about
    Household h{};
    for (auto& b : s.plan) if (b.done) h.home = b.id;
    bool asks = false;
    for (auto& n : s.homeNeeds(h, 3)) asks |= n.id == "furniture";
    CHECK(asks, "a house needs furniture");
}

// Status and work: the higher they rise the less they haul; notables lift those
// beside them and pay for it in standing; the gap breeds resentment; ships trade.
static void makeAllHomes(Sim& s, const char* key) {
    for (auto& h : s.households) if (auto* b = s.projectById(h.home)) b->key = key;
}

static void testStatusAndWork() {
    std::printf("status and work\n");
    CHECK(Sim::willing(4, "wood") < .3f && Sim::willing(3, "stone") < Sim::willing(2, "stone") && Sim::willing(1, "wood") == 1 && Sim::willing(4, "craft") == 1,
          "the higher a household, the less it cares for hard labour, and crafts are still fine");

    // the same village for three days, as it is and with every home a manor
    float gathered[2];
    for (int pass = 0; pass < 2; pass++) {
        Sim s;
        s.newWorld(20261017);
        runDays(s, 60);
        if (pass) makeAllHomes(s, "manor");
        for (auto& h : s.households) if (h.home < 0) for (auto& b : s.plan) if (b.done && pass && b.key == "leanto") { h.home = b.id; b.key = "manor"; break; }
        float before = s.stock.food;
        int fishOrForage = 0;
        for (int hr = 0; hr < 72; hr++) { s.step(60); s.pendingFate.clear(); for (auto* p : s.members()) fishOrForage += p->kind == "food"; }
        gathered[pass] = s.stock.food - before + fishOrForage;
    }
    CHECK(gathered[1] < gathered[0], cat("a village of notables gathers less food (", int(gathered[1]), " against ", int(gathered[0]), ")").c_str());

    // a notable at common work: those beside them work faster, and too much of it costs the manor
    Sim n;
    n.newWorld(20261017);
    runDays(n, 60);
    Household* top = nullptr;
    for (auto& h : n.households) if (h.home >= 0 && n.projectById(h.home) && (!top || h.members.size() > top->members.size())) top = &h;
    if (!top) { CHECK(false, "a household with a home"); return; }
    n.projectById(top->home)->key = "manor";
    Person* lord = n.person(top->members[0]);
    Person* other = nullptr;
    for (auto* p : n.members()) if (!p->child && !n.householdOf(p->id)->members.empty() && n.householdOf(p->id) != top) other = p;
    lord->kind = "wood";
    CHECK(other && n.alongside(*other, "chop") > 1.15f && n.alongside(*other, "farm") == 1, "working beside a notable who chops wood, others chop faster");
    int topId = top->id;
    bool fell = false;
    for (int d = 0; d < Sim::FALL_DAYS + 2 && !fell; d++) {
        untilMidnight(n);
        if (auto* h = n.household(topId)) h->toil = 12;
        pastMidnight(n);
        for (int i = 0; i < 6; i++) fell |= n.journal[i].text.find("anything but common work") != std::string::npos;
    }
    CHECK(fell, "a notable household always at common work loses its manor");

    // resentment grows below a manor and fades when the gap closes
    Sim r;
    r.newWorld(777);
    runDays(r, 60);
    Household* rich = nullptr;
    for (auto& h : r.households) if (h.home >= 0 && r.projectById(h.home)) rich = &h;
    if (!rich) { CHECK(false, "a household with a home"); return; }
    int richId = rich->id;
    Person* poor = nullptr;
    for (auto* p : r.members()) if (!p->child && r.householdOf(p->id) && r.householdOf(p->id)->id != richId && r.statusOf(*p) <= 1) poor = p;
    if (!poor) { CHECK(false, "someone of low status"); return; }
    for (int d = 0; d < 6; d++) { untilMidnight(r); r.projectById(r.household(richId)->home)->key = "manor"; r.household(richId)->falling = 0; pastMidnight(r); }
    float high = poor->resentment;
    for (int d = 0; d < 6; d++) { untilMidnight(r); r.projectById(r.household(richId)->home)->key = "hut"; pastMidnight(r); }
    CHECK(high > 20 && poor->resentment < high, cat("resentment grows below a manor (", int(high), ") and fades once the gap closes (", int(poor->resentment), ")").c_str());

    // a ship calls while a beacon stands, and a notable trades for spices and fine cloth
    Sim t;
    t.newWorld(20261017);
    runDays(t, 30);
    Project sig;
    static_cast<ProjectDef&>(sig) = *projectDef("signal");
    sig.id = t.nextProjId++; sig.done = sig.placed = true; sig.x = t.campX + 6; sig.y = t.campY + 6;
    t.plan.push_back(sig);
    makeAllHomes(t, "manor");
    t.stock.food = 400;
    t.nextShip = t.day() + 1;
    float sp = t.good("spices"), fc = t.good("finecloth");
    untilMidnight(t); pastMidnight(t);
    CHECK(t.good("spices") > sp && t.good("finecloth") > fc && t.nextShip > t.day(), "a passing ship trades spices and fine cloth with a notable");
}

// Workmanship: practised, cheerful, gifted hands build better, and it counts.
static void testWorkmanship() {
    std::printf("workmanship\n");
    float craft[2];
    for (int pass = 0; pass < 2; pass++) {
        Sim s;
        s.newWorld(20261017);
        runDays(s, 3);
        std::set<int> before;   // only projects begun after the builders are set
        for (auto& b : s.plan) if (b.progress > 0 || b.done) before.insert(b.id);
        Project* first = nullptr;
        for (int h = 0; h < 24 * 30 && !first; h++) {
            for (auto* p : s.members()) { p->sk["build"] = pass ? 9.f : 1.f; p->mood = pass ? 90.f : 30.f; }
            s.step(60);
            if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); }
            for (auto& b : s.plan) if (b.done && b.craftW > 0 && !before.count(b.id) && !first) first = &b;
        }
        craft[pass] = first ? first->craft : 0;
    }
    CHECK(craft[1] > craft[0] + .15f, cat("skilled, cheerful builders build better (", int(craft[1] * 100), " against ", int(craft[0] * 100), ")").c_str());
    Sim q;
    q.newWorld(4242);
    runDays(q, 25);
    Project* st = nullptr;
    for (auto& b : q.plan) if (b.key == "store") st = &b;
    if (st) {
        st->done = st->placed = true; st->siteQuality = 1;
        st->craft = 1.2f; float fine = q.siteQ("store");
        st->craft = .8f; float rough = q.siteQ("store");
        CHECK(fine > rough, "a finely built store works better than a rough one");
    }
}

// The village by hand: worn paths, clearings, orchards, footbridges, decorations, names.
static Project* drawUp(Sim& s, const char* key, int x, int y) {
    // a blueprint placed at a chosen spot, then built at once
    int id = s.blueprint(key);
    if (id < 0) return nullptr;
    Project* b = s.projectById(id);
    if (x >= 0 && !s.placeAt(id, x, y)) { s.cancelBlueprint(); return nullptr; }
    if (x < 0) s.placeSuggested(id);
    return b;
}

static void testVillageByHand() {
    std::printf("the village by hand\n");
    // paths wear in where people walk, and walking on them is quicker
    Sim s;
    s.newWorld(20261017);
    runDays(s, 30);
    int worn = 0;
    for (auto w : s.wear) worn += w >= Sim::PATH;
    CHECK(worn >= 10, cat("paths wear in where people walk (", worn, " tiles)").c_str());
    float moved[2];
    for (int pass = 0; pass < 2; pass++) {
        Sim t;
        t.newWorld(777);
        runDays(t, 1);
        Person& p = *t.members()[0];
        for (auto* q : t.members()) if (q != &p) q->q.clear();
        int y = int(p.y);
        for (int x = int(p.x) - 12; x <= int(p.x) + 12; x++) if (Sim::inb(x, y)) t.wear[Sim::idx(x, y)] = pass ? 200 : 0;
        Action a; a.type = Action::WALK; a.x = p.x + 8; a.y = p.y; a.hasPath = true; a.path = {{p.x + 8, p.y}};
        p.q.clear(); p.q.push_back(a);
        float x0 = p.x;
        t.step(10);
        moved[pass] = p.x - x0;
    }
    CHECK(moved[1] > moved[0] * 1.15f, "walking along a worn path is quicker");
    // paths fade where nobody walks
    int far = -1;
    for (int i = 0; i < GW * GH && far < 0; i++) if (s.tt[i] >= 2 && !s.explored[i]) far = i;
    if (far >= 0) { s.wear[size_t(far)] = 200; runDays(s, 3); CHECK(s.wear[size_t(far)] < 200, "a path nobody walks fades"); }

    // the narrator's hand: unlock everything for the rest
    Sim v;
    v.newWorld(20261017);
    v.askPlacement = true;
    runDays(v, 8);
    for (auto* p : v.members()) p->tiers = {90, 90, 90, 90, 10};
    // a clearing: the trees come down as the work is done, and the timber goes to the stores
    std::pair<int, int> grove{-1, -1};
    int best = 0;
    for (int y = v.campY - 14; y <= v.campY + 14; y++) for (int x = v.campX - 14; x <= v.campX + 14; x++) {
        if (!v.evaluateSite(*projectDef("clearing"), x, y).legal) continue;
        int n = 0;
        for (int yy = y; yy < y + 3; yy++) for (int xx = x; xx < x + 3; xx++) if (v.objAt[Sim::idx(xx, yy)] >= 0 && v.objs[size_t(v.objAt[Sim::idx(xx, yy)])].type == "tree") n++;
        if (n > best) { best = n; grove = {x, y}; }
    }
    Project* cl = best > 0 ? drawUp(v, "clearing", grove.first, grove.second) : nullptr;
    CHECK(cl && cl->placed, cat("a clearing is drawn up over a grove (", best, " trees)").c_str());
    if (cl) {
        float wood0 = v.stock.wood;
        int id = cl->id;
        for (int h = 0; h < 24 * 30 && !v.projectById(id)->done; h++) { v.step(60); if (!v.pendingFate.empty()) { auto f = v.pendingFate[0]; v.pendingFate.clear(); v.applyFate(f); } }
        int left = 0;
        for (int yy = grove.second; yy < grove.second + 3; yy++) for (int xx = grove.first; xx < grove.first + 3; xx++) if (v.objAt[Sim::idx(xx, yy)] >= 0 && v.objs[size_t(v.objAt[Sim::idx(xx, yy)])].type == "tree") left++;
        bool freed = true;
        for (int yy = grove.second; yy < grove.second + 3; yy++) for (int xx = grove.first; xx < grove.first + 3; xx++) freed &= v.bOcc[Sim::idx(xx, yy)] != id;
        CHECK(v.projectById(id)->done && left == 0 && freed && v.stock.wood > wood0 - 60, "once cleared, the trees are gone, the timber stacked and the ground open");
    }
    // an orchard bears fruit, a new kind of food, and makes the homes round it pleasanter
    Project* oc = drawUp(v, "orchard", -1, -1);
    if (oc) {
        float before = v.beauty(oc->x + 2, oc->y + 1);
        oc->done = true; oc->progress = float(oc->work); oc->state = "ripe";
        if (v.active == oc->id) v.active = -1;
        CHECK(v.beauty(oc->x + 2, oc->y + 1) > before, "an orchard makes its surroundings more beautiful");
        bool fruit = false;
        for (int h = 0; h < 24 * 6 && !fruit; h++) { v.step(60); if (!v.pendingFate.empty()) { auto f = v.pendingFate[0]; v.pendingFate.clear(); v.applyFate(f); } fruit = v.foodSeen.count("fruit"); }
        CHECK(fruit && oc->state != "ripe", "ripe fruit is picked and brought in as its own kind of food");
    } else CHECK(false, "an orchard can be drawn up");
    // flowers by a home
    Household* hh = nullptr;
    for (auto& h : v.households) if (h.home >= 0 && v.projectById(h.home)) hh = &h;
    if (hh) {
        Project* home = v.projectById(hh->home);
        float b0 = v.beauty(home->x, home->y);
        Project fl;
        static_cast<ProjectDef&>(fl) = *projectDef("flowers");
        fl.id = v.nextProjId++; fl.done = fl.placed = true; fl.x = home->x + 2; fl.y = home->y;
        v.plan.push_back(fl);
        CHECK(v.beauty(home->x, home->y) > b0, "a flower bed by a home makes it more beautiful");
    }
    // a footbridge: legal only over water with land at both ends, and once built it can be walked
    // anywhere on the island (explored), either way round
    for (auto& e : v.explored) e = 1;
    std::pair<int, int> spot{-1, -1};
    ProjectDef br = *projectDef("bridge");
    for (int turn = 0; turn < 2 && spot.first < 0; turn++) {
        if (turn) std::swap(br.w, br.h);
        for (int y = 0; y < GH && spot.first < 0; y++) for (int x = 0; x < GW; x++) if (v.evaluateSite(br, x, y).legal) { spot = {x, y}; break; }
    }
    CHECK(spot.first >= 0 && !v.evaluateSite(br, v.campX, v.campY).legal, "a footbridge goes only over water, with land at both ends");
    if (spot.first >= 0) {
        Project bp;
        static_cast<ProjectDef&>(bp) = br;
        bp.id = v.nextProjId++; bp.placed = true; bp.x = spot.first; bp.y = spot.second;
        v.plan.push_back(bp);
        int len = std::max(br.w, br.h);
        float ex = spot.first + .5f + (br.w > br.h ? len - 1 : 0), ey = spot.second + .5f + (br.w > br.h ? 0 : len - 1);
        for (int k = 0; k < len; k++) v.bOcc[Sim::idx(spot.first + (br.w > br.h ? k : 0), spot.second + (br.w > br.h ? 0 : k))] = bp.id;
        auto before = v.findPath(spot.first + .5f, spot.second + .5f, ex, ey);
        v.plan.back().done = true;
        auto after = v.findPath(spot.first + .5f, spot.second + .5f, ex, ey);
        CHECK(!after.empty() && int(after.size()) <= len && (before.empty() || before.size() > after.size()), cat("once built, a footbridge is a short walk across (", int(after.size()), " steps, ", int(before.size()), " before)").c_str());
    }
    // names and looks are the narrator's, and are kept
    Project* named = nullptr;
    for (auto& b : v.plan) if (b.done && !b.byHand) { named = &b; break; }
    if (named) {
        v.rename(named->id, "  The Old Mill  ");
        v.restyle(named->id);
        Sim w;
        w.load(v.save());
        Project* again = w.projectById(named->id);
        CHECK(again && again->customName == "The Old Mill" && again->style == 1 && w.nameOf(*again) == "The Old Mill", "a building's name and look are kept");
    }
}

// Petitions: households ask for things by a day; meeting one earns rapport,
// letting it lapse costs some. Trials: foreseen each year, as hard as the
// village was unready, two at once from year five.
static void testPetitionsAndTrials() {
    std::printf("petitions and trials\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 40);
    int asked = s.nextPetition - 1;
    CHECK(asked >= 2, cat("households ask the narrator for things (", asked, " petitions in 40 days)").c_str());
    // the same night, once with a petition met and once with one that lapses
    Household* h = nullptr;
    for (auto& x : s.households) if (!x.members.empty()) h = &x;
    if (!h) { CHECK(false, "a household"); return; }
    untilMidnight(s);
    std::string at = s.save();
    float r[2];
    for (int pass = 0; pass < 2; pass++) {
        Sim t;
        t.load(at);
        t.petitions.clear();
        t.rapport = 50;
        Petition p;
        p.id = 999; p.hh = h->id; p.pid = h->members[0]; p.kind = "luxury"; p.key = "spices"; p.text = "Spices?"; p.due = pass ? t.day() - 1 : t.day() + 5;
        t.goods["spices"] = pass ? 0 : 5;
        t.petitions.push_back(p);
        if (pass == 0) { Sim c; c.load(t.save()); CHECK(c.petitions.size() == 1 && c.petitions[0].id == 999, "a petition survives a save"); }
        pastMidnight(t);
        r[pass] = t.rapport;
        CHECK(std::none_of(t.petitions.begin(), t.petitions.end(), [](const Petition& q) { return q.id == 999; }), pass ? "a petition past its day lapses" : "a met petition is settled");
    }
    CHECK(r[0] > r[1] + 8, cat("meeting a petition earns rapport, letting one lapse costs it (", int(r[0]), " against ", int(r[1]), ")").c_str());

    // the year's trial is foreseen, with a checklist
    Sim y;
    y.newWorld(777);
    runDays(y, 3);
    CHECK(!y.trials.empty() && y.trials[0].kind == "winter" && y.trials[0].start == 31 && !y.trialChecklist("winter").empty(), "early in the year the narrator sees the trial coming: a hard winter");
    CHECK(Sim::trialKinds(2)[0] == "sickness" && Sim::trialKinds(3)[0] == "drought" && Sim::trialKinds(4)[0] == "storms" && Sim::trialKinds(5).size() == 2, "trials escalate: sickness, drought, storms, then two at once");
    // the same village going into winter, ready and not
    while (y.day() < 30) { y.step(60); if (!y.pendingFate.empty()) { auto f = y.pendingFate[0]; y.pendingFate.clear(); y.applyFate(f); } }
    std::string eve = y.save();
    float kept[2], ready[2];
    for (int pass = 0; pass < 2; pass++) {
        Sim t;
        t.load(eve);
        t.stock.food = 1500;
        t.stock.wood = pass ? 0 : 120;
        t.goods["cloth"] = pass ? 0 : 12;
        while (t.day() < 33) { t.step(60); t.pendingFate.clear(); t.stock.wood = std::max(t.stock.wood, pass ? 0.f : 60.f); }
        float atStart = t.stock.food;
        ready[pass] = t.trials.empty() ? -1 : t.trials[0].ready;
        while (t.day() < 41) { t.step(60); t.pendingFate.clear(); }
        kept[pass] = t.stock.food / std::max(1.f, atStart);
    }
    CHECK(ready[0] > ready[1], cat("readiness is measured as the trial begins (", int(ready[0] * 100), "% against ", int(ready[1] * 100), "%)").c_str());
    CHECK(kept[0] > kept[1], cat("a ready village loses less to a hard winter (kept ", int(kept[0] * 100), "% of its food against ", int(kept[1] * 100), "%)").c_str());
}

int main() {
    for (uint32_t seed : {20261017u, 777u, 4242u}) testSurvival(seed);
    testNoSmithNoForge();
    testCharismaPushes();
    testNarrator();
    testContrary();
    testNoGodWords();
    testLines();
    testQuestions();
    testRapportCurrency();
    testLedger();
    testHouseholds();
    testWorkers();
    testPathsAroundBuildings();
    testGoods();
    testStatusAndWork();
    testWorkmanship();
    testVillageByHand();
    testPetitionsAndTrials();
    testDeterminism();
    testFocus(20261017);
    testFocus(777);
    testSaving();
    testSites();
    testPlacementPause();
    testSiteEffects();
    std::printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
    return failures;
}
