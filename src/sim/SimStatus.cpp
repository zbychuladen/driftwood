// Status and work. The higher a household rises, the less its people care for
// hard labour; notables lead, trade with passing ships, commission monuments
// and give artisans patronage instead. A notable seen at common work lifts the
// others, and loses standing if it goes on. The gap between the top and the
// bottom breeds resentment.
#include <algorithm>
#include "Sim.h"

namespace dw {

namespace {
bool hardJob(const std::string& job) { return job == "wood" || job == "stone" || job == "food" || job == "farm" || job == "build"; }
// what kind of work a skill is
std::string jobOfSkill(const std::string& k) { return k == "chop" ? "wood" : k == "mine" ? "stone" : k == "forage" || k == "fish" ? "food" : k; }
} // namespace

int Sim::statusOf(const Person& p) {
    auto* h = householdOf(p.id);
    return h ? status(*h) : 0;
}

float Sim::willing(int status, const std::string& job) {
    static const float HARD[5] = {1, 1, .85f, .6f, .25f};
    return hardJob(job) ? HARD[std::clamp(status, 0, 4)] : 1.f;
}

bool Sim::notableAt(const std::string& job, const Person* besides) {
    for (auto* q : members()) if (q != besides && q->kind == job && statusOf(*q) >= 4) return true;
    return false;
}

float Sim::alongside(const Person& p, const std::string& skill) {
    // working beside a notable who has rolled up their sleeves
    return notableAt(jobOfSkill(skill), &p) ? 1.2f : 1.f;
}

int Sim::topStatus() {
    int top = 0;
    for (auto& h : households) top = std::max(top, status(h));
    return top;
}

void Sim::statusHourly() {
    // a notable at common work: their household's toil, which costs them standing if it goes on
    for (auto* p : members()) if (!p->child && hardJob(p->kind) && statusOf(*p) >= 4) if (auto* h = householdOf(p->id)) h->toil += 1;
}

void Sim::statusDaily() {
    auto m = members();
    std::vector<Person*> notables;
    for (auto* p : m) if (!p->child && statusOf(*p) >= 4) notables.push_back(p);
    for (auto& h : households) h.toil *= .6f;

    // patronage: notables keep the artisans going
    if (!notables.empty())
        for (auto* p : m) {
            if (p->child || p->calling.empty()) continue;
            p->callingNeed = std::max(0.f, p->callingNeed - 4);
            p->esteemPts = std::min(100.f, p->esteemPts + 1);
            for (auto& [g, lv] : p->gifts) { practise(*p, g); break; }
        }

    // a passing ship, while a signal fire or lighthouse stands
    bool beacon = built("signal") || built("lighthouse");
    if (beacon && nextShip == 0) nextShip = day() + 4;
    if (beacon && day() >= nextShip) {
        nextShip = day() + (built("lighthouse") ? 5 : 9);
        Person* trader = notables.empty() ? nullptr : notables[0];
        for (auto* p : m) if (!trader && !p->child && statusOf(*p) >= 3) trader = p;
        if (trader && (stock.food >= 30 || stock.wood >= 25)) {
            // notables trade well; a householder gets less for the same
            bool notable = statusOf(*trader) >= 4;
            if (stock.food >= 30) stock.food -= 30; else stock.wood -= 25;
            addGood("spices", notable ? 3 : 1);
            if (notable) addGood("finecloth", 2);
            note(cat("A ship put in at the cove. ", trader->name, " traded with the crew and came back with ", notable ? "spices and a bolt of fine cloth." : "a little spice.", " The ship sailed on before dark."));
        } else note("A ship passed close to the island, but nobody here had the standing, or the stores, to trade with it. It sailed on.");
    }

    // a notable commissions a monument, once there is stone and none stands for them yet
    int monuments = 0;
    for (auto& b : plan) monuments += b.key == "monument" && !b.skip;
    if (!notables.empty() && monuments < int(notables.size() + 1) / 2 && stock.stone >= 30 && day() % 10 == 0) {
        Person* n = notables[(day() / 10) % notables.size()];
        mkProj(*projectDef("monument"));
        note(cat(n->name, " commissioned a monument, to stand where everyone will see it."), "big");
    }

    // inequality: those well below the top resent it; it fades when the gap closes
    int top = topStatus();
    for (auto* p : m) {
        if (p->child) continue;
        int gap = top - statusOf(*p);
        float before = p->resentment;
        p->resentment = std::clamp(p->resentment + (gap >= 2 ? 2.5f * (gap - 1) : -3.f), 0.f, 100.f);
        if (p->resentment > 0) {
            p->mood = std::max(0.f, p->mood - p->resentment / 25);
            p->buyIn = std::max(0.f, p->buyIn - p->resentment / 60);
        }
        if (before < 50 && p->resentment >= 50) {
            const char* place = top >= 4 ? "the manor" : top == 3 ? "the big house" : "the cottages";
            note(cat(p->name, " has started to say bitter things about ", place, ". Not everyone here is rising."), "sad");
        }
    }
}

} // namespace dw
