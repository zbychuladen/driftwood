#include "game/Art.h"
#include <algorithm>
#include <cmath>
#include "engine/Draw.h"
#include "game/Iso.h"
#include "game/Scale.h"

using namespace draw;
using iso::P;
using iso::wp;
using scale::px;

namespace art {

static float frand(int seed) { uint32_t x = uint32_t(seed) * 2654435761u; x ^= x >> 13; x *= 0x5bd1e995; x ^= x >> 15; return (x & 0xffff) / 65535.f; }

static void poly4(V2 a, V2 b, V2 c, V2 d, Color col) { poly({a, b, c, d}, col); }
static V2 lerpV(V2 a, V2 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
static void foot(float x, float y, float w, float h, Color c, float up = 0) { poly4(P(x, y, up), P(x + w, y, up), P(x + w, y + h, up), P(x, y + h, up), c); }

static void flame(const Ctx& c, float cx, float cy, float sc) {
    if (!c.effects) return;
    double t = c.t;
    const uint32_t cols[3] = {0xf2662a, 0xf6a531, 0xffe08a};
    for (int k = 0; k < 3; k++) {
        float ph = float(t * .4 + k * 2), hh = (12 + std::sin(ph) * 3) * sc;
        V2 a{cx - 6 * sc + k * 1.6f * sc, cy}, b{cx + 6 * sc - k * 1.6f * sc, cy};
        V2 tip{cx + std::sin(ph * 1.3f) * 1.5f, cy - hh * (1 - k * .22f)};
        auto p1 = bezier(a, {cx - 3 * sc + std::sin(ph) * 2, cy - hh * .6f}, tip, 8);
        auto p2 = bezier(tip, {cx + 3 * sc + std::sin(ph) * 2, cy - hh * .6f}, b, 8);
        p1.insert(p1.end(), p2.begin() + 1, p2.end());
        p1.insert(p1.begin(), {cx, cy - hh * .3f});
        polyV(p1, hex(cols[k]));
    }
}
static void smoke(const Ctx& c, float cx, float cy, int n = 3, float sz = 1) {
    if (!c.effects) return;
    double t = c.t;
    for (int k = 0; k < n; k++) {
        float ph = float(std::fmod(t * .015 + double(k) / n, 1.0));
        circle(cx + ph * 10 * sz, cy - ph * 34 * sz, (2 + ph * 5) * sz, rgba(210, 210, 210, .45f * (1 - ph)));
    }
}

// Box with a hip roof, the workhorse for rectangular buildings.
struct PrismOpt { bool planks = false, door = false, thatch = false; bool hasWin = false; Color win{}; };
static void prism(float x, float y, float w, float h, float wh, Color wall, Color roof, float rh, PrismOpt o = {}) {
    V2 L = P(x, y + h), B = P(x + w, y + h), Rr = P(x + w, y), Lu = P(x, y + h, wh), Bu = P(x + w, y + h, wh), Ru = P(x + w, y, wh), Tu = P(x, y, wh);
    poly4(L, B, Bu, Lu, shade(wall, .74f));
    poly4(B, Rr, Ru, Bu, wall);
    if (o.planks)
        for (int i = 1; i < 4; i++) {
            float f = i / 4.f;
            V2 a = lerpV(L, Lu, f), b = lerpV(B, Bu, f), c = lerpV(Rr, Ru, f);
            line(a.x, a.y, b.x, b.y, .8f, rgba(50, 30, 15, .35f));
            line(b.x, b.y, c.x, c.y, .8f, rgba(50, 30, 15, .35f));
        }
    if (o.door) {
        V2 a = lerpV(L, B, .4f), b = lerpV(L, B, .6f);
        float dh = doorHeight(wh);
        poly4(a, b, {b.x, b.y - dh}, {a.x, a.y - dh}, hex(0x2e2116));
    }
    if (o.hasWin) {
        V2 a = lerpV(B, Rr, .4f), b = lerpV(B, Rr, .6f);
        a.y -= wh * .35f; b.y -= wh * .35f;
        poly4(a, b, {b.x, b.y - wh * .3f}, {a.x, a.y - wh * .3f}, o.win);
    }
    if (w >= h) {
        V2 r1 = P(x + h / 2, y + h / 2, wh + rh), r2 = P(x + w - h / 2, y + h / 2, wh + rh);
        poly4(Tu, Ru, r2, r1, shade(roof, .72f));
        tri(Tu, Lu, r1, shade(roof, .84f));
        poly4(Lu, Bu, r2, r1, shade(roof, .95f));
        tri(Ru, Bu, r2, shade(roof, 1.1f));
        if (o.thatch)
            for (int i = 1; i < 6; i++) {
                float f = i / 6.f;
                V2 a = lerpV(Lu, Bu, f), b = lerpV(r1, r2, f);
                line(a.x, a.y, b.x, b.y, .8f, rgba(90, 60, 20, .35f));
            }
    } else {
        V2 r1 = P(x + w / 2, y + w / 2, wh + rh), r2 = P(x + w / 2, y + h - w / 2, wh + rh);
        poly4(Tu, Lu, r2, r1, shade(roof, .78f));
        tri(Tu, Ru, r1, shade(roof, .92f));
        tri(Lu, Bu, r2, shade(roof, .9f));
        poly4(Ru, Bu, r2, r1, shade(roof, 1.1f));
    }
}

float doorHeight(float wallPx) { return std::min(px(scale::DOOR), wallPx - px(.25f)); }

static V2 roundHut(float x, float y, float w, float h, float wh, float rh, bool snow) {
    V2 c = P(x + w / 2, y + h / 2);
    float rx = w * iso::HW * .62f, ry = rx * .5f;
    ellipse(c.x + 6, c.y + 3, rx * 1.15f, ry * 1.1f, rgba(30, 30, 20, .28f));
    std::vector<V2> wall = {{c.x - rx, c.y - wh}, {c.x + rx, c.y - wh}};
    auto bottom = arc(c.x, c.y, rx, ry, 0, 3.14159f, 16);
    wall.insert(wall.end(), bottom.begin(), bottom.end());
    polyGradX(wall, c.x - rx, c.x + rx, hex(0x7c6646), hex(0xb89b6c));
    float dh = doorHeight(wh);
    ellipse(c.x - rx * .35f, c.y + ry * .75f - dh / 2, 7, dh / 2, hex(0x2e2116));
    float R2 = rx * 1.22f, ay = c.y - wh - rh, by = c.y - wh + 2;
    std::vector<V2> cone = {{c.x, ay}};
    auto rim = arc(c.x, by, R2, R2 * .5f, 3.14159f, 0, 18);   // left, through the front, to right
    cone.insert(cone.end(), rim.begin(), rim.end());
    polyGradX(cone, c.x - R2, c.x + R2, snow ? hex(0xc9d2d8) : hex(0xa7843c), snow ? hex(0xf4f7f9) : hex(0xe2c06a));
    Color st = snow ? rgba(140, 160, 170, .5f) : rgba(110, 80, 30, .45f);
    for (int i = -4; i <= 4; i++) {
        float fx = i / 4.6f;
        line(c.x + fx * R2, by + std::sqrt(std::max(0.f, 1 - fx * fx)) * R2 * .5f, c.x + i * 1.2f, ay + 4, .9f, st);
    }
    return c;
}

void tree(const Ctx& c, const dw::Obj& o) {
    int s = c.season;
    V2 p = wp(o.x + .5f, o.y + .6f);
    // the art below is drawn k units tall per unit of its natural height
    bool bare = !o.palm && !o.conifer && s == 3;
    float H = o.palm ? scale::PALM : o.conifer ? scale::CONIFER : scale::BROADLEAF;
    float natural = o.palm ? 33.f : o.conifer ? 40.f : bare ? 29.f : 36.f;
    float x = p.x, y = p.y, k = px(H) / natural * o.size * (o.sapling ? .35f + o.grow * .6f : 1);
    float sway = std::sin(float(c.t * .02 + o.i)) * 1.2f * k * (c.sim->weather == "storm" ? 3 : 1);
    ellipse(x + 6 * k, y + 1, 12 * k, 5 * k, rgba(20, 40, 20, .25f));
    if (o.palm) {
        V2 top{x + 2 * k + sway, y - 26 * k};
        curve({x, y}, {x + 4 * k, y - 14 * k}, top, 3 * k, hex(0x8a6a43));
        Color leaf = s == 3 ? hex(0x7f9a74) : hex(0x4f8f4a);
        for (int a = 0; a < 6; a++) {
            float an = a / 6.f * 6.283f + .3f;
            V2 tip{top.x + std::cos(an) * 16 * k, top.y + std::sin(an) * 7 * k + 3};
            V2 mid{top.x + std::cos(an) * 8 * k, top.y + std::sin(an) * 4 * k - 2};
            float nx = -std::sin(an) * 2.5f * k, ny = std::cos(an) * 1.2f * k;
            poly({top, {mid.x + nx, mid.y + ny}, tip, {mid.x - nx, mid.y - ny}}, leaf);
        }
        return;
    }
    rect(x - 2 * k, y - 12 * k, 4 * k, 12 * k, hex(0x6b4a2e));
    rect(x - 2 * k, y - 12 * k, 1.5f * k, 12 * k, hex(0x553a24));
    if (o.conifer) {
        uint32_t cols[3] = {0x2f5d3a, 0x3b6f45, 0x4a8152};
        if (s == 3) { cols[0] = 0x3e5b4a; cols[1] = 0x4d6b58; cols[2] = 0x5c7a66; }
        for (int l = 0; l < 3; l++) {
            float w = (13 - l * 3.2f) * k, yy = y - 10 * k - l * 8 * k, sx = sway * .3f * l, tx = sway * (.4f + l * .3f);
            tri({x - w + sx, yy}, {x + tx, yy - 14 * k}, {x + w + sx, yy}, hex(cols[l]));
            tri({x - w + sx, yy}, {x + tx, yy - 14 * k}, {x - w * .2f + sx, yy}, rgba(0, 0, 0, .12f));
            if (s == 3) tri({x - w * .45f + sx, yy - 7 * k}, {x + tx, yy - 14 * k}, {x + w * .45f + sx, yy - 7 * k}, rgba(255, 255, 255, .85f));
        }
        return;
    }
    if (s == 3) {
        for (int b = 0; b < 5; b++) {
            float an = -1.5708f + (b - 2) * .45f;
            line(x, y - 11 * k, x + std::cos(an) * 13 * k + sway, y - 11 * k + std::sin(an) * 15 * k, 1.6f * k, hex(0x5b4330));
        }
        circle(x + sway, y - 24 * k, 5 * k, rgba(255, 255, 255, .7f));
        return;
    }
    uint32_t pal[3];
    if (s == 2) { if (o.i % 3 == 0) { pal[0] = 0xa8442a; pal[1] = 0xc8642c; pal[2] = 0xe0884a; } else { pal[0] = 0xb47a22; pal[1] = 0xd39a2e; pal[2] = 0xecc054; } }
    else if (s == 1) { pal[0] = 0x3d6e34; pal[1] = 0x4f8740; pal[2] = 0x68a052; }
    else { pal[0] = 0x4a8a3e; pal[1] = 0x5fa04a; pal[2] = 0x7dba5e; }
    float cx = x + sway, cy = y - 22 * k;
    circle(cx - 6 * k, cy + 3 * k, 9 * k, hex(pal[0]));
    circle(cx + 6 * k, cy + 3 * k, 9 * k, hex(pal[0]));
    circle(cx, cy - 3 * k, 11 * k, hex(pal[1]));
    circle(cx + 3 * k, cy - 6 * k, 6 * k, hex(pal[2]));
    circle(cx - 5 * k, cy + 5 * k, 5 * k, rgba(0, 0, 0, .1f));
    if (s == 0 && o.i % 4 == 0) for (int b = 0; b < 5; b++) circle(cx + std::cos(b * 1.3f) * 8 * k, cy + std::sin(b * 1.7f) * 6 * k, 1.4f * k, hex(0xf6d6e4));
}

void stump(const Ctx&, const dw::Obj& o) {
    V2 p = wp(o.x + .5f, o.y + .6f);
    ellipse(p.x, p.y, 6, 3, hex(0x5a3d25));
    ellipse(p.x, p.y - 2, 5.5f, 2.6f, hex(0xb48a5a));
    ellipse(p.x, p.y - 2, 2.4f, 1.2f, hex(0x8a6640));
}

void rock(const Ctx& c, const dw::Obj& o) {
    V2 p = wp(o.x + .5f, o.y + .6f);
    float x = p.x, y = p.y, k = px(scale::ROCK) / 12.f * o.size;
    bool sn = c.season == 3;
    ellipse(x + 3, y + 1, 11 * k, 4.5f * k, rgba(20, 30, 20, .25f));
    poly({{x - 9 * k, y}, {x - 7 * k, y - 8 * k}, {x - 1 * k, y - 12 * k}, {x + 6 * k, y - 9 * k}, {x + 9 * k, y - 2 * k}, {x + 5 * k, y + 1 * k}}, sn ? hex(0x9aa1a5) : hex(0x8a8d86));
    poly({{x - 1 * k, y - 12 * k}, {x + 6 * k, y - 9 * k}, {x + 9 * k, y - 2 * k}, {x + 5 * k, y + 1 * k}, {x + 1 * k, y - 5 * k}}, sn ? hex(0xb6bcc0) : hex(0xa2a59e));
    if (sn) ellipse(x - 1 * k, y - 10 * k, 5 * k, 2 * k, rgba(255, 255, 255, .9f));
}

void bush(const Ctx& c, const dw::Obj& o) {
    V2 p = wp(o.x + .5f, o.y + .6f);
    float x = p.x, y = p.y;
    int s = c.season;
    push(p, px(scale::BUSH) / 17.f);
    ellipse(x + 3, y + 1, 10, 4, rgba(20, 40, 20, .25f));
    uint32_t c0 = s == 3 ? 0x5f6b5c : s == 2 ? 0x6e7a34 : 0x3f7a3a, c1 = s == 3 ? 0x76826f : s == 2 ? 0x8a9440 : 0x55964a;
    circle(x - 5, y - 5, 7, hex(c0));
    circle(x + 5, y - 5, 7, hex(c0));
    circle(x, y - 9, 8, hex(c1));
    if (s == 3) { circle(x, y - 13, 4.5f, rgba(255, 255, 255, .8f)); pop(); return; }
    int n = int(o.berries);
    for (int b = 0; b < n * 2; b++) circle(x + std::cos(b * 2.4f + o.i) * 6, y - 8 + std::sin(b * 1.9f + o.i) * 4, 1.8f, s == 0 ? hex(0xe8eef8) : hex(0xc0303f));
    pop();
}

void totem(const Ctx& c, const dw::Obj& o) {
    V2 p = wp(o.x + .5f, o.y + .6f);
    float x = p.x, y = p.y, hgt = px(scale::TOTEM) + o.v * px(.3f);
    ellipse(x + 4, y + 1, 8, 3.5f, rgba(20, 30, 20, .28f));
    const uint32_t cols[4][2] = {{0x8a5e38, 0xb07a48}, {0x7a5434, 0xa8703f}, {0x946640, 0xc08a55}, {0x6e4b2e, 0x9a6a3e}};
    rect(x - 4, y - hgt, 8, hgt, hex(cols[o.v][0]));
    rect(x, y - hgt, 4, hgt, hex(cols[o.v][1]));
    const uint32_t fc[4] = {0xc8553d, 0x2fa39b, 0xe0b13a, 0x3d7ec8};
    for (int k = 0; k < 3; k++) {
        float fy = y - 8 - k * (hgt / 3.4f);
        rect(x - 4, fy - 4, 8, 2, hex(fc[(o.v + k) % 4]));
        circle(x - 1.8f, fy - 6.5f, 1.1f, hex(0x1d1d1d));
        circle(x + 1.8f, fy - 6.5f, 1.1f, hex(0x1d1d1d));
        rect(x - 2, fy - 2.5f, 4, 1.2f, hex(0x2a1c12));
    }
    poly({{x - 11, y - hgt + 4}, {x, y - hgt - 2}, {x + 11, y - hgt + 4}, {x, y - hgt + 1}}, hex(fc[o.v]));
    if (c.season == 3) ellipse(x, y - hgt - 1, 4, 1.6f, rgba(255, 255, 255, .9f));
}

void boat(const Ctx& c) {
    V2 p = wp(c.sim->boatX + .5f, c.sim->boatY + .5f);
    float x = p.x, y = p.y;
    push(p, scale::BOAT_LENGTH * scale::GROUND_PX_PER_M / 38.f);
    ellipse(x, y + 3, 20, 6, rgba(20, 30, 20, .25f));
    poly({{x - 20, y - 3}, {x + 18, y - 8}, {x + 14, y + 2}, {x - 14, y + 6}}, hex(0x7a5434));
    poly({{x - 20, y - 3}, {x + 18, y - 8}, {x + 16, y - 11}, {x - 17, y - 6}}, hex(0x5a3c22));
    line(x - 5, y - 6, x - 3, y + 3, 1, hex(0x3e2a18));
    line(x + 6, y - 8, x + 7, y + 1, 1, hex(0x3e2a18));
    pop();
}

void stockpile(const Ctx& c) {
    auto& s = *c.sim;
    foot(s.stockX, s.stockY, 2, 1, rgba(120, 90, 50, .35f));
    int logs = std::min(10, int(std::ceil(s.stock.wood / 4)));
    for (int i = 0; i < logs; i++) {
        int row = i < 4 ? 0 : i < 7 ? 1 : i < 9 ? 2 : 3, col = i < 4 ? i : i < 7 ? i - 4 : i < 9 ? i - 7 : 0;
        V2 l = P(s.stockX + .2f + col * .18f + row * .09f, s.stockY + .75f, row * 4.5f);
        push(l, 1.6f);
        poly({{l.x - 7, l.y - 3}, {l.x + 5, l.y - 8}, {l.x + 7, l.y - 5}, {l.x - 5, l.y}}, hex(0x8a5e38));
        ellipse(l.x - 6, l.y - 1.5f, 2.4f, 2.8f, hex(0xc79a62));
        pop();
    }
    int stones = std::min(8, int(std::ceil(s.stock.stone / 4)));
    for (int i = 0; i < stones; i++) { V2 q = P(s.stockX + 1.25f + (i % 3) * .22f, s.stockY + .35f + (i / 3) * .2f, (i / 3) * 2.f); circle(q.x, q.y, 3.6f, i % 2 ? hex(0x9a9d96) : hex(0x7e817a)); }
    int baskets = std::min(5, int(std::ceil(s.stock.food / 12)));
    for (int i = 0; i < baskets; i++) {
        V2 b = P(s.stockX + 1.7f, s.stockY + .15f + i * .17f);
        poly({{b.x - 4, b.y - 6}, {b.x + 4, b.y - 6}, {b.x + 3, b.y}, {b.x - 3, b.y}}, hex(0xb38a4e));
        circle(b.x - 1.5f, b.y - 6.5f, 1.6f, hex(0xc0303f));
        circle(b.x + 1.5f, b.y - 6.5f, 1.6f, hex(0xd88a2c));
    }
}

bool isFlat(const std::string& k) { return k == "garden" || k == "dock" || k == "herbs" || k == "bath" || k == "fields"; }

void building(const Ctx& c, dw::Project& b) {
    if (!b.placed || b.skip) return;
    auto& S = *c.sim;
    float prog = b.done ? 1 : b.progress / b.work;
    int s = c.season;
    bool sn = s == 3 && b.done;
    float x = b.x, y = b.y, w = b.w, h = b.h;
    double t = c.t;
    bool night = c.hour >= 19 || c.hour < 6;
    float savedA = draw::alpha;
    if (!b.done) {
        if (b.key != "dock") {
            V2 cs[4] = {P(x, y), P(x + w, y), P(x + w, y + h), P(x, y + h)};
            for (int i = 0; i < 4; i++) {
                V2 a = cs[i], e = cs[(i + 1) % 4];
                for (int d = 0; d < 8; d += 2) { V2 p0 = lerpV(a, e, d / 8.f), p1 = lerpV(a, e, (d + 1) / 8.f); line(p0.x, p0.y, p1.x, p1.y, 1, rgba(60, 40, 20, .6f)); }
            }
        }
        for (int i = 0; i < std::min(5, int(std::ceil(b.dw / 6))); i++) { V2 q = P(x + w - .15f, y + h - .3f - i * .15f); poly({{q.x - 6, q.y - 2}, {q.x + 3, q.y - 6}, {q.x + 5, q.y - 4}, {q.x - 4, q.y}}, hex(0x8a5e38)); }
        for (int i = 0; i < std::min(4, int(std::ceil(b.ds / 6))); i++) { V2 q = P(x + w - .3f, y + .2f + i * .15f); circle(q.x, q.y, 3, hex(0x8a8d86)); }
        draw::alpha = savedA * (.22f + prog * .78f);
    }
    const std::string& k = b.key;
    if (k == "fire") {
        V2 q = P(x + .5f, y + .5f);
        for (int a = 0; a < 9; a++) circle(q.x + std::cos(a / 9.f * 6.283f) * 10, q.y + std::sin(a / 9.f * 6.283f) * 5, 3, hex(0x7d7a72));
        poly({{q.x - 8, q.y - 1}, {q.x + 8, q.y + 2}, {q.x + 8, q.y + 4}, {q.x - 8, q.y + 1}}, hex(0x5a3c22));
        poly({{q.x - 6, q.y + 3}, {q.x + 6, q.y - 3}, {q.x + 7, q.y - 1}, {q.x - 5, q.y + 5}}, hex(0x4a3020));
        if (b.done && b.fuel > 0) flame(c, q.x, q.y + 1, std::min(1.f, b.fuel / 3) * .5f + .6f);
        else if (b.done) smoke(c, q.x, q.y, 2, .6f);
    } else if (k == "leanto") {
        foot(x, y, w, h, rgba(40, 30, 20, .25f));
        float hi = px(scale::LEANTO);
        V2 A = P(x, y, hi), B2 = P(x + w, y, hi), C = P(x + w, y + h, 2), D = P(x, y + h, 2);
        for (V2 p : {P(x, y), P(x + w, y)}) line(p.x, p.y, p.x, p.y - hi, 2, hex(0x5a3c22));
        poly4(A, B2, C, D, sn ? hex(0xdfe5e8) : s == 2 ? hex(0x8a8a3a) : hex(0x5a8a43));
        for (int i = 0; i <= 8; i++) { float f = i / 8.f; V2 p0 = lerpV(A, B2, f), p1 = lerpV(D, C, f); line(p0.x, p0.y, p1.x, p1.y, 1.2f, rgba(50, 30, 15, .5f)); }
    } else if (k == "rack") {
        V2 q = P(x + .5f, y + .5f);
        push(q, px(scale::RACK) / 28.f);
        line(q.x - 12, q.y + 2, q.x - 11, q.y - 22, 2.4f, hex(0x6b4a2e));
        line(q.x + 12, q.y - 4, q.x + 11, q.y - 28, 2.4f, hex(0x6b4a2e));
        line(q.x - 13, q.y - 21, q.x + 13, q.y - 27, 2.4f, hex(0x6b4a2e));
        for (int i = 0; i < 5; i++) { float f = i / 4.f; rect(q.x - 9 + f * 18, q.y - 21 - f * 6, 2.4f, 10 + std::sin(float(t * .03 + i)) * .6f, i % 2 ? hex(0xc98a5a) : hex(0xb0704a)); }
        pop();
    } else if (k == "hut") {
        V2 q = roundHut(x, y, w, h, px(scale::HUT_WALL), px(scale::HUT_ROOF), sn);
        bool occupied = false;
        for (auto* m : S.members()) if (m->bed == b.id) occupied = true;
        if (b.done && night && occupied) circle(q.x + 16, q.y - 10, 2.5f, rgba(255, 200, 110, .95f));
    } else if (k == "garden" || k == "herbs" || k == "fields") {
        foot(x, y, w, h, hex(0x6a4a2e));
        for (int r = 1; r < h * 3; r++) { V2 a = P(x + .1f, y + r / 3.f), e = P(x + w - .1f, y + r / 3.f); line(a.x, a.y, e.x, e.y, 1.5f, hex(0x4f3820)); }
        V2 cs[4] = {P(x, y), P(x + w, y), P(x + w, y + h), P(x, y + h)};
        for (int i = 0; i < 4; i++) line(cs[i].x, cs[i].y, cs[(i + 1) % 4].x, cs[(i + 1) % 4].y, 2, hex(0x8a6640));
        if (k == "herbs" && b.done) {
            for (int i = 0; i < 14; i++) {
                V2 q = P(x + .2f + (i % 7) * (w - .4f) / 6, y + .3f + (i / 7) * .45f);
                circle(q.x, q.y - 2, 3, s == 3 ? hex(0x9aa59a) : hex(0x4f8a4a));
                if (s < 2) circle(q.x + 1, q.y - 4, 1.3f, i % 2 ? hex(0xc9b6f2) : hex(0xf6f1c4));
            }
        } else if (b.done && b.state != "empty") {
            float gr = b.state == "ripe" ? 1 : b.growth;
            for (int r = 0; r < h * 3 - 1; r++) for (int cc = 0; cc < w * 3; cc++) {
                V2 q = P(x + .2f + cc * (w - .4f) / (w * 3 - 1), y + (r + 1) / 3.f);
                if (k == "fields") {
                    Color st = b.state == "ripe" ? hex(0xd9b44a) : gr > .6f ? hex(0xa9b84a) : hex(0x6faa4a);
                    float sw = std::sin(float(t * .03 + cc + r)) * 1.2f;
                    line(q.x, q.y, q.x - 1 + sw, q.y - 3 - gr * 7, 1.4f, st);
                    line(q.x + 2, q.y, q.x + 3 + sw, q.y - 2 - gr * 6, 1.4f, st);
                } else {
                    circle(q.x, q.y - 1, 1 + gr * 3, hex(0x5fa04a));
                    if (b.state == "ripe" && (r + cc) % 2) circle(q.x + 1, q.y, 2.5f, (r + cc) % 4 == 1 ? hex(0xe0893a) : hex(0xd8b84a));
                }
            }
        }
        if (s == 3 && b.done) foot(x, y, w, h, rgba(255, 255, 255, .55f));
    } else if (k == "well") {
        V2 q = P(x + .5f, y + .5f);
        float cu = px(scale::WELL_CURB), top = px(scale::WELL_ROOF), r = 16;
        ellipse(q.x + 3, q.y + 3, r + 2, r / 2 + 1, rgba(30, 30, 20, .25f));
        ellipse(q.x, q.y, r, r / 2, hex(0x8a8d86));
        rect(q.x - r, q.y - cu, r * 2, cu, hex(0x8a8d86));
        ellipse(q.x, q.y - cu, r, r / 2, hex(0xa3a69f));
        ellipse(q.x, q.y - cu, r * .7f, r * .33f, hex(0x26404c));
        line(q.x - r + 2, q.y - cu + 1, q.x - r + 2, q.y - top + 9, 2, hex(0x6b4a2e));
        line(q.x + r - 2, q.y - cu + 1, q.x + r - 2, q.y - top + 9, 2, hex(0x6b4a2e));
        poly({{q.x - r - 4, q.y - top + 10}, {q.x, q.y - top}, {q.x + r + 4, q.y - top + 10}, {q.x, q.y - top + 16}}, sn ? hex(0xe8edf0) : hex(0x8a5e38));
    } else if (k == "store") {
        PrismOpt o; o.planks = true; o.door = true;
        prism(x + .1f, y + .1f, w - .2f, h - .2f, px(scale::STORE_WALL), hex(0x8a5e38), sn ? hex(0xe4e9ec) : hex(0x6b4a2e), px(scale::STORE_ROOF), o);
    } else if (k == "smoke") {
        PrismOpt o; o.planks = true; o.door = true;
        prism(x + .1f, y + .1f, w - .2f, h - .2f, px(scale::SMOKE_WALL), hex(0x7a5a3a), sn ? hex(0xe4e9ec) : hex(0x5a4030), px(scale::SMOKE_ROOF), o);
        if (b.done) { V2 q = P(x + w / 2, y + h / 2, px(scale::SMOKE_WALL + scale::SMOKE_ROOF)); smoke(c, q.x, q.y, 4, 1); }
    } else if (k == "longhouse") {
        PrismOpt o; o.planks = true; o.door = true; o.thatch = true; o.hasWin = true;
        o.win = b.done && (c.hour >= 18 || c.hour < 6) ? rgba(255, 200, 110, .95f) : hex(0x3a2a1c);
        prism(x + .1f, y + .15f, w - .2f, h - .3f, px(scale::LONGHOUSE_WALL), hex(0x9a7a52), sn ? hex(0xe8edf0) : hex(0xc9a352), px(scale::LONGHOUSE_ROOF), o);
        if (b.done && (c.hour >= 17 || c.hour < 8)) { V2 q = P(x + w * .7f, y + h / 2, px(scale::LONGHOUSE_WALL + scale::LONGHOUSE_ROOF)); smoke(c, q.x, q.y, 3, 1); }
    } else if (k == "school") {
        PrismOpt o; o.planks = true; o.door = true; o.hasWin = true; o.win = hex(0x3a2a1c);
        prism(x + .15f, y + .15f, w - .3f, h - .3f, px(scale::SCHOOL_WALL), hex(0xb98d5e), sn ? hex(0xe8edf0) : hex(0x8a4a3a), px(scale::SCHOOL_ROOF), o);
    } else if (k == "forge") {
        PrismOpt o; o.door = true; o.hasWin = true; o.win = b.done ? rgba(255, 140, 40, .95f) : hex(0x2e2116);
        prism(x + .15f, y + .15f, w - .3f, h - .3f, px(scale::FORGE_WALL), hex(0x8d8f88), sn ? hex(0xe4e9ec) : hex(0x5a4a3e), px(scale::FORGE_ROOF), o);
        // chimney from inside the roof up to its full height
        V2 q = P(x + w * .7f, y + h * .3f, px(scale::FORGE_WALL));
        float ch = px(scale::FORGE_CHIMNEY - scale::FORGE_WALL);
        rect(q.x - 5, q.y - ch, 10, ch + 4, hex(0x6d6f69));
        if (b.done) {
            smoke(c, q.x, q.y - ch - 2, 4, 1);
            V2 a = P(x + w + .4f, y + h * .55f);
            rect(a.x - 6, a.y - 8, 12, 3, hex(0x333333));
            rect(a.x - 2, a.y - 5, 4, 5, hex(0x333333));
            if (std::sin(t * 1.3) > .8) for (int i = 0; i < 3; i++) circle(a.x + (frand(int(t) + i) - .5f) * 10, a.y - 10 - frand(int(t) * 3 + i) * 6, 1, hex(0xffb347));
        }
    } else if (k == "mine") {
        V2 q = P(x + .5f, y + .5f);
        push(q, px(scale::MINE_HILL) / 26.f);
        poly({{q.x - 22, q.y + 4}, {q.x - 15, q.y - 14}, {q.x, q.y - 26}, {q.x + 17, q.y - 14}, {q.x + 22, q.y + 4}}, s == 3 ? hex(0xa4aaae) : hex(0x7d7f78));
        poly({{q.x, q.y - 26}, {q.x + 17, q.y - 14}, {q.x + 22, q.y + 4}, {q.x + 4, q.y + 4}}, s == 3 ? hex(0xb9bfc2) : hex(0x8f918a));
        rect(q.x - 8, q.y - 14, 16, 17, hex(0x1d1b18));
        rect(q.x - 10, q.y - 16, 3, 19, hex(0x6b4a2e));
        rect(q.x + 7, q.y - 16, 3, 19, hex(0x6b4a2e));
        rect(q.x - 11, q.y - 18, 22, 3, hex(0x6b4a2e));
        pop();
        V2 kc = P(x + 1.25f, y + .9f);
        push(kc, 1.6f);
        rect(kc.x - 6, kc.y - 8, 12, 6, hex(0x5a3c22));
        circle(kc.x - 4, kc.y - 1, 2, hex(0x222222)); circle(kc.x + 4, kc.y - 1, 2, hex(0x222222));
        circle(kc.x - 2, kc.y - 9, 2.2f, hex(0x9a9d96)); circle(kc.x + 2, kc.y - 9.5f, 2, hex(0x8a8d86));
        pop();
    } else if (k == "pottery") {
        foot(x + .1f, y + .1f, w - .2f, h - .2f, rgba(150, 110, 70, .35f));
        V2 q = P(x + .9f, y + .9f);
        ellipse(q.x + 4, q.y + 3, 20, 8, rgba(30, 30, 20, .25f));
        auto dome = bezier({q.x - 18, q.y}, {q.x - 18, q.y - 34}, {q.x, q.y - 34}, 10);
        auto d2 = bezier({q.x, q.y - 34}, {q.x + 18, q.y - 34}, {q.x + 18, q.y}, 10);
        dome.insert(dome.end(), d2.begin() + 1, d2.end());
        auto base = arc(q.x, q.y, 18, 8, 0, 3.14159f, 12);
        dome.insert(dome.end(), base.begin(), base.end());
        dome.insert(dome.begin(), {q.x, q.y - 14});
        polyGradX(dome, q.x - 18, q.x + 18, hex(0x8e4e2c), hex(0xc47a4a));
        auto mouth = arc(q.x - 6, q.y + 2, 5, 6, 3.14159f, 6.283f, 10);
        mouth.insert(mouth.begin(), {q.x - 6, q.y});
        polyV(mouth, b.done ? rgba(255, 130, 40, .9f) : hex(0x2e2116));
        if (b.done) smoke(c, q.x, q.y - 36, 3, .8f);
        const uint32_t pc[3] = {0xb8683e, 0xa85a34, 0xc98a5a};
        for (int i = 0; i < 3; i++) { V2 pp = P(x + 1.5f + i * .15f, y + 1.6f - i * .2f); ellipse(pp.x, pp.y - 4, 3.5f, 5, hex(pc[i])); rect(pp.x - 2, pp.y - 10, 4, 2, hex(pc[i])); }
    } else if (k == "loomhouse") {
        PrismOpt o; o.door = true; o.planks = true;
        prism(x + .15f, y + .15f, w - .3f, h - .3f, px(scale::LOOM_WALL), hex(0xa88a5e), sn ? hex(0xe8edf0) : hex(0x7a5a8a), px(scale::LOOM_ROOF), o);
        if (b.done) { const uint32_t cl[4] = {0xc8553d, 0xe0b13a, 0x3d7ec8, 0x2fa39b}; for (int i = 0; i < 4; i++) { V2 q = P(x + w + .15f, y + .3f + i * .38f); rect(q.x - 2, q.y - 18, 4, 14 + std::sin(float(t * .04 + i)) * 1.5f, hex(cl[i])); } }
    } else if (k == "infirmary") {
        PrismOpt o; o.door = true; o.hasWin = true; o.win = hex(0x4f8a4a);
        prism(x + .15f, y + .15f, w - .3f, h - .3f, px(scale::INFIRMARY_WALL), hex(0xd6cfbd), sn ? hex(0xe8edf0) : hex(0x6f8a5e), px(scale::INFIRMARY_ROOF), o);
    } else if (k == "dock") {
        for (int i = 0; i < 3; i++) {
            float tx = x + b.dx * i, ty = y + b.dy * i;
            foot(tx + .1f, ty + .1f, .8f, .8f, rgba(20, 40, 60, .3f));
            foot(tx + .1f, ty + .1f, .8f, .8f, hex(0x9a7448), 4);
            for (V2 pp : {P(tx + .15f, ty + .15f), P(tx + .85f, ty + .85f)}) rect(pp.x - 1.5f, pp.y - 4, 3, 8, hex(0x5a3c22));
        }
    } else if (k == "tower") {
        V2 q = P(x + .5f, y + .5f);
        Color wd = hex(0x6b4a2e);
        push(q, px(scale::TOWER) / 92.f);
        ellipse(q.x + 4, q.y + 2, 12, 5, rgba(30, 30, 20, .28f));
        line(q.x - 10, q.y, q.x - 7, q.y - 64, 2.6f, wd); line(q.x + 10, q.y, q.x + 7, q.y - 64, 2.6f, wd); line(q.x, q.y + 5, q.x, q.y - 62, 2.6f, wd);
        for (int i = 0; i < 5; i++) line(q.x - 10 + i * .6f, q.y - i * 12, q.x + 9 - i * .6f, q.y - i * 12 - 12, 1.2f, wd);
        poly({{q.x - 14, q.y - 64}, {q.x, q.y - 70}, {q.x + 14, q.y - 64}, {q.x, q.y - 58}}, hex(0x8a5e38));
        poly({{q.x - 15, q.y - 78}, {q.x, q.y - 92}, {q.x + 15, q.y - 78}, {q.x, q.y - 72}}, sn ? hex(0xe8edf0) : wd);
        line(q.x - 12, q.y - 65, q.x - 12, q.y - 78, 1.2f, wd); line(q.x + 12, q.y - 65, q.x + 12, q.y - 78, 1.2f, wd);
        if (b.done && night) circle(q.x + 10, q.y - 72, 2.5f, hex(0xffd27a));
        pop();
    } else if (k == "signal") {
        V2 q = P(x + .5f, y + .5f);
        push(q, px(scale::SIGNAL) / 26.f);
        ellipse(q.x + 3, q.y + 2, 14, 6, rgba(30, 30, 20, .28f));
        for (int l = 0; l < 5; l++) { float ww = 14 - l * 2.4f; poly({{q.x - ww, q.y - l * 5}, {q.x + ww, q.y - l * 5 - 3}, {q.x + ww, q.y - l * 5 - 6}, {q.x - ww, q.y - l * 5 - 3}}, l % 2 ? hex(0x6b4a2e) : hex(0x8a5e38)); }
        if (b.done) { flame(c, q.x, q.y - 24, 1.3f); smoke(c, q.x, q.y - 36, 5, 1.6f); }
        pop();
    } else if (k == "lighthouse") {
        V2 q = P(x + .5f, y + .5f);
        push(q, px(scale::LIGHTHOUSE) / 103.f);
        ellipse(q.x + 4, q.y + 2, 14, 6, rgba(30, 30, 20, .28f));
        poly({{q.x - 11, q.y}, {q.x - 7, q.y - 80}, {q.x + 7, q.y - 80}, {q.x + 11, q.y}}, hex(0xe9e6dc));
        rect(q.x - 9.5f, q.y - 30, 19, 8, hex(0xb8434a));
        rect(q.x - 8.2f, q.y - 58, 16.4f, 8, hex(0xb8434a));
        poly({{q.x - 11, q.y}, {q.x - 7, q.y - 80}, {q.x - 2, q.y - 80}, {q.x - 4, q.y}}, rgba(0, 0, 0, .12f));
        rect(q.x - 9, q.y - 84, 18, 4, hex(0x3a3a3a));
        circle(q.x, q.y - 89, 6, b.done ? hex(0xffd27a) : hex(0x666666));
        tri({q.x - 9, q.y - 94}, {q.x, q.y - 103}, {q.x + 9, q.y - 94}, hex(0x3a3a3a));
        pop();
    } else if (k == "shrine") {
        std::vector<std::pair<float, float>> st = {{.3f, .3f}, {1.7f, .3f}, {1.7f, 1.7f}, {.3f, 1.7f}, {1, .15f}, {1.85f, 1}, {1, 1.85f}, {.15f, 1}};
        std::sort(st.begin(), st.end(), [](auto& a, auto& b2) { return a.first + a.second < b2.first + b2.second; });
        foot(x + .3f, y + .3f, 1.4f, 1.4f, s == 3 ? rgba(255, 255, 255, .4f) : rgba(120, 140, 100, .4f));
        for (auto [sx, sy] : st) {
            V2 q = P(x + sx, y + sy);
            push(q, px(scale::SHRINE_STONE) / 20.f);
            poly({{q.x - 4, q.y}, {q.x - 3.5f, q.y - 18}, {q.x + 3, q.y - 20}, {q.x + 4, q.y}}, hex(0x8d918a));
            poly({{q.x, q.y}, {q.x + .5f, q.y - 19}, {q.x + 3, q.y - 20}, {q.x + 4, q.y}}, hex(0xa7aba3));
            pop();
        }
        if (b.done) { V2 q = P(x + 1, y + 1); radial(q.x, q.y - 6, 18, 18, rgba(190, 150, 255, .55f), rgba(190, 150, 255, 0)); }
    } else if (k == "boatyard") {
        foot(x, y, w, h, rgba(120, 90, 50, .3f));
        V2 q = P(x + 1, y + 1);
        Color wd = hex(0x6b4a2e);
        push(q, px(scale::MAST) / 34.f);
        line(q.x - 18, q.y + 2, q.x - 18, q.y - 14, 2, wd); line(q.x + 18, q.y - 6, q.x + 18, q.y - 22, 2, wd); line(q.x - 19, q.y - 13, q.x + 19, q.y - 21, 2, wd);
        poly({{q.x - 16, q.y - 2}, {q.x + 14, q.y - 10}, {q.x + 10, q.y - 2}, {q.x - 12, q.y + 4}}, hex(0x7a5434));
        line(q.x - 1, q.y - 4, q.x, q.y - 34, 1.5f, hex(0x5a3c22));
        if (b.done) tri({q.x, q.y - 33}, {q.x + 12, q.y - 12}, {q.x, q.y - 10}, hex(0xefe8d6));
        pop();
    } else if (k == "bath") {
        foot(x + .15f, y + .15f, w - .3f, h - .3f, hex(0x8d918a));
        foot(x + .4f, y + .4f, w - .8f, h - .8f, s == 3 ? hex(0x7fb3c4) : hex(0x5fa7b8), 2);
        if (b.done) for (int i = 0; i < 4; i++) { V2 q = P(x + .6f + i * .25f, y + 1); float ph = float(std::fmod(t * .012 + i * .25, 1.0)); circle(q.x + ph * 4, q.y - ph * 24, 3 + ph * 4, rgba(240, 240, 240, .4f * (1 - ph))); }
    }
    draw::alpha = savedA;
    if (!b.done && b.progress > 0) {
        V2 q = P(x + w / 2, y + h / 2, std::max(px(5.5f), w * 16));
        rect(q.x - 16, q.y - 3, 32, 5, rgba(20, 20, 20, .45f));
        rect(q.x - 16, q.y - 3, 32 * prog, 5, hex(0xf2b84b));
    }
}

void poi(const Ctx& c, const dw::Poi& p) {
    V2 q = wp(p.x + .5f, p.y + .6f);
    float x = q.x, y = q.y;
    int s = c.season;
    if (p.type == "hermit" && !p.joined) {
        ellipse(x + 4, y + 2, 16, 6, rgba(30, 30, 20, .28f));
        PrismOpt o; o.door = true;
        prism(p.x + .15f, p.y + .2f, .7f, .6f, px(scale::HERMIT_WALL), hex(0x7a6546), s == 3 ? hex(0xdfe5e8) : hex(0x5a7a40), px(scale::HERMIT_ROOF), o);
        V2 sm = P(p.x + .5f, p.y + .5f, px(scale::HERMIT_WALL + scale::HERMIT_ROOF));
        smoke(c, sm.x, sm.y, 2, .7f);
    } else if (p.type == "cave") {
        push(q, px(scale::MINE_HILL) / 24.f);
        poly({{x - 20, y + 2}, {x - 14, y - 16}, {x, y - 24}, {x + 16, y - 16}, {x + 20, y + 2}}, s == 3 ? hex(0xa4aaae) : hex(0x7d7f78));
        auto m = arc(x, y - 2, 8, 10, 3.14159f, 6.283f, 10);
        m.insert(m.begin(), {x, y});
        polyV(m, hex(0x1d1b18));
        rect(x - 8, y - 2, 16, 3, hex(0x1d1b18));
        pop();
    } else if (p.type == "ruins") {
        push(q, 1.6f);
        float cols[4][3] = {{-.3f, -.3f, 18}, {.3f, -.2f, 10}, {-.2f, .3f, 24}, {.35f, .35f, 7}};
        for (auto& cc : cols) {
            V2 pp = P(p.x + .5f + cc[0], p.y + .5f + cc[1]);
            rect(pp.x - 3, pp.y - cc[2], 6, cc[2], hex(0xb2ad9f));
            rect(pp.x - 4, pp.y - cc[2] - 2, 8, 3, hex(0xd0cbbd));
        }
        rect(x - 14, y - 4, 8, 3, rgba(80, 120, 60, .6f));
        pop();
    } else if (p.type == "wreck") {
        push(q, 2.f);
        poly({{x - 22, y}, {x + 18, y - 12}, {x + 14, y - 2}, {x - 16, y + 6}}, hex(0x5a4030));
        poly({{x - 22, y}, {x + 18, y - 12}, {x + 16, y - 18}, {x - 18, y - 6}}, hex(0x3e2c1f));
        line(x - 2, y - 6, x + 6, y - 34, 2, hex(0x3e2c1f));
        pop();
    }
}

static void note(float x, float y, Color c) {
    ellipse(x, y, 1.8f, 1.3f, c);
    line(x + 1.6f, y, x + 1.6f, y - 5, .8f, c);
}

void person(const Ctx& c, const dw::Person& p, bool selected) {
    if (p.hidden || p.dead) return;
    V2 o = wp(p.x, p.y);
    // drawn around the feet at (0,0); the art is 25.2 units from feet to the top of the hair
    float k = px(p.child ? scale::CHILD : scale::PERSON) / 25.2f;
    push(o, k, {0, 0});
    Color tunic = hex(p.tunic), skin = hex(p.skin), hair = hex(p.hair);
    if (selected) ring(0, 0, 12, 6, 1.8f, hex(0xffd27a));
    if (p.sleeping) {
        ellipse(0, 1, 11, 4, rgba(20, 30, 20, .25f));
        ellipse(1, -2, 9, 4.5f, tunic);
        ellipse(3, -3, 6, 4, hex(0x8a6a4a));
        circle(-7, 1, 3.8f, skin);
        auto h = arc(-7.5f, .5f, 3.9f, 3.9f, 1.25f, 4.4f, 10);
        h.insert(h.begin(), {-7.5f, .5f});
        polyV(h, hair);
        pop();
        return;
    }
    ellipse(0, 0, 7, 3, rgba(20, 30, 20, .3f));
    bool walking = p.moving;
    float ph = p.phase, swing = walking ? std::sin(ph) * 3 : 0, bob = walking ? std::abs(std::sin(ph)) * 1.2f : 0;
    const std::string& an = p.anim;
    float crouch = (an == "forage" || an == "farm" || an == "drink") ? 3.f : (an == "sit" || an == "eat") ? 4.f : 0.f;
    float f = float(p.face);
    Color leg = hex(0x4a3b32);
    if (crouch >= 4) { line(-2, -5, 3 * f, -2, 2.4f, leg); line(1, -5, 5 * f, -1.5f, 2.4f, leg); }
    else { line(-1.7f, -6 + crouch, -1.7f + swing * .6f, 0, 2.4f, leg); line(1.7f, -6 + crouch, 1.7f - swing * .6f, 0, 2.4f, leg); }
    float by = -6 - bob + crouch;
    rect(-5, by - 8, 10, 9, tunic);
    circle(-2.5f, by - 8, 2.5f, tunic); circle(2.5f, by - 8, 2.5f, tunic);
    rect(-5, by - 8, 3, 9, rgba(0, 0, 0, .14f));
    rect(-5, by - .5f, 10, 1.5f, rgba(0, 0, 0, .15f));
    double t = c.t;
    float ax = f * 4.5f, ay = by - 7;
    if (an == "chop" || an == "mine" || an == "build") {
        float sw = std::sin(float(t * 1.2 + p.id)) * 1.1f, hx = ax + f * (4 + std::cos(sw) * 2), hy = ay - 4 + std::sin(sw) * 6;
        line(ax, ay, hx, hy, 2.1f, skin);
        float tx = hx + f * std::cos(sw - .6f) * 9, ty = hy - std::sin(sw + .8f) * 9;
        line(hx, hy, tx, ty, 1.7f, hex(0x6b4a2e));
        rect(tx - 2, ty - 2, an == "mine" ? 6 : 4, an == "mine" ? 2 : 4, an == "build" ? hex(0x555555) : hex(0x9aa0a6));
        if (std::sin(t * 1.2 + p.id) > .93) circle(hx + f * 11, hy + 2, 1.3f, an == "chop" ? hex(0xc79a62) : hex(0xbbbbbb));
    } else if (an == "fish") {
        line(ax, ay, ax + f * 5, ay - 2, 2.1f, skin);
        float rx = ax + f * 24, ry = ay - 14;
        line(ax + f * 5, ay - 2, rx, ry, 1.2f, hex(0x5a3c22));
        float bobY = ry + 22 + std::sin(float(t * .2)) * 1.5f;
        line(rx, ry, rx + f * 3, bobY, .6f, rgba(255, 255, 255, .75f));
        circle(rx + f * 3, bobY, 1.4f, hex(0xe04a3a));
    } else if (an == "forage" || an == "farm") {
        float sw = std::sin(float(t * .8 + p.id)) * 2;
        line(ax, ay, ax + f * 5, ay + 5 + sw, 2.1f, skin);
        line(-ax * .6f, ay, -ax * .2f + f * 3, ay + 6 - sw, 2.1f, skin);
    } else if (an == "eat") {
        float sw = std::sin(float(t * .5 + p.id));
        line(ax, ay, ax + f, ay - 3 - sw * 2, 2.1f, skin);
        circle(ax + f * 2, ay - 4 - sw * 2, 1.7f, hex(0xc98a5a));
    } else if (an == "sit") {
        line(ax, ay, ax + f * 4, ay + 4, 2.1f, skin);
    } else if (an == "music") {
        line(ax, ay, f * 3, by - 12, 2.1f, skin);
        line(-ax, ay, f * 6, by - 11, 2.1f, skin);
        line(f * 2, by - 13, f * 13, by - 9, 1.6f, hex(0xc9a46a));
        for (int i = 0; i < 2; i++) { float q = float(std::fmod(t * .03 + i * .5, 1.0)); note(f * (10 + q * 8) + i * 4, by - 18 - q * 16, rgba(80, 60, 140, 1 - q)); }
    } else {
        line(4.5f, ay, 4.5f + (walking ? -swing * .6f : 0), ay + 6, 2.1f, skin);
        line(-4.5f, ay, -4.5f + (walking ? swing * .6f : 0), ay + 6, 2.1f, skin);
    }
    float hy = by - 14;
    circle(0, hy, 4.4f, skin);
    auto hr = arc(0, hy - .6f, 4.6f, 4.6f, 3.3f, 6.12f, 10);
    hr.insert(hr.begin(), {0, hy - .6f});
    polyV(hr, hair);
    if (p.hairStyle == 1) rect(-f * 4.6f - .5f, hy - 2, 1.8f, 5, hair);
    if (p.hairStyle == 2) circle(-f * 4, hy - 1, 2.3f, hair);
    circle(f * 1.9f, hy, .75f, hex(0x1d1d1d));
    if (p.status == "hermit") tri({-3, hy + 2}, {3, hy + 2}, {0, hy + 8}, hex(0xdddddd));
    if (p.carry) {
        float cy = by - 21;
        const auto& ct = p.carry->t;
        if (ct == "wood") { rect(-7, cy, 13, 3, hex(0x8a5e38)); rect(-6, cy - 3, 12, 3, hex(0x8a5e38)); circle(6, cy + 1.5f, 1.6f, hex(0xc79a62)); }
        else if (ct == "stone") { circle(-2, cy, 3.2f, hex(0x8a8d86)); circle(2.6f, cy + .5f, 2.8f, hex(0x9a9d96)); }
        else if (ct == "fish") { ellipse(f * 6, by - 4, 5, 2, hex(0x8fb4c2)); tri({f * 10, by - 4}, {f * 13, by - 7}, {f * 13, by - 1}, hex(0x8fb4c2)); }
        else if (ct == "food") { rect(f * 4 - 3, by - 6, 7, 5, hex(0xb38a4e)); circle(f * 4 - 1, by - 6.5f, 1.4f, hex(0xc0303f)); circle(f * 4 + 1.5f, by - 6.5f, 1.4f, hex(0xc0303f)); }
        else if (ct == "meal") ellipse(f * 5, by - 4, 4, 1.6f, hex(0xa0754a));
    }
    if (p.sick > 0) circle(0, hy - 9, 1.5f, hex(0x7fbf6a));
    if (p.whisper && p.whisper->until > c.sim->tMin) poly({{0, hy - 12}, {2, hy - 9}, {0, hy - 6}, {-2, hy - 9}}, rgba(185, 154, 230, .9f));
    pop();
}

void wolf(const Ctx&, const dw::Wolf& w) {
    V2 o = wp(w.x, w.y);
    float f = float(w.face), l = std::sin(w.ph) * 2.5f;
    // drawn around the paws at (0,0); the back is 12.5 units up
    push(o, px(scale::WOLF) / 12.5f, {0, 0});
    Color fur = hex(0x6b6b73), dark = hex(0x4f4f55);
    ellipse(0, 0, 11, 3.5f, rgba(10, 10, 10, .3f));
    line(-6, -6, -6 + l, 0, 2, dark); line(6, -6, 6 - l, 0, 2, dark);
    ellipse(0, -8, 9, 4.5f, fur);
    ellipse(f * 10, -11, 4.5f, 3.5f, fur);
    tri({f * 9, -14}, {f * 10, -18}, {f * 12, -14}, fur);
    tri({f * 13, -11}, {f * 17, -10}, {f * 13, -8.5f}, fur);
    curve({-f * 8, -9}, {-f * 14, -12}, {-f * 15, -6}, 3, fur);
    circle(f * 12, -12, .9f, hex(0xffd27a));
    pop();
}

} // namespace art
