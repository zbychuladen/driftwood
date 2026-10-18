// Renderer geometry tests: projection and picking against the real WorldView
// camera math. Nothing here opens a window or touches the GPU.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <cmath>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include "engine/Draw.h"
#include "engine/Ui.h"
#include "game/Art.h"
#include "game/Building.h"
#include "game/Depth.h"
#include "game/Figure.h"
#include "game/Inspect.h"
#include "game/Iso.h"
#include "game/Labels.h"
#include "game/Saves.h"
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
    // measured under the hair's back corner, as in the figure test below
    Vector2 under = iso::wp(20 - .16f / scale::TILE_M, 20 - .16f / scale::TILE_M);
    float hAdult = heightM(under, [&] { art::person(ctx, adult, false, 0, 1); });
    float hChild = heightM(under, [&] { art::person(ctx, child, false, 0, 1); });
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
        // assembled buildings are measured in 3D by their recipes (testBuildings);
        // their eaves run past the point this on-screen measure stands under
        if (bld::assembled(bd.key)) {
            auto d = bld::make(bd.key, 1, 20, 20, def->w, def->h, 0, false);
            if (def->beds > 0 || std::string(bd.key) == "store" || std::string(bd.key) == "school") shelters.push_back({std::string(bd.key) + " / person", d.top / scale::PERSON, 2.2f, 4});
            continue;
        }
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

// ---------------------------------------------------------------- clicking things
static const char* kindName(Drawable::Kind k) {
    const char* n[] = {"settler", "object", "building", "place", "stockpile", "boat", "wolf"};
    return n[k];
}

// A world point the art of d visibly covers, found by probing a grid over its box.
static bool pointOn(const Drawable& d, const art::Ctx& ctx, Vector2& out) {
    const auto& a = d.at;
    for (float fy = .3f; fy <= .9f; fy += .1f)
        for (float fx = .5f; fx > .05f; fx -= .1f)
            for (float side : {1.f, -1.f}) {
                Vector2 w{a.sx0 + (a.sx1 - a.sx0) * (.5f + (fx - .5f) * side), a.sy0 + (a.sy1 - a.sy0) * fy};
                draw::Box box;
                draw::Probe pr{w};
                draw::record(&box);
                draw::probe(&pr);
                d.draw(ctx);
                draw::probe(nullptr);
                draw::record(nullptr);
                if (pr.hit) { out = w; return true; }
            }
    return false;
}

