// Headless simulation tests. No framework needed: each check prints and
// counts failures; the process exit code is the number of failures.
#include <cmath>
#include <algorithm>
#include <cstdio>
#include <regex>
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
    p->traits = {"cheerful", "curious"};   // neither easygoing nor stubborn
    s.narrate(*p, "explore");
    CHECK(s.narrate(*p, "explore") == "pushed", "narrating someone twice in a day irritates them");
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
            // an empty queue means a narrated line or fever just interrupted them; they get a new job next update
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

// Narrating one person over and over turns them contrary for a while; how much
// they put up with first depends on who they are.
static void testContrary() {
    std::printf("contrariness\n");
    auto pushesUntilContrary = [](std::vector<std::string> traits) {
        Sim s;
        s.newWorld(20261017);
        runDays(s, 2);
        auto* p = s.members()[1];
        p->traits = traits;
        s.rapport = Sim::RAPPORT_MAX;
        for (int n = 1; n <= 10; n++) if (s.narrate(*p, "wood") == "contrary") return n;
        return 99;
    };
    int easy = pushesUntilContrary({"easygoing", "cheerful"}), plain = pushesUntilContrary({"cheerful", "curious"}), stubborn = pushesUntilContrary({"obstinate", "curious"});
    CHECK(plain == 3, cat("pushing someone three times in a day makes them contrary (took ", plain, ")").c_str());
    CHECK(easy > plain && plain > stubborn, cat("easygoing settlers put up with more than stubborn ones (", easy, " pushes vs ", plain, " vs ", stubborn, ")").c_str());

    Sim s;
    s.newWorld(20261017);
    runDays(s, 2);
    auto* p = s.members()[1];
    p->traits = {"cheerful", "curious"};
    s.rapport = Sim::RAPPORT_MAX;
    for (int i = 0; i < 3; i++) s.narrate(*p, "wood");
    CHECK(s.contrary(*p) && p->told && p->told->contrary && p->told->job == "rest", "a contrary settler asked to fetch wood takes the day off instead");
    CHECK(s.narrate(*p, "rest") == "contrary" && p->told->job == "wood", "and asked to rest, goes and works");
    std::string data = s.save();
    Sim t;
    CHECK(t.load(data) && t.contrary(*t.person(p->id)) && t.person(p->id)->pressure == p->pressure, "contrariness and pressure survive a save");
    runDays(s, 4);   // four pushes, fading by one a day
    bool noted = false;
    for (auto& e : s.journal) if (e.text.find(p->name + " has stopped bristling") != std::string::npos) noted = true;
    CHECK(!s.contrary(*p) && p->pressure < .01f && noted, cat("after a few days it fades, and the journal says so (pressure ", p->pressure, noted ? "" : ", no journal line", ")").c_str());
    CHECK(s.narrate(*p, "wood") != "contrary", "then they hear a line again");
}

// The narrator is part of life, never divine: nothing the game writes calls
// it a voice, a god or something to pray to.
static void testNoGodWords() {
    std::printf("the narrator is not a god\n");
    static const std::regex god("\\b(voices?|faith|pray(s|ed|er|ers|ing)?|shrines?|worship\\w*|divine|gods?|holy|sacred|altars?|mystics?|meditat\\w*|visions?|omens?|belie(f|fs|ve|ves|ved))\\b", std::regex::icase);
    std::string bad;
    auto scan = [&](const std::string& where, const std::string& text) { if (std::regex_search(text, god) && bad.size() < 600) bad += "\n          " + where + ": " + text; };
    for (auto& d : allProjects()) { scan("project", d.name); scan("project", d.desc); }
    for (auto& [k, g] : giftInfo()) { scan("gift", g.n); scan("gift", g.v); }
    for (auto& [k, c] : callingInfo()) scan("calling", c.n);
    for (auto& [k, t] : traitNames()) scan("trait", t);
    for (uint32_t seed : {20261017u, 777u}) {
        Sim s;
        s.newWorld(seed);
        int lastDay = 0;
        while (s.day() < 80) {
            s.step(60);
            if (!s.pendingFate.empty()) {
                for (auto& f : s.pendingFate) { scan("fate", f.title); scan("fate", f.desc); }
                auto f = s.pendingFate[s.day() % s.pendingFate.size()];
                s.pendingFate.clear();
                s.applyFate(f);
            }
            // a busy narrator: a line or three every day, words in the sand, proposals
            if (s.day() != lastDay && s.hour() > 9) {
                lastDay = s.day();
                auto m = s.members();
                for (int i = 0; i < 3 && !m.empty(); i++) {
                    auto* p = m[(s.day() * 7 + i) % m.size()];
                    if (!p->child) s.narrate(*p, i == 2 ? "rest" : "wood", m[0] != p ? m[0] : nullptr);
                }
                s.writeSand(s.day() % 2 ? "Winter is coming, gather food" : "something odd");
                auto props = s.proposalList();
                if (!props.empty() && s.day() % 5 == 0) s.propose(props[0]->key);
            }
        }
        for (auto& e : s.journal) scan("journal", e.text);
        for (auto& p : s.people) scan("bubble", p.bubble ? p.bubble->text : std::string());
    }
    CHECK(bad.empty(), ("no voices, faith, prayers or shrines in anything the game writes" + bad).c_str());
}

