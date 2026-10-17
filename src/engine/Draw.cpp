#include "engine/Draw.h"
#include <algorithm>
#include <cmath>
#include "rlgl.h"

namespace draw {

float alpha = 1.f;
Tone tone;
Ink ink;

static unsigned char u8(float v) { return (unsigned char)std::clamp(int(std::lround(v)), 0, 255); }

Color hex(uint32_t rgb, float a) { return Color{u8((rgb >> 16) & 255), u8((rgb >> 8) & 255), u8(rgb & 255), u8(a * 255)}; }
Color rgba(int r, int g, int b, float a) { return Color{u8(r), u8(g), u8(b), u8(a * 255)}; }
Color shade(Color c, float f) { return Color{u8(c.r * f), u8(c.g * f), u8(c.b * f), c.a}; }
Color mix(Color a, Color b, float t) { return Color{u8(a.r + (b.r - a.r) * t), u8(a.g + (b.g - a.g) * t), u8(a.b + (b.b - a.b) * t), u8(a.a + (b.a - a.a) * t)}; }
Color withA(Color c, float a) { c.a = u8(a * 255); return c; }
Color apply(Color c) {
    if (tone.desat > 0 || tone.wash > 0) {
        float g = c.r * .3f + c.g * .59f + c.b * .11f;
        auto ch = [&](float v, float paper) { v += (g - v) * tone.desat; return v + (paper - v) * tone.wash; };
        c = Color{u8(ch(c.r, tone.paper.r)), u8(ch(c.g, tone.paper.g)), u8(ch(c.b, tone.paper.b)), c.a};
    }
    if (alpha < 1) c.a = u8(c.a * alpha);
    return c;
}

struct Xf { V2 at, from; float k; };
static std::vector<Xf> stack;
static Box* rec = nullptr;
static Probe* prb = nullptr;
static V2 tri3[3];
static int triN = 0;

void push(V2 pivot, float k) { stack.push_back({pivot, pivot, k}); }
void push(V2 at, float k, V2 from) { stack.push_back({at, from, k}); }
void pop() { if (!stack.empty()) stack.pop_back(); }
void record(Box* box) { rec = box; triN = 0; }
void probe(Probe* p) { prb = p; triN = 0; }

static bool inside(V2 p, V2 a, V2 b, V2 c) {
    auto s = [](V2 p1, V2 p2, V2 p3) { return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y); };
    float d1 = s(p, a, b), d2 = s(p, b, c), d3 = s(p, c, a);
    bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
    return !(neg && pos);
}

// Combined scale of the transform stack, so outlines keep one width.
static float scaleNow() { float k = 1; for (auto& x : stack) k *= x.k; return k; }

static V2 xf(V2 p) {
    for (auto it = stack.rbegin(); it != stack.rend(); ++it) p = {it->at.x + (p.x - it->from.x) * it->k, it->at.y + (p.y - it->from.y) * it->k};
    return p;
}
// Every primitive emits triangles through these three, so transform and
// recording live in one place.
static void begin(int verts) { if (rec) return; rlCheckRenderBatchLimit(verts); rlBegin(RL_TRIANGLES); }
static void end() { if (!rec) rlEnd(); }
static void vtx(V2 p, Color c) {
    p = xf(p);
    if (rec) {
        rec->x0 = std::min(rec->x0, p.x); rec->y0 = std::min(rec->y0, p.y); rec->x1 = std::max(rec->x1, p.x); rec->y1 = std::max(rec->y1, p.y);
        if (prb) {
            // primitives emit whole triangles, three vertices at a time
            tri3[triN++] = p;
            if (triN == 3) { triN = 0; if (c.a >= 90 * alpha && !prb->hit && inside(prb->at, tri3[0], tri3[1], tri3[2])) prb->hit = true; }
        }
        return;
    }
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlVertex2f(p.x, p.y);
}

