// People: creation, gifts, family, beds, and Maslow's hierarchy of needs.
#include <algorithm>
#include "Sim.h"

namespace dw {

namespace {
const std::vector<uint32_t> SKINS = {0xf1c9a5, 0xd9a27b, 0xb47a54, 0x8a5a3b, 0x5e3b26, 0xe8b892};
const std::vector<uint32_t> HAIRS = {0x2b1d14, 0x5a3a1e, 0xa0622d, 0xd8b25c, 0x151515, 0x8a8a8a, 0x8c3b1f};
}

std::map<std::string, int> Sim::rollGifts(const std::vector<std::string>& traits) {
    std::map<std::string, int> g;
    std::vector<std::string> keys;
    for (auto& [k, v] : giftInfo()) keys.push_back(k);
    float r = rnd();
    int n = r < .18f ? 0 : r < .82f ? 1 : 2;
    static const std::map<std::string, std::string> bias = {{"greenthumb", "farmer"}, {"strong", "smith"}, {"angler", "sailor"}, {"hardworking", "carpenter"}, {"curious", "potter"}, {"easygoing", "herbalist"}};
    for (int i = 0; i < n; i++) {
        std::string k = pick(keys);
        if (i == 0 && rnd() < .5f) for (auto& t : traits) if (bias.count(t)) { k = bias.at(t); break; }
        if (g.count(k)) continue;
        g[k] = 3 + ri(7);
    }
    return g;
}

Person& Sim::makePerson(const std::string& name, uint32_t tunic, float x, float y, bool child) {
    Person p;
    p.id = nextPid++;
    p.name = name; p.tunic = tunic; p.skin = pick(SKINS); p.hair = pick(HAIRS); p.hairStyle = ri(3);
    p.x = x; p.y = y;
    std::vector<std::string> keys;
    for (auto& [k, v] : traitNames()) keys.push_back(k);
    std::shuffle(keys.begin(), keys.end(), gen);
    for (auto& k : keys) {
        if (p.traits.size() >= 2) break;
        auto has = [&](const char* t) { return std::find(p.traits.begin(), p.traits.end(), t) != p.traits.end(); };
        if ((k == "loner" && has("social")) || (k == "social" && has("loner")) || (k == "easygoing" && has("obstinate")) || (k == "obstinate" && has("easygoing"))) continue;
        p.traits.push_back(k);
    }
    for (auto& [k, v] : skillNames()) {
        float s = 1 + ri(3);
        if ((k == "fish" && p.has("angler")) || (k == "farm" && p.has("greenthumb")) || ((k == "chop" || k == "mine") && p.has("strong"))) s += 3;
        p.sk[k] = s;
    }
    p.needs = {15 + rnd() * 15, 30 + rnd() * 10, 30, 0, 20};
    p.rapport = p.has("easygoing") ? 62 : p.has("obstinate") ? 14 : 34 + rnd() * 14;
    p.child = child;
    p.age = child ? 0 : 18 + ri(30);
    p.bornDay = child ? day() : 0;
    p.gifts = child ? std::map<std::string, int>{} : rollGifts(p.traits);
    p.charisma = std::clamp(int(std::lround(2 + rnd() * 6 + (p.has("social") ? 2 : 0) + (p.has("cheerful") ? 1 : 0) - (p.has("loner") ? 2 : 0) - (p.has("gloomy") ? 1 : 0))), 1, 10);
    people.push_back(std::move(p));
    return people.back();
}

std::string Sim::giftText(const Person& p) const {
    std::string s;
    for (auto& [k, v] : p.gifts) s += (s.empty() ? "" : ", ") + lower(giftInfo().at(k).n) + " (" + std::to_string(v) + ")";
    return s;
}
const char* Sim::charWord(int c) { return c >= 8 ? "magnetic" : c >= 6 ? "persuasive" : c >= 4 ? "ordinary" : "quiet"; }

void Sim::announceGifts(Person& p) {
    if (p.gifts.empty()) return;
    std::string g;
    for (auto& [k, v] : p.gifts) g += (g.empty() ? "" : " and ") + lower(giftInfo().at(k).n);
    note(cat(p.name, " turns out to be a skilled ", g, "."));
    for (auto& b : plan) {
        if (b.done || b.skip || b.gift.empty() || !p.gifts.count(b.gift) || p.gifts[b.gift] < b.min) continue;
        bool other = false;
        for (auto* q : members()) if (q != &p && !q->child && q->gifts.count(b.gift) && q->gifts[b.gift] >= b.min) other = true;
        if (!other) { note(cat("With ", p.name, " here, a ", lower(b.name), " is finally possible."), "big"); break; }
    }
}

void Sim::birth(Person& a, Person& b, bool twin) {
    std::string nm = newName();
    auto& c = makePerson(nm, rnd() < .5f ? a.tunic : b.tunic, a.x, a.y, true);
    c.parents = {a.id, b.id};
    c.skin = rnd() < .5f ? a.skin : b.skin;
    c.hair = rnd() < .5f ? a.hair : b.hair;
    c.status = "member"; c.rapport = 50;
    for (auto* q : members()) { c.aff[q->id] = 50; q->aff[c.id] = 50; }
    assignBeds();
    note(twin ? cat("And then a second baby! We named them ", nm, ".") : cat(a.name, " and ", b.name, " welcomed a baby. We named them ", nm, ". The island feels less lonely."), "big");
}

void Sim::assignBeds() {
    std::vector<Project*> beds;
    for (auto k : {"longhouse", "hut", "leanto"}) for (auto* b : builtAll(k)) for (int i = 0; i < b->beds; i++) beds.push_back(b);
    auto m = members();
    std::stable_sort(m.begin(), m.end(), [](Person* a, Person* b) { return int(a->child) < int(b->child); });
    for (size_t i = 0; i < m.size(); i++) m[i]->bed = i < beds.size() ? beds[i]->id : -1;
}

int Sim::totalBeds(bool plannedToo) {
    int n = 0;
    for (auto& b : plan) if (b.beds && (b.done || (plannedToo && !b.skip))) n += b.beds;
    return n;
}

void Sim::bubble(Person& p, const std::string& text, double mins) { p.bubble = Bubble{text, tMin + mins}; }

void Sim::die(Person& p) {
    p.dead = true;
    clearQ(p);
    note(cat(p.name, " did not wake this morning. There was not enough food. We buried them on the hill above the sea."), "sad");
    for (auto* q : members()) {
        q->mood = std::max(0.f, q->mood - 25);
        q->rapport = std::max(0.f, q->rapport - 6);
        if (q->partner == p.id) q->partner = -1;
    }
    assignBeds();
}

// Maslow: Body, Safety, Belonging, Esteem, Purpose. Higher tiers only count
// (via "gates") once the ones below are reasonably met.
float Sim::computeTiers(Person& p) {
    auto& n = p.needs;
    auto m = members();
    float body = clampf(100 - (n.hunger * .28f + n.thirst * .22f + std::max(0.f, n.fatigue - 40) * .4f + n.cold * .3f), 0, 100);
    float bed = 15;
    if (auto* b = projectById(p.bed)) bed = b->key == "leanto" ? 55.f : 100.f;
    bool raid = lastRaid && day() - lastRaid < 6;
    float safety = clampf(bed * .35f + clampf(foodDays() / 10, 0, 1) * 30 + (p.sick > 0 ? 15 : 100) * .2f + (built("tower") ? 100 : raid ? 25 : 65) * .15f, 0, 100);
    int friends = 0;
    for (auto* q : m) if (q != &p && affOf(p, q->id) > 55) friends++;
    bool family = false;
    for (int pid : p.parents) for (auto* q : m) if (q->id == pid) family = true;
    for (auto* c : m) for (int pid : c->parents) if (pid == p.id) family = true;
    float fam = p.partner >= 0 ? 100.f : family ? 70.f : 25.f;
    float comm = built("longhouse") ? 100.f : fire() ? 60.f : 25.f;
    float belonging = clampf((100 - n.social) * .35f + std::min(100.f, friends * 34.f) * .3f + fam * .15f + comm * .2f, 0, 100);
    float resp = 30;
    { float s = 0; int c = 0; for (auto* q : m) if (q != &p) { s += affOf(*q, p.id); c++; } if (c) resp = s / c; }
    float mastery = 0;
    for (auto& [k, v] : p.sk) mastery = std::max(mastery, v * 10);
    float esteem = clampf(p.esteemPts * .45f + mastery * .3f + resp * .25f + (p.titles.empty() ? 0 : 8), 0, 100);
    float purpose = !p.calling.empty() ? clampf(100 - p.callingNeed, 0, 100) : clampf(8 + p.rapport * .12f, 0, 100);
    p.tiers = {body, safety, belonging, esteem, purpose};
    float g1 = clampf((body - 40) / 30, 0, 1), g2 = g1 * clampf((safety - 40) / 30, 0, 1), g3 = g2 * clampf((belonging - 40) / 30, 0, 1), g4 = g3 * clampf((esteem - 35) / 30, 0, 1);
    p.gates = {1, g1, g2, g3, g4};
    const float W[5] = {.36f, .24f, .2f, .12f, .08f};
    float s = 0, w = 0;
    for (int i = 0; i < 5; i++) { s += W[i] * p.gates[i] * p.tiers[i]; w += W[i] * p.gates[i]; }
    p.seeking = -1;
    for (int i = 0; i < 5; i++) if (p.tiers[i] < (i == 4 ? 60 : 55)) { p.seeking = i; break; }
    return s / w;
}

void Sim::awaken(Person& p) {
    std::map<std::string, float> w = {{"carver", 1}, {"musician", 1}, {"storyteller", 1}, {"explorer", 1}, {"inventor", 1}, {"healer", 1}, {"teacher", .5f}, {"philosopher", .8f}};
    if (p.has("curious")) { w["explorer"] += 2; w["inventor"] += 1.5f; }
    if (p.has("easygoing")) w["philosopher"] += 3;
    if (p.has("obstinate")) { w["philosopher"] = 0; w["inventor"] += 1; }
    if (p.has("social")) { w["storyteller"] += 2; w["musician"] += 1; }
    if (p.has("cheerful")) w["musician"] += 2;
    if (p.has("greenthumb")) w["healer"] += 2.5f;
    if (p.has("hardworking")) w["carver"] += 1.5f;
    if (p.has("loner")) { w["carver"] += 1.5f; w["explorer"] += 1; }
    if (p.has("strong")) w["carver"] += 1;
    if (p.has("gloomy")) { w["musician"] += 1; w["storyteller"] += .5f; }
    if (p.has("tough")) w["explorer"] += 1;
    for (auto* q : members()) if (q->child) { w["teacher"] += 1.5f; break; }
    for (auto* q : members()) if (!q->calling.empty()) w[q->calling] *= .35f;
    float tot = 0;
    for (auto& [k, v] : w) tot += v;
    float r = rnd() * tot;
    std::string pickK = "carver";
    for (auto& [k, v] : w) { r -= v; if (r <= 0) { pickK = k; break; } }
    p.calling = pickK; p.callingNeed = 70; p.esteemPts = std::min(100.f, p.esteemPts + 5);
    static const std::map<std::string, std::string> lines = {
        {"carver", " has begun carving faces into driftwood. It seems to give them a kind of peace nothing else does."},
        {"musician", " made a flute from a hollow reed and played it at dusk. Nobody spoke until the song ended."},
        {"storyteller", " has become our storyteller. When they talk, everyone leans closer to the fire."},
        {"explorer", " can't stop looking at the horizon. They have started mapping the island on a sheet of bark."},
        {"inventor", " keeps sketching contraptions in the sand. \"What if we could...\" is their favorite phrase now."},
        {"healer", " has started collecting plants and testing which ones ease pain. We have a healer."},
        {"teacher", " has started teaching others what they know. They light up when a lesson lands."},
        {"philosopher", " sits by the water for hours, turning the narrator's lines over. They say the story makes more sense from there."}};
    note(p.name + lines.at(pickK), "big");
}

std::vector<std::string> Sim::lacking(Person& p) {
    std::vector<std::string> out;
    auto& n = p.needs;
    auto join = [](std::vector<std::string> v, const std::string& fallback) {
        std::string s; for (auto& x : v) s += (s.empty() ? "" : ", ") + x; return s.empty() ? fallback : s; };
    if (p.tiers[BODY] < 60) {
        std::vector<std::string> v;
        if (n.hunger > 50) v.push_back("hungry");
        if (n.thirst > 50) v.push_back("thirsty");
        if (n.fatigue > 60) v.push_back("exhausted");
        if (n.cold > 40) v.push_back("cold");
        out.push_back("Body: " + join(v, "worn down"));
    }
    if (p.tiers[SAFETY] < 60) {
        std::vector<std::string> v;
        auto* b = projectById(p.bed);
        if (!b) v.push_back("no bed"); else if (b->key == "leanto") v.push_back("sleeps in a drafty lean-to");
        if (foodDays() < 4) v.push_back("worried about food");
        if (p.sick > 0) v.push_back("sick");
        if (lastRaid && day() - lastRaid < 6) v.push_back("shaken by wolves");
        out.push_back("Safety: " + join(v, "uneasy"));
    }
    if (p.tiers[BELONGING] < 60) out.push_back(std::string("Belonging: ") + (n.social > 55 ? "lonely, " : "") + (p.partner < 0 ? "no partner, " : "") + "few close friends");
    if (p.tiers[ESTEEM] < 55) out.push_back("Esteem: wants to be recognized for something");
    if (p.tiers[PURPOSE] < 60) out.push_back(!p.calling.empty() ? "Purpose: needs time for being a " + lower(callingInfo().at(p.calling).n) : "Purpose: has not found a calling yet");
    return out;
}

} // namespace dw