// The lines offered always fit the settler, and looking at them changes nothing.
static void testLines() {
    std::printf("narrated lines\n");
    std::string bad;
    int offered = 0, few = 0;
    bool still = true;
    for (uint32_t seed : {20261017u, 777u}) {
        Sim s;
        s.newWorld(seed);
        for (int k = 0; k < 160; k++) {
            runDays(s, 0);
            s.step(6 * 60);
            if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); }
            std::string before = k % 40 == 0 ? s.save() : "";
            for (auto* p : s.members()) {
                if (p->child) continue;
                auto ls = s.linesFor(*p);
                if (ls.size() < 2 || ls.size() > 3) few++;
                for (auto& l : ls) {
                    offered++;
                    Person* t = l.target >= 0 ? s.person(l.target) : nullptr;
                    bool hermit = false;
                    for (auto& h : s.pois) hermit |= h.type == "hermit" && h.found && !h.joined;
                    bool ok = l.text.find(p->name) != std::string::npos
                           && (l.job != "build" || s.activeProject())
                           && (l.job != "calling" || !p->calling.empty())
                           && (l.job != "hermit" || hermit)
                           && ((l.job != "peace" && l.job != "talk") || (t && t != p && !t->dead && !t->child && t->status == "member"));
                    if (!ok && bad.size() < 300) bad += "\n          " + l.job + ": " + l.text;
                }
            }
            if (!before.empty() && s.save() != before) still = false;
        }
    }
    CHECK(few == 0, cat("every adult is offered two or three lines (", offered, " offered)").c_str());
    CHECK(bad.empty(), ("every line fits: a project to help with, a calling to follow, someone to talk to" + bad).c_str());
    CHECK(still, "reading the lines never changes the game");
    // replies follow buy-in against the price, and the narrator pays from its rapport: they go
    // along and it is paid; they barely buy in and haggle (paid, later); they hardly do and refuse (free)
    Sim s;
    s.newWorld(20261017);
    runDays(s, 2);
    std::string got;
    bool paid = true, later = true;
    auto adults = s.members();
    for (int k = 0; k < 3; k++) {
        Person& p = *adults[k];
        p.traits = {"cheerful", "curious"};
        Line l;
        for (auto& o : s.linesFor(p)) if (o.job != "peace" && o.job != "talk") l = o;
        p.buyIn = (k == 0 ? 5.f : k == 1 ? 3.f : 2.f) * l.cost;
        s.rapport = 50;
        got += s.narrate(p, l) + " ";
        if (k < 2 && std::abs(s.rapport - (50 - l.cost)) > .01f) paid = false;
        if (k == 2 && s.rapport != 50) paid = false;
        if (k == 1 && !(p.told && p.told->from > s.tMin)) later = false;
    }
    CHECK(got == "agreed haggled refused ", ("those who buy in go along, those who barely do haggle, those who hardly do refuse (" + got + ")").c_str());
    CHECK(paid, "going along costs the narrator exactly the price shown; a refusal costs nothing");
    Person& poor = *adults[3];
    s.rapport = 1;
    float b0 = poor.buyIn;
    CHECK(s.narrate(poor, s.linesFor(poor)[0]) == "unaffordable" && s.rapport == 1 && poor.buyIn == b0 && poor.pressure < 1, "without the rapport to pay, the narrator cannot ask");
    CHECK(later, "a haggled line starts a few hours later");
}

static int askAndAnswer(Sim& s, Person& p, const char* topic, int reply, int about = -1) {
    s.ask(p, topic, about);
    int id = s.questions.back().id;
    s.answer(id, reply);
    return id;
}

