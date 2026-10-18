// The Narrator's Ledger: what is going wrong in the village, who could put it
// right and what asking them would cost, and the bigger nudges: a season's
// work, an appointed role, an appeal to everyone.
#include <algorithm>
#include "Sim.h"

namespace dw {

namespace {
const char* doing(const std::string& job) {
    return job == "wood" ? "cutting wood" : job == "food" ? "gathering food" : job == "build" ? "building" : job == "explore" ? "exploring" : job == "rest" ? "resting" : "that";
}
float skillFor(const Person& p, const std::string& job) {
    auto sk = [&](const char* k) { auto it = p.sk.find(k); return it == p.sk.end() ? 0.f : it->second; };
    if (job == "wood") return sk("chop");
    if (job == "food") return std::max(sk("forage"), sk("fish"));
    if (job == "build") return sk("build");
    if (job == "explore") return p.has("curious") ? 6.f : 3.f;
    return 0;
}
} // namespace

const char* Sim::roleName(const std::string& job) {
    return job == "wood" ? "woodcutter" : job == "food" ? "forager" : job == "build" ? "builder" : job == "explore" ? "explorer" : "helper";
}

std::vector<Gap> Sim::ledger() {
    std::vector<Gap> out;
    std::vector<Person*> adults;
    for (auto* p : members()) if (!p->child) adults.push_back(p);
    if (adults.empty()) return out;
    auto on = [&](std::initializer_list<const char*> kinds) {
        int n = 0;
        for (auto* p : adults) for (auto k : kinds) if (p->kind == k) { n++; break; }
        return n;
    };
    auto gap = [&](const std::string& id, const std::string& text, const std::string& job, const std::string& fix) { out.push_back({id, text, job, fix, {}}); };
    float fd = foodDays();
    auto* f = fire();
    int inSeason = (day() - 1) % 10;
    // who is doing what only means something in working hours; at night everyone sleeps
    bool working = hour() >= 7 && hour() < 18;
    if (working && stock.wood < 12 && on({"wood", "fuel"}) == 0) gap("wood", cat("Nobody is cutting wood, and only ", int(stock.wood), " logs are left."), "wood", "Appoint a woodcutter, so wood keeps coming without asking each day.");
    if (working && fd < 4 && on({"food", "farm"}) < 2) gap("food", cat("Food will last about ", std::max(0, int(fd)), fd >= 1 && fd < 2 ? " day" : " days", ", and few are gathering."), "food", "A forager for the season, or fields and a farmer to tend them.");
    if (auto* pr = activeProject(); working && pr && pr->progress > 0 && on({"build"}) == 0) gap("builder", cat("No builder on the ", lower(pr->name), "."), "build", "Appoint a builder. A carpenter builds faster.");
    if (f && f->done && f->fuel < 2 && (season() == 3 || (season() == 2 && inSeason >= 6)) && on({"fuel", "wood"}) == 0) gap("fire", "The fire is low, and winter is close.", "wood", "Keep a woodcutter on through the winter.");
    for (auto* p : adults)
        if (p->sick > 0 && !giftHolder("herbalist", 1)) { gap("herbalist", cat(p->name, " is sick, and there is no herbalist."), "", "Newcomers sometimes bring a herbalist. Until then, rest and a warm fire help."); break; }
    float tired = 0;
    for (auto* p : adults) tired += p->needs.fatigue;
    if (working && tired / adults.size() > 70) gap("tired", "Everyone is worn out.", "rest", "Huts and a longhouse let people sleep better.");
    int land = 0, known = 0;
    for (int i = 0; i < GW * GH; i++) if (tt[i] >= 2) { land++; known += explored[i]; }
    if (working && day() >= 4 && land && known < land / 4 && on({"explore"}) == 0) gap("explore", "Most of the island is still unknown, and nobody is exploring.", "explore", "An explorer for the season.");

    // a home about to fall back a level
    for (auto& h : households) {
        if (h.falling < 2) continue;
        auto* b = h.home >= 0 ? projectById(h.home) : nullptr;
        if (!b) continue;
        int lv = homeLevel(b->key);
        std::string lack;
        for (auto& n : homeNeeds(h, lv)) if (!n.met) { lack = n.text; break; }
        gap("home", cat(householdName(h), "'s ", lower(b->name), " is short of ", lack, ". In ", FALL_DAYS - h.falling, FALL_DAYS - h.falling == 1 ? " day" : " days", " it falls back to a ", levelKey(lv - 1), "."), "", "Find what it lacks, or a service within reach of it.");
    }
    // goods a household is waiting on, that nobody is making
    std::map<std::string, std::string> waiting;   // good -> the home level wanting it
    bool sameFood = false;
    for (auto& h : households) {
        auto* b = h.home >= 0 ? projectById(h.home) : nullptr;
        if (!b) continue;
        int lv = homeLevel(b->key);
        for (int want : {lv, lv + 1}) for (auto& n : homeNeeds(h, want)) {
            if (n.met) continue;
            if (n.id == "variety") sameFood = true;
            for (auto& g : goodList()) if (n.id == g.key && !waiting.count(g.key)) waiting[g.key] = levelKey(want);
        }
    }
    for (auto& g : goodList()) {
        if (!waiting.count(g.key)) continue;
        bool maker = built(g.building) && giftHolder(g.gift, std::string(g.key) == "finecloth" ? 5 : 1);
        if (maker) continue;
        std::string what = lower(g.name), home = waiting[g.key];
        std::string fix = !built(g.building) ? cat("A ", lower(projectDef(g.building)->name), " and a ", lower(giftInfo().at(g.gift).n), " to work it.")
                        : std::string(g.key) == "finecloth" ? "A weaver practised enough (level 5) makes fine cloth once there are blankets."
                        : cat("A ", lower(giftInfo().at(g.gift).n), " to work the ", lower(projectDef(g.building)->name), ". Newcomers may bring one.");
        gap("goods", cat("A ", home, " needs ", what, ", and nobody is making any."), "", fix);
    }
    if (sameFood) gap("variety", cat("The same food day after day: ", foodVariety(), foodVariety() == 1 ? " kind" : " kinds", " lately."), "food", "A garden, fields, a dock or a smokehouse add kinds of food.");
    // who could do it, cheapest first: the price, their skill, and the gap their going would leave
    for (auto& g : out) {
        if (g.job.empty()) continue;
        struct Scored { float score; Candidate c; };
        std::vector<Scored> v;
        for (auto* p : adults) {
            if (p->sick > 0 || contrary(*p) || p->kind == g.job) continue;
            int cost = price(*p, g.job, 1);
            for (auto& l : linesFor(*p, true)) if (l.job == g.job) cost = l.cost;
            float sk = skillFor(*p, g.job);
            bool only = (p->kind == "food" || p->kind == "build" || p->kind == "wood") && on({p->kind.c_str()}) == 1;
            std::string why = only ? cat("the only one ", doing(p->kind)) : g.job != "rest" && p->needs.fatigue > 70 ? "worn out already" : sk >= 5 ? cat("good at ", doing(g.job)) : likely(*p, Line{g.job, "", -1, cost}) == "refused" ? "unlikely to agree" : cat("buys in ", buyInWord(p->buyIn));
            float score = cost - (g.job == "rest" ? p->needs.fatigue / 25 : sk / 3) + (only ? 3 : 0) + (likely(*p, Line{g.job, "", -1, cost}) == "refused" ? 6 : 0);
            v.push_back({score, {p->id, cost, why}});
        }
        std::stable_sort(v.begin(), v.end(), [](const Scored& a, const Scored& b) { return a.score < b.score; });
        for (size_t i = 0; i < v.size() && i < 3; i++) g.candidates.push_back(v[i].c);
    }
    return out;
}

// ---------------------------------------------------------------- the bigger nudges

namespace {
int dayCost(Sim& s, const Person& p, const std::string& job) {
    for (auto& l : s.linesFor(p, true)) if (l.job == job) return l.cost;
    return Sim::price(p, job, 1);
}
} // namespace

int Sim::seasonCost(const Person& p, const std::string& job) { return dayCost(*this, p, job) * 3; }
int Sim::roleCost(const Person& p, const std::string& job) { return dayCost(*this, p, job) * 2; }
int Sim::appealCost(const std::string&) {
    int n = 0;
    for (auto* p : members()) n += !p->child;
    return 4 + 3 * n;
}

std::string Sim::askSeason(Person& p, const std::string& job) {
    int cost = seasonCost(p, job);
    if (float(cost) > rapport) return "unaffordable";
    lastToldName = p.name;
    p.pressure += 1;
    // a longer ask needs more buy-in than a day's
    if (contrary(p) || likely(p, Line{job, "", -1, int(std::lround(dayCost(*this, p, job) * 1.5f))}) == "refused") {
        bubble(p, "A whole season? No.", 70);
        lastSaid = "A whole season? No.";
        note(cat("The narrator asked ", p.name, " to spend the season ", doing(job), ". They would not hear of it."), "narrator");
        return lastReply = "refused";
    }
    earn(-float(cost));
    int next = ((day() - 1) / 10 + 1) * 10 + 1;   // the first day of next season
    p.duty = Duty{job, (next - 1) * 1440.0, false};
    bubble(p, "Till the season turns, then.", 70);
    lastSaid = "Till the season turns, then.";
    note(cat(p.name, " agreed to spend the rest of the season ", doing(job), "."), "narrator");
    return lastReply = "agreed";
}

std::string Sim::appoint(Person& p, const std::string& job) {
    int cost = roleCost(p, job);
    if (float(cost) > rapport) return "unaffordable";
    lastToldName = p.name;
    p.pressure += 1;
    if (contrary(p) || likely(p, Line{job, "", -1, int(std::lround(dayCost(*this, p, job) * 1.5f))}) == "refused") {
        // turning down a title stings both ways
        p.buyIn = std::max(0.f, p.buyIn - 3);
        bubble(p, cat("Me, the ", roleName(job), "? I don't think so."), 70);
        lastSaid = cat("Me, the ", roleName(job), "? I don't think so.");
        note(cat("The narrator wanted ", p.name, " as the camp's ", roleName(job), ". ", p.name, " said no, and was a little put out to be asked."), "narrator");
        return lastReply = "refused";
    }
    earn(-float(cost));
    p.duty = Duty{job, 0, true};
    bubble(p, cat("The ", roleName(job), ". All right."), 70);
    lastSaid = cat("The ", roleName(job), ". All right.");
    note(cat(p.name, " is the camp's ", roleName(job), " now, by the narrator's choosing."), "narrator");
    return lastReply = "agreed";
}

void Sim::revoke(Person& p) {
    if (!p.duty) return;
    std::string job = p.duty->job;
    bool role = p.duty->role;
    p.duty.reset();
    note(role ? cat(p.name, " is no longer the camp's ", roleName(job), ".") : cat(p.name, " was let off the season's ", doing(job), "."));
}

std::string Sim::appeal(const std::string& job) {
    int cost = appealCost(job);
    if (float(cost) > rapport) return "unaffordable";
    earn(-float(cost));
    int yes = 0, all = 0;
    for (auto* p : members()) {
        if (p->child) continue;
        all++;
        int c = dayCost(*this, *p, job);
        if (contrary(*p) || p->sick > 0 || likely(*p, Line{job, "", -1, c}) == "refused") continue;
        yes++;
        p->told = Narration{job, tMin + 9 * 60, false, tMin};
        clearQ(*p);
        p->carry.reset();
        bubble(*p, "Everyone? Fine.", 60);
    }
    note(cat("The narrator asked the whole camp to spend the day ", doing(job), ". ", yes, " of ", all, " went along."), "narrator");
    lastToldName.clear();
    lastSaid.clear();
    return lastReply = cat("agreed ", yes, " of ", all);
}

void Sim::dutiesDaily() {
    for (auto* p : members()) {
        if (!p->duty) continue;
        if (!p->duty->role && p->duty->until <= tMin) {
            note(cat(p->name, "'s season of ", doing(p->duty->job), " is over."));
            p->duty.reset();
        } else if (p->duty->role) {
            // a role is kept up with a little rapport each day; with none left it lapses
            if (rapport < 1) { note(cat(p->name, " stopped acting as the camp's ", roleName(p->duty->job), ". Nobody seemed to be asking any more.")); p->duty.reset(); }
            else earn(-1);
        }
    }
}

} // namespace dw
