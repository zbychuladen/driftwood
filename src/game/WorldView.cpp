#include "game/WorldView.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include "engine/Draw.h"
#include "engine/Ui.h"
#include "game/Art.h"
#include "game/Figure.h"
#include "game/Iso.h"
#include "game/Labels.h"
#include "game/Scale.h"
#include "rlgl.h"

using namespace dw;
using draw::V2;
using iso::P;
using iso::wp;
using scale::px;

// Unexplored land and the world's edge are blank paper, as on an illustrated map.
static const Color FOG = {233, 230, 218, 255};
// The soft illustrated look: colors pulled toward grey and paper (see draw::Tone).
static const draw::Tone WORLD_TONE{.32f, .2f, {236, 233, 220, 255}};

void WorldView::init(Sim& sim) {
    if (!ground.id) {
        ground = LoadRenderTexture(int(iso::WORLD_W), int(iso::WORLD_H));
        SetTextureFilter(ground.texture, TEXTURE_FILTER_BILINEAR);
    }
    gc.season = -1;
    bakedSeason = -1;
    fogVersion = -1;
    centerOnTile(sim.campX + .5f, sim.campY + .5f);
    cam.zoom = 1.15f;
}

void WorldView::unload() {
    if (ground.id) UnloadRenderTexture(ground);
    ground = {};
    lighting.unload();
}

void WorldView::centerOnTile(float tx, float ty) { V2 w = wp(tx, ty); cam.target = w; }

void WorldView::clampCam() {
    cam.zoom = std::clamp(cam.zoom, MIN_ZOOM, MAX_ZOOM);
    cam.target.x = std::clamp(cam.target.x, 0.f, iso::WORLD_W);
    cam.target.y = std::clamp(cam.target.y, 0.f, iso::WORLD_H);
}

void WorldView::zoomBy(float f, Vector2 pivot) {
    V2 before = GetScreenToWorld2D(pivot, cam);
    cam.zoom *= f;
    clampCam();
    V2 after = GetScreenToWorld2D(pivot, cam);
    cam.target.x += before.x - after.x;
    cam.target.y += before.y - after.y;
    clampCam();
}

void WorldView::handleInput(float dt, Rectangle map, bool pointerFree) {
    lastMap = map;
    cam.offset = {map.x + map.width / 2, map.y + map.height / 2};
    clicked = false;
    V2 m = GetMousePosition();
    bool overMap = CheckCollisionPointRec(m, map) && pointerFree;
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        pressOnMap = overMap;
        dragging = false;
        pressAt = m;
        camAtPress = cam.target;
    }
    if (pressOnMap && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        float dx = m.x - pressAt.x, dy = m.y - pressAt.y;
        if (std::abs(dx) + std::abs(dy) > 5) dragging = true;
        if (dragging) { cam.target = {camAtPress.x - dx / cam.zoom, camAtPress.y - dy / cam.zoom}; clampCam(); }
    }
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        if (pressOnMap && !dragging) { clicked = true; clickAt = m; }
        pressOnMap = false;
        dragging = false;
    }
    if (overMap) {
        float wheel = GetMouseWheelMove();
        if (wheel != 0) zoomBy(std::exp(wheel * .12f), m);
    }
    float sp = 700 * dt / cam.zoom;
    if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A)) cam.target.x -= sp;
    if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D)) cam.target.x += sp;
    if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W)) cam.target.y -= sp;
    if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S)) cam.target.y += sp;
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) zoomBy(1.2f, cam.offset);
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) zoomBy(1 / 1.2f, cam.offset);
    clampCam();
}

