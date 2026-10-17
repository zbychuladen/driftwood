// Renderer geometry tests: projection and picking against the real WorldView
// camera math. Nothing here opens a window or touches the GPU.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>
#include "engine/Draw.h"
#include "game/Art.h"
#include "game/Depth.h"
#include "game/Iso.h"
#include "game/Labels.h"
#include "game/Scale.h"
#include "game/WorldView.h"
#include "sim/Sim.h"

using namespace dw;

static int failures = 0;
#define CHECK(cond, msg) do { if (cond) std::printf("  ok    %s\n", msg); else { std::printf("  FAIL  %s\n", msg); failures++; } } while (0)

static void runDays(Sim& s, int days) {
    int target = s.day() + days;
    while (s.day() < target) {
        s.step(60);
        if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); }
    }
}

// A camera like the game's: a 1600x900 window with the sidebar taking the right side.
static void setupView(WorldView& v, float zoom) {
    v.cam.offset = {590, 479};
    v.cam.zoom = zoom;
}

static void testProjection() {
    std::printf("projection\n");
    bool round = true;
    for (int y = 0; y <= GH; y += 7) for (int x = 0; x <= GW; x += 5) {
        Vector2 w = iso::wp(float(x), float(y)), t = iso::toTile(w.x, w.y);
        if (std::abs(t.x - x) > 1e-3f || std::abs(t.y - y) > 1e-3f) round = false;
    }
    CHECK(round, "screen to tile undoes tile to screen");
    Vector2 a = iso::wp(0, 0), b = iso::wp(1, 0), c = iso::wp(0, 1);
    CHECK(b.x > a.x && b.y > a.y && c.x < a.x && c.y > a.y, "x runs right-down, y runs left-down");
}

static void testPickPeople(uint32_t seed) {
    std::printf("picking people, seed %u\n", seed);
    Sim s;
    s.newWorld(seed);
    runDays(s, 30);
    WorldView v;
    setupView(v, 1.5f);
    int tried = 0, hit = 0;
    for (auto& p : s.people) {
        if (p.dead || p.hidden || !s.isExplored(p.x, p.y)) continue;
        tried++;
        v.centerOnTile(p.x, p.y);
        Vector2 body = iso::wp(p.x, p.y);
        body.y -= WorldView::bodyHeight(p);
        int id = v.pick(s, GetWorldToScreen2D(body, v.cam));
        // two people standing on the same spot are both fair answers
        auto* q = s.person(id);
        if (q && std::hypot(q->x - p.x, q->y - p.y) < .3f) hit++;
    }
    char msg[96];
    std::snprintf(msg, sizeof msg, "clicking a settler's body selects them (%d of %d)", hit, tried);
    CHECK(tried > 0 && hit == tried, msg);

    // deep water in the corner of the map holds nobody
    v.centerOnTile(.5f, .5f);
    CHECK(v.pick(s, v.cam.offset) == -1, "clicking open sea selects nothing");
}

// ---------------------------------------------------------------- scale
// Draws a piece of art into the bounds recorder and returns how far its top
// rises above a ground point, in metres.
static float heightM(Vector2 ground, const std::function<void()>& drawIt) {
    draw::Box box;
    draw::record(&box);
    drawIt();
    draw::record(nullptr);
    return box.empty() ? 0 : (ground.y - box.y0) / scale::PX_PER_M;
}

struct Measured { std::string what; float got, lo, hi; };

// One check per rule; the message lists every offender.
static void checkAll(const std::vector<Measured>& rows, const char* rule) {
    std::string bad;
    for (auto& r : rows)
        if (r.got < r.lo || r.got > r.hi) {
            char b[160];
            std::snprintf(b, sizeof b, "\n          %s: %.2f, wanted %.2f to %.2f", r.what.c_str(), r.got, r.lo, r.hi);
            bad += b;
        }
    CHECK(bad.empty(), (std::string(rule) + bad).c_str());
}

