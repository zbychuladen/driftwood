#include "engine/Draw.h"
#include <algorithm>
#include <cmath>
#include "rlgl.h"

namespace draw {

float alpha = 1.f;

static unsigned char u8(float v) { return (unsigned char)std::clamp(int(std::lround(v)), 0, 255); }

Color hex(uint32_t rgb, float a) { return Color{u8((rgb >> 16) & 255), u8((rgb >> 8) & 255), u8(rgb & 255), u8(a * 255)}; }
Color rgba(int r, int g, int b, float a) { return Color{u8(r), u8(g), u8(b), u8(a * 255)}; }
Color shade(Color c, float f) { return Color{u8(c.r * f), u8(c.g * f), u8(c.b * f), c.a}; }
Color mix(Color a, Color b, float t) { return Color{u8(a.r + (b.r - a.r) * t), u8(a.g + (b.g - a.g) * t), u8(a.b + (b.b - a.b) * t), u8(a.a + (b.a - a.a) * t)}; }
Color withA(Color c, float a) { c.a = u8(a * 255); return c; }
Color apply(Color c) { if (alpha < 1) c.a = u8(c.a * alpha); return c; }

static void vtx(V2 p, Color c) { rlColor4ub(c.r, c.g, c.b, c.a); rlVertex2f(p.x, p.y); }

void poly(const V2* pts, int n, Color c) {
    if (n < 3) return;
    c = apply(c);
    if (c.a == 0) return;
    rlCheckRenderBatchLimit((n - 2) * 3);
    rlBegin(RL_TRIANGLES);
    for (int i = 1; i + 1 < n; i++) { vtx(pts[0], c); vtx(pts[i], c); vtx(pts[i + 1], c); }
    rlEnd();
}
void poly(std::initializer_list<V2> pts, Color c) { poly(pts.begin(), int(pts.size()), c); }
void polyV(const std::vector<V2>& pts, Color c) { poly(pts.data(), int(pts.size()), c); }

void tri(V2 a, V2 b, V2 c, Color col) { V2 p[3] = {a, b, c}; poly(p, 3, col); }

void quad(V2 a, V2 b, V2 c, V2 d, Color ca, Color cb, Color cc, Color cd) {
    ca = apply(ca); cb = apply(cb); cc = apply(cc); cd = apply(cd);
    rlCheckRenderBatchLimit(6);
    rlBegin(RL_TRIANGLES);
    vtx(a, ca); vtx(b, cb); vtx(c, cc);
    vtx(a, ca); vtx(c, cc); vtx(d, cd);
    rlEnd();
}

void ellipse(float cx, float cy, float rx, float ry, Color c, int seg) {
    if (rx <= 0 || ry <= 0) return;
    c = apply(c);
    if (c.a == 0) return;
    if (seg <= 0) seg = std::clamp(int(std::max(rx, ry) * 1.2f), 10, 40);
    rlCheckRenderBatchLimit(seg * 3);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < seg; i++) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        vtx({cx, cy}, c);
        vtx({cx + std::cos(a0) * rx, cy + std::sin(a0) * ry}, c);
        vtx({cx + std::cos(a1) * rx, cy + std::sin(a1) * ry}, c);
    }
    rlEnd();
}
void circle(float cx, float cy, float r, Color c) { ellipse(cx, cy, r, r, c); }

void ring(float cx, float cy, float rx, float ry, float thick, Color c) {
    c = apply(c);
    int seg = 28;
    rlCheckRenderBatchLimit(seg * 6);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < seg; i++) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        V2 o0{cx + std::cos(a0) * rx, cy + std::sin(a0) * ry}, o1{cx + std::cos(a1) * rx, cy + std::sin(a1) * ry};
        V2 i0{cx + std::cos(a0) * (rx - thick), cy + std::sin(a0) * (ry - thick)}, i1{cx + std::cos(a1) * (rx - thick), cy + std::sin(a1) * (ry - thick)};
        vtx(o0, c); vtx(o1, c); vtx(i1, c);
        vtx(o0, c); vtx(i1, c); vtx(i0, c);
    }
    rlEnd();
}

void radial(float cx, float cy, float rx, float ry, Color inner, Color outer, int seg) {
    inner = apply(inner); outer = apply(outer);
    rlCheckRenderBatchLimit(seg * 3);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < seg; i++) {
        float a0 = 6.2831853f * i / seg, a1 = 6.2831853f * (i + 1) / seg;
        vtx({cx, cy}, inner);
        vtx({cx + std::cos(a0) * rx, cy + std::sin(a0) * ry}, outer);
        vtx({cx + std::cos(a1) * rx, cy + std::sin(a1) * ry}, outer);
    }
    rlEnd();
}

void line(float x1, float y1, float x2, float y2, float w, Color c) {
    c = apply(c);
    if (c.a == 0) return;
    float dx = x2 - x1, dy = y2 - y1, len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return;
    float nx = -dy / len * w * .5f, ny = dx / len * w * .5f;
    V2 p[4] = {{x1 + nx, y1 + ny}, {x2 + nx, y2 + ny}, {x2 - nx, y2 - ny}, {x1 - nx, y1 - ny}};
    rlCheckRenderBatchLimit(6);
    rlBegin(RL_TRIANGLES);
    vtx(p[0], c); vtx(p[1], c); vtx(p[2], c);
    vtx(p[0], c); vtx(p[2], c); vtx(p[3], c);
    rlEnd();
}

void rect(float x, float y, float w, float h, Color c) { poly({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, c); }
void rectGradH(float x, float y, float w, float h, Color l, Color r) { quad({x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}, l, r, r, l); }

void polyGradX(const std::vector<V2>& p, float x0, float x1, Color l, Color r) {
    if (p.size() < 3) return;
    auto col = [&](V2 v) { return apply(mix(l, r, std::clamp((v.x - x0) / std::max(1.f, x1 - x0), 0.f, 1.f))); };
    rlCheckRenderBatchLimit(int(p.size()) * 3);
    rlBegin(RL_TRIANGLES);
    for (size_t i = 1; i + 1 < p.size(); i++) { vtx(p[0], col(p[0])); vtx(p[i], col(p[i])); vtx(p[i + 1], col(p[i + 1])); }
    rlEnd();
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