// ---------------------------------------------------------------- terrain
// The ground's colors and marks are worked out once per season and seed, then
// painted either into one big baked image (zoomed out) or live for just the
// visible tiles (zoomed in, where the baked image would look soft).
void WorldView::buildGround(Sim& s) {
    int season = s.season();
    const int N = GW * GH;
    // distance from land, for water depth shading
    std::vector<float> distLand(N, 99);
    std::vector<int> q;
    for (int i = 0; i < N; i++) if (s.tt[i] >= 2) { distLand[i] = 0; q.push_back(i); }
    for (size_t h = 0; h < q.size(); h++) {
        int i = q[h], x = i % GW, y = i / GW;
        const int D[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (auto& d : D) { int nx = x + d[0], ny = y + d[1]; if (!Sim::inb(nx, ny)) continue; int j = Sim::idx(nx, ny); if (distLand[j] > distLand[i] + 1) { distLand[j] = distLand[i] + 1; q.push_back(j); } }
    }
    // pale sage and cream, the seasons only nudging the hue
    const uint32_t grass[4] = {0xb4c496, 0xadbd8a, 0xc4bc8e, 0xeceee9};
    const uint32_t forest[4] = {0x9cae80, 0x96a878, 0xae9f76, 0xdfe3df};
    const uint32_t sand[4] = {0xeee3c4, 0xf0e4c2, 0xeadcbc, 0xeeece5};
    const uint32_t hill[4] = {0xb0b592, 0xabb08a, 0xbcae88, 0xd9dcdc};
    Mulberry r(s.seed + 7);
    auto& col = gc.col;
    col.assign(N, {});
    waterTiles.clear();
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
        int i = Sim::idx(x, y), t = s.tt[i];
        Color c;
        if (t < 2) {
            waterTiles.push_back(i);
            float dd = s.fresh[i] ? (t == DEEP ? 1.2f : .4f) : std::clamp(distLand[i] / 4.f, 0.f, 1.4f);
            c = s.fresh[i] ? draw::rgba(int(118 - dd * 14), int(160 - dd * 20), int(150 - dd * 12)) : draw::rgba(int(132 - dd * 30), int(174 - dd * 36), int(184 - dd * 30));
            if (season == 3) c = s.fresh[i] ? draw::rgba(186, 210, 220) : draw::shade(c, .92f);
        } else {
            uint32_t base = t == SAND ? sand[season] : t == FOREST ? forest[season] : t == HILL ? hill[season] : grass[season];
            c = draw::shade(draw::hex(base), .95f + r() * .1f);
        }
        col[i] = c;
    }
    gc.corner.assign((GW + 1) * (GH + 1), {});
    for (int cy = 0; cy <= GH; cy++) for (int cx = 0; cx <= GW; cx++) {
        int rr = 0, gg = 0, bb = 0, n = 0;
        for (int dy = -1; dy <= 0; dy++) for (int dx = -1; dx <= 0; dx++) {
            int x = cx + dx, y = cy + dy;
            if (!Sim::inb(x, y)) continue;
            Color c = col[Sim::idx(x, y)];
            rr += c.r; gg += c.g; bb += c.b; n++;
        }
        gc.corner[cy * (GW + 1) + cx] = n ? Color{(unsigned char)(rr / n), (unsigned char)(gg / n), (unsigned char)(bb / n), 255} : col[0];
    }
    // grass blades, flowers, leaves, pebbles, sand speckles, snow: the same
    // density whatever the island's size, then filed under the tile they sit on
    std::vector<GroundCache::Mark> all;
    const int marks = 12000 * GW * GH / (64 * 64);
    for (int k = 0; k < marks; k++) {
        float tx = r() * GW, ty = r() * GH;
        int t = s.tt[Sim::idx(int(tx), int(ty))];
        using M = GroundCache::Mark;
        if (t >= 3) {
            if (season == 3) { all.push_back({tx, ty, M::SNOW, draw::rgba(255, 255, 255, .3f + r() * .4f), 0, 0}); continue; }
            Color bl = r() < .5f ? draw::rgba(60, 80, 50, .1f + r() * .08f) : draw::rgba(250, 250, 230, .12f + r() * .1f);
            float dx = (r() - .5f) * 2, dy = -2 - r() * 3;
            all.push_back({tx, ty, M::BLADE, bl, dx, dy});
            if (t == GRASS && r() < .04f && season <= 1) { const uint32_t fl[4] = {0xfff7d6, 0xf6d24a, 0xe98ab3, 0xc9b6f2}; all.push_back({tx, ty, M::FLOWER, draw::hex(fl[int(r() * 4) % 4]), 0, 0}); }
            if (season == 2 && r() < .05f) { const uint32_t lv[3] = {0xc8642c, 0xe0a83a, 0xa9442a}; all.push_back({tx, ty, M::LEAF, draw::hex(lv[int(r() * 3) % 3]), 0, 0}); }
            if (t == HILL && r() < .08f) all.push_back({tx, ty, M::PEBBLE, draw::rgba(90, 90, 80, .35f), 0, 0});
        } else if (t == SAND) all.push_back({tx, ty, M::SPECK, draw::rgba(120, 100, 60, .15f + r() * .2f), 0, 0});
    }
    gc.markStart.assign(N + 1, 0);
    for (auto& m : all) gc.markStart[Sim::idx(int(m.tx), int(m.ty)) + 1]++;
    for (int i = 0; i < N; i++) gc.markStart[i + 1] += gc.markStart[i];
    gc.marks.assign(all.size(), {});
    std::vector<int> fill(gc.markStart.begin(), gc.markStart.end() - 1);
    for (auto& m : all) gc.marks[fill[Sim::idx(int(m.tx), int(m.ty))]++] = m;
    gc.season = season;
    gc.seed = s.seed;
}