static void testScale() {
    std::printf("scale\n");
    Sim s;
    s.newWorld(20261017);
    art::Ctx ctx{&s, 0, 1, 12};   // summer noon
    ctx.effects = false;
    const float tol = .08f;
    auto near = [&](const std::string& what, float got, float want) { return Measured{what, got, want * (1 - tol), want * (1 + tol)}; };

    // people, drawn around their feet
    Person adult, child;
    adult.x = adult.y = child.x = child.y = 20;
    child.child = true;
    float hAdult = heightM(iso::wp(20, 20), [&] { art::person(ctx, adult, false); });
    float hChild = heightM(iso::wp(20, 20), [&] { art::person(ctx, child, false); });
    checkAll({near("adult", hAdult, scale::PERSON), near("child", hChild, scale::CHILD)}, "people are drawn at their real height");

    // trees, bushes and rocks at a size of 1, measured from where they stand
    auto obj = [](const char* type, bool palm, bool conifer) { Obj o; o.type = type; o.x = o.y = 20; o.palm = palm; o.conifer = conifer; return o; };
    Vector2 objGround = iso::wp(20.5f, 20.6f);
    struct T { const char* name; Obj o; float want; int season; };
    std::vector<T> plants = {
        {"broadleaf", obj("tree", false, false), scale::BROADLEAF, 1}, {"bare broadleaf in winter", obj("tree", false, false), scale::BROADLEAF, 3},
        {"conifer", obj("tree", false, true), scale::CONIFER, 1}, {"palm", obj("tree", true, false), scale::PALM, 1},
        {"bush", obj("bush", false, false), scale::BUSH, 1}, {"rock", obj("rock", false, false), scale::ROCK, 1}};
    std::vector<Measured> rows, ratios;
    for (auto& p : plants) {
        art::Ctx c = ctx;
        c.season = p.season;
        float h = heightM(objGround, [&] {
            if (p.o.type == "tree") art::tree(c, p.o);
            else if (p.o.type == "bush") art::bush(c, p.o);
            else art::rock(c, p.o);
        });
        rows.push_back(near(p.name, h, p.want));
        if (p.o.type == "tree") ratios.push_back({std::string(p.name) + " / person", h / hAdult, 3, 5});
        else ratios.push_back({std::string(p.name) + " / person", h / hAdult, 0, .9f});
    }
    checkAll(rows, "plants and stones are drawn at their real height");
    checkAll(ratios, "trees stand 3 to 5 people tall; bushes and rocks stay below head height");

    // buildings, measured above the back end of the roof ridge (the middle of a
    // square footprint), which is where the drawn top sits at its real height
    struct B { const char* key; float want; };
    std::vector<B> builds = {
        {"hut", scale::HUT_WALL + scale::HUT_ROOF}, {"store", scale::STORE_WALL + scale::STORE_ROOF},
        {"smoke", scale::SMOKE_WALL + scale::SMOKE_ROOF}, {"longhouse", scale::LONGHOUSE_WALL + scale::LONGHOUSE_ROOF},
        {"school", scale::SCHOOL_WALL + scale::SCHOOL_ROOF}, {"loomhouse", scale::LOOM_WALL + scale::LOOM_ROOF},
        {"infirmary", scale::INFIRMARY_WALL + scale::INFIRMARY_ROOF}, {"forge", scale::FORGE_CHIMNEY},
        {"well", scale::WELL_ROOF}, {"rack", scale::RACK}, {"tower", scale::TOWER}, {"lighthouse", scale::LIGHTHOUSE},
        {"signal", scale::SIGNAL}, {"mine", scale::MINE_HILL}};
    rows.clear();
    std::vector<Measured> shelters;
    for (auto& bd : builds) {
        const ProjectDef* def = projectDef(bd.key);
        if (!def) { rows.push_back({std::string(bd.key) + " (no such project)", 0, 1, 1}); continue; }
        Project b;
        static_cast<ProjectDef&>(b) = *def;
        b.x = b.y = 20;
        b.placed = b.done = true;
        float m = std::min(b.w, b.h) / 2.f;
        float h = heightM(iso::wp(b.x + m, b.y + m), [&] { art::building(ctx, b); });
        rows.push_back(near(bd.key, h, bd.want));
        if (def->beds > 0 || std::string(bd.key) == "store" || std::string(bd.key) == "school") shelters.push_back({std::string(bd.key) + " / person", h / hAdult, 2.2f, 4});
    }
    checkAll(rows, "buildings are drawn at their real height");
    checkAll(shelters, "a building people live or work in is 2.2 to 4 people tall");

    // every wall with a door in it lets a grown person through without stooping
    std::vector<Measured> doors;
    struct W { const char* name; float wall; };
    for (W w : {W{"hut", scale::HUT_WALL}, W{"store", scale::STORE_WALL}, W{"smokehouse", scale::SMOKE_WALL}, W{"longhouse", scale::LONGHOUSE_WALL},
                W{"school", scale::SCHOOL_WALL}, W{"forge", scale::FORGE_WALL}, W{"loom house", scale::LOOM_WALL},
                W{"infirmary", scale::INFIRMARY_WALL}, W{"hermit's hut", scale::HERMIT_WALL}})
        doors.push_back({std::string(w.name) + " door / person", art::doorHeight(scale::px(w.wall)) / scale::px(scale::PERSON), 1.05f, 1.3f});
    checkAll(doors, "doors are a little taller than a person");
}

// ---------------------------------------------------------------- depth
static int posIn(const std::vector<int>& order, int i) { return int(std::find(order.begin(), order.end(), i) - order.begin()); }

// A footprint plus a generous screen box, so every pair here overlaps on screen.
static depth::Item fp(float x0, float y0, float x1, float y1) { return {x0, y0, x1, y1, 0, 0, 100, 100}; }