static void testInspect(uint32_t seed, int days) {
    std::printf("clicking things, seed %u, day %d\n", seed, days + 1);
    Sim s;
    s.newWorld(seed);
    runDays(s, days);
    WorldView v;
    setupView(v, 1.5f);
    art::Ctx ctx{&s, s.tMin, s.season(), s.hour()};
    ctx.effects = false;
    Rectangle all{-200, -200, iso::WORLD_W + 400, iso::WORLD_H + 400};
    auto items = WorldView::sorted(v.collect(s, ctx, all, -1));

    // every drawable has a card with a title, a kind, and the facts its kind promises
    std::string bad;
    for (auto& d : items) {
        Info in = describe(s, {d.kind, d.id, true});
        auto has = [&](const char* row) { for (auto& r : in.rows) if (r.first == row) return true; return false; };
        bool ok = !in.title.empty() && !in.kind.empty() && (d.kind == Drawable::PERSON || !in.desc.empty());
        if (d.kind == Drawable::BUILDING) ok = ok && has("Status") && has("Size");
        if (d.kind == Drawable::STOCKPILE) ok = ok && has("Wood") && has("Food");
        if (d.kind == Drawable::OBJECT && in.kind == "Tree") ok = ok && has("Height");
        if (!ok && bad.size() < 300) bad += cat("\n          ", kindName(d.kind), " #", d.id, " '", in.title, "'");
    }
    CHECK(bad.empty(), ("every building, tree, rock and place has a full info card" + bad).c_str());

    // a click on a part of something that nothing else covers selects it, for
    // up to three of each kind
    auto covers = [&](const Drawable& d, Vector2 w) {
        const auto& a = d.at;
        if (w.x < a.sx0 || w.x > a.sx1 || w.y < a.sy0 || w.y > a.sy1) return false;
        draw::Box box;
        draw::Probe pr{w};
        draw::record(&box);
        draw::probe(&pr);
        d.draw(ctx);
        draw::probe(nullptr);
        draw::record(nullptr);
        return pr.hit;
    };
    std::map<std::string, int> tried, hit;
    for (size_t i = 0; i < items.size(); i++) {
        std::string k = items[i].kind == Drawable::OBJECT ? describe(s, {items[i].kind, items[i].id, true}).kind : kindName(items[i].kind);
        if (tried[k] >= 3) continue;
        Vector2 w;
        if (!pointOn(items[i], ctx, w)) continue;
        bool clear = true;
        for (size_t j = 0; j < items.size() && clear; j++) if (j != i && covers(items[j], w)) clear = false;
        if (!clear) continue;
        tried[k]++;
        v.cam.target = w;
        Pick p = v.pickAt(s, GetWorldToScreen2D(w, v.cam));
        if (p.is(items[i].kind, items[i].id)) hit[k]++;
    }
    std::string missed, covered;
    for (auto& [k, n] : tried) {
        covered += " " + k;
        if (hit[k] != n) missed += cat("\n          ", k, ": ", hit[k], " of ", n);
    }
    CHECK(missed.empty() && tried.count("Tree") && tried.count("building"), ("a click on a visible part selects it (" + covered.substr(1) + ")" + missed).c_str());

    // where two pieces of art cover the same spot, the one drawn in front wins
    int pairs = 0, frontWins = 0;
    for (size_t j = 0; j < items.size() && pairs < 20; j++) {
        Vector2 w;
        if (items[j].kind == Drawable::PERSON || !pointOn(items[j], ctx, w)) continue;
        for (size_t i = 0; i < j; i++) {
            const auto& a = items[i].at;
            if (w.x < a.sx0 || w.x > a.sx1 || w.y < a.sy0 || w.y > a.sy1) continue;
            draw::Box box;
            draw::Probe pr{w};
            draw::record(&box);
            draw::probe(&pr);
            items[i].draw(ctx);
            draw::probe(nullptr);
            draw::record(nullptr);
            if (!pr.hit) continue;
            // something behind is also under the point; nothing in front of j may cover it
            bool frontmost = true;
            for (size_t k = j + 1; k < items.size() && frontmost; k++) {
                const auto& b = items[k].at;
                if (w.x < b.sx0 || w.x > b.sx1 || w.y < b.sy0 || w.y > b.sy1) continue;
                draw::Box bb;
                draw::Probe pk{w};
                draw::record(&bb);
                draw::probe(&pk);
                items[k].draw(ctx);
                draw::probe(nullptr);
                draw::record(nullptr);
                if (pk.hit) frontmost = false;
            }
            if (!frontmost) break;
            pairs++;
            v.cam.target = w;
            if (v.pickAt(s, GetWorldToScreen2D(w, v.cam)).is(items[j].kind, items[j].id)) frontWins++;
            break;
        }
    }
    char msg[120];
    std::snprintf(msg, sizeof msg, "where art overlaps, the click goes to the front one (%d of %d)", frontWins, pairs);
    CHECK(pairs > 0 && frontWins == pairs, msg);

    // a tree's shadow on the grass is not the tree
    bool shadowChecked = false, shadowIgnored = true;
    for (auto& d : items) {
        if (d.kind != Drawable::OBJECT || s.objs[d.id].type != "tree" || s.objs[d.id].palm) continue;
        const Obj& o = s.objs[d.id];
        Vector2 base = iso::wp(o.x + .5f, o.y + .6f);
        // the shadow spreads along the ground beside the trunk, well below the crown
        float h = scale::px(o.conifer ? scale::CONIFER : scale::BROADLEAF) * o.size * (o.sapling ? .35f + o.grow * .6f : 1);
        if (o.conifer) continue;   // a fir's lowest tier reaches down to the ground
        Vector2 w{base.x + h * .22f, base.y + 1};
        v.cam.target = w;
        Pick p = v.pickAt(s, GetWorldToScreen2D(w, v.cam));
        shadowChecked = true;
        if (p.is(d.kind, d.id)) shadowIgnored = false;
        break;
    }
    CHECK(shadowChecked && shadowIgnored, "clicking a tree's shadow does not select the tree");
}