void WorldView::paintGround(Sim& s, int x0, int y0, int x1, int y1) {
    int season = gc.season;
    auto corner = [&](int x, int y) { return gc.corner[y * (GW + 1) + x]; };
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        Color c = gc.col[Sim::idx(x, y)];
        V2 a = wp(x, y), b = wp(x + 1, y), cc = wp(x + 1, y + 1), d = wp(x, y + 1), m = wp(x + .5f, y + .5f);
        Color ca = corner(x, y), cb = corner(x + 1, y), ccc = corner(x + 1, y + 1), cd = corner(x, y + 1);
        draw::quad(m, a, b, m, c, ca, cb, c);
        draw::quad(m, b, cc, m, c, cb, ccc, c);
        draw::quad(m, cc, d, m, c, ccc, cd, c);
        draw::quad(m, d, a, m, c, cd, ca, c);
    }
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        int i = Sim::idx(x, y);
        for (int k = gc.markStart[i]; k < gc.markStart[i + 1]; k++) {
            const auto& mk = gc.marks[k];
            V2 p = wp(mk.tx, mk.ty);
            switch (mk.kind) {
            case GroundCache::Mark::SNOW: draw::rect(p.x, p.y, 1.6f, 1.2f, mk.c); break;
            case GroundCache::Mark::BLADE: draw::line(p.x, p.y, p.x + mk.a, p.y + mk.b, 1, mk.c); break;
            case GroundCache::Mark::FLOWER: draw::circle(p.x, p.y, 1.3f, mk.c); break;
            case GroundCache::Mark::LEAF: draw::rect(p.x, p.y, 2.4f, 1.4f, mk.c); break;
            case GroundCache::Mark::PEBBLE: draw::ellipse(p.x, p.y, 3, 1.5f, mk.c); break;
            case GroundCache::Mark::SPECK: draw::rect(p.x, p.y, 1.3f, 1.1f, mk.c); break;
            }
        }
    }
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        if (s.tt[Sim::idx(x, y)] != HILL) continue;
        V2 c = wp(x + .5f, y + .5f);
        draw::radial(c.x - 6, c.y - 4, 26, 13, season == 3 ? draw::rgba(255, 255, 255, .35f) : draw::rgba(255, 255, 230, .18f), draw::rgba(255, 255, 230, 0));
    }
    // shore foam
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        if (s.tt[Sim::idx(x, y)] < 2) continue;
        struct E { int dx, dy; V2 a, b; } edges[4] = {{1, 0, P(x + 1, y), P(x + 1, y + 1)}, {-1, 0, P(x, y), P(x, y + 1)}, {0, 1, P(x, y + 1), P(x + 1, y + 1)}, {0, -1, P(x, y), P(x + 1, y)}};
        for (auto& e : edges) {
            int nx = x + e.dx, ny = y + e.dy;
            if (!Sim::inb(nx, ny) || s.tt[Sim::idx(nx, ny)] >= 2) continue;
            // soft surf: a wide faint band and a thin bright line, pushed onto the water
            float ox = (e.dx - e.dy) * 3.f, oy = (e.dx + e.dy) * 1.5f;
            V2 a2{e.a.x + ox, e.a.y + oy}, b2{e.b.x + ox, e.b.y + oy};
            V2 mid{(a2.x + b2.x) / 2 + ox * .6f, (a2.y + b2.y) / 2 + oy * .6f};
            draw::curve(a2, mid, b2, 5.f, draw::rgba(255, 255, 255, .12f), 6);
            draw::curve(e.a, {(e.a.x + e.b.x) / 2 + ox * .4f, (e.a.y + e.b.y) / 2 + oy * .4f}, e.b, 1.2f, draw::rgba(255, 255, 255, .35f), 6);
        }
    }
}

void WorldView::bakeGround(Sim& s) {
    BeginTextureMode(ground);
    rlDisableBackfaceCulling();
    ClearBackground(Color{0, 0, 0, 0});
    paintGround(s, 0, 0, GW - 1, GH - 1);
    EndTextureMode();
    bakedSeason = gc.season;
    bakedSeed = gc.seed;
}

WorldView::TileRange WorldView::visibleTiles(Rectangle screen) const {
    // the tiles under the four corners of the view, widened for marks and foam that spill over
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (V2 sc : {V2{screen.x, screen.y}, V2{screen.x + screen.width, screen.y}, V2{screen.x, screen.y + screen.height}, V2{screen.x + screen.width, screen.y + screen.height}}) {
        V2 w = GetScreenToWorld2D(sc, cam), t = iso::toTile(w.x, w.y);
        x0 = std::min(x0, t.x); y0 = std::min(y0, t.y); x1 = std::max(x1, t.x); y1 = std::max(y1, t.y);
    }
    return {std::max(0, int(std::floor(x0)) - 2), std::max(0, int(std::floor(y0)) - 2), std::min(GW - 1, int(std::ceil(x1)) + 2), std::min(GH - 1, int(std::ceil(y1)) + 2)};
}

// ---------------------------------------------------------------- fog
void WorldView::buildFog(Sim& s) {
    fogCorner.assign((GW + 1) * (GH + 1), 0);
    for (int cy = 0; cy <= GH; cy++) for (int cx = 0; cx <= GW; cx++) {
        int un = 0, n = 0;
        for (int dy = -1; dy <= 0; dy++) for (int dx = -1; dx <= 0; dx++) {
            int x = cx + dx, y = cy + dy;
            if (!Sim::inb(x, y)) { un++; n++; continue; }
            n++;
            if (!s.explored[Sim::idx(x, y)]) un++;
        }
        fogCorner[cy * (GW + 1) + cx] = float(un) / n;
    }
    fogVersion = s.exploredVersion;
}

void WorldView::drawFog(Sim& s) {
    if (fogVersion != s.exploredVersion || int(fogCorner.size()) != (GW + 1) * (GH + 1)) buildFog(s);
    V2 tl = GetScreenToWorld2D({0, 0}, cam), br = GetScreenToWorld2D({float(GetScreenWidth()), float(GetScreenHeight())}, cam);
    // corners off the map count as unknown, so the fog runs a few tiles past the
    // edge and no seam shows where the map meets the paper around it
    auto at = [&](int x, int y) { return x < 0 || y < 0 || x > GW || y > GH ? 1.f : fogCorner[y * (GW + 1) + x]; };
    auto fc = [&](int x, int y) { return Color{FOG.r, FOG.g, FOG.b, (unsigned char)(std::min(1.f, at(x, y) * 1.15f) * 255)}; };
    const int margin = 3;
    for (int y = -margin; y < GH + margin; y++) for (int x = -margin; x < GW + margin; x++) {
        float a = at(x, y), b = at(x + 1, y), c = at(x + 1, y + 1), d = at(x, y + 1);
        if (a + b + c + d == 0) continue;
        V2 m = wp(x + .5f, y + .5f);
        if (m.x < tl.x - 64 || m.x > br.x + 64 || m.y < tl.y - 64 || m.y > br.y + 64) continue;
        draw::quad(P(x, y), P(x + 1, y), P(x + 1, y + 1), P(x, y + 1), fc(x, y), fc(x + 1, y), fc(x + 1, y + 1), fc(x, y + 1));
    }
}