static void testQuestions() {
    std::printf("questions for the narrator\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 3);
    s.questions.clear();
    auto m = s.members();
    Person& a = *m[0];
    a.traits = {"cheerful", "curious"};
    float r0 = a.buyIn;
    askAndAnswer(s, a, "skill", 0);
    CHECK(a.buyIn > r0, "an honest answer builds rapport");
    a.needs.social = 60;
    r0 = a.buyIn;
    askAndAnswer(s, a, "home", 1);
    CHECK(a.needs.social < 60 && a.buyIn >= r0, "teasing makes them feel part of things");
    a.traits = {"gloomy", "curious"};
    r0 = a.buyIn;
    askAndAnswer(s, a, "home", 1);
    CHECK(a.buyIn < r0, "teasing costs rapport with the sensitive");
    // a misleading answer feels good now and costs more when it comes out
    float gained = 0, lost = 0;
    int found = 0;
    for (int k = 0; k < 12 && found < 3; k++) {
        Person& p = *m[k % m.size()];
        if (p.child) continue;
        p.buyIn = 50;
        askAndAnswer(s, p, "winter", 2);
        gained += p.buyIn - 50;
        float before = p.buyIn;
        int v0 = s.journalVersion;
        runDays(s, 4);
        bool out = false;
        // the journal is newest first
        for (size_t i = 0; i < std::min(s.journal.size(), size_t(s.journalVersion - v0)); i++) out |= s.journal[i].text.find(p.name) != std::string::npos && s.journal[i].text.find("the truth about winter") != std::string::npos;
        if (out) { found++; lost += before - p.buyIn; }
    }
    CHECK(found > 0 && lost / found > gained / 12 * 2, cat("a misleading answer that comes out costs more than it gained (", found, " came out)").c_str());
    // questions lapse after a day, and survive a save while they wait
    s.questions.clear();
    s.ask(*m[1], "friend", m[2]->id);
    int id = s.questions.back().id;
    std::string data = s.save();
    Sim t;
    CHECK(t.load(data) && t.questions.size() == 1 && t.questions[0].id == id && t.questions[0].replies.size() == 3, "a waiting question survives a save");
    runDays(t, 2);
    CHECK(std::none_of(t.questions.begin(), t.questions.end(), [&](const Question& q) { return q.id == id; }), "an unanswered question lapses after a day");
    // left alone, settlers do ask now and then
    Sim u;
    u.newWorld(777);
    int asked = 0, last = 0;
    for (int d = 0; d < 20; d++) { runDays(u, 1); for (auto& q : u.questions) if (q.id > last) { asked++; last = q.id; } }
    CHECK(asked >= 3 && asked <= 20, cat("settlers ask the narrator things now and then (", asked, " in 20 days)").c_str());
}

// Rapport is what the narrator spends: lines that fit are cheap, plans that work
// out earn it back.
static void testRapportCurrency() {
    std::printf("rapport as currency\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 2);
    Person& p = *s.members()[0];
    p.traits = {"cheerful", "curious"};
    p.needs.fatigue = 90;
    p.kind = "wood";
    int rest = -1, work = -1;
    for (auto& l : s.linesFor(p)) { if (l.job == "rest") rest = l.cost; else if (l.job == "food" || l.job == "explore" || l.job == "build") work = std::max(work, l.cost); }
    CHECK(rest > 0 && work > 0 && rest < work, cat("asking a tired settler to rest is cheaper than asking them to work (", rest, " vs ", work, ")").c_str());
    Person q = p;
    q.traits = {"obstinate", "curious"};
    Person e = p;
    e.traits = {"easygoing", "curious"};
    CHECK(Sim::price(e, "wood", 1) < Sim::price(p, "wood", 1) && Sim::price(p, "wood", 1) < Sim::price(q, "wood", 1), "the same request costs more with stubborn settlers than easygoing ones");

    // a season with nobody lost earns rapport; the same season with a death does not
    float credit[2], pool[2];
    for (int pass = 0; pass < 2; pass++) {
        Sim a;
        a.newWorld(777);
        while (!(a.day() == 10 && a.hour() >= 23)) { a.step(60); if (!a.pendingFate.empty()) { auto f = a.pendingFate[0]; a.pendingFate.clear(); a.applyFate(f); } }
        a.lostThisSeason = pass;
        a.rapport = 40;
        float before = a.avgBuyIn();
        a.step(120);
        credit[pass] = a.avgBuyIn() - before;
        pool[pass] = a.rapport;
    }
    CHECK(credit[0] > credit[1] + 4 && pool[0] >= pool[1] + 9.9f, cat("a season with nobody lost earns the narrator rapport (", int(pool[0] - pool[1]), " more) and raises buy-in").c_str());
    // the daily income follows the average buy-in
    Sim inc;
    inc.newWorld(4242);
    runDays(inc, 1);
    while (inc.hour() < 23) inc.step(30);
    inc.rapport = 20;
    float expect = inc.rapportIncome();
    inc.step(120);
    CHECK(std::abs(inc.rapport - 20 - expect) < .05f && expect > 1, cat("rapport comes in every day from how much they buy in (", expect, " a day)").c_str());

    // the narrator's own choice of spot: a good one earns rapport when it is finished, a bad one costs it
    Sim base;
    base.newWorld(20261017);
    base.askPlacement = true;
    int guard = 0;
    while (!base.pendingPlacement && guard++ < 24 * 120) { base.step(60); if (!base.pendingFate.empty()) { auto f = base.pendingFate[0]; base.pendingFate.clear(); base.applyFate(f); } }
    if (!base.pendingPlacement) { CHECK(false, "a building comes up for the narrator to place"); return; }
    std::string start = base.save();
    float change[2] = {0, 0};
    for (int pass = 0; pass < 2; pass++) {
        Sim b;
        b.load(start);
        int id = b.pendingPlacement->project;
        auto all = sites(b, *b.projectById(id), 20);
        auto pickIt = pass == 0 ? std::max_element(all.begin(), all.end(), [](auto& x, auto& y) { return x.second.quality < y.second.quality; })
                                : std::min_element(all.begin(), all.end(), [](auto& x, auto& y) { return x.second.quality < y.second.quality; });
        b.placeAt(id, pickIt->first.first, pickIt->first.second);
        b.askPlacement = false;
        for (int h = 0; h < 24 * 40 && !b.projectById(id)->done; h++) {
            float before = b.avgBuyIn();
            b.step(60);
            if (!b.pendingFate.empty()) { auto f = b.pendingFate[0]; b.pendingFate.clear(); b.applyFate(f); }
            if (b.projectById(id)->done) change[pass] = b.avgBuyIn() - before;
        }
    }
    CHECK(change[0] > 0 && change[0] > change[1], cat("a well-chosen spot earns rapport when it is finished, a bad one less (", change[0], " vs ", change[1], ")").c_str());
}

// The Narrator's Ledger: each gap shows when it should and clears once fixed;
// candidates are people who could actually do it; the bigger nudges last.
static void testLedger() {
    std::printf("the ledger\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 4);
    while (s.hour() < 10 || s.hour() >= 11) s.step(30);
    s.pendingFate.clear();
    auto adults = s.members();
    auto has = [&](const char* id) { for (auto& g : s.ledger()) if (g.id == id) return true; return false; };
    auto idle = [&] { for (auto* p : adults) { p->kind = "idle"; p->sick = 0; p->needs.fatigue = 30; } };
    std::string bad;
    auto expect = [&](const char* id, bool want, const char* when) { if (has(id) != want && bad.size() < 300) bad += cat("\n          ", id, want ? " missing " : " still there ", when); };
    std::string before = s.save();
    idle();
    s.stock.wood = 0; expect("wood", true, "with no wood and nobody cutting");
    adults[0]->kind = "wood"; expect("wood", false, "once someone cuts wood");
    idle(); s.stock.food = 0; expect("food", true, "with no food");
    adults[0]->kind = "food"; adults[1]->kind = "farm"; expect("food", false, "with two gathering");
    idle(); s.stock.food = 200;
    if (auto* pr = s.currentProject()) {
        pr->progress = std::max(pr->progress, 1.f);
        expect("builder", true, "with a project and no builder");
        adults[2]->kind = "build"; expect("builder", false, "with a builder");
    }
    idle();
    for (auto* p : adults) p->gifts.erase("herbalist");
    adults[3]->sick = 20; expect("herbalist", true, "with someone sick and no herbalist");
    adults[0]->gifts["herbalist"] = 4; expect("herbalist", false, "with a herbalist");
    idle(); for (auto* p : adults) p->needs.fatigue = 90; expect("tired", true, "with everyone worn out");
    idle(); expect("tired", false, "once rested");
    expect("explore", true, "with the island unknown and nobody exploring");
    adults[1]->kind = "explore"; expect("explore", false, "with someone exploring");
    idle(); s.stock.wood = 0;
    double day = s.tMin;
    s.tMin = std::floor(s.tMin / 1440) * 1440 + 22 * 60;
    expect("wood", false, "at night, when everyone sleeps");
    s.tMin = day;
    if (auto* f = s.fire(); f && f->done) {
        s.stock.wood = 50;
        f->fuel = 0;
        s.tMin = 30 * 1440 + 10 * 60;   // a winter morning
        expect("fire", true, "with the fire out in winter");
        f->fuel = 5; expect("fire", false, "once the fire is fed");
        s.tMin = day;
    }
    CHECK(bad.empty(), ("every ledger gap shows when it should and clears once fixed" + bad).c_str());

    // candidates: never someone already at it, contrary or sick; those likely to agree rank above those who would refuse
    idle(); s.stock.wood = 0;
    adults[1]->contraryUntil = s.tMin + 600;
    adults[2]->sick = 10;
    adults[3]->buyIn = 1;   // would refuse anything
    bool sane = true, order = true;
    for (auto& g : s.ledger()) {
        bool seenRefuser = false;
        for (auto& c : g.candidates) {
            Person* p = s.person(c.pid);
            if (!p || p == adults[1] || p == adults[2] || p->kind == g.job) sane = false;
            bool refuses = s.likely(*p, Line{g.job, "", -1, c.cost}) == "refused";
            if (!refuses && seenRefuser) order = false;
            seenRefuser |= refuses;
        }
    }
    CHECK(sane, "candidates are never already at it, contrary or sick");
    CHECK(order, "those likely to agree are listed before those who would refuse");
    adults[1]->contraryUntil = 0; adults[2]->sick = 0; adults[3]->buyIn = 40;
    s.load(before);

    // an appointed role holds until revoked, and costs a little rapport each day
    Sim r;
    r.newWorld(777);
    runDays(r, 3);
    Person* p = nullptr;
    for (auto* q : r.members()) if (!q->child && (!p || q->buyIn > p->buyIn)) p = q;
    p->buyIn = 90;
    r.rapport = 100;
    int cost = r.roleCost(*p, "wood");
    CHECK(r.appoint(*p, "wood") == "agreed" && std::abs(r.rapport - (100 - cost)) < .01f && p->duty && p->duty->role, "a settler who buys in takes a role, and it costs its price");
    int chopping = 0;
    float before2 = r.rapport;
    for (int h = 0; h < 24 * 4; h++) { r.step(60); r.pendingFate.clear(); chopping += p->kind == "wood"; }
    CHECK(p->duty && chopping >= 8, cat("the woodcutter keeps cutting wood day after day (", chopping, " hours in 4 days)").c_str());
    CHECK(r.rapport < before2 + 4 * r.rapportIncome() - 3, "a role costs a little rapport every day");
    r.revoke(*p);
    CHECK(!p->duty, "a role ends when revoked");
    // a season's work lasts until the season turns
    r.rapport = 100;
    CHECK(r.askSeason(*p, "food") == "agreed" && p->duty && !p->duty->role, "a settler agrees to a season's work");
    int turn = ((r.day() - 1) / 10 + 1) * 10 + 1;
    while (r.day() < turn) { r.step(60); r.pendingFate.clear(); }
    r.step(120);
    CHECK(!p->duty, "a season's work ends when the season turns");
    // an appeal to everyone
    r.rapport = 100;
    int ac = r.appealCost("wood");
    auto res = r.appeal("wood");
    int told = 0;
    for (auto* q : r.members()) told += q->told && q->told->job == "wood" && q->told->until > r.tMin;
    CHECK(res.rfind("agreed", 0) == 0 && told >= 1 && std::abs(r.rapport - (100 - ac)) < .01f, cat("an appeal costs its price and moves those who buy in (", res, ")").c_str());
    r.rapport = 1;
    CHECK(r.appeal("wood") == "unaffordable" && r.rapport == 1, "an appeal the narrator cannot afford does nothing");
    // reading the ledger never changes the game
    std::string a1 = r.save();
    r.ledger();
    CHECK(r.save() == a1, "reading the ledger never changes the game");
}

int main() {
    for (uint32_t seed : {20261017u, 777u, 4242u}) testSurvival(seed);
    testNoSmithNoForge();
    testCharismaPushes();
    testNarrator();
    testContrary();
    testNoGodWords();
    testLines();
    testQuestions();
    testRapportCurrency();
    testLedger();
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
