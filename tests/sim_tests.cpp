// Headless simulation tests. No framework needed: each check prints and
// counts failures; the process exit code is the number of failures.
#include <cmath>
#include <algorithm>
#include <cstdio>
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
    s.whisper(*p, "explore");
    CHECK(s.whisper(*p, "explore") == "pushed", "second whisper in a day unsettles them");
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
            // an empty queue means a whisper or fever just interrupted them; they get a new job next update
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
    const SiteReport *byHomes = nullptr, *byStone = nullptr;
    for (auto& [at, r] : forgeSites) {
        if (!byHomes && mentions(r, "homes")) byHomes = &r;
        if (!byStone && mentions(r, "stone") && !mentions(r, "homes")) byStone = &r;
    }
    CHECK(byHomes && byStone && byStone->quality > byHomes->quality, "a forge with stone at hand beats one beside the homes");
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
    for (const char* key : {"forge", "well", "store", "pottery", "school", "cookhouse"}) {
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
    std::printf("placing a building\n");
    Sim s;
    s.newWorld(20261017);
    s.askPlacement = true;
    int guard = 0;
    while (!s.pendingPlacement && guard++ < 24 * 120) { s.step(60); if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); } }
    CHECK(s.pendingPlacement.has_value(), "a specialist building comes up for the narrator to place");
    if (!s.pendingPlacement) return;
    double t = s.tMin;
    s.step(600);
    CHECK(s.tMin == t, "the game stands still while the spot is chosen");
    // the choice survives saving
    Sim copy;
    copy.load(s.save());
    CHECK(copy.pendingPlacement && copy.pendingPlacement->project == s.pendingPlacement->project, "a pending placement survives a save");
    int id = s.pendingPlacement->project;
    CHECK(!s.placeAt(id, 0, 0), "a spot in the sea is refused");
    Project* b = s.projectById(id);
    // choose the worst legal spot on purpose, then see it recorded
    auto all = sites(s, *b, 20);
    auto worst = std::min_element(all.begin(), all.end(), [](auto& a, auto& c) { return a.second.quality < c.second.quality; });
    CHECK(worst != all.end() && s.placeAt(id, worst->first.first, worst->first.second) && b->placed && b->sited && std::abs(b->siteQuality - worst->second.quality) < 1e-4f,
          "the narrator's spot is used, and its quality kept");
    s.step(120);
    CHECK(!s.pendingPlacement && s.tMin > t, "the game goes on once the spot is chosen");
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
    paired("store", 1.2f, .7f, 30, [](Sim& s) { return s.stock.food; }, g, b);
    CHECK(g > b, cat("a well-placed store keeps more food (", int(g), " vs ", int(b), " on average)").c_str());
    paired("well", 1.2f, .7f, 20, [](Sim& s) { float t = 0; int n = 0; for (auto* p : s.members()) { t += p->needs.thirst; n++; } return n ? t / n : 0; }, g, b);
    CHECK(g < b, cat("people are less thirsty with a well in a good spot (", int(g), " vs ", int(b), ")").c_str());
}

int main() {
    for (uint32_t seed : {20261017u, 777u, 4242u}) testSurvival(seed);
    testNoSmithNoForge();
    testCharismaPushes();
    testNarrator();
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