// ---------------------------------------------------------------- weather
void WorldView::drawWeather(Sim& s, float dt, float speed) {
    int target = s.weather == "rain" ? 220 : s.weather == "storm" ? 420 : s.weather == "snow" ? 240 : 0;
    float W = float(GetScreenWidth()), H = float(GetScreenHeight());
    while (int(drops.size()) < target) drops.push_back({float(GetRandomValue(0, int(W))), float(GetRandomValue(0, int(H))), .6f + GetRandomValue(0, 60) / 100.f});
    if (int(drops.size()) > target) drops.resize(target);
    if (drops.empty()) return;
    float k = dt * 60 * (speed > 0 ? std::clamp(speed, .6f, 2.f) : .4f);
    float wind = s.weather == "storm" ? 4 : 1;
    for (auto& d : drops) {
        if (s.weather == "snow") {
            d.y += d.v * 1.2f * k; d.x += std::sin((d.y + d.x) * .02f) * .5f * k;
            if (d.y > H) { d.y = -4; d.x = float(GetRandomValue(0, int(W))); }
            draw::circle(d.x, d.y, 1.2f + d.v, draw::rgba(255, 255, 255, .85f));
        } else {
            d.y += d.v * 9 * k; d.x += wind * k;
            if (d.y > H) { d.y = -10; d.x = float(GetRandomValue(0, int(W))); }
            if (d.x > W) d.x = 0;
            draw::line(d.x, d.y, d.x - wind * 2, d.y - 9, 1, draw::rgba(200, 215, 235, .55f));
        }
    }
}

static float darkness(double h) {
    if (h >= 8 && h < 17.5) return 0;
    if (h >= 20 || h < 5) return .62f;
    if (h >= 17.5) return float((h - 17.5) / 2.5 * .62);
    return float((8 - h) / 3 * .62);
}

// ---------------------------------------------------------------- main draw
void WorldView::draw(Sim& s, Rectangle map, int selectedId, float realDt, float speed, Pick inspected) {
    draw::tone = WORLD_TONE;
    updateFacings(s);
    updateDoors(s);
    if (gc.season != s.season() || gc.seed != s.seed) buildGround(s);
    bool liveGround = cam.zoom >= LIVE_GROUND_ZOOM;
    if (!liveGround && (bakedSeason != gc.season || bakedSeed != gc.seed)) bakeGround(s);
    lighting.resize(GetScreenWidth(), GetScreenHeight());
    ClearBackground(FOG);
    art::Ctx ctx{&s, s.tMin, s.season(), s.hour()};
    double t = s.tMin;
    V2 tl = GetScreenToWorld2D({0, 0}, cam), br = GetScreenToWorld2D({float(GetScreenWidth()), float(GetScreenHeight())}, cam);
    auto visible = [&](V2 w, float m = 90) { return w.x > tl.x - m && w.x < br.x + m && w.y > tl.y - m && w.y < br.y + m * 1.8f; };

    BeginMode2D(cam);
    if (liveGround) {
        TileRange tr = visibleTiles({0, 0, float(GetScreenWidth()), float(GetScreenHeight())});
        paintGround(s, tr.x0, tr.y0, tr.x1, tr.y1);
    } else DrawTextureRec(ground.texture, {0, 0, iso::WORLD_W, -iso::WORLD_H}, {0, 0}, WHITE);
    // water shimmer
    for (int i : waterTiles) {
        int x = i % GW, y = i / GW;
        float ph = float((x * 928371 + y * 12345) % 628) / 100.f;
        float a = std::sin(float(t * .05) + ph * 3 + x * .7f);
        if (a < .55f) continue;
        V2 w = wp(x + .5f, y + .5f);
        if (!visible(w, 40)) continue;
        float o = std::sin(float(t * .02) + ph) * 5;
        draw::curve({w.x - 6 + o, w.y}, {w.x + o, w.y - 2}, {w.x + 6 + o, w.y}, 1, draw::rgba(255, 255, 255, (a - .55f) * 2 * (s.season() == 3 ? .35f : .45f)), 4);
    }
    // worn ground around camp
    {
        V2 c = wp(s.campX + .5f, s.campY + .5f);
        float wear = std::min(.28f, s.day() * .02f + .06f);
        draw::radial(c.x, c.y, 5 * iso::HW, 5 * iso::HH, draw::rgba(140, 110, 70, wear), draw::rgba(140, 110, 70, 0), 40);
    }
    for (auto& b : s.plan) if (b.placed && !b.skip && art::isFlat(b.key) && visible(wp(b.x + b.w / 2.f, b.y + b.h / 2.f), 160)) art::building(ctx, b);

    drawFog(s);   // under everything standing, so tall art at the edge of the known island is not cut off
    drawHighlight(s, inspected);
    Rectangle viewR{tl.x - 90, tl.y - 90, br.x - tl.x + 180, br.y - tl.y + 250};
    draw::ink = {std::max(.55f, 1.05f / cam.zoom), {52, 62, 52, 255}};
    for (auto& d : sorted(collect(s, ctx, viewR, selectedId))) d.draw(ctx);
    draw::ink.width = 0;
    drawLinks(s, selectedId, inspected);
    EndMode2D();

    draw::tone = {};
    // screen-space atmosphere
    drawWeather(s, realDt, speed);
    double h = s.hour();
    float sw = float(GetScreenWidth()), shh = float(GetScreenHeight());
    if ((h >= 17 && h < 20) || (h >= 5.5 && h < 7.5)) draw::rect(0, 0, sw, shh, draw::rgba(255, 140, 60, .1f));
    if (s.weather == "storm" || s.weather == "rain") draw::rect(0, 0, sw, shh, draw::rgba(60, 70, 90, s.weather == "storm" ? .14f : .06f));
    float dk = darkness(h);
    if (dk > 0) {
        std::vector<Light> lights;
        auto toS = [&](V2 w) { return GetWorldToScreen2D(w, cam); };
        if (auto* f = s.fire(); f && f->fuel > 0)
            lights.push_back({toS(wp(f->x + .5f, f->y + .5f)), (130 + std::sin(float(t * .7)) * 8 + std::sin(float(t * 1.9)) * 4) * std::min(1.f, .55f + f->fuel / 6) * cam.zoom, Color{255, 150, 60, 70}});
        for (auto& b : s.plan) if (b.done && (b.key == "hut" || b.key == "longhouse" || b.key == "school" || b.key == "infirmary"))
            lights.push_back({toS(wp(b.x + b.w / 2.f, b.y + b.h / 2.f)), 55 * cam.zoom, Color{255, 170, 80, 40}});
        for (auto key : {"tower", "signal", "lighthouse", "forge"}) if (auto* b = s.built(key)) {
            float up = std::string(key) == "lighthouse" ? px(scale::LIGHTHOUSE - .9f) : std::string(key) == "tower" ? px(scale::TOWER - 1.8f) : px(1.5f);
            V2 w = wp(b->x + .5f, b->y + .5f); w.y -= up;
            lights.push_back({toS(w), (std::string(key) == "lighthouse" ? 170.f : 85.f) * cam.zoom, Color{255, 160, 70, 60}});
        }
        if (auto* b = s.built("shrine")) lights.push_back({toS(wp(b->x + 1.f, b->y + 1.f)), 55 * cam.zoom, Color{190, 150, 255, 50}});
        lighting.render(dk, Color{12, 18, 44, 255}, lights);
    }
    if (s.flash > 0) { draw::rect(0, 0, sw, shh, draw::rgba(230, 235, 255, s.flash * .5f)); s.flash = std::max(0.f, s.flash - realDt * 4); }
    drawLabels(s, selectedId);
    for (auto& c : captions) {
        V2 sc = GetWorldToScreen2D(c.world, cam);
        V2 m = ui::measure(ui::F.bold, c.text, 13);
        Rectangle r{sc.x - m.x / 2 - 8, sc.y - 10, m.x + 16, 20};
        DrawRectangleRounded(r, 1, 8, Color{255, 246, 224, 240});
        DrawRectangleRoundedLinesEx(r, 1, 8, 1, Color{176, 140, 70, 255});
        ui::text(ui::F.bold, c.text, r.x + 8, r.y + 3, 13, Color{60, 46, 24, 255});
    }
}