// ---------------------------------------------------------------- links
static void testLinks(uint32_t seed, int days) {
    std::printf("links between settlers and their work, seed %u\n", seed);
    Sim s;
    s.newWorld(seed);
    WorldView v;
    art::Ctx ctx{&s, s.tMin, s.season(), 12};
    Rectangle all{-200, -200, iso::WORLD_W + 400, iso::WORLD_H + 400};
    const std::set<std::string> jobs = {"wood", "stone", "food", "build", "farm", "fuel", "eat", "drink", "warm", "visit", "friend", "craft"};
    int checked = 0, linked = 0, drawn = 0, needDrawn = 0;
    std::string firstBad;
    for (int h = 0; h < days * 24; h += 7) {
        s.step(7 * 60);
        if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); }
        std::vector<Drawable> items;
        bool listed = false;
        for (auto* p : s.members()) {
            if (!jobs.count(p->kind) || p->q.empty() || p->hidden) continue;
            checked++;
            FocusTarget t = focusTarget(s, p->focus);
            if (t.ok || (p->focus.toStock && p->carry)) linked++;
            else if (firstBad.empty()) firstBad = cat(" (first: ", p->name, ", '", p->doing, "')");
            // what the line ends at is something the player can see and click
            if (t.ok && t.what.ok && t.what.kind != Drawable::PERSON && s.isExplored(t.x, t.y)) {
                if (!listed) { items = v.collect(s, ctx, all, -1); listed = true; }
                needDrawn++;
                for (auto& d : items) if (d.kind == t.what.kind && d.id == t.what.id) { drawn++; break; }
                if (t.what.kind == Drawable::BUILDING && art::isFlat(s.projectById(t.what.id)->key)) drawn++;   // gardens and docks lie on the ground
            }
        }
    }
    CHECK(checked > 50 && linked == checked, (cat("every settler at work has a line to draw (", linked, " of ", checked, ")") + firstBad).c_str());
    CHECK(needDrawn > 20 && drawn == needDrawn, cat("every line ends at something drawn on the map (", drawn, " of ", needDrawn, ")").c_str());
}

// ---------------------------------------------------------------- readable colors
static float luminance(Color c) {
    auto ch = [](unsigned char v) { float f = v / 255.f; return f <= .03928f ? f / 12.92f : std::pow((f + .055f) / 1.055f, 2.4f); };
    return .2126f * ch(c.r) + .7152f * ch(c.g) + .0722f * ch(c.b);
}
static float contrast(Color a, Color b) {
    float la = luminance(a), lb = luminance(b);
    return (std::max(la, lb) + .05f) / (std::min(la, lb) + .05f);
}
// a translucent fill as it ends up over the panel behind it
static Color over(Color fill, float a, Color back) {
    auto m = [&](unsigned char f, unsigned char b) { return (unsigned char)std::lround(f * a + b * (1 - a)); };
    return {m(fill.r, back.r), m(fill.g, back.g), m(fill.b, back.b), 255};
}

static void testContrast() {
    std::printf("readable colors\n");
    using ui::T;
    struct Pair { const char* what; Color fg, bg; float need; };
    std::vector<Pair> pairs = {
        {"body text on a panel", T.text, T.panel, 7}, {"body text on a card", T.text, T.panel2, 7},
        {"secondary text on a panel", T.muted, T.panel, 4.5f}, {"secondary text on a card", T.muted, T.panel2, 4.5f},
        {"accent text", T.accent, T.panel, 4.5f}, {"the narrator's text", T.narrator, T.panel, 4.5f},
        {"warning text", T.berry, T.panel, 4.5f}, {"ember text", T.ember, T.panel, 4.5f}, {"good-news text", T.ok, T.panel, 4.5f},
        {"a selected button's label", T.onAccent, T.accent, 4.5f}, {"a hovered button's label", T.text, T.hover, 7},
        {"typing in a text field", T.text, T.field, 7}, {"a text field's placeholder", T.muted, T.field, 4.5f},
        {"a settler's name tag", {38, 52, 40, 255}, {250, 249, 244, 255}, 7}, {"the selected settler's tag", {255, 255, 255, 255}, {79, 107, 69, 255}, 4.5f},
        {"a work caption", {60, 46, 24, 255}, {255, 246, 224, 255}, 7}};
    const char* needs[5] = {"body", "safety", "belonging", "esteem", "purpose"};
    for (int i = 0; i < 5; i++) {
        pairs.push_back({needs[i], ui::TIER_INK[i], T.panel, 4.5f});
        pairs.push_back({needs[i], T.dark, over(ui::TIER[i], .85f, T.panel2), 4.5f});   // labels on the need bars
    }
    std::string bad;
    for (auto& p : pairs) {
        float c = contrast(p.fg, p.bg);
        if (c < p.need) { char b[120]; std::snprintf(b, sizeof b, "\n          %s: %.1f:1, needs %.1f:1", p.what, c, p.need); bad += b; }
    }
    CHECK(bad.empty(), ("every text color reads against its background (WCAG AA)" + bad).c_str());
}

