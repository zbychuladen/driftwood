#include "game/Art.h"
#include <algorithm>
#include <cmath>
#include <set>
#include "engine/Draw.h"
#include "game/Figure.h"
#include "game/Iso.h"
#include "game/Scale.h"

using namespace draw;
using iso::P;
using iso::wp;
using scale::px;

// Soft ground shadows under everything that stands.
static const Color SHADOW = {70, 76, 56, 40};

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
    ellipse(c.x + 6, c.y + 3, rx * 1.15f, ry * 1.1f, SHADOW);
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

// ---------------------------------------------------------------- trees
// Every tree is drawn around its foot at (0,0) exactly 100 units tall, then
// scaled to its real height from Scale.h. Crowns are one bumpy silhouette with
// a single ink outline, shaded inside without ink: a darker underside, a lit
// mass toward the light (up and left), highlights and a scatter of leaves.

namespace {

// Turns ink outlines off for interior detail, back on when it goes out of scope.
struct NoInk {
    float w;
    NoInk() : w(draw::ink.width) { draw::ink.width = 0; }
    ~NoInk() { draw::ink.width = w; }
};

struct Lobe { float x, y, r; };

// The outline of a union of circles, seen from a point inside it, with that
// point first so the fill fans from the middle and the outline traces the rim.
std::vector<V2> blob(const std::vector<Lobe>& L, V2 o, int n = 60) {
    std::vector<V2> pts{o};
    for (int i = 0; i < n; i++) {
        float a = 6.2831853f * i / n, dx = std::cos(a), dy = std::sin(a), best = 0;
        for (auto& l : L) {
            float cx = l.x - o.x, cy = l.y - o.y, b = dx * cx + dy * cy, disc = b * b - (cx * cx + cy * cy - l.r * l.r);
            if (disc >= 0) best = std::max(best, b + std::sqrt(disc));
        }
        pts.push_back({o.x + dx * best, o.y + dy * best});
    }
    pts.push_back(pts[1]);   // close the fan's last wedge
    return pts;
}

struct Leaf { Color dark, base, light, fleck; };

Leaf leafColors(int season, int seed) {
    if (season == 0) return {hex(0x5a8a42), hex(0x76a654), hex(0x9cc672), hex(0xbcdc8e)};
    if (season == 1) return {hex(0x466f34), hex(0x5f8f44), hex(0x84b25c), hex(0xa4c878)};
    static const Leaf autumn[4] = {
        {hex(0x9a6e24), hex(0xc4952f), hex(0xe2bb58), hex(0xf2d684)},   // gold
        {hex(0x9c4f22), hex(0xc8702f), hex(0xe69a50), hex(0xf2bc78)},   // orange
        {hex(0x86362a), hex(0xae4b36), hex(0xcf7258), hex(0xe69a7c)},   // red
        {hex(0x5e6c2c), hex(0x7f8f3a), hex(0xa8b45a), hex(0xc8cc7c)}};  // still green-ish
    return autumn[seed % 4];
}

// A crown: shaded silhouette plus inner light, from a set of lobes around o.
void crown(const std::vector<Lobe>& L, V2 o, const Leaf& col, int seed, bool blossom) {
    polyV(blob(L, o), col.dark);   // the outlined silhouette in the shade colour
    NoInk ni;
    std::vector<Lobe> lit, hi;
    float avgR = 0;
    for (auto& l : L) {
        lit.push_back({l.x - l.r * .10f, l.y - l.r * .14f, l.r * .82f});   // stays inside its own lobe
        hi.push_back({l.x - l.r * .24f, l.y - l.r * .30f, l.r * .46f});
        avgR += l.r;
    }
    avgR /= float(L.size());
    polyV(blob(lit, {o.x - avgR * .1f, o.y - avgR * .14f}), col.base);
    // highlights only on the upper lobes, where the light falls
    for (size_t i = 0; i < L.size(); i++) if (L[i].y <= o.y + avgR * .2f) circle(hi[i].x, hi[i].y, hi[i].r, col.light);
    // a scatter of single leaves breaks up the shapes
    for (int k = 0; k < 14; k++) {
        const Lobe& l = L[size_t(k * 5 + seed) % L.size()];
        float a = frand(seed * 31 + k) * 6.283f, d = frand(seed * 17 + k * 3) * l.r * .7f;
        float lx = l.x + std::cos(a) * d, ly = l.y + std::sin(a) * d * .9f, lr = avgR * (.09f + frand(seed + k * 7) * .05f);
        ellipse(lx, ly, lr * 1.3f, lr, ly < o.y ? col.fleck : col.dark);
    }
    if (blossom)
        for (int k = 0; k < 10; k++) {
            const Lobe& l = L[size_t(k * 3 + seed) % L.size()];
            float a = frand(seed * 7 + k * 11) * 6.283f, d = frand(seed * 13 + k) * l.r * .75f;
            circle(l.x + std::cos(a) * d, l.y + std::sin(a) * d, avgR * .07f, hex(0xf7e4ec));
        }
}

// A tapered trunk, outlined, with its shaded side on the right.
void trunk(V2 base, V2 top, float w0, float w1, Color bark) {
    float dx = top.x - base.x, dy = top.y - base.y, len = std::max(.01f, std::hypot(dx, dy)), nx = -dy / len, ny = dx / len;
    poly({{base.x - nx * w0, base.y - ny * w0}, {top.x - nx * w1, top.y - ny * w1}, {top.x + nx * w1, top.y + ny * w1}, {base.x + nx * w0, base.y + ny * w0}}, bark);
    NoInk ni;
    poly({{base.x, base.y}, {top.x, top.y}, {top.x + nx * w1, top.y + ny * w1}, {base.x + nx * w0, base.y + ny * w0}}, shade(bark, .78f));
}

// Moves a crown so its highest point sits at the tree's full height.
void topAt(std::vector<Lobe>& L, V2& o, float top) {
    float hi = 1e9f;
    for (auto& l : L) hi = std::min(hi, l.y - l.r);
    float d = top - hi;
    for (auto& l : L) l.y += d;
    o.y += d;
}

void broadleaf(const Ctx& c, const dw::Obj& o, float sway) {
    int seed = o.i, shape = int(frand(seed * 7 + 3) * 4) % 4;
    Leaf col = leafColors(c.season, int(frand(seed * 13) * 4));
    Color bark = hex(0x6e5440);
    // crown: a core and a ring of lobes, in one of four shapes
    struct Shape { float cy, core, rx, ry, lobe, trunkTop; int n; } S[4] = {
        {-60, 23, 22, 17, 15, -44, 8},    // round
        {-58, 17, 14, 26, 12, -36, 8},    // tall oval
        {-58, 25, 33, 15, 15, -30, 9},    // wide oak
        {-60, 21, 19, 16, 13, -40, 8}};   // two masses
    const Shape& sh = S[shape];
    V2 o0{sway, sh.cy};
    std::vector<Lobe> L{{o0.x, o0.y, sh.core}};
    for (int k = 0; k < sh.n; k++) {
        float a = 6.283f * k / sh.n + frand(seed + k) * .5f;
        L.push_back({o0.x + std::cos(a) * sh.rx, o0.y + std::sin(a) * sh.ry, sh.lobe * (.8f + frand(seed * 3 + k) * .4f)});
    }
    if (shape == 3) { L.push_back({o0.x + 17, o0.y + 14, 14}); L.push_back({o0.x + 24, o0.y + 6, 10}); }
    topAt(L, o0, -100);
    // soft shadow right under the crown
    float half = (shape == 2 ? 46 : shape == 1 ? 28 : 36);
    ellipse(6, 2, half * .9f, half * .34f, SHADOW);
    // trunk and the forks that carry the crown
    V2 fork{sway * .3f, sh.trunkTop};
    trunk({0, 0}, fork, 4.f, 2.8f, bark);
    float spread = shape == 2 ? 16 : shape == 1 ? 6 : 10;
    for (float side : {-1.f, 1.f}) {
        V2 end{o0.x + side * spread, o0.y + 6};
        trunk(fork, end, 2.2f, 1.2f, bark);
    }
    if (shape == 2) trunk(fork, {o0.x + 2, o0.y - 4}, 2.f, 1.f, bark);
    crown(L, o0, col, seed, c.season == 0 && seed % 3 == 0);
}

// Winter: bare, finely branching limbs.
struct Seg { V2 a, b; float w; int depth; };
void limbs(std::vector<Seg>& out, V2 a, float ang, float len, float w, int depth, int seed) {
    V2 b{a.x + std::cos(ang) * len, a.y + std::sin(ang) * len};
    out.push_back({a, b, w, depth});
    if (depth == 0) return;
    int n = 2 + (frand(seed) > .6f ? 1 : 0);
    for (int k = 0; k < n; k++) {
        float off = (n == 2 ? (k ? .42f : -.42f) : (k - 1) * .5f) + (frand(seed * 5 + k) - .5f) * .3f;
        limbs(out, b, ang + off, len * (.68f + frand(seed * 3 + k) * .1f), w * .62f, depth - 1, seed * 7 + k + 1);
    }
}

void bareTree(const Ctx&, const dw::Obj& o, float sway) {
    const float trunkTop = -26;
    std::vector<Seg> segs;
    // two or three main limbs from the top of the trunk, each branching five times
    int mains = 2 + (frand(o.i * 3) > .5f ? 1 : 0);
    for (int m = 0; m < mains; m++) {
        float ang = -1.5708f + (m - (mains - 1) / 2.f) * .5f + (frand(o.i + m) - .5f) * .2f;
        limbs(segs, {0, trunkTop}, ang, 20, 2.4f, 5, o.i * 11 + m * 101 + 1);
    }
    // stretch the branches above the trunk so the tips reach full height
    float hi = 1e9f;
    for (auto& s : segs) hi = std::min({hi, s.a.y, s.b.y});
    float k = (-100 - trunkTop) / (hi - trunkTop);
    auto fit = [&](V2 p) { float f = (trunkTop - p.y) / (trunkTop + 100); return V2{p.x * k + sway * f, trunkTop + (p.y - trunkTop) * k}; };
    float half = 30;
    for (auto& s : segs) half = std::max(half, std::abs(fit(s.b).x));
    ellipse(6, 2, half * .8f, half * .3f, SHADOW);
    Color bark = hex(0x6a5544);
    trunk({0, 0}, {0, trunkTop}, 3.8f, 2.6f, bark);
    NoInk ni;
    for (auto& s : segs) {
        V2 a = fit(s.a), b = fit(s.b);
        line(a.x, a.y, b.x, b.y, std::max(.45f, s.w), s.depth <= 1 ? hex(0x86705c) : bark);
        // a little snow along the tops of the lower limbs
        if (s.depth >= 4 && std::abs(b.x - a.x) > std::abs(b.y - a.y) * .4f) line(a.x, a.y - s.w * .7f, b.x, b.y - s.w * .6f, s.w * .6f, hex(0xf6f8f8));
    }
}

void fir(const Ctx& c, const dw::Obj& o, float sway) {
    int seed = o.i;
    float W = 30 + frand(seed * 5) * 10;   // half-width of the lowest tier
    bool winter = c.season == 3;
    Color dark = winter ? hex(0x3f5c4a) : hex(0x335c3e), lit = winter ? hex(0x557562) : hex(0x4a7a50), bark = hex(0x5e4636);
    ellipse(5, 2, W * .95f, W * .34f, SHADOW);
    trunk({0, 0}, {0, -14}, 3.f, 2.4f, bark);
    const int tiers = 5;
    for (int i = 0; i < tiers; i++) {
        float yb = -10 - i * 16.5f, yt = i == tiers - 1 ? -100 : yb - 34, hw = W * (1 - i * .17f);
        float sx = sway * (-yt / 100);   // tips sway more than the base
        V2 apex{sx, yt};
        // drooping bottom edge: tips hang lower than the notches between them
        int tips = i < 2 ? 4 : 3;
        std::vector<V2> rim;
        rim.push_back({sx * .5f, (yt + yb) / 2 + 6});   // a point inside, for the fan
        rim.push_back(apex);
        rim.push_back({sx * .5f + hw * .55f, (yt + yb) / 2 + 2});
        for (int t = 0; t <= tips * 2; t++) {
            float f = 1 - float(t) / (tips * 2);
            float x = sx * .3f + (f * 2 - 1) * hw, droop = t % 2 == 0 ? 4.f : -3.f;
            rim.push_back({x, yb + droop});
        }
        rim.push_back({sx * .5f - hw * .55f, (yt + yb) / 2 + 2});
        rim.push_back(apex);   // close the fan
        polyV(rim, dark);
        NoInk ni;
        // the lit left half of the tier
        std::vector<V2> left{apex, {sx * .3f, yb - 1}};
        for (int t = tips * 2; t >= tips; t--) {
            float f = 1 - float(t) / (tips * 2);
            left.push_back({sx * .3f + (f * 2 - 1) * hw * .92f, yb + (t % 2 == 0 ? 2.f : -4.f)});
        }
        left.push_back({sx * .5f - hw * .52f, (yt + yb) / 2 + 2});
        std::vector<V2> fan{{sx * .1f - hw * .25f, (yt + yb) / 2 + 4}};
        fan.insert(fan.end(), left.begin(), left.end());
        fan.push_back(apex);
        polyV(fan, lit);
        if (winter) {
            // snow resting on the drooping branch tips, the part of each tier left in view
            // irregular drifts, mostly on the lit side; not every branch holds snow
            for (int t = 0; t <= tips * 2; t += 2) {
                if (frand(seed * 19 + i * 7 + t) < .35f) continue;
                float f = 1 - float(t) / (tips * 2), x = sx * .3f + (f * 2 - 1) * hw * .82f;
                float w = hw * (.18f + frand(seed + i * 3 + t) * .14f) * (x < 0 ? 1.f : .7f);
                poly({{x - w, yb + 1}, {x - w * .4f, yb - 2.2f}, {x + w * .5f, yb - 1.6f}, {x + w, yb + .6f}}, hex(0xf4f6f6));
            }
            if (i == tiers - 1) poly({apex, {sx + 4, yt + 9}, {sx, yt + 7}, {sx - 4, yt + 10}}, hex(0xf4f6f6));
        }
    }
}

void palm(const Ctx& c, const dw::Obj& o, float sway) {
    int seed = o.i;
    float lean = frand(seed * 3) > .5f ? 1.f : -1.f;
    V2 top{lean * 13 + sway, -86}, ctrl{lean * 2, -44};
    ellipse(6 + lean * 6, 2, 34, 12, SHADOW);
    // trunk: a curve that thins toward the top, outlined, with growth rings
    std::vector<V2> L, R, spine;
    for (int i = 0; i <= 14; i++) {
        float t = i / 14.f, u = 1 - t;
        V2 p{2 * u * t * ctrl.x + t * t * top.x, 2 * u * t * ctrl.y + t * t * top.y};
        V2 d{2 * u * (ctrl.x) + 2 * t * (top.x - ctrl.x), 2 * u * ctrl.y + 2 * t * (top.y - ctrl.y)};
        float len = std::max(.01f, std::hypot(d.x, d.y)), w = 3.6f - t * 1.4f;
        L.push_back({p.x + d.y / len * w, p.y - d.x / len * w});
        R.push_back({p.x - d.y / len * w, p.y + d.x / len * w});
        spine.push_back(p);
    }
    for (size_t i = 0; i + 1 < spine.size(); i++) poly({L[i], L[i + 1], R[i + 1], R[i]}, hex(0x9a7652));
    {
        NoInk ni;
        for (size_t i = 1; i + 1 < spine.size(); i++) line(L[i].x, L[i].y, R[i].x, R[i].y, .8f, hex(0x6e5238));
    }
    // ink along the two sides of the trunk
    for (size_t i = 0; i + 1 < spine.size(); i++) {
        line(L[i].x, L[i].y, L[i + 1].x, L[i + 1].y, draw::ink.width, draw::ink.color);
        line(R[i].x, R[i].y, R[i + 1].x, R[i + 1].y, draw::ink.width, draw::ink.color);
    }
    // fronds: back ones first and darker, feathered and drooping
    Color frondLit = c.season == 3 ? hex(0x7f9a6e) : hex(0x7fae4a), frondDark = c.season == 3 ? hex(0x5e7a54) : hex(0x5a8a36);
    const int n = 9;
    auto frond = [&](float a, Color col) {
        float Lf = 34 + frand(seed * 5 + int(a * 10)) * 6;
        V2 tip{top.x + std::cos(a) * Lf, top.y + std::sin(a) * Lf * .42f + 24};
        V2 mid{top.x + std::cos(a) * Lf * .55f, top.y - 13 + std::sin(a) * Lf * .18f};
        std::vector<V2> le, ri;
        for (int i = 0; i <= 12; i++) {
            float t = i / 12.f, u = 1 - t;
            V2 p{u * u * top.x + 2 * u * t * mid.x + t * t * tip.x, u * u * top.y + 2 * u * t * mid.y + t * t * tip.y};
            V2 d{2 * u * (mid.x - top.x) + 2 * t * (tip.x - mid.x), 2 * u * (mid.y - top.y) + 2 * t * (tip.y - mid.y)};
            float len = std::max(.01f, std::hypot(d.x, d.y)), w = 6.f * std::pow(std::sin(3.1416f * std::min(t * 1.1f, 1.f)), .7f) * (i % 2 ? .45f : 1.f);
            le.push_back({p.x + d.y / len * w, p.y - d.x / len * w});
            ri.push_back({p.x - d.y / len * w * .8f, p.y + d.x / len * w * .8f});
        }
        {
            NoInk ni;
            for (size_t i = 0; i + 1 < le.size(); i++) poly({le[i], le[i + 1], ri[i + 1], ri[i]}, col);
        }
        float iw = draw::ink.width * .8f;
        for (size_t i = 0; i + 1 < le.size(); i++) {
            line(le[i].x, le[i].y, le[i + 1].x, le[i + 1].y, iw, draw::ink.color);
            line(ri[i].x, ri[i].y, ri[i + 1].x, ri[i + 1].y, iw, draw::ink.color);
        }
    };
    std::vector<float> back, front;
    for (int k = 0; k < n; k++) {
        float a = 6.283f * k / n + frand(seed + k) * .3f;
        (std::sin(a) < 0 ? back : front).push_back(a);
    }
    for (float a : back) frond(a, frondDark);
    for (int k = 0; k < 3; k++) circle(top.x - 3 + k * 3, top.y + 3 + (k % 2) * 1.5f, 2.4f, hex(0x7a5634));
    for (float a : front) frond(a, frondLit);
}

} // namespace

