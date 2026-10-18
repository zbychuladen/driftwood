// Endings and the shape of a run. Each year closes with a review; the story
// is kept in a chronicle; and the run ends well (Haven, Homeward, Awakening)
// or badly (Overruled, Lost, Divided). After an ending the island goes on, if
// the player wants it to.
#include <algorithm>
#include "Sim.h"

namespace dw {

const char* Sim::endingTitle(const std::string& e) {
    return e == "haven" ? "Haven" : e == "homeward" ? "Homeward" : e == "awakening" ? "Awakening" : e == "overruled" ? "Overruled" : e == "lost" ? "Lost" : e == "divided" ? "Divided" : "";
}

const char* Sim::endingLine(const std::string& e) {
    if (e == "haven") return "A balanced, beautiful village that looks after its own. Nobody here needs rescuing any more.";
    if (e == "homeward") return "The notables paid for a ship, the rest built it, and one bright morning everyone sailed for home.";
    if (e == "awakening") return "In the ruins they found the truth: the narrator is the story they tell about themselves, and always was.";
    if (e == "overruled") return "For a whole year the village followed someone else. The story goes on, but it is no longer the narrator's.";
    if (e == "lost") return "The last fire went out. The island keeps the shape of the village for a while, then the trees come back.";
    if (e == "divided") return "The ones who never rose packed up and left to start a camp of their own. Two villages now, and no peace between them.";
    return "";
}

bool Sim::goodEnding(const std::string& e) { return e == "haven" || e == "homeward" || e == "awakening"; }

void Sim::endWith(const std::string& e) {
    if (!ending.empty()) return;
    ending = e;
    endedDay = day();
    note(cat(endingTitle(e), ". ", endingLine(e)), goodEnding(e) ? "big" : "sad");
}

void Sim::endingsDaily() {
    int d = day();
    // the year in review, as a new year begins
    if (d > 1 && (d - 1) % 40 == 0) {
        YearSummary y;
        y.year = yearN() - 1;
        for (auto& h : households) y.homes[std::clamp(status(h), 0, 4)]++;
        y.people = int(members().size());
        y.births = yBirths; y.deaths = yDeaths; y.met = yMet; y.lapsed = yLapsed;
        for (auto& t : trials) if (t.year == y.year && t.over) { y.trial = t.kind; y.ready = t.ready; }
        y.rival = rival >= 0 ? rivalStanding() : -1;
        y.buyIn = avgBuyIn();
        years.push_back(y);
        yBirths = yDeaths = yMet = yLapsed = 0;
    }
    if (!ending.empty()) return;
    auto m = members();
    if (m.empty()) { endWith("lost"); return; }
    if (overruled() && d - overruledSince >= 40) { endWith("overruled"); return; }
    // divided: half the adults or more bitter for ten days, and they leave
    std::vector<Person*> adults, bitter;
    for (auto* p : m) if (!p->child) { adults.push_back(p); if (p->resentment >= 60) bitter.push_back(p); }
    dividedDays = bitter.size() >= 2 && bitter.size() * 2 >= adults.size() ? dividedDays + 1 : 0;
    if (dividedDays >= 10) {
        for (auto* p : bitter) { p->dead = true; p->gone = true; clearQ(*p); }
        note(cat(listNames(bitter), " walked out of the village with what they could carry, to make a camp of their own."), "sad");
        endWith("divided");
        return;
    }
    for (auto& b : plan) if (b.key == "ship" && b.done) { endWith("homeward"); return; }
    // the ruins' secret: a philosopher, the ruins found, and a story circle where the village tells its tales
    bool ruins = false, thinker = false;
    for (auto& p : pois) ruins |= p.type == "ruins" && p.found;
    for (auto* p : adults) thinker |= p->calling == "philosopher";
    if (ruins && thinker && built("circle") && yearN() >= 2) { endWith("awakening"); return; }
    // haven: a year with nobody lost, no bitterness, most households risen, homes pleasant and served, and trust
    if (yearN() >= 3 && d - lastDeathDay >= 40 && avgBuyIn() >= 60) {
        bool calm = std::none_of(adults.begin(), adults.end(), [](Person* p) { return p->resentment >= 40; });
        int risen = 0, served = 0;
        float beautySum = 0;
        int homes = 0;
        for (auto& h : households) {
            auto* b = h.home >= 0 ? projectById(h.home) : nullptr;
            if (!b) continue;
            homes++;
            risen += status(h) >= 2;
            beautySum += beauty(b->x, b->y);
            for (auto k : {"well", "bath", "school", "infirmary", "circle", "cookhouse"}) if (reach(k, b->x, b->y) <= 14) { served++; break; }
        }
        if (calm && homes && risen * 2 >= homes && served == homes && beautySum / homes >= 3) endWith("haven");
    }
}

} // namespace dw