// ---------------------------------------------------------------- name tags
static void testTags() {
    std::printf("name tags\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 30);
    auto m = s.members();
    Person* a = m[0];
    Person* b = m[1];
    Pick none{};
    Pick overA{Drawable::PERSON, a->id, true};
    int shownIdle = 0;
    for (auto* p : m) shownIdle += WorldView::showTag(s, *p, none, -1);
    CHECK(shownIdle == 0, "with the pointer over nothing, no settler shows a name");
    CHECK(WorldView::showTag(s, *a, overA, -1) && !WorldView::showTag(s, *b, overA, -1), "hovering a settler shows their name and nobody else's");
    CHECK(WorldView::showTag(s, *b, none, b->id), "the selected settler keeps their name");
    // someone asleep inside their hut answers to the pointer over the hut
    Person* sleeper = nullptr;
    for (auto* p : m) if (s.projectById(p->bed)) { sleeper = p; break; }
    if (sleeper) {
        sleeper->hidden = true;
        Pick overHut{Drawable::BUILDING, sleeper->bed, true}, overOther{Drawable::BUILDING, sleeper->bed + 1000, true};
        CHECK(WorldView::showTag(s, *sleeper, overHut, -1) && !WorldView::showTag(s, *sleeper, overOther, -1), "hovering a hut shows who sleeps inside");
    } else CHECK(false, "a settler with a bed to test with");
}

// ---------------------------------------------------------------- live ground
// Zoomed in, only the visible tiles are painted; none that shows may be missed.
static void testVisibleTiles() {
    std::printf("live ground\n");
    WorldView v;
    Rectangle screen{0, 0, 1600, 900};
    v.cam.offset = {590, 479};
    int missed = 0, cams = 0;
    for (float z : {WorldView::LIVE_GROUND_ZOOM, 1.15f, 2.5f, WorldView::MAX_ZOOM})
        for (int k = 0; k < 12; k++) {
            v.cam.zoom = z;
            v.centerOnTile(10 + k * 9.5f, 120 - k * 9.f);
            auto r = v.visibleTiles(screen);
            cams++;
            for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
                // any tile with a corner on screen must be painted
                bool shows = false;
                for (Vector2 c : {iso::wp(x, y), iso::wp(x + 1, y), iso::wp(x + 1, y + 1), iso::wp(x, y + 1)}) {
                    Vector2 sc = GetWorldToScreen2D(c, v.cam);
                    if (CheckCollisionPointRec(sc, screen)) shows = true;
                }
                if (shows && (x < r.x0 || x > r.x1 || y < r.y0 || y > r.y1)) missed++;
            }
        }
    CHECK(missed == 0, cat("every tile on screen is painted, ", cams, " camera positions from the live-ground zoom to the closest").c_str());
}

// ---------------------------------------------------------------- box figures
static void testFigures() {
    std::printf("box figures\n");
    figure::Look adult;
    adult.jacket = adult.shirt = adult.legs = adult.skin = adult.hair = adult.shoes = WHITE;
    figure::Look child = adult;
    child.child = true;
    std::vector<Measured> rows;
    const char* names[4] = {"SE", "SW", "NE", "NW"};
    // measured above the ground under the back corner of the hair, the point the
    // top of the head appears above at its true height (about 0.16 m back and
    // to the side of the feet, along both tile axes)
    Vector2 g = iso::wp(20 - .16f / scale::TILE_M, 20 - .16f / scale::TILE_M);
    for (int f = 0; f < 4; f++)
        for (int style = 0; style < 3; style++) {
            adult.hairStyle = child.hairStyle = style;
            float ha = heightM(g, [&] { figure::draw(20, 20, figure::Facing(f), adult, figure::idle(0), 1); });
            float hc = heightM(g, [&] { figure::draw(20, 20, figure::Facing(f), child, figure::idle(0), 1); });
            rows.push_back({cat("adult facing ", names[f], ", hair ", style), ha, scale::PERSON * .94f, scale::PERSON * 1.04f});
            rows.push_back({cat("child facing ", names[f], ", hair ", style), hc, scale::CHILD * .94f, scale::CHILD * 1.04f});
        }
    checkAll(rows, "figures stand at their real height in every facing and hair style");
    // walking never lifts the figure off the ground or stretches it
    float lowest = 1e9f, highest = 0;
    for (int i = 0; i < 16; i++) {
        float h = heightM(g, [&] { figure::draw(20, 20, figure::SE, adult, figure::walk(i * 6.283f / 16), 1); });
        lowest = std::min(lowest, h); highest = std::max(highest, h);
    }
    CHECK(lowest > scale::PERSON * .9f && highest < scale::PERSON * 1.05f, cat("through a stride the figure stays 1.5 to 1.8 m tall (", int(lowest * 100), " to ", int(highest * 100), " cm)").c_str());
    // every activity the simulation uses has a pose of its own
    std::set<std::string> seen;
    for (uint32_t seed : {20261017u, 777u}) {
        Sim s;
        s.newWorld(seed);
        for (int h = 0; h < 60 * 24 * 4; h++) {
            s.step(15);
            if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); }
            for (auto& p : s.people) if (!p.dead) seen.insert(p.anim);
        }
    }
    std::string missing, list;
    for (auto& a : seen) { list += " '" + a + "'"; if (!figure::hasPose(a)) missing += " '" + a + "'"; }
    CHECK(missing.empty() && seen.size() > 8, ("every activity the sim uses has a pose:" + list + (missing.empty() ? "" : "; missing" + missing)).c_str());
    CHECK(figure::facingToward(1, .2f, figure::NE) == figure::SE && figure::facingToward(-.1f, 1, figure::NE) == figure::SW
          && figure::facingToward(0, -2, figure::SE) == figure::NE && figure::facingToward(-3, 1, figure::SE) == figure::NW
          && figure::facingToward(0, 0, figure::NW) == figure::NW, "a figure faces the way it moves, and keeps its facing when still");
}

