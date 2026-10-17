// Headless simulation tests. No framework needed: each check prints and
// counts failures; the process exit code is the number of failures.
#include <cmath>
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

int main() {
    for (uint32_t seed : {20261017u, 777u, 4242u}) testSurvival(seed);
    testNoSmithNoForge();
    testCharismaPushes();
    testNarrator();
    testDeterminism();
    testFocus(20261017);
    testFocus(777);
    testSaving();
    std::printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
    return failures;
}