// Outline a closed shape in ink when the fill is solid and big enough to read.
static void outline(const V2* pts, int n, Color fill) {
    if (ink.width <= 0 || rec || fill.a < 200 || n < 3) return;
    // fans often start with a point in the middle; trace only the rim then
    if (n > 3) {
        bool in = false;
        for (int i = 1, j = n - 1; i < n; j = i++)
            if ((pts[i].y > pts[0].y) != (pts[j].y > pts[0].y) && pts[0].x < (pts[j].x - pts[i].x) * (pts[0].y - pts[i].y) / (pts[j].y - pts[i].y) + pts[i].x) in = !in;
        if (in) { pts++; n--; }
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) { V2 p = xf(pts[i]); x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
    if (x1 - x0 < 3.5f && y1 - y0 < 3.5f) return;
    Color c = ink.color;
    c.a = u8(c.a * alpha);
    float w = ink.width;
    Ink saved = ink;
    ink.width = 0;   // the outline's own lines are not outlined
    for (int i = 0; i < n; i++) { V2 a = pts[i], b = pts[(i + 1) % n]; line(a.x, a.y, b.x, b.y, w / (stack.empty() ? 1 : scaleNow()), c); }
    ink = saved;
}

void poly(const V2* pts, int n, Color c) {
    if (n < 3) return;
    c = apply(c);
    if (c.a == 0) return;
    begin((n - 2) * 3);
    for (int i = 1; i + 1 < n; i++) { vtx(pts[0], c); vtx(pts[i], c); vtx(pts[i + 1], c); }
    end();
    outline(pts, n, c);
}
void poly(std::initializer_list<V2> pts, Color c) { poly(pts.begin(), int(pts.size()), c); }
void polyV(const std::vector<V2>& pts, Color c) { poly(pts.data(), int(pts.size()), c); }

void tri(V2 a, V2 b, V2 c, Color col) { V2 p[3] = {a, b, c}; poly(p, 3, col); }

void quad(V2 a, V2 b, V2 c, V2 d, Color ca, Color cb, Color cc, Color cd) {
    ca = apply(ca); cb = apply(cb); cc = apply(cc); cd = apply(cd);
    begin(6);
    vtx(a, ca); vtx(b, cb); vtx(c, cc);
    vtx(a, ca); vtx(c, cc); vtx(d, cd);
    end();
}

void ellipse(float cx, float cy, float rx, float ry, Color c, int seg) {
    if (rx <= 0 || ry <= 0) return;
    c = apply(c);
    if (c.a == 0) return;
    if (seg <= 0) seg = std::clamp(int(std::max(rx, ry) * 1.2f), 10, 40);
    begin(seg * 3);
    for (int i = 0; i < seg; i++) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        vtx({cx, cy}, c);
        vtx({cx + std::cos(a0) * rx, cy + std::sin(a0) * ry}, c);
        vtx({cx + std::cos(a1) * rx, cy + std::sin(a1) * ry}, c);
    }
    end();
    if (ink.width > 0 && !rec && c.a >= 200 && rx > 1.8f && ry > 1.8f) {
        std::vector<V2> rim;
        for (int i = 0; i < seg; i++) { float a = 6.2831853f * i / seg; rim.push_back({cx + std::cos(a) * rx, cy + std::sin(a) * ry}); }
        outline(rim.data(), int(rim.size()), c);
    }
}
void circle(float cx, float cy, float r, Color c) { ellipse(cx, cy, r, r, c); }

void ring(float cx, float cy, float rx, float ry, float thick, Color c) {
    c = apply(c);
    int seg = 28;
    begin(seg * 6);
    for (int i = 0; i < seg; i++) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        V2 o0{cx + std::cos(a0) * rx, cy + std::sin(a0) * ry}, o1{cx + std::cos(a1) * rx, cy + std::sin(a1) * ry};
        V2 i0{cx + std::cos(a0) * (rx - thick), cy + std::sin(a0) * (ry - thick)}, i1{cx + std::cos(a1) * (rx - thick), cy + std::sin(a1) * (ry - thick)};
        vtx(o0, c); vtx(o1, c); vtx(i1, c);
        vtx(o0, c); vtx(i1, c); vtx(i0, c);
    }
    end();
}

void radial(float cx, float cy, float rx, float ry, Color inner, Color outer, int seg) {
    inner = apply(inner); outer = apply(outer);
    begin(seg * 3);
    for (int i = 0; i < seg; i++) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        vtx({cx, cy}, inner);
        vtx({cx + std::cos(a0) * rx, cy + std::sin(a0) * ry}, outer);
        vtx({cx + std::cos(a1) * rx, cy + std::sin(a1) * ry}, outer);
    }
    end();
}

void line(float x1, float y1, float x2, float y2, float w, Color c) {
    c = apply(c);
    if (c.a == 0) return;
    float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float nx = -dy / len * w * .5f, ny = dx / len * w * .5f;
    V2 p[4] = {{x1 + nx, y1 + ny}, {x2 + nx, y2 + ny}, {x2 - nx, y2 - ny}, {x1 - nx, y1 - ny}};
    begin(6);
    vtx(p[0], c); vtx(p[1], c); vtx(p[2], c);
    vtx(p[0], c); vtx(p[2], c); vtx(p[3], c);
    end();
}

void rect(float x, float y, float w, float h, Color c) { poly({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, c); }
void rectGradH(float x, float y, float w, float h, Color l, Color r) { quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, l, r, r, l); }

void polyGradX(const std::vector<V2>& p, float x0, float x1, Color l, Color r) {
    if (p.size() < 3) return;
    auto col = [&](V2 v) { return apply(mix(l, r, std::clamp((v.x - x0) / std::max(1.f, x1 - x0), 0.f, 1.f))); };
    begin(int(p.size()) * 3);
    for (size_t i = 1; i + 1 < p.size(); i++) { vtx(p[0], col(p[0])); vtx(p[i], col(p[i])); vtx(p[i + 1], col(p[i + 1])); }
    end();
    outline(p.data(), int(p.size()), col(p[0]));
}

std::vector<V2> arc(float cx, float cy, float rx, float ry, float a0, float a1, int seg) {
    std::vector<V2> v;
    for (int i = 0; i <= seg; i++) { float a = a0 + (a1 - a0) * i / seg; v.push_back({cx + std::cos(a) * rx, cy + std::sin(a) * ry}); }
    return v;
}
std::vector<V2> bezier(V2 a, V2 c, V2 b, int seg) {
    std::vector<V2> v;
    for (int i = 0; i <= seg; i++) {
        float t = float(i) / seg, u = 1 - t;
        v.push_back({u * u * a.x + 2 * u * t * c.x + t * t * b.x, u * u * a.y + 2 * u * t * c.y + t * t * b.y});
    }
    return v;
}
void curve(V2 a, V2 c, V2 b, float w, Color col, int seg) {
    auto pts = bezier(a, c, b, seg);
    for (size_t i = 0; i + 1 < pts.size(); i++) line(pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, w, col);
}

} // namespace draw