// ---------------------------------------------------------------- assembled buildings
static void testBuildings() {
    std::printf("assembled buildings\n");
    struct T { const char* key; int w, h; float tall; };
    const T types[] = {{"hut", 2, 2, scale::HUT_WALL + scale::HUT_ROOF}, {"cottage", 2, 2, scale::COTTAGE_WALL + scale::COTTAGE_ROOF},
                       {"house", 2, 2, scale::HOUSE_WALL + scale::HOUSE_ROOF}, {"manor", 3, 3, scale::MANOR_WALL + scale::MANOR_ROOF}, {"leanto", 3, 1, scale::LEANTO}, {"longhouse", 4, 2, scale::LONGHOUSE_WALL + scale::LONGHOUSE_ROOF},
                       {"store", 2, 2, scale::STORE_WALL + scale::STORE_ROOF}, {"school", 2, 2, scale::SCHOOL_WALL + scale::SCHOOL_ROOF}, {"forge", 2, 2, scale::FORGE_CHIMNEY},
                       {"smoke", 2, 1, scale::SMOKE_WALL + scale::SMOKE_ROOF}, {"loomhouse", 2, 2, scale::LOOM_WALL + scale::LOOM_ROOF}, {"infirmary", 2, 2, scale::INFIRMARY_WALL + scale::INFIRMARY_ROOF},
                       {"cookhouse", 2, 2, scale::COOK_WALL + scale::COOK_ROOF}, {"workshop", 2, 2, scale::WORKSHOP_WALL + scale::WORKSHOP_ROOF},
                       {"well", 1, 1, scale::WELL_ROOF}, {"rack", 1, 1, scale::RACK}, {"tower", 1, 1, scale::TOWER}, {"lighthouse", 1, 1, scale::LIGHTHOUSE},
                       {"signal", 1, 1, scale::SIGNAL}, {"mine", 1, 1, scale::MINE_HILL}, {"dock", 1, 3, .6f}, {"circle", 2, 2, scale::CIRCLE_SEAT},
                       {"pottery", 2, 2, scale::KILN}, {"boatyard", 2, 2, scale::MAST}, {"monument", 1, 1, scale::MONUMENT}, {"ship", 3, 2, scale::SHIP_MAST}};
    // what each structure's recipe promises
    const std::map<std::string, std::vector<std::string>> promised = {
        {"well", {"curb", "water", "windlass", "bucket", "roof"}}, {"rack", {"posts", "crossbar", "fish"}}, {"tower", {"legs", "platform", "railing", "roof", "ladder"}},
        {"lighthouse", {"tower", "gallery", "lantern", "roof", "door"}}, {"signal", {"ring", "crib"}}, {"mine", {"mound", "entrance", "cart"}},
        {"dock", {"deck", "posts", "bollard"}}, {"circle", {"seats", "firepit"}}, {"pottery", {"kiln", "mouth", "flue", "pots"}}, {"boatyard", {"slip", "trestles", "hull", "mast", "sail"}}, {"monument", {"plinth", "column"}}, {"ship", {"hull", "masts", "sails", "cabin"}}};
    std::string missing, outside, tall, front, stages, hinge;
    int made = 0;
    std::map<std::string, std::set<std::string>> variety;
    for (auto& t : types)
        for (int id = 0; id < 50; id++) {
            int side = id % 4;
            auto d = bld::make(t.key, id, 10, 10, t.w, t.h, side, false, 0, 1);
            made++;
            auto count = [&](bld::Part p) { int n = 0; for (auto& pc : d.pieces) n += pc.part == p; return n; };
            std::string k = t.key;
            // the recipe's must-haves
            std::string lack;
            bool structure = promised.count(k) > 0;
            if (structure) {
                for (auto& f : promised.at(k)) if (!d.features.count(f)) lack += " " + f;
                if (!count(bld::WALL)) lack += " body";
            } else {
                if (!count(bld::ROOF)) lack += " roof";
                if (!count(bld::WALL)) lack += " walls";
                if (!d.openSide && (!count(bld::DOOR) || d.doorWidth <= 0)) lack += " door";
            }
            if (k == "longhouse" && (d.windows < 2 || !d.smokes)) lack += " windows or smoke outlet";
            if (k == "cottage" && (d.windows < 2 || !d.chimney)) lack += " two windows and a chimney";
            if (k == "house" && (d.windows < 4 || !d.chimney)) lack += " two rows of windows and a chimney";
            if (k == "manor" && (d.windows < 6 || !d.chimney || !d.porch)) lack += " two rows of windows, chimneys and a porch";
            if (k == "forge" && (!d.chimney || !d.openSide || count(bld::EXTRA) < 2)) lack += " chimney, open front or anvil";
            if ((k == "store" || k == "workshop") && d.doorWidth < 1.6f) lack += " wide door";
            if (k == "school" && d.windows < 3) lack += " three windows";
            if ((k == "loomhouse" || k == "infirmary") && d.windows < 2) lack += " two windows";
            if (!lack.empty() && missing.size() < 300) missing += cat("\n          ", k, " #", id, ":", lack);
            variety[k].insert(d.walls + "|" + d.roof + "|" + d.doorStyle + "|" + std::to_string(d.windows) + (d.porch ? "|porch" : ""));
            // everything stays on the footprint, give or take the eaves and a porch
            float x0 = 10 * scale::TILE_M - .9f, y0 = x0, x1 = (10 + t.w) * scale::TILE_M + .9f, y1 = (10 + t.h) * scale::TILE_M + .9f;
            for (auto& pc : d.pieces) {
                const auto& sl = pc.s;
                float ex = std::abs(sl.ax[0].x) * sl.h[0] + std::abs(sl.ax[1].x) * sl.h[1] + std::abs(sl.ax[2].x) * sl.h[2];
                float ey = std::abs(sl.ax[0].y) * sl.h[0] + std::abs(sl.ax[1].y) * sl.h[1] + std::abs(sl.ax[2].y) * sl.h[2];
                if ((sl.c.x - ex < x0 || sl.c.x + ex > x1 || sl.c.y - ey < y0 || sl.c.y + ey > y1) && outside.size() < 200) { outside += cat("\n          ", k, " #", id, " part ", int(pc.part)); break; }
            }
            // height: the table's, plus at most the plinth and a chimney standing clear of the ridge
            if ((d.top < t.tall * .95f || d.top > t.tall + .7f) && tall.size() < 200) tall += cat("\n          ", k, " #", id, ": ", int(d.top * 100), " cm, wanted ", int(t.tall * 100));
            // the door is on the side it was asked for
            if (d.doorPiece >= 0 && !structure) {
                const solid::V3 dirs[4] = {{1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}};
                solid::V3 mid{(10 + t.w / 2.f) * scale::TILE_M, (10 + t.h / 2.f) * scale::TILE_M, 0};
                solid::V3 off = d.pieces[d.doorPiece].s.c - mid;
                float along = off.x * dirs[side].x + off.y * dirs[side].y;
                if (along < .25f && front.size() < 200) front += cat("\n          ", k, " #", id, " door not on side ", side);
            }
            // construction only ever adds pieces
            int last = 0;
            for (float p = 0; p <= 1.001f; p += .05f) {
                int n = bld::visible(d, p);
                if (n < last && stages.size() < 200) stages += cat("\n          ", k, " #", id, " at ", int(p * 100), "%");
                last = n;
            }
            if (bld::visible(d, 1) != int(d.pieces.size())) stages += cat("\n          ", k, " #", id, " not all pieces at 100%");
            // an opening door stays on its hinge: the hinge edge does not move
            if (d.doorPiece >= 0 && !structure) {
                const auto& sl = d.pieces[d.doorPiece].s;
                solid::V3 edge = sl.c - sl.ax[0] * sl.h[0];
                float a = -1.75f;
                solid::V3 rel = edge - d.hinge, moved = {rel.x * std::cos(a) - rel.y * std::sin(a), rel.x * std::sin(a) + rel.y * std::cos(a), rel.z};
                float gap = std::hypot(moved.x - rel.x, moved.y - rel.y);
                if (std::hypot(rel.x, rel.y) > .05f || gap > .1f) if (hinge.size() < 200) hinge += cat("\n          ", k, " #", id);
            }
        }
    CHECK(missing.empty(), cat("every one of ", made, " buildings and structures has its recipe's parts", missing).c_str());
    CHECK(outside.empty(), ("every part stays within the footprint, eaves and porch" + outside).c_str());
    CHECK(tall.empty(), ("buildings stand at the scale table's height" + tall).c_str());
    CHECK(front.empty(), ("the door is on the side facing camp" + front).c_str());
    CHECK(stages.empty(), ("construction only adds parts, and finishes with all of them" + stages).c_str());
    CHECK(hinge.empty(), ("doors swing about their hinge" + hinge).c_str());
    std::string few;
    for (auto& [k, v] : variety) if (v.size() < 6) few += cat(" ", k, " (", int(v.size()), ")");
    CHECK(few.empty(), ("each type comes in at least six different designs over 50 seeds" + (few.empty() ? std::string() : ":" + few)).c_str());
    auto a = bld::make("longhouse", 7, 3, 4, 4, 2, 1, false), b = bld::make("longhouse", 7, 3, 4, 4, 2, 1, false);
    bool same = a.pieces.size() == b.pieces.size() && a.walls == b.walls && a.roof == b.roof;
    for (size_t i = 0; same && i < a.pieces.size(); i++) same = a.pieces[i].s.c.x == b.pieces[i].s.c.x && a.pieces[i].s.col.r == b.pieces[i].s.col.r;
    CHECK(same, "the same building id always makes the same building");
}