void tree(const Ctx& c, const dw::Obj& o) {
    V2 p = wp(o.x + .5f, o.y + .6f);
    float H = o.palm ? scale::PALM : o.conifer ? scale::CONIFER : scale::BROADLEAF;
    float k = px(H) / 100.f * o.size * (o.sapling ? .35f + o.grow * .6f : 1);
    float sway = std::sin(float(c.t * .02 + o.i)) * 1.2f * (c.sim && c.sim->weather == "storm" ? 3 : 1);
    push(p, k, {0, 0});
    if (o.palm) palm(c, o, sway);
    else if (o.conifer) fir(c, o, sway);
    else if (c.season == 3) bareTree(c, o, sway);
    else broadleaf(c, o, sway);
    pop();
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
    ellipse(x + 3, y + 1, 11 * k, 4.5f * k, SHADOW);
    poly({{x - 9 * k, y}, {x - 7 * k, y - 8 * k}, {x - 1 * k, y - 12 * k}, {x + 6 * k, y - 9 * k}, {x + 9 * k, y - 2 * k}, {x + 5 * k, y + 1 * k}}, sn ? hex(0x9aa1a5) : hex(0x8a8d86));
    poly({{x - 1 * k, y - 12 * k}, {x + 6 * k, y - 9 * k}, {x + 9 * k, y - 2 * k}, {x + 5 * k, y + 1 * k}, {x + 1 * k, y - 5 * k}}, sn ? hex(0xb6bcc0) : hex(0xa2a59e));
    if (sn) ellipse(x - 1 * k, y - 10 * k, 5 * k, 2 * k, rgba(255, 255, 255, .9f));
}