// ---------------------------------------------------------------- what to draw, in what order
std::vector<Drawable> WorldView::collect(Sim& s, const art::Ctx& ctx, Rectangle viewR, int selectedId) const {
    std::vector<Drawable> out;
    out.reserve(1200);
    auto inView = [&](float tx, float ty) { V2 w = wp(tx, ty); return CheckCollisionPointRec(w, viewR); };
    // footprint helpers: a box of half-size r around a ground point, or a tile rectangle
    auto around = [](float x, float y, float r) { return depth::Item{x - r, y - r, x + r, y + r, 0, 0, 0, 0}; };
    auto rectOf = [](float x, float y, float w, float h) { return depth::Item{x, y, x + w, y + h, 0, 0, 0, 0}; };
    auto add = [&](Drawable::Kind k, int id, depth::Item fp, std::function<void(const art::Ctx&)> f) { out.push_back({k, id, fp, std::move(f)}); };

    for (auto& o : s.objs) {
        if (o.dead || !s.explored[Sim::idx(o.x, o.y)] || !inView(o.x + .5f, o.y + .5f)) continue;
        const Obj* op = &o;
        float r = o.type == "rock" ? .35f : o.type == "stump" ? .15f : .25f;
        std::function<void(const art::Ctx&)> f;
        if (o.type == "tree") f = [op](const art::Ctx& c) { art::tree(c, *op); };
        else if (o.type == "rock") f = [op](const art::Ctx& c) { art::rock(c, *op); };
        else if (o.type == "bush") f = [op](const art::Ctx& c) { art::bush(c, *op); };
        else if (o.type == "totem") f = [op](const art::Ctx& c) { art::totem(c, *op); };
        else f = [op](const art::Ctx& c) { art::stump(c, *op); };
        add(Drawable::OBJECT, o.i, around(o.x + .5f, o.y + .6f, r), f);
    }
    for (auto& b : s.plan) {
        if (!b.placed || b.skip || art::isFlat(b.key) || !inView(b.x + b.w / 2.f, b.y + b.h / 2.f)) continue;
        Project* bp = &b;
        add(Drawable::BUILDING, b.id, rectOf(b.x + .05f, b.y + .05f, b.w - .1f, b.h - .1f), [bp](const art::Ctx& c) { art::building(c, *bp); });
    }
    for (size_t i = 0; i < s.pois.size(); i++) {
        auto& p = s.pois[i];
        if (!s.explored[Sim::idx(p.x, p.y)] || !inView(p.x + .5f, p.y + .5f)) continue;
        const Poi* pp = &p;
        add(Drawable::POI, int(i), rectOf(p.x + .1f, p.y + .1f, .8f, .8f), [pp](const art::Ctx& c) { art::poi(c, *pp); });
    }
    if (inView(s.stockX + 1.f, s.stockY + .5f)) add(Drawable::STOCKPILE, 0, rectOf(s.stockX, s.stockY, 2, 1), [](const art::Ctx& c) { art::stockpile(c); });
    if (inView(s.boatX + .5f, s.boatY + .5f)) add(Drawable::BOAT, 0, around(s.boatX + .5f, s.boatY + .5f, .6f), [](const art::Ctx& c) { art::boat(c); });
    for (auto& p : s.people) {
        if (p.dead || p.hidden || !s.isExplored(p.x, p.y) || !inView(p.x, p.y)) continue;
        const Person* pp = &p;
        bool sel = p.id == selectedId;
        auto tr = tracks.find(p.id);
        int facing = tr == tracks.end() ? figure::SE : tr->second.facing;
        int detail = px(scale::PERSON) * cam.zoom >= figure::DETAIL_PX ? 1 : 0;
        add(Drawable::PERSON, p.id, around(p.x, p.y, .12f), [pp, sel, facing, detail](const art::Ctx& c) { art::person(c, *pp, sel, facing, detail); });
    }
    if (s.wolf && s.isExplored(s.wolf->x, s.wolf->y) && inView(s.wolf->x, s.wolf->y)) {
        const Wolf* w = &*s.wolf;
        add(Drawable::WOLF, 0, around(w->x, w->y, .2f), [w](const art::Ctx& c) { art::wolf(c, *w); });
    }
    // measure what each piece of art covers
    art::Ctx quiet = ctx;
    quiet.effects = false;
    for (auto& d : out) {
        draw::Box box;
        draw::record(&box);
        d.draw(quiet);
        draw::record(nullptr);
        if (box.empty()) { V2 w = wp((d.at.x0 + d.at.x1) / 2, (d.at.y0 + d.at.y1) / 2); box = {w.x, w.y, w.x, w.y}; }
        d.at.sx0 = box.x0; d.at.sy0 = box.y0; d.at.sx1 = box.x1; d.at.sy1 = box.y1;
    }
    return out;
}