static void testDepthRules() {
    std::printf("depth order\n");
    // a 2x2 hut on tiles (10,10)-(12,12)
    depth::Item hut = fp(10.05f, 10.05f, 11.95f, 11.95f);
    // someone at (12.2, 10.5), beside its right-hand wall: the old single depth
    // number put them behind the hut and the wall drew over them
    depth::Item beside = fp(12.08f, 10.38f, 12.32f, 10.62f);
    auto o = depth::order({hut, beside});
    CHECK(posIn(o, 0) < posIn(o, 1), "a settler beside a building's front wall is drawn over it");
    // a tree on the tile behind the hut's back corner
    depth::Item tree = fp(10.25f, 9.35f, 10.75f, 9.85f);
    o = depth::order({tree, hut});
    CHECK(posIn(o, 0) < posIn(o, 1), "a tree behind a building is drawn under it");
    // the long side of a 4x2 longhouse against someone in front of its middle
    depth::Item hall = fp(20.05f, 20.05f, 23.95f, 21.95f), front = fp(21.9f, 22.1f, 22.1f, 22.3f);
    o = depth::order({front, hall});
    CHECK(posIn(o, 1) < posIn(o, 0), "a settler in front of a longhouse's long wall is drawn over it");
    // just behind the far wall near one end: further forward than the hall's
    // centre, so any one-number depth draws them on top of the roof
    depth::Item behind = fp(23.38f, 19.48f, 23.62f, 19.72f);
    o = depth::order({hall, behind});
    CHECK(posIn(o, 1) < posIn(o, 0), "a settler behind a longhouse's far wall is drawn under it");
    // three items whose pairwise rules form a loop: all are still drawn
    depth::Item a = fp(0, 0, 1, 3), b = fp(1, 0, 3, 1), c = fp(2, 1, 3, 3);
    o = depth::order({a, b, c});
    std::vector<int> sortedO = o;
    std::sort(sortedO.begin(), sortedO.end());
    CHECK(sortedO == std::vector<int>({0, 1, 2}), "a loop in the rules still draws every item once");
}

// Builds the real draw list for a grown village and checks every pair that
// overlaps on screen is drawn in the order its footprints say.
static void testDepthVillage(uint32_t seed, int days) {
    std::printf("depth order in a real village, seed %u, day %d\n", seed, days + 1);
    Sim s;
    s.newWorld(seed);
    runDays(s, days);
    WorldView v;
    art::Ctx ctx{&s, s.tMin, s.season(), 12};
    Rectangle all{-200, -200, iso::WORLD_W + 400, iso::WORLD_H + 400};
    auto t0 = std::chrono::steady_clock::now();
    auto items = WorldView::sorted(v.collect(s, ctx, all, -1));
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    int pairs = 0, wrong = 0;
    std::string example;
    for (size_t i = 0; i < items.size(); i++)
        for (size_t j = i + 1; j < items.size(); j++) {
            const auto &a = items[i].at, &b = items[j].at;   // a is drawn before b
            if (a.sx1 <= b.sx0 || b.sx1 <= a.sx0 || a.sy1 <= b.sy0 || b.sy1 <= a.sy0) continue;
            pairs++;
            if (depth::compare(a, b) > 0) {
                if (wrong++ == 0) {
                    char m[160];
                    std::snprintf(m, sizeof m, " (first: kind %d #%d drawn before kind %d #%d)", items[i].kind, items[i].id, items[j].kind, items[j].id);
                    example = m;
                }
            }
        }
    int people = 0;
    for (auto& p : s.people) if (!p.dead && !p.hidden && s.isExplored(p.x, p.y)) people++;
    int peopleDrawn = 0;
    for (auto& d : items) if (d.kind == Drawable::PERSON) peopleDrawn++;
    char msg[200];
    std::snprintf(msg, sizeof msg, "%d overlapping pairs, none drawn in front of something that stands before it%s", pairs, example.c_str());
    CHECK(wrong == 0 && pairs > 0, msg);
    CHECK(peopleDrawn == people, "every visible settler is in the draw list");
    std::printf("  info  %zu drawables measured and sorted in %.1f ms (whole island)\n", items.size(), ms);
}

// ---------------------------------------------------------------- labels
static void testLabels() {
    std::printf("labels\n");
    // six settlers huddled round a fire ask for nearly the same spot
    std::vector<labels::Want> w;
    for (int i = 0; i < 6; i++) w.push_back({{100.f + i * 9, 200.f + (i % 2) * 4, 60, 16}, i == 4});
    auto out = labels::layout(w);
    bool clear = true;
    for (size_t i = 0; i < out.size(); i++)
        for (size_t j = i + 1; j < out.size(); j++)
            if (out[i].h > 0 && out[j].h > 0 && labels::overlap(out[i], out[j])) clear = false;
    CHECK(clear, "no two shown labels overlap");
    CHECK(out[4].h > 0 && out[4].x == w[4].at.x && out[4].y == w[4].at.y, "the selected settler's label stays on its spot");
    int shown = 0;
    for (auto& b : out) shown += b.h > 0;
    CHECK(shown >= 5, "a crowd mostly keeps its names, moved up or down");
    // labels that never touch are left alone
    out = labels::layout({{{0, 0, 50, 16}}, {{200, 0, 50, 16}}});
    CHECK(out[0].y == 0 && out[1].y == 0, "labels with room stay where they are");
}

int main() {
    testProjection();
    testPickPeople(20261017);
    testPickPeople(7);
    testScale();
    testDepthRules();
    testDepthVillage(20261017, 60);
    testDepthVillage(7, 100);
    testLabels();
    std::printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
    return failures;
}