// ---------------------------------------------------------------- doors in the game
static void testDoors() {
    std::printf("doors\n");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 40);   // long enough for huts and a longhouse, where settlers sleep indoors
    WorldView v;
    std::map<int, bool> inside;
    std::map<int, double> passed;
    for (auto& p : s.people) inside[p.id] = p.hidden;
    v.updateDoors(s);   // the view starts from the same picture of who is indoors
    int passes = 0, openWhenPassing = 0, shutAfter = 0, checkedShut = 0;
    for (int i = 0; i < 3 * 24 * 12; i++) {   // three days in five-minute steps
        s.step(5);
        if (!s.pendingFate.empty()) { auto f = s.pendingFate[0]; s.pendingFate.clear(); s.applyFate(f); }
        v.updateDoors(s);
        for (auto& p : s.people) {
            if (p.dead) continue;
            // the lean-to is an open shelter with no door to open
            if (inside.count(p.id) && inside[p.id] != p.hidden && s.projectById(p.bed) && s.projectById(p.bed)->key != "leanto") {
                passes++;
                passed[p.bed] = s.tMin;
                if (v.doorOpen(p.bed) == 1) openWhenPassing++;
            }
            inside[p.id] = p.hidden;
        }
        for (auto& [id, t] : passed)
            if (s.tMin - t > WorldView::DOOR_OPEN_MIN + WorldView::DOOR_CLOSE_MIN + 1) { checkedShut++; if (v.doorOpen(id) == 0) shutAfter++; }
    }
    CHECK(passes > 10 && openWhenPassing == passes, cat("a door is open whenever someone goes in or out (", openWhenPassing, " of ", passes, ")").c_str());
    CHECK(checkedShut > 100 && shutAfter == checkedShut, "doors swing shut once nobody has passed for a while");
}

