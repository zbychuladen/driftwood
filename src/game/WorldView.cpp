#include "game/WorldView.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include "engine/Draw.h"
#include "engine/Ui.h"
#include "game/Art.h"
#include "game/Iso.h"
#include "rlgl.h"

using namespace dw;
using draw::V2;
using iso::P;
using iso::wp;

static const Color FOG = {22, 32, 43, 255};

void WorldView::init(Sim& sim) {
    if (!ground.id) {
        ground = LoadRenderTexture(int(iso::WORLD_W), int(iso::WORLD_H));
        SetTextureFilter(ground.texture, TEXTURE_FILTER_BILINEAR);
    }
    groundSeason = -1;
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
    cam.zoom = std::clamp(cam.zoom, .35f, 3.f);
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
void WorldView::renderGround(Sim& s) {
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
    const uint32_t grass[4] = {0x689e50, 0x709640, 0x9c8e48, 0xe4e9ec};
    const uint32_t forest[4] = {0x4a7c40, 0x507434, 0x846836, 0xd6dde2};
    const uint32_t sand[4] = {0xe2ce9a, 0xe8d49e, 0xdec694, 0xe8e6e0};
    const uint32_t hill[4] = {0x7c8c60, 0x808658, 0x968054, 0xccd2d6};
    Mulberry r(s.seed + 7);
    std::vector<Color> col(N);
    waterTiles.clear();
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
        int i = Sim::idx(x, y), t = s.tt[i];
        Color c;
        if (t < 2) {
            waterTiles.push_back(i);
            float dd = s.fresh[i] ? (t == DEEP ? 1.2f : .4f) : std::clamp(distLand[i] / 4.f, 0.f, 1.4f);
            c = s.fresh[i] ? draw::rgba(int(66 - dd * 14), int(128 - dd * 24), int(130 - dd * 12)) : draw::rgba(int(78 - dd * 34), int(156 - dd * 52), int(176 - dd * 40));
            if (season == 3) c = s.fresh[i] ? draw::rgba(186, 210, 220) : draw::shade(c, .92f);
        } else {
            uint32_t base = t == SAND ? sand[season] : t == FOREST ? forest[season] : t == HILL ? hill[season] : grass[season];
            c = draw::shade(draw::hex(base), .95f + r() * .1f);
        }
        col[i] = c;
    }
    auto cornerCol = [&](int cx, int cy) {
        int rr = 0, gg = 0, bb = 0, n = 0;
        for (int dy = -1; dy <= 0; dy++) for (int dx = -1; dx <= 0; dx++) {
            int x = cx + dx, y = cy + dy;
            if (!Sim::inb(x, y)) continue;
            Color c = col[Sim::idx(x, y)];
            rr += c.r; gg += c.g; bb += c.b; n++;
        }
        return n ? Color{(unsigned char)(rr / n), (unsigned char)(gg / n), (unsigned char)(bb / n), 255} : col[0];
    };
    BeginTextureMode(ground);
    rlDisableBackfaceCulling();
    ClearBackground(Color{0, 0, 0, 0});
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
        Color c = col[Sim::idx(x, y)];
        V2 a = wp(x, y), b = wp(x + 1, y), cc = wp(x + 1, y + 1), d = wp(x, y + 1), m = wp(x + .5f, y + .5f);
        Color ca = cornerCol(x, y), cb = cornerCol(x + 1, y), ccc = cornerCol(x + 1, y + 1), cd = cornerCol(x, y + 1);
        draw::quad(m, a, b, m, c, ca, cb, c);
        draw::quad(m, b, cc, m, c, cb, ccc, c);
        draw::quad(m, cc, d, m, c, ccc, cd, c);
        draw::quad(m, d, a, m, c, cd, ca, c);
    }
    // grass blades, flowers, sand speckles, snow
    for (int k = 0; k < 26000; k++) {
        float tx = r() * GW, ty = r() * GH;
        int t = s.tt[Sim::idx(int(tx), int(ty))];
        V2 p = wp(tx, ty);
        if (t >= 3) {
            if (season == 3) { draw::rect(p.x, p.y, 1.6f, 1.2f, draw::rgba(255, 255, 255, .3f + r() * .4f)); continue; }
            Color bl = r() < .5f ? draw::rgba(30, 60, 20, .2f + r() * .15f) : draw::rgba(220, 240, 160, .14f + r() * .12f);
            draw::line(p.x, p.y, p.x + (r() - .5f) * 2, p.y - 2 - r() * 3, 1, bl);
            if (t == GRASS && r() < .04f && season <= 1) { const uint32_t fl[4] = {0xfff7d6, 0xf6d24a, 0xe98ab3, 0xc9b6f2}; draw::circle(p.x, p.y, 1.3f, draw::hex(fl[int(r() * 4) % 4])); }
            if (season == 2 && r() < .05f) { const uint32_t lv[3] = {0xc8642c, 0xe0a83a, 0xa9442a}; draw::rect(p.x, p.y, 2.4f, 1.4f, draw::hex(lv[int(r() * 3) % 3])); }
            if (t == HILL && r() < .08f) draw::ellipse(p.x, p.y, 3, 1.5f, draw::rgba(90, 90, 80, .35f));
        } else if (t == SAND) draw::rect(p.x, p.y, 1.3f, 1.1f, draw::rgba(120, 100, 60, .15f + r() * .2f));
    }
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
        if (s.tt[Sim::idx(x, y)] != HILL) continue;
        V2 c = wp(x + .5f, y + .5f);
        draw::radial(c.x - 6, c.y - 4, 26, 13, season == 3 ? draw::rgba(255, 255, 255, .35f) : draw::rgba(255, 255, 230, .18f), draw::rgba(255, 255, 230, 0));
    }
    // shore foam
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
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
    EndTextureMode();
    groundSeason = season;
    groundSeed = s.seed;
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
    auto fc = [&](int x, int y) { float v = fogCorner[y * (GW + 1) + x]; return Color{FOG.r, FOG.g, FOG.b, (unsigned char)(std::min(1.f, v * 1.15f) * 255)}; };
    for (int y = 0; y < GH; y++) for (int x = 0; x < GW; x++) {
        float a = fogCorner[y * (GW + 1) + x], b = fogCorner[y * (GW + 1) + x + 1], c = fogCorner[(y + 1) * (GW + 1) + x + 1], d = fogCorner[(y + 1) * (GW + 1) + x];
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
void WorldView::draw(Sim& s, Rectangle map, int selectedId, float realDt, float speed) {
    if (groundSeason != s.season() || groundSeed != s.seed) renderGround(s);
    lighting.resize(GetScreenWidth(), GetScreenHeight());
    ClearBackground(FOG);
    art::Ctx ctx{&s, s.tMin, s.season(), s.hour()};
    double t = s.tMin;
    V2 tl = GetScreenToWorld2D({0, 0}, cam), br = GetScreenToWorld2D({float(GetScreenWidth()), float(GetScreenHeight())}, cam);
    auto visible = [&](V2 w, float m = 90) { return w.x > tl.x - m && w.x < br.x + m && w.y > tl.y - m && w.y < br.y + m * 1.8f; };

    BeginMode2D(cam);
    DrawTextureRec(ground.texture, {0, 0, iso::WORLD_W, -iso::WORLD_H}, {0, 0}, WHITE);
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
        float wear = std::min(.45f, s.day() * .03f + .08f);
        draw::radial(c.x, c.y, 5 * iso::HW, 5 * iso::HH, draw::rgba(140, 110, 70, wear), draw::rgba(140, 110, 70, 0), 40);
    }
    for (auto& b : s.plan) if (b.placed && !b.skip && art::isFlat(b.key) && visible(wp(b.x + b.w / 2.f, b.y + b.h / 2.f), 160)) art::building(ctx, b);

    struct Item { float d; std::function<void()> f; };
    std::vector<Item> items;
    items.reserve(1200);
    for (auto& o : s.objs) {
        if (o.dead || !s.explored[Sim::idx(o.x, o.y)]) continue;
        if (!visible(wp(o.x + .5f, o.y + .5f))) continue;
        const Obj* op = &o;
        if (o.type == "tree") items.push_back({o.x + o.y + 1.f, [&ctx, op] { art::tree(ctx, *op); }});
        else if (o.type == "rock") items.push_back({o.x + o.y + 1.f, [&ctx, op] { art::rock(ctx, *op); }});
        else if (o.type == "bush") items.push_back({o.x + o.y + 1.f, [&ctx, op] { art::bush(ctx, *op); }});
        else if (o.type == "totem") items.push_back({o.x + o.y + 1.f, [&ctx, op] { art::totem(ctx, *op); }});
        else items.push_back({o.x + o.y + 1.f, [&ctx, op] { art::stump(ctx, *op); }});
    }
    for (auto& b : s.plan) if (b.placed && !b.skip && !art::isFlat(b.key)) { Project* bp = &b; items.push_back({float(b.x + b.w + b.y + b.h - 1), [&ctx, bp] { art::building(ctx, *bp); }}); }
    for (auto& p : s.pois) if (s.explored[Sim::idx(p.x, p.y)]) { const Poi* pp = &p; items.push_back({p.x + p.y + 1.f, [&ctx, pp] { art::poi(ctx, *pp); }}); }
    items.push_back({float(s.stockX + s.stockY + 2), [&ctx] { art::stockpile(ctx); }});
    items.push_back({float(s.boatX + s.boatY + 1), [&ctx] { art::boat(ctx); }});
    for (auto& p : s.people) {
        if (p.dead || !s.isExplored(p.x, p.y)) continue;
        const Person* pp = &p;
        bool sel = p.id == selectedId;
        items.push_back({p.x + p.y + .01f, [&ctx, pp, sel] { art::person(ctx, *pp, sel); }});
    }
    if (s.wolf && s.isExplored(s.wolf->x, s.wolf->y)) items.push_back({s.wolf->x + s.wolf->y, [&ctx, &s] { art::wolf(ctx, *s.wolf); }});
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.d < b.d; });
    for (auto& it : items) it.f();
    drawFog(s);
    EndMode2D();

    // screen-space atmosphere
    drawWeather(s, realDt, speed);
    double h = s.hour();
    float sw = float(GetScreenWidth()), shh = float(GetScreenHeight());
    if ((h >= 17 && h < 20) || (h >= 5.5 && h < 7.5)) draw::rect(0, 0, sw, shh, draw::rgba(255, 140, 60, .1f));
    if (s.weather == "storm" || s.weather == "rain") draw::rect(0, 0, sw, shh, draw::rgba(40, 50, 70, s.weather == "storm" ? .22f : .12f));
    float dk = darkness(h);
    if (dk > 0) {
        std::vector<Light> lights;
        auto toS = [&](V2 w) { return GetWorldToScreen2D(w, cam); };
        if (auto* f = s.fire(); f && f->fuel > 0)
            lights.push_back({toS(wp(f->x + .5f, f->y + .5f)), (130 + std::sin(float(t * .7)) * 8 + std::sin(float(t * 1.9)) * 4) * std::min(1.f, .55f + f->fuel / 6) * cam.zoom, Color{255, 150, 60, 70}});
        for (auto& b : s.plan) if (b.done && (b.key == "hut" || b.key == "longhouse" || b.key == "school" || b.key == "infirmary"))
            lights.push_back({toS(wp(b.x + b.w / 2.f, b.y + b.h / 2.f)), 55 * cam.zoom, Color{255, 170, 80, 40}});
        for (auto key : {"tower", "signal", "lighthouse", "forge"}) if (auto* b = s.built(key)) {
            float up = std::string(key) == "lighthouse" ? 85.f : std::string(key) == "tower" ? 70.f : 25.f;
            V2 w = wp(b->x + .5f, b->y + .5f); w.y -= up;
            lights.push_back({toS(w), (std::string(key) == "lighthouse" ? 170.f : 85.f) * cam.zoom, Color{255, 160, 70, 60}});
        }
        if (auto* b = s.built("shrine")) lights.push_back({toS(wp(b->x + 1.f, b->y + 1.f)), 55 * cam.zoom, Color{190, 150, 255, 50}});
        lighting.render(dk, Color{12, 18, 44, 255}, lights);
    }
    if (s.flash > 0) { draw::rect(0, 0, sw, shh, draw::rgba(230, 235, 255, s.flash * .5f)); s.flash = std::max(0.f, s.flash - realDt * 4); }
    drawLabels(s, selectedId);
}

