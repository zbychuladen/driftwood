// The rival: a charismatic settler with their own idea of how things should
// go, who wins people over (the resentful first) and competes with the
// narrator for the village's trust. Fall behind them for a season and they
// take over the planning: blueprints and appointments stop working until the
// village turns back.
#include <algorithm>
#include "Sim.h"

namespace dw {

float Sim::rivalStanding() {
    float s = 0;
    int n = 0;
    for (auto* p : members()) if (!p->child && p->id != rival) { s += p->follow; n++; }
    return n ? s / n : 0;
}

bool Sim::overruled() const { return overruledSince > 0; }

void Sim::rivalDaily() {
    auto m = members();
    Person* r = rival >= 0 ? person(rival) : nullptr;
    if (r && (r->dead || r->status != "member")) { note(cat("With ", r->name, " gone, nobody speaks against the narrator any more."), "narrator"); rival = -1; overruledSince = 0; rivalAhead = 0; r = nullptr; for (auto* p : m) p->follow = 0; }
    // someone charismatic, well placed and unconvinced steps forward
    if (!r && day() >= 20) {
        Person* best = nullptr;
        float bs = 0;
        for (auto* p : m) {
            if (p->child || p->charisma < 7) continue;
            float s = p->charisma * 2 + statusOf(*p) * 3 + (100 - p->buyIn) / 10 + p->resentment / 10;
            if (s > bs && p->buyIn < 70) { bs = s; best = p; }
        }
        if (best && avgBuyIn() < 75) {
            rival = best->id;
            r = best;
            note(cat(best->name, " has started saying the village would do better without the narrator's say. Some are listening."), "sad");
        }
    }
    if (!r) return;
    // people drift toward the rival: the resentful and the unconvinced fastest; the rival works on the most bitter each day
    float pullK = r->charisma / 7.f;
    Person* target = nullptr;
    for (auto* p : m) {
        if (p->child || p == r) continue;
        float d = (.4f + p->resentment / 25 + (60 - p->buyIn) / 50) * pullK;
        p->follow = std::clamp(p->follow + d, 0.f, 100.f);
        if (!target || p->resentment > target->resentment) target = p;
    }
    if (target && target->resentment > 10) { target->follow = std::min(100.f, target->follow + 8); bubble(*r, "We could run this ourselves", 80); }
    // ahead of the narrator for a season: the rival takes over the planning
    float mine = avgBuyIn(), theirs = rivalStanding();
    rivalAhead = theirs > mine ? rivalAhead + 1 : 0;
    if (!overruled() && rivalAhead >= 10) {
        overruledSince = day();
        note(cat("The village has gone ", r->name, "'s way. They decide what gets built now, and the narrator's blueprints and appointments count for nothing until that changes."), "sad");
        for (auto* p : m) if (p->duty) p->duty.reset();
    }
    // winning them back
    if (overruled() && mine > theirs + 10) {
        overruledDays += day() - overruledSince;
        overruledSince = 0;
        rivalAhead = 0;
        note(cat("The village has turned back to the narrator. ", r->name, " grumbles, but the blueprints are the narrator's again."), "narrator");
    }
}

void Sim::rivalCools(float n, const Household* h) {
    // the narrator's plans working out: followers drift back
    for (auto* p : members()) {
        if (h && std::find(h->members.begin(), h->members.end(), p->id) == h->members.end()) continue;
        p->follow = std::max(0.f, p->follow - n);
    }
}

} // namespace dw