std::vector<Drawable> WorldView::sorted(std::vector<Drawable> items) {
    std::vector<depth::Item> at;
    at.reserve(items.size());
    for (auto& d : items) at.push_back(d.at);
    std::vector<Drawable> out;
    out.reserve(items.size());
    for (int i : depth::order(at)) out.push_back(std::move(items[i]));
    return out;
}

bool WorldView::showTag(Sim& s, const Person& p, Pick hovered, int selectedId) {
    if (p.id == selectedId || hovered.is(Drawable::PERSON, p.id)) return true;
    return p.hidden && hovered.ok && hovered.kind == Drawable::BUILDING && hovered.id == p.bed && s.projectById(p.bed);
}

void WorldView::drawLabels(Sim& s, int selectedId) {
    if (cam.zoom < .55f) return;
    const float size = 13;
    // Gather names and speech bubbles, lay them out so none overlap, then draw.
    struct Lab { const Person* p; std::string text; bool bubble; };
    std::vector<Lab> labs;
    std::vector<labels::Want> wants;
    std::map<int, int> stackAtBed;
    for (auto& p : s.people) {
        if (p.dead || !s.isExplored(p.x, p.y)) continue;
        bool tag = showTag(s, p, hovered, selectedId);
        bool talking = p.bubble && s.tMin < p.bubble->until && !p.hidden;
        if (!tag && !talking) continue;   // speech bubbles still show; names wait for the pointer
        V2 w;
        if (p.hidden) {
            auto* b = s.projectById(p.bed);
            if (!b) continue;
            w = wp(b->x + b->w / 2.f, b->y + b->h / 2.f);
            w.y -= px(5.5f) + stackAtBed[b->id]++ * 16;
        } else { w = wp(p.x, p.y); w.y += 10; }
        V2 sc = GetWorldToScreen2D(w, cam);
        if (sc.x < -60 || sc.y < -60 || sc.x > GetScreenWidth() + 60 || sc.y > GetScreenHeight() + 60) continue;
        std::string lab = p.name + (p.sleeping ? "  zz" : p.status == "hermit" ? " (hermit)" : p.status == "arriving" ? " (stranger)" : "");
        V2 m = ui::measure(ui::F.bold, lab, size);
        bool sel = p.id == selectedId;
        if (tag) {
            labs.push_back({&p, lab, false});
            wants.push_back({{sc.x - m.x / 2 - 6, sc.y - 8, m.x + 12, 16}, sel || hovered.is(Drawable::PERSON, p.id)});
        }
        if (p.bubble && s.tMin < p.bubble->until && !p.hidden) {
            V2 head = GetWorldToScreen2D(wp(p.x, p.y), cam);
            float by = head.y - (px(p.child ? scale::CHILD : scale::PERSON) + 14) * cam.zoom;
            V2 bm = ui::measure(ui::F.body, p.bubble->text, 14);
            labs.push_back({&p, p.bubble->text, true});
            wants.push_back({{head.x - bm.x / 2 - 8, by - 11, bm.x + 16, 22}, sel});
        }
    }
    auto boxes = labels::layout(wants);
    for (size_t i = 0; i < labs.size(); i++) {
        const auto& L = labs[i];
        const auto& b = boxes[i];
        if (b.h == 0) continue;
        Rectangle r{b.x, b.y, b.w, b.h};
        bool sel = L.p->id == selectedId;
        if (L.bubble) {
            V2 head = GetWorldToScreen2D(wp(L.p->x, L.p->y), cam);
            DrawRectangleRounded(r, .8f, 8, Color{255, 255, 255, 240});
            DrawRectangleRoundedLinesEx(r, .8f, 8, 1, Color{52, 62, 52, 140});
            // the tail points at the speaker even when the bubble had to move
            float tx = std::clamp(head.x, r.x + 8, r.x + r.width - 8);
            DrawTriangle({tx - 4, r.y + r.height - 1}, {tx, r.y + r.height + 5}, {tx + 4, r.y + r.height - 1}, Color{255, 255, 255, 240});
            ui::text(ui::F.body, L.text, r.x + 8, r.y + 3, 14, Color{30, 42, 35, 255});
        } else {
            // light tags with an ink edge, like map labels; the selected one in the accent green
            Color bg = sel ? Color{79, 107, 69, 250} : L.p->status != "member" ? Color{122, 96, 64, 235} : Color{250, 249, 244, 235};
            Color fg = sel || L.p->status != "member" ? Color{255, 255, 255, 255} : Color{38, 52, 40, 255};
            DrawRectangleRounded(r, .45f, 6, bg);
            DrawRectangleRoundedLinesEx(r, .45f, 6, 1, Color{52, 62, 52, 170});
            ui::text(ui::F.bold, L.text, r.x + 6, r.y + 1.5f, size, fg);
        }
    }
}