// ---------------------------------------------------------------- save slots
static void testSaveSlots() {
    std::printf("save slots\n");
    namespace fs = std::filesystem;
    fs::path tmp = fs::temp_directory_path() / ("driftwood-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    saves::setDir(tmp.string());
    CHECK(!saves::any() && saves::list().size() == size_t(saves::SLOTS + 1), "a fresh install has a quick slot and five empty ones, nothing to load");
    Sim s;
    s.newWorld(20261017);
    runDays(s, 15);
    std::string err;
    bool wrote = saves::write(2, s.save(), &err) && saves::write(saves::QUICK, s.save(), &err);
    auto slots = saves::list();
    CHECK(wrote && slots[2].used && !slots[2].damaged && slots[2].info.day == s.day() && slots[saves::QUICK].used && !slots[1].used,
          ("saves land in their slots and the list reads their day back" + (err.empty() ? std::string() : ": " + err)).c_str());
    bool leftovers = false;
    for (auto& e : fs::directory_iterator(tmp)) if (e.path().extension() == ".tmp") leftovers = true;
    CHECK(!leftovers, "nothing half-written is left in the save folder");
    std::string data;
    Sim back;
    CHECK(saves::read(2, data, &err) && back.load(data, &err) && back.day() == s.day() && back.members().size() == s.members().size(), "a slot reads back into the same game");
    // a damaged file shows as damaged, and loading it fails without crashing
    { std::ofstream f(tmp / "slot4.dwsave", std::ios::binary); f << "this is not a save"; }
    slots = saves::list();
    CHECK(slots[4].used && slots[4].damaged && saves::read(4, data, &err) && !back.load(data, &err), "a damaged save is listed as damaged and refused");
    CHECK(!saves::read(5, data, &err) && !err.empty(), "reading an empty slot says so");
    fs::remove_all(tmp);
    saves::setDir("");
}

// ---------------------------------------------------------------- the graphics pass
static void testGraphicsPass() {
    std::printf("see-through trees and easing\n");
    Sim s;
    s.newWorld(20261017);
    int tree = -1;
    for (auto& o : s.objs) if (!o.dead && o.type == "tree") { tree = o.i; break; }
    auto item = [](Drawable::Kind k, int id, float sx0, float sy0, float sx1, float sy1) {
        Drawable d{k, id, {0, 0, 1, 1, sx0, sy0, sx1, sy1}, {}};
        return d;
    };
    // a settler drawn first (behind), then a tree whose crown covers them
    std::vector<Drawable> behind{item(Drawable::PERSON, 1, 100, 100, 110, 130), item(Drawable::OBJECT, tree, 80, 60, 140, 150)};
    std::vector<Drawable> front{item(Drawable::OBJECT, tree, 80, 60, 140, 150), item(Drawable::PERSON, 1, 100, 100, 110, 130)};
    std::vector<Drawable> apart{item(Drawable::PERSON, 1, 300, 100, 310, 130), item(Drawable::OBJECT, tree, 80, 60, 140, 150)};
    CHECK(tree >= 0 && WorldView::seeThrough(s, behind)[1], "a tree in front of a settler is drawn see-through");
    CHECK(!WorldView::seeThrough(s, front)[0] && !WorldView::seeThrough(s, apart)[1], "a tree behind a settler, or beside them, is drawn as usual");

    // easing between poses
    figure::Pose a = figure::activity("sit", 3, 1), b = figure::walk(1.f);
    figure::Pose h0 = figure::blend(a, b, 0), h1 = figure::blend(a, b, 1), half = figure::blend(a, b, .5f);
    CHECK(h0.drop == a.drop && h1.drop == b.drop && std::abs(half.hip[0] - (a.hip[0] + b.hip[0]) / 2) < 1e-5f, "a pose eases part way from one to the next");
    // an axe goes up slowly and comes down fast, with a strike at the bottom
    int up = 0, down = 0, strikes = 0;
    float last = -1;
    for (int i = 0; i < 1000; i++) {
        float t = i * .00526f;   // one swing
        auto p = figure::activity("chop", t, 0);
        float sh = p.shoulder[0];
        if (last >= 0) { if (sh > last + 1e-4f) up++; else if (sh < last - 1e-4f) down++; }
        strikes += p.strike > .5f;
        last = sh;
    }
    CHECK(up > down * 2 && strikes > 0 && strikes < 100, cat("a swing lifts slowly and strikes fast (", up, " steps up, ", down, " down)").c_str());
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
    testInspect(20261017, 60);
    testInspect(7, 100);
    testLinks(20261017, 40);
    testLinks(777, 40);
    testContrast();
    testTags();
    testVisibleTiles();
    testFigures();
    testBuildings();
    testDoors();
    testSaveSlots();
    testGraphicsPass();
    std::printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
    return failures;
}
