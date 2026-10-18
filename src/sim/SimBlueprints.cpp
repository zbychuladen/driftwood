// Blueprints: with the narrator placing, the settlers build their own homes and
// small things, and everything else waits for a blueprint from the build menu.
// Blueprints unlock as the village clears the levels of the needs pyramid, and
// what gets built is only as good as the hands that build it.
#include <algorithm>
#include "Sim.h"

namespace dw {

bool Sim::settlersBuild(const ProjectDef& d) { return d.key == "fire" || d.key == "leanto" || d.key == "rack" || d.key == "hut"; }

int Sim::villageLevel() {
    std::vector<Person*> adults;
    for (auto* p : members()) if (!p->child) adults.push_back(p);
    if (adults.empty()) return 0;
    int level = 0;
    for (int t = 0; t < 5; t++) {
        float avg = 0;
        for (auto* p : adults) avg += p->tiers[t];
        if (avg / adults.size() < 55) break;
        level = t + 1;
    }
    return level;
}

int Sim::blueprintLevel(const std::string& k) {
    // 0 from the start; 1 once bodies are cared for; 2 once people feel safe; 3 once they belong; 4 once they are esteemed
    static const std::map<std::string, int> L = {
        {"garden", 0}, {"well", 0}, {"dock", 0}, {"store", 0}, {"smoke", 0},
        {"fields", 1}, {"cookhouse", 1}, {"tower", 1}, {"forge", 1}, {"mine", 1}, {"herbs", 1}, {"pottery", 1},
        {"loomhouse", 2}, {"workshop", 2}, {"longhouse", 2}, {"signal", 2},
        {"circle", 3}, {"bath", 3}, {"infirmary", 3}, {"school", 3},
        {"lighthouse", 4}, {"boatyard", 4}};
    auto it = L.find(k);
    return it == L.end() ? 0 : it->second;
}

bool Sim::unlocked(const std::string& key, std::string* why) {
    static const char* WHEN[5] = {"", "once everyone is fed and rested", "once people feel safe", "once people feel they belong", "once people are esteemed"};
    int need = blueprintLevel(key);
    if (villageLevel() >= need) return true;
    if (why) *why = cat("Unlocks ", WHEN[need], ".");
    return false;
}

std::vector<const ProjectDef*> Sim::blueprints() {
    std::vector<const ProjectDef*> v;
    for (auto& d : allProjects()) if (!settlersBuild(d) && !d.upgrade && !d.commissioned) v.push_back(&d);
    std::stable_sort(v.begin(), v.end(), [](const ProjectDef* a, const ProjectDef* b) { return blueprintLevel(a->key) < blueprintLevel(b->key); });
    return v;
}

int Sim::blueprint(const std::string& key) {
    const ProjectDef* d = projectDef(key);
    if (!d || settlersBuild(*d) || d->upgrade || d->commissioned || !unlocked(key) || pendingPlacement) return -1;
    // an unplaced one already in the plan, or a new one
    Project* b = nullptr;
    for (auto& p : plan) if (p.key == key && !p.placed && !p.done && !p.skip) { b = &p; break; }
    bool fresh = !b;
    if (!b) b = &mkProj(*d);
    auto s = suggestSite(*b);
    if (!s) { if (fresh) b->skip = true; return -1; }
    pendingPlacement = PendingPlacement{b->id, s->first, s->second, true};
    if (fresh) b->waitNoted = true;   // marks it as made for this blueprint (cancelBlueprint removes it)
    return b->id;
}

void Sim::cancelBlueprint() {
    if (!pendingPlacement || !pendingPlacement->chosen) return;
    if (auto* b = projectById(pendingPlacement->project); b && !b->placed && b->waitNoted && b->progress <= 0) b->skip = true;
    pendingPlacement.reset();
}

std::vector<Project*> Sim::wishes() {
    std::vector<std::pair<float, Project*>> v;
    int i = 0;
    for (auto& b : plan) {
        i++;
        if (b.placed || b.done || b.skip || settlersBuild(b) || !prereqOk(b) || !giftOk(b) || !unlocked(b.key)) continue;
        float s = needOf(b.key) * 2 - i * .03f;
        if (auto ch = champion(b)) s += ch->push;
        v.push_back({s, &b});
    }
    std::stable_sort(v.begin(), v.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<Project*> out;
    for (size_t k = 0; k < v.size() && k < 2; k++) out.push_back(v[k].second);
    return out;
}

const char* Sim::craftWord(float c) { return c >= 1.12f ? "fine" : c >= .95f ? "sound" : c >= .85f ? "plain" : "rough"; }

} // namespace dw