Pick WorldView::pickAt(Sim& s, Vector2 screen) const {
    V2 w = GetScreenToWorld2D(screen, cam);
    art::Ctx ctx{&s, s.tMin, s.season(), s.hour()};
    ctx.effects = false;
    // things whose ground point lies below the click (tall art rises far above it) or a little above
    Rectangle nearby{w.x - 260, w.y - 120, 520, 520};
    auto items = sorted(collect(s, ctx, nearby, -1));
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        const auto& a = it->at;
        if (w.x < a.sx0 || w.x > a.sx1 || w.y < a.sy0 || w.y > a.sy1) continue;
        draw::Box box;
        draw::Probe pr{w};
        draw::record(&box);
        draw::probe(&pr);
        it->draw(ctx);
        draw::probe(nullptr);
        draw::record(nullptr);
        if (pr.hit) return {it->kind, it->id, true};
    }
    // settlers are small; a click just beside one still counts
    int id = const_cast<WorldView*>(this)->pick(s, screen);
    if (id >= 0) return {Drawable::PERSON, id, true};
    return {};
}

// ---------------------------------------------------------------- facing
void WorldView::updateFacings(Sim& s) {
    for (auto& p : s.people) {
        if (p.dead) continue;
        auto [it, fresh] = tracks.try_emplace(p.id, Track{p.x, p.y, figure::SE});
        Track& t = it->second;
        int f = t.facing;
        if (p.moving) {
            f = figure::facingToward(p.x - t.x, p.y - t.y, figure::Facing(f));
        } else if (p.anim == "fish") {
            // face the water beside them
            const int D[4][2] = {{1, 0}, {0, 1}, {0, -1}, {-1, 0}};
            for (auto& d : D) if (s.isWater(int(p.x) + d[0], int(p.y) + d[1])) { f = figure::facingToward(float(d[0]), float(d[1]), figure::Facing(f)); break; }
        } else if (!p.anim.empty()) {
            // at work, turn toward what they are working on
            FocusTarget ft = focusTarget(s, p.focus);
            if (ft.ok && std::hypot(ft.x - p.x, ft.y - p.y) > .3f) f = figure::facingToward(ft.x - p.x, ft.y - p.y, figure::Facing(f));
        }
        t = {p.x, p.y, f};
    }
}

// ---------------------------------------------------------------- doors
void WorldView::updateDoors(Sim& s) {
    lastPassClock = s.tMin;
    for (auto& p : s.people) {
        if (p.dead) continue;
        auto [it, fresh] = wasInside.try_emplace(p.id, p.hidden);
        if (!fresh && it->second != p.hidden && s.projectById(p.bed)) lastPass[p.bed] = s.tMin;   // in or out through the door
        it->second = p.hidden;
    }
    for (auto& [id, t] : lastPass) art::setDoor(id, doorOpen(id));
}

float WorldView::doorOpen(int id) const {
    auto it = lastPass.find(id);
    if (it == lastPass.end()) return 0;
    double since = lastPassClock - it->second;
    if (since < 0) return 0;
    if (since <= DOOR_OPEN_MIN) return 1;
    return float(std::clamp(1 - (since - DOOR_OPEN_MIN) / DOOR_CLOSE_MIN, 0.0, 1.0));
}

// ---------------------------------------------------------------- links between people and their work
FocusTarget focusTarget(Sim& s, const Focus& f) {
    FocusTarget t;
    switch (f.kind) {
    case Focus::PROJECT:
        if (auto* b = s.projectById(f.id)) t = {true, b->x + b->w / 2.f, b->y + b->h / 2.f, {Drawable::BUILDING, b->id, true}};
        break;
    case Focus::OBJECT:
        if (f.id >= 0 && f.id < int(s.objs.size()) && !s.objs[f.id].dead) t = {true, s.objs[f.id].x + .5f, s.objs[f.id].y + .6f, {Drawable::OBJECT, f.id, true}};
        break;
    case Focus::POI:
        if (f.id >= 0 && f.id < int(s.pois.size())) t = {true, s.pois[f.id].x + .5f, s.pois[f.id].y + .5f, {Drawable::POI, f.id, true}};
        break;
    case Focus::PERSON:
        if (auto* q = s.person(f.id); q && !q->dead) t = {true, q->x, q->y, {Drawable::PERSON, q->id, true}};
        break;
    case Focus::STOCK: t = {true, s.stockX + 1.f, s.stockY + .5f, {Drawable::STOCKPILE, 0, true}}; break;
    case Focus::SPOT: t = {true, f.x, f.y, {}}; break;
    case Focus::NONE: break;
    }
    return t;
}

