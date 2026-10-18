// Petitions and trials. Households ask for things (a building, a better home,
// a feast, a luxury) with a deadline; meeting one earns rapport, letting it
// lapse costs some. Each year the narrator foresees a trial, the village gets
// ready or does not, and the trial goes as hard as the readiness allows.
#include <algorithm>
#include <cmath>
#include "Sim.h"

namespace dw {

namespace {
const char* const TRIALS[4] = {"winter", "sickness", "drought", "storms"};
float u01(std::mt19937& g) { return std::uniform_real_distribution<float>(0, 1)(g); }
} // namespace

// ---------------------------------------------------------------- petitions

bool Sim::petitionMet(const Petition& p) {
    if (p.kind == "building") { for (auto& b : plan) if (b.key == p.key && b.done && !b.skip) return true; return false; }
    if (p.kind == "home") { auto* h = household(p.hh); return h && status(*h) >= homeLevel(p.key); }
    if (p.kind == "luxury") return good(p.key) >= 2;
    if (p.kind == "feast") return day() >= p.due && stock.food >= members().size() * 8.f;
    return false;
}

std::string Sim::petitionNeed(const Petition& p) {
    if (p.kind == "building") return cat("a ", lower(projectDef(p.key)->name), " standing in the village");
    if (p.kind == "home") return cat("their home made a ", p.key);
    if (p.kind == "luxury") for (auto& g : goodList()) if (p.key == g.key) return cat(lower(g.name), " in the village (2 or more)");
    if (p.kind == "feast") return cat("food for a feast on the day: ", int(members().size() * 8), " in store");
    return "";
}

void Sim::petitionsDaily() {
    // settle what is met or overdue
    for (size_t i = 0; i < petitions.size();) {
        Petition p = petitions[i];
        Household* h = household(p.hh);
        bool met = h && petitionMet(p), late = day() > p.due || !h;
        if (!met && !late) { i++; continue; }
        petitions.erase(petitions.begin() + long(i));
        if (!h) continue;
        std::string who = householdName(*h);
        if (met) {
            earn(6);
            yMet++;
            rivalCools(10, h);
            for (int m : h->members) if (auto* q = person(m)) { q->buyIn = std::min(100.f, q->buyIn + 5); q->mood = std::min(100.f, q->mood + 4); }
            if (p.kind == "feast") { stock.food -= members().size() * 4.f; for (auto* q : members()) q->mood = std::min(100.f, q->mood + 10); }
            note(p.kind == "feast" ? cat("The feast ", who, " asked for: a long night of food and songs by the fire.") : cat(who, " got what they asked the narrator for: ", petitionNeed(p), ". They will not forget it."), "narrator");
        } else {
            earn(-4);
            yLapsed++;
            for (int m : h->members) if (auto* q = person(m)) q->buyIn = std::max(0.f, q->buyIn - 4);
            note(cat(who, " asked for ", petitionNeed(p), ", and the time ran out. They feel the narrator did not listen."), "sad");
        }
    }
    // now and then a household asks for something
    if (petitions.size() >= 3 || households.empty() || u01(talkGen) > .3f) return;
    Household& h = households[size_t(std::uniform_int_distribution<size_t>(0, households.size() - 1)(talkGen))];
    for (auto& p : petitions) if (p.hh == h.id) return;
    Person* speaker = nullptr;
    for (int m : h.members) if (auto* q = person(m); q && !q->child) { speaker = q; break; }
    if (!speaker) return;
    Petition p;
    p.id = nextPetition++;
    p.hh = h.id;
    p.pid = speaker->id;
    p.due = day() + 8;
    std::vector<std::string> kinds;
    auto w = askPlacement ? wishes() : std::vector<Project*>{};
    if (!w.empty()) kinds.push_back("building");
    int lv = status(h);
    if (h.home >= 0 && lv >= 1 && lv < 4) {
        auto next = homeNeeds(h, lv + 1);
        int met = 0;
        for (auto& n : next) met += n.met;
        if (met * 2 >= int(next.size())) kinds.push_back("home");
    }
    if (lv >= 2) kinds.push_back("luxury");
    if ((day() - 1) % 10 >= 6) kinds.push_back("feast");
    if (kinds.empty()) return;
    p.kind = kinds[size_t(std::uniform_int_distribution<size_t>(0, kinds.size() - 1)(talkGen))];
    if (p.kind == "building") { p.key = w[0]->key; p.text = cat("Could we have a ", lower(w[0]->name), "? We have been talking about it for days."); p.due = day() + 12; }
    else if (p.kind == "home") { p.key = levelKey(lv + 1); p.text = cat("We would like to make our home a ", p.key, ". We are nearly there."); p.due = day() + 10; }
    else if (p.kind == "luxury") {
        static const char* LUX[4] = {"spices", "furniture", "cloth", "finecloth"};
        p.key = LUX[std::uniform_int_distribution<int>(0, lv >= 3 ? 3 : 2)(talkGen)];
        std::string nm;
        for (auto& g : goodList()) if (p.key == g.key) nm = lower(g.name);
        p.text = cat("A little ", nm, " would make all the difference to us.");
    } else { p.due = ((day() - 1) / 10 + 1) * 10 + 1; p.text = "When the season turns, could we have a feast? Everyone has worked so hard."; }
    petitions.push_back(p);
    bubble(*speaker, "Narrator, a favour?", 80);
}

// ---------------------------------------------------------------- visions and trials

std::vector<std::string> Sim::trialKinds(int year) {
    std::vector<std::string> v{TRIALS[(year - 1) % 4]};
    if (year >= 5) v.push_back(TRIALS[year % 4]);   // later years bring two at once
    return v;
}

int Sim::trialSeason(const std::string& kind) { return kind == "winter" ? 3 : kind == "storms" ? 2 : 1; }

std::string Sim::trialName(const std::string& kind) {
    return kind == "winter" ? "a hard winter" : kind == "sickness" ? "a sickness" : kind == "drought" ? "a drought" : "a season of storms";
}

std::vector<HomeNeed> Sim::trialChecklist(const std::string& kind) {
    std::vector<HomeNeed> v;
    auto add = [&](const std::string& id, bool met, const std::string& text) { v.push_back({id, text, met}); };
    auto count = [&](const std::string& id, float have, float need, const std::string& text) { v.push_back({id, text, have >= need, have, need}); };
    float fd = foodDays();
    int people = int(members().size());
    if (kind == "winter") {
        count("food", fd, 20, "food for twenty days");
        count("wood", stock.wood, 40, "forty logs stacked");
        count("cloth", good("cloth"), 4, "blankets for the cold");
        count("beds", float(totalBeds(false)), float(people), "a roof over everyone");
    } else if (kind == "sickness") {
        add("herbalist", giftHolder("herbalist", 1) != nullptr, "a herbalist");
        count("herbs", good("herbs"), 6, "remedies put by");
        add("infirmary", built("infirmary") != nullptr, "an infirmary");
        add("well", built("well") != nullptr, "clean water from a well");
    } else if (kind == "drought") {
        add("well", built("well") != nullptr, "a well that will not dry up");
        count("food", fd, 15, "food for fifteen days");
        add("store", built("store") != nullptr, "a storehouse");
        add("keep", built("smoke") || built("rack"), "a smokehouse or drying rack");
    } else {
        add("store", built("store") != nullptr, "a storehouse to keep food dry");
        count("wood", stock.wood, 30, "thirty logs for repairs");
        int housed = 0;
        for (auto& h : households) housed += h.home >= 0;
        count("homes", float(housed), std::max(1.f, std::ceil(households.size() / 2.f)), "proper homes for most households");
        add("tower", built("tower") != nullptr, "a watchtower to see them coming");
    }
    return v;
}

float Sim::readiness(const std::string& kind) {
    auto c = trialChecklist(kind);
    int met = 0;
    for (auto& n : c) met += n.met;
    return c.empty() ? 1.f : float(met) / c.size();
}

float Sim::trialHard(const std::string& kind) {
    // how hard a running trial bites: unreadiness, growing with the years
    for (auto& t : trials) if (t.kind == kind && t.active) return (1 - t.ready) * (1 + .2f * (t.year - 1));
    return 0;
}

void Sim::trialsDaily() {
    int d = day(), y = yearN(), inYear = (d - 1) % 40;
    // early each year, the narrator sees what is coming
    if (inYear == 1) {
        bool seen = false;
        for (auto& t : trials) seen |= t.year == y;
        if (!seen)
            for (auto& k : trialKinds(y)) {
                Trial t;
                t.kind = k; t.year = y; t.start = (y - 1) * 40 + trialSeason(k) * 10 + 1;
                trials.push_back(t);
                static const char* WHEN[4] = {"in spring", "this summer", "in the autumn", "this winter"};
                note(cat("The narrator can see what is coming: ", trialName(k), ", ", WHEN[trialSeason(k)], ". Everyone heard, and took it seriously."), "narrator");
            }
    }
    for (auto& t : trials) {
        if (!t.active && !t.over && d >= t.start) {
            t.active = true;
            t.ready = readiness(t.kind);
            note(cat(t.kind == "winter" ? "The hard winter has come." : t.kind == "sickness" ? "The sickness has come." : t.kind == "drought" ? "The drought has begun. Not a drop of rain." : "The storms have begun.", " We were ", t.ready >= .99f ? "as ready as we could be." : t.ready >= .5f ? "partly ready." : "not ready."), t.ready >= .5f ? "big" : "sad");
            if (t.kind == "sickness") {
                float hard = (1 - t.ready) * (1 + .2f * (t.year - 1));
                int n = int(std::lround(hard * members().size() * .4f));
                std::vector<Person*> m = members();
                for (auto* p : m) if (n > 0 && !p->child && p->sick <= 0) { p->sick = 25; clearQ(*p); n--; }
                lastSick = d;
            }
        }
        if (t.active && d >= t.start + 10) {
            t.active = false;
            t.over = true;
            earn(t.ready * 12 - 4);
            if (t.ready >= .5f) rivalCools(12 * t.ready);
            for (auto* p : members()) p->buyIn = std::clamp(p->buyIn + (t.ready >= .5f ? 3.f : -3.f), 0.f, 100.f);
            note(cat("The ", t.kind == "winter" ? "hard winter" : t.kind == "storms" ? "storms" : t.kind, " ", t.kind == "storms" ? "have" : "has", " passed. ", t.ready >= .99f ? "We were ready for it, and the narrator had seen it coming." : t.ready >= .5f ? "It could have gone worse." : "It cost us more than it should have."), t.ready >= .5f ? "big" : "sad");
        }
        if (!t.active) continue;
        float hard = (1 - t.ready) * (1 + .2f * (t.year - 1));
        if (t.kind == "winter") stock.food -= std::floor(stock.food * .04f * hard);
        if (t.kind == "sickness" && hard > 0 && u01(gen) < .15f * hard) {
            std::vector<Person*> well;
            for (auto* p : members()) if (!p->child && p->sick <= 0) well.push_back(p);
            if (!well.empty()) { Person* p = pick(well); p->sick = 20; clearQ(*p); }
        }
        if (t.kind == "storms") {
            weather = u01(gen) < .55f ? "storm" : weather;
            stock.wood = std::max(0.f, stock.wood - 3 * hard);
            stock.food -= std::floor(stock.food * .04f * hard);
            for (auto* p : members()) p->mood = std::max(0.f, p->mood - 3 * hard);
        }
    }
}

} // namespace dw
