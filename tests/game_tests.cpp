// Renderer geometry tests: projection and picking against the real WorldView
// camera math. Nothing here opens a window or touches the GPU.
#include <cmath>
#include <cstdio>
#include "game/Iso.h"
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
        body.y -= 14;   // middle of the torso
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

int main() {
    testProjection();
    testPickPeople(20261017);
    testPickPeople(7);
    std::printf(failures ? "\n%d check(s) failed\n" : "\nall checks passed\n", failures);
    return failures;
}