void bush(const Ctx& c, const dw::Obj& o) {
    V2 p = wp(o.x + .5f, o.y + .6f);
    float x = p.x, y = p.y;
    int s = c.season;
    push(p, px(scale::BUSH) / 17.f);
    ellipse(x + 3, y + 1, 10, 4, SHADOW);
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
    ellipse(x + 4, y + 1, 8, 3.5f, SHADOW);
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
    ellipse(x, y + 3, 20, 6, SHADOW);
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

// A low stone slab under a building, the way the reference sets each one on a plinth.
static void plinth(float x, float y, float w, float h, bool snow) {
    const float up = 4;
    Color top = snow ? hex(0xeef1f2) : hex(0xd9d3c0), side = hex(0xb9b19a), front = hex(0xa79f88);
    poly4(P(x, y + h), P(x + w, y + h), P(x + w, y + h, up), P(x, y + h, up), front);
    poly4(P(x + w, y + h), P(x + w, y), P(x + w, y, up), P(x + w, y + h, up), side);
    foot(x, y, w, h, top, up);
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
    static const std::set<std::string> onPlinth = {"hut", "store", "smoke", "longhouse", "school", "forge", "loomhouse", "infirmary", "pottery", "well", "shrine"};
    if (b.done && onPlinth.count(k)) plinth(x + .02f, y + .02f, w - .04f, h - .04f, sn);
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
        ellipse(q.x + 3, q.y + 3, r + 2, r / 2 + 1, SHADOW);
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
        ellipse(q.x + 4, q.y + 3, 20, 8, SHADOW);
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
        ellipse(q.x + 4, q.y + 2, 12, 5, SHADOW);
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
        ellipse(q.x + 3, q.y + 2, 14, 6, SHADOW);
        for (int l = 0; l < 5; l++) { float ww = 14 - l * 2.4f; poly({{q.x - ww, q.y - l * 5}, {q.x + ww, q.y - l * 5 - 3}, {q.x + ww, q.y - l * 5 - 6}, {q.x - ww, q.y - l * 5 - 3}}, l % 2 ? hex(0x6b4a2e) : hex(0x8a5e38)); }
        if (b.done) { flame(c, q.x, q.y - 24, 1.3f); smoke(c, q.x, q.y - 36, 5, 1.6f); }
        pop();
    } else if (k == "lighthouse") {
        V2 q = P(x + .5f, y + .5f);
        push(q, px(scale::LIGHTHOUSE) / 103.f);
        ellipse(q.x + 4, q.y + 2, 14, 6, SHADOW);
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
        ellipse(x + 4, y + 2, 16, 6, SHADOW);
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

void person(const Ctx& c, const dw::Person& p, bool selected, int facing, int detail) {
    if (p.hidden || p.dead) return;
    V2 o = wp(p.x, p.y);
    figure::Pose pose = p.moving ? figure::walk(p.phase) : figure::activity(p.sleeping ? "sleep" : p.anim, float(c.t), p.id);
    if (p.carry && !pose.lying) figure::carrying(pose, p.carry->t);
    if (p.sick > 0 && !pose.lying && !p.moving && pose.prop == figure::NONE) figure::unwell(pose);
    if (!pose.lying) ellipse(o.x, o.y, 9, 4, SHADOW);
    if (selected) ring(o.x, o.y, pose.lying ? 22 : 13, pose.lying ? 11 : 6.5f, 1.8f, hex(0xffd27a));
    figure::Look look = figure::lookOf(p);
    look.beard = p.status == "hermit";
    figure::draw(p.x, p.y, figure::Facing(facing), look, pose, detail);
    // chips fly from an axe, sparks from a pick or hammer, at the moment it lands
    if (pose.strike > 0 && c.effects) {
        static const float FX[4] = {1, 0, 0, -1}, FY[4] = {0, 1, -1, 0};
        float tx = p.x + FX[facing] * .3f, ty = p.y + FY[facing] * .3f;
        V2 hit = P(tx, ty, px(pose.prop == figure::HAMMER ? .3f : .5f));
        Color chip = pose.prop == figure::AXE ? hex(0xc79a62) : hex(0xffd27a);
        for (int i = 0; i < 4; i++) circle(hit.x + (frand(p.id * 7 + i + int(c.t)) - .5f) * 14, hit.y - frand(p.id + i * 3 + int(c.t)) * 10, 1.4f, chip);
    }
    // the sick and the whispered-to carry a small sign over their heads
    float top = px((p.child ? scale::CHILD : scale::PERSON) + .25f);
    V2 head = P(p.x, p.y, pose.lying ? px(.6f) : top);
    if (p.sick > 0) circle(head.x, head.y, 2.4f, hex(0x7fbf6a));
    if (p.whisper && p.whisper->until > c.sim->tMin) poly({{head.x, head.y - 9}, {head.x + 3, head.y - 5}, {head.x, head.y - 1}, {head.x - 3, head.y - 5}}, rgba(185, 154, 230, .9f));
}

void wolf(const Ctx&, const dw::Wolf& w) {
    V2 o = wp(w.x, w.y);
    float f = float(w.face), l = std::sin(w.ph) * 2.5f;
    // drawn around the paws at (0,0); the back is 12.5 units up
    push(o, px(scale::WOLF) / 12.5f, {0, 0});
    Color fur = hex(0x6b6b73), dark = hex(0x4f4f55);
    ellipse(0, 0, 11, 3.5f, SHADOW);
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