void WorldView::drawLabels(Sim& s, int selectedId) {
    if (cam.zoom < .55f) return;
    float size = 13;
    std::map<int, int> stackAtBed;
    for (auto& p : s.people) {
        if (p.dead || !s.isExplored(p.x, p.y)) continue;
        V2 w;
        if (p.hidden) {
            auto* b = s.projectById(p.bed);
            if (!b) continue;
            w = wp(b->x + b->w / 2.f, b->y + b->h / 2.f);
            w.y -= 60 + stackAtBed[b->id]++ * 16;
        } else { w = wp(p.x, p.y); w.y += 10; }
        V2 sc = GetWorldToScreen2D(w, cam);
        if (sc.x < -60 || sc.y < -60 || sc.x > GetScreenWidth() + 60 || sc.y > GetScreenHeight() + 60) continue;
        std::string lab = p.name + (p.sleeping ? "  zz" : p.status == "hermit" ? " (hermit)" : p.status == "arriving" ? " (stranger)" : "");
        V2 m = ui::measure(ui::F.bold, lab, size);
        Rectangle r{sc.x - m.x / 2 - 6, sc.y - 8, m.x + 12, 16};
        bool sel = p.id == selectedId;
        DrawRectangleRounded(r, 1, 8, sel ? Color{255, 210, 122, 245} : p.status != "member" ? Color{80, 60, 40, 160} : Color{18, 26, 22, 160});
        ui::text(ui::F.bold, lab, r.x + 6, r.y + 1.5f, size, sel ? Color{30, 42, 35, 255} : Color{244, 241, 232, 255});
        if (p.bubble && s.tMin < p.bubble->until && !p.hidden) {
            V2 head = GetWorldToScreen2D(wp(p.x, p.y), cam);
            float by = head.y - (p.child ? 26 : 42) * cam.zoom;
            V2 bm = ui::measure(ui::F.body, p.bubble->text, 14);
            Rectangle br{head.x - bm.x / 2 - 8, by - 11, bm.x + 16, 22};
            DrawRectangleRounded(br, .8f, 8, Color{255, 255, 255, 240});
            DrawTriangle({head.x - 4, by + 10}, {head.x, by + 16}, {head.x + 4, by + 10}, Color{255, 255, 255, 240});
            ui::text(ui::F.body, p.bubble->text, br.x + 8, br.y + 3, 14, Color{30, 42, 35, 255});
        }
    }
}

int WorldView::pick(Sim& s, Vector2 screen) {
    V2 w = GetScreenToWorld2D(screen, cam);
    int best = -1;
    float bd = 18;
    for (auto& p : s.people) {
        if (p.dead || p.hidden || !s.isExplored(p.x, p.y)) continue;
        V2 pw = wp(p.x, p.y);
        float d = std::hypot(pw.x - w.x, pw.y - 14 - w.y);
        if (d < bd) { bd = d; best = p.id; }
    }
    return best;
}