// A dashed line along the ground whose dashes march toward b.
static void dashed(V2 a, V2 b, float w, Color c, double t, float zoom) {
    float dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
    if (len < 1) return;
    float dash = 9 / zoom + 3, gap = 6 / zoom + 2, period = dash + gap;
    float off = std::fmod(float(t * .9), 1.f) * period;
    for (float d = -period + off; d < len; d += period) {
        float d0 = std::max(0.f, d), d1 = std::min(len, d + dash);
        if (d1 <= d0) continue;
        draw::line(a.x + dx * d0 / len, a.y + dy * d0 / len, a.x + dx * d1 / len, a.y + dy * d1 / len, w, c);
    }
    draw::circle(b.x, b.y, w * 1.6f, c);
}

void WorldView::drawLinks(Sim& s, int selectedId, Pick inspected) {
    captions.clear();
    float z = cam.zoom, w = 2.6f / z + .7f;
    const Color work = draw::hex(0xa8701e), haul = draw::rgba(132, 84, 44, .9f), social = draw::hex(0x7a58aa);
    V2 stock = wp(s.stockX + 1.f, s.stockY + .5f);
    for (auto& p : s.people) {
        if (p.dead || p.hidden || !s.isExplored(p.x, p.y)) continue;
        FocusTarget t = focusTarget(s, p.focus);
        bool carrying = p.carry.has_value();
        if (!t.ok && !(p.focus.toStock && carrying)) continue;   // a felled tree is gone, but its logs still go home
        bool mine = p.id == selectedId;
        bool toInspected = inspected.ok && t.what.ok && t.what.kind == inspected.kind && t.what.id == inspected.id;
        if (!mine && !toInspected && !showAllLinks) continue;
        V2 me = wp(p.x, p.y), to = t.ok ? wp(t.x, t.y) : stock;
        Color c = p.focus.kind == Focus::PERSON ? social : work;
        float a = mine || toInspected ? 1.f : .55f;
        if (p.focus.fromStock && !carrying) {
            // on the way to fetch materials: to the stockpile first, then on to the work
            dashed(me, stock, w, draw::withA(haul, a * .85f), s.tMin, z);
            dashed(stock, to, w * .8f, draw::withA(c, a * .45f), s.tMin, z);
        } else if (p.focus.toStock && carrying) {
            dashed(me, stock, w, draw::withA(haul, a), s.tMin, z);   // bringing the haul home
        } else {
            dashed(me, to, w, draw::withA(c, a), s.tMin, z);
        }
        if (mine) {
            if (t.what.ok && t.what.kind != Drawable::PERSON) drawHighlight(s, t.what);
            captions.push_back({{(me.x + to.x) / 2, (me.y + to.y) / 2 - 6}, p.doing});
        }
    }
}

// A gold outline on the ground around what the card is describing.
void WorldView::drawHighlight(Sim& s, Pick p) {
    if (!p.ok || p.kind == Drawable::PERSON) return;
    float x = 0, y = 0, w = 1, h = 1;
    if (p.kind == Drawable::BUILDING) { auto* b = s.projectById(p.id); if (!b) return; x = b->x; y = b->y; w = b->w; h = b->h; }
    else if (p.kind == Drawable::OBJECT) { if (p.id < 0 || p.id >= int(s.objs.size())) return; x = s.objs[p.id].x; y = s.objs[p.id].y; }
    else if (p.kind == Drawable::POI) { if (p.id < 0 || p.id >= int(s.pois.size())) return; x = s.pois[p.id].x; y = s.pois[p.id].y; }
    else if (p.kind == Drawable::STOCKPILE) { x = s.stockX; y = s.stockY; w = 2; }
    else if (p.kind == Drawable::BOAT) { x = s.boatX; y = s.boatY; }
    else if (p.kind == Drawable::WOLF && s.wolf) { x = std::floor(s.wolf->x); y = std::floor(s.wolf->y); }
    V2 c[4] = {wp(x, y), wp(x + w, y), wp(x + w, y + h), wp(x, y + h)};
    draw::quad(c[0], c[1], c[2], c[3], draw::rgba(255, 210, 122, .18f), draw::rgba(255, 210, 122, .18f), draw::rgba(255, 210, 122, .18f), draw::rgba(255, 210, 122, .18f));
    for (int i = 0; i < 4; i++) draw::line(c[i].x, c[i].y, c[(i + 1) % 4].x, c[(i + 1) % 4].y, 2.2f / cam.zoom + 1, draw::hex(0xffd27a));
}

float WorldView::bodyHeight(const Person& p) { return px(p.child ? scale::CHILD : scale::PERSON) * .55f; }

int WorldView::pick(Sim& s, Vector2 screen) {
    V2 w = GetScreenToWorld2D(screen, cam);
    int best = -1;
    float bd = 18;
    for (auto& p : s.people) {
        if (p.dead || p.hidden || !s.isExplored(p.x, p.y)) continue;
        V2 pw = wp(p.x, p.y);
        float d = std::hypot(pw.x - w.x, pw.y - bodyHeight(p) - w.y);
        if (d < bd) { bd = d; best = p.id; }
    }
    return best;
}
