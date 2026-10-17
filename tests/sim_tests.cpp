// Headless simulation tests. No framework needed: each check prints and
// counts failures; the process exit code is the number of failures.
#include <cstdio>
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
    runDays(s, 100);
    int built = 0;
    for (auto& b : s.plan) if (b.done) built++;
    CHECK(!s.members().empty(), "village survives 100 days");
    CHECK(built >= 6, "at least six structures built");
    CHECK(s.fire() != nullptr, "campfire built");
    int explored = 0;
    for (auto e : s.explored) explored += e;
    CHECK(explored > GW * GH / 4, "a quarter of the island explored");
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

static void testDeterminism() {
    std::printf("determinism\n");
    Sim a, b;
    a.newWorld(99); b.newWorld(99);
    runDays(a, 30); runDays(b, 30);
    CHECK(a.stock.food == b.stock.food && a.members().size() == b.members().size() && a.journal.size() == b.journal.size(), "same seed, same history");
}

int main() {
    for (uint32_t seed : {20261017u, 777u, 4242u}) testSurvival(seed);
    testNoSmithNoForge();
    testCharismaPushes();
    testNarrator();
    testDeterminism();
    std::printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
    return failures;
}
