#include "game/Solid.h"
#include <algorithm>
#include <cmath>
#include "game/Iso.h"
#include "game/Scale.h"

namespace solid {

using draw::V2;

V3 unit(V3 a) { float l = std::sqrt(dot(a, a)); return l > 1e-6f ? a * (1 / l) : V3{0, 0, 1}; }

const V3 VIEW = unit({1, 1, 16.f / 17.f});
const V3 LIGHT = unit({.2f, .6f, 1.f});

V2 project(V3 p) { return iso::P(p.x / scale::TILE_M, p.y / scale::TILE_M, p.z * scale::PX_PER_M); }

Solid upright(Kind k, V3 base, float length, float width, float height, float yaw, Color col, Tex tex) {
    Solid s;
    s.kind = k;
    s.ax[0] = {std::cos(yaw), std::sin(yaw), 0};
    s.ax[1] = {-std::sin(yaw), std::cos(yaw), 0};
    s.ax[2] = {0, 0, 1};
    s.h[0] = length / 2; s.h[1] = width / 2; s.h[2] = height / 2;
    s.c = base + V3{0, 0, height / 2};
    s.col = col;
    s.tex = tex;
    return s;
}

Solid along(V3 a, V3 b, float w, float d, V3 side, Color col) {
    Solid s;
    V3 len = b - a, ax = unit(len);
    V3 sd = unit(side - ax * dot(side, ax));
    if (dot(len, len) < 1e-8f) { ax = {0, 0, 1}; sd = unit(side); }
    s.ax[0] = sd; s.ax[1] = cross(ax, sd); s.ax[2] = ax;
    s.h[0] = w / 2; s.h[1] = d / 2; s.h[2] = std::sqrt(dot(len, len)) / 2;
    s.c = (a + b) * .5f;
    s.col = col;
    return s;
}

namespace {

// The faces of a solid as point lists in world space.
void faces(const Solid& s, std::vector<std::vector<V3>>& out) {
    auto P = [&](float u, float v, float w) { return s.c + s.ax[0] * (u * s.h[0]) + s.ax[1] * (v * s.h[1]) + s.ax[2] * (w * s.h[2]); };
    out.clear();
    switch (s.kind) {
    case BOX: {
        V3 c[8];
        for (int i = 0; i < 8; i++) c[i] = P(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1);
        const int F[6][4] = {{0, 1, 3, 2}, {4, 6, 7, 5}, {0, 4, 5, 1}, {2, 3, 7, 6}, {0, 2, 6, 4}, {1, 5, 7, 3}};
        for (auto& f : F) out.push_back({c[f[0]], c[f[1]], c[f[2]], c[f[3]]});
        break;
    }
    case GABLE: case HIP: {
        // the ridge runs along the length; a hip roof pulls its ends in
        float r = s.kind == HIP ? std::max(0.f, 1 - s.h[1] / s.h[0]) : 1;
        V3 b0 = P(-1, -1, -1), b1 = P(1, -1, -1), b2 = P(1, 1, -1), b3 = P(-1, 1, -1);
        V3 r0 = P(-r, 0, 1), r1 = P(r, 0, 1);
        out.push_back({b0, b1, r1, r0});
        out.push_back({b2, b3, r0, r1});
        out.push_back({b1, b2, r1});
        out.push_back({b3, b0, r0});
        out.push_back({b0, b3, b2, b1});
        break;
    }
    case CONE: case CYLINDER: {
        const int n = 16;
        std::vector<V3> base, top;
        for (int i = 0; i < n; i++) {
            float a = 6.2831853f * i / n;
            base.push_back(P(std::cos(a), std::sin(a), -1));
            top.push_back(P(std::cos(a), std::sin(a), 1));
        }
        V3 tip = P(0, 0, 1);
        for (int i = 0; i < n; i++) {
            int j = (i + 1) % n;
            if (s.kind == CONE) out.push_back({base[i], base[j], tip});
            else out.push_back({base[i], base[j], top[j], top[i]});
        }
        if (s.kind == CYLINDER) out.push_back(top);
        std::reverse(base.begin(), base.end());
        out.push_back(base);
        break;
    }
    }
}

V3 normalOf(const std::vector<V3>& f, V3 centre) {
    V3 n = unit(cross(f[1] - f[0], f[2] - f[0]));
    V3 mid{};
    for (auto& p : f) mid = mid + p;
    mid = mid * (1.f / f.size());
    return dot(n, mid - centre) < 0 ? n * -1 : n;
}

// Clip the line p + t*dir to a convex face (in its own plane); false if it misses.
bool clip(const std::vector<V3>& f, V3 n, V3 p, V3 dir, float& t0, float& t1) {
    t0 = -1e9f; t1 = 1e9f;
    for (size_t i = 0; i < f.size(); i++) {
        V3 a = f[i], b = f[(i + 1) % f.size()];
        V3 edgeIn = cross(n, b - a);   // points into the face
        float den = dot(dir, edgeIn), num = dot(p - a, edgeIn);
        if (std::abs(den) < 1e-7f) { if (num < 0) return false; continue; }
        float t = -num / den;
        if (den > 0) t0 = std::max(t0, t); else t1 = std::min(t1, t);
    }
    return t1 > t0;
}

float hash(int a, int b) { uint32_t x = uint32_t(a * 73856093) ^ uint32_t(b * 19349663); x *= 0x5bd1e995; x ^= x >> 15; return (x & 0xffff) / 65535.f; }

// Surface marks: rows across the face (planks, logs, stone courses, thatch,
// shingles), with staggered joints where the material has them.
void texture(const Solid& s, const std::vector<V3>& f, V3 n, const Warp* warp) {
    if (s.tex == PLAIN || f.size() < 3) return;
    // "up" on the face: world up laid into the face's plane (up the slope on a roof)
    V3 up = V3{0, 0, 1} - n * n.z;
    if (dot(up, up) < 1e-4f) up = s.ax[0];
    up = unit(up);
    V3 across = unit(cross(n, up));
    float lo = 1e9f, hi = -1e9f, alo = 1e9f, ahi = -1e9f;
    for (auto& p : f) { lo = std::min(lo, dot(p, up)); hi = std::max(hi, dot(p, up)); alo = std::min(alo, dot(p, across)); ahi = std::max(ahi, dot(p, across)); }
    V3 origin = f[0] - up * dot(f[0], up) - across * dot(f[0], across);   // the plane's point at (0, 0)
    origin = origin + n * .003f;
    auto at = [&](float a, float u) { return origin + across * a + up * u; };
    auto P = [&](V3 p) { return project(warp && warp->place ? warp->place(p) : p); };
    Color ink = draw::shade(s.col, .76f);
    const float w = .7f;
    float step = s.tex == LOGS ? .24f : s.tex == STONE ? .26f : s.tex == THATCH ? .16f : s.tex == BOARDS ? .2f : .19f;
    if (s.tex == BOARDS) {
        // upright boards: joints run up the face
        for (float a = alo + step; a < ahi - step * .3f; a += step) {
            float t0, t1;
            if (clip(f, n, at(a, 0), up, t0, t1)) { V2 p0 = P(at(a, t0)), p1 = P(at(a, t1)); draw::line(p0.x, p0.y, p1.x, p1.y, w, ink); }
        }
        return;
    }
    int row = 0;
    for (float u = lo + step; u < hi - step * .25f; u += step, row++) {
        float t0, t1;
        if (!clip(f, n, at(0, u), across, t0, t1)) continue;
        V2 p0 = P(at(t0, u)), p1 = P(at(t1, u));
        draw::line(p0.x, p0.y, p1.x, p1.y, s.tex == LOGS ? 1.1f : w, ink);
        // joints between this row and the one below, offset every other row
        if (s.tex == STONE || s.tex == SHINGLE || s.tex == SLATE) {
            float jw = s.tex == STONE ? .42f : .26f;
            for (float a = t0 + jw * (row % 2 ? .5f : 1.f); a < t1 - .05f; a += jw) {
                V2 j0 = P(at(a, u)), j1 = P(at(a, u - step));
                draw::line(j0.x, j0.y, j1.x, j1.y, w, ink);
            }
        }
        if (s.tex == THATCH) {
            // loose straws hanging over each course
            for (float a = t0 + .05f; a < t1 - .05f; a += .11f) {
                float k = hash(int(a * 100), row) * .7f + .3f;
                V2 j0 = P(at(a, u)), j1 = P(at(a + .02f, u - step * k));
                draw::line(j0.x, j0.y, j1.x, j1.y, w * .8f, ink);
            }
        }
    }
}

} // namespace

float depth(const Solid& s, const Warp* warp) { return dot(warp && warp->place ? warp->place(s.c) : s.c, VIEW); }

void draw(const Solid& s, const Warp* warp) {
    thread_local std::vector<std::vector<V3>> fs;
    faces(s, fs);
    for (auto& f : fs) {
        V3 n = normalOf(f, s.c);
        V3 seen = warp && warp->turn ? warp->turn(n) : n;
        if (dot(seen, VIEW) <= 1e-4f) continue;
        std::vector<V2> q;
        q.reserve(f.size());
        for (auto& p : f) q.push_back(project(warp && warp->place ? warp->place(p) : p));
        float k = .72f + .34f * std::max(0.f, dot(seen, LIGHT));
        draw::polyV(q, draw::shade(s.col, k));
        texture(s, f, n, warp);
    }
}

void drawAll(std::vector<Solid> solids, const Warp* warp) {
    std::stable_sort(solids.begin(), solids.end(), [&](const Solid& a, const Solid& b) { return depth(a, warp) < depth(b, warp); });
    for (auto& s : solids) draw(s, warp);
}

} // namespace solid
