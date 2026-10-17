#include "game/Figure.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include "engine/Draw.h"
#include "game/Iso.h"
#include "game/Scale.h"

namespace figure {

using draw::V2;

namespace {

struct V3 { float x, y, z; };   // metres: x and y along the tile axes, z up
V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 unit(V3 a) { float l = std::sqrt(dot(a, a)); return l > 1e-6f ? a * (1 / l) : V3{0, 0, 1}; }

// Toward the camera, in metres: the direction that projects to a single pixel.
const V3 VIEW = unit({1, 1, 16.f / 17.f});
// Light from the upper left of the screen: tops brightest, left faces next.
const V3 LIGHT = unit({.2f, .6f, 1.f});

Color tone(Color c, float k) { return draw::shade(c, k); }

// One box: from a to b along its length, w wide across the body, d deep.
struct Box {
    V3 a, b;
    float w, d;
    Color c;
    int decal = -1;   // which face detail to draw on it (the head's face)
};

struct Rig {
    V3 at;            // the figure's feet, in metres
    V3 fwd, side;     // unit vectors in the ground plane
    V3 world(float f, float s, float u) const { return at + fwd * f + side * s + V3{0, 0, u}; }
};

V2 project(V3 p) { return iso::P(p.x / scale::TILE_M, p.y / scale::TILE_M, p.z * scale::PX_PER_M); }

void drawBox(const Box& bx, const Rig& rig) {
    V3 len = bx.b - bx.a, ax = unit(len);
    // keep the box square to the body: its width runs along the figure's side
    V3 sd = unit(rig.side - ax * dot(rig.side, ax)), dp = cross(ax, sd);
    if (dot(len, len) < 1e-8f) { ax = {0, 0, 1}; sd = rig.side; dp = rig.fwd; }
    V3 c = (bx.a + bx.b) * .5f;
    float hl = std::sqrt(dot(len, len)) / 2, hw = bx.w / 2, hd = bx.d / 2;
    struct Face { V3 n; V3 u, v; float hu, hv, hn; };
    Face faces[6] = {{ax, sd, dp, hw, hd, hl}, {ax * -1, sd, dp, hw, hd, hl}, {sd, ax, dp, hl, hd, hw},
                     {sd * -1, ax, dp, hl, hd, hw}, {dp, ax, sd, hl, hw, hd}, {dp * -1, ax, sd, hl, hw, hd}};
    for (auto& f : faces) {
        if (dot(f.n, VIEW) <= 0) continue;
        V3 o = c + f.n * f.hn;
        V2 q[4] = {project(o + f.u * f.hu + f.v * f.hv), project(o - f.u * f.hu + f.v * f.hv), project(o - f.u * f.hu - f.v * f.hv), project(o + f.u * f.hu - f.v * f.hv)};
        float k = .72f + .34f * std::max(0.f, dot(f.n, LIGHT));
        draw::poly(q, 4, tone(bx.c, k));
    }
}

// A flat patch on a box face, for eyes, brows and the mouth: offsets in metres
// across (s) and up (u) from a point on the face, pushed out along its normal.
void decal(V3 centre, V3 normal, V3 across, V3 up, float s, float u, float w, float h, Color c) {
    V3 o = centre + normal * .004f + across * s + up * u;
    V2 q[4] = {project(o - across * (w / 2) + up * (h / 2)), project(o + across * (w / 2) + up * (h / 2)), project(o + across * (w / 2) - up * (h / 2)), project(o - across * (w / 2) - up * (h / 2))};
    draw::poly(q, 4, c);
}

float frand(uint32_t x) { x *= 2654435761u; x ^= x >> 13; x *= 0x5bd1e995; x ^= x >> 15; return (x & 0xffff) / 65535.f; }

} // namespace

Facing facingToward(float dx, float dy, Facing otherwise) {
    if (std::abs(dx) < 1e-4f && std::abs(dy) < 1e-4f) return otherwise;
    if (std::abs(dx) >= std::abs(dy)) return dx > 0 ? SE : NW;
    return dy > 0 ? SW : NE;
}

Look lookOf(const dw::Person& p) {
    // clothes that the sim does not track come from the person's id, so each
    // settler always wears the same outfit
    static const uint32_t shirts[4] = {0xece6da, 0xd9d4c8, 0xe8dcc4, 0xcfd8d6};
    static const uint32_t legs[4] = {0x5a6f8e, 0x8a7a58, 0x6e5440, 0x4e5a4a};
    static const uint32_t shoes[4] = {0xb5523c, 0x6e5440, 0x3f4a5a, 0x8a6a3a};
    uint32_t h = uint32_t(p.id) * 2246822519u + 7;
    Look l;
    l.jacket = draw::hex(p.tunic);
    l.shirt = draw::hex(shirts[h % 4]);
    l.legs = draw::hex(legs[(h >> 3) % 4]);
    l.shoes = draw::hex(shoes[(h >> 6) % 4]);
    l.skin = draw::hex(p.skin);
    l.hair = draw::hex(p.hair);
    l.hairStyle = p.hairStyle;
    l.shorts = frand(h) < .45f;
    l.child = p.child;
    return l;
}

Pose idle(float t) {
    Pose p;
    float b = std::sin(t * 1.6f);
    p.bob = .004f * b;
    p.shoulder[0] = .03f * b;
    p.shoulder[1] = -.03f * b;
    return p;
}

Pose walk(float ph) {
    Pose p;
    float s = std::sin(ph);
    p.hip[0] = .36f * s;
    p.hip[1] = -.36f * s;
    // the leg swinging forward bends at the knee, the one pushing off straightens
    p.knee[0] = .08f + .38f * std::max(0.f, std::sin(ph + 1.9f));
    p.knee[1] = .08f + .38f * std::max(0.f, std::sin(ph + 1.9f + 3.1416f));
    p.shoulder[0] = -.38f * s;
    p.shoulder[1] = .38f * s;
    p.elbow[0] = p.elbow[1] = .3f;
    p.bob = .018f * std::abs(std::cos(ph));
    p.lean = .06f;
    return p;
}

void draw(float x, float y, Facing f, const Look& L, const Pose& P, int detail) {
    static const V3 FWD[4] = {{1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}};
    Rig rig;
    rig.at = {x * scale::TILE_M, y * scale::TILE_M, 0};
    rig.fwd = FWD[f];
    rig.side = {-rig.fwd.y, rig.fwd.x, 0};   // the figure's right hand
    // proportions: an adult is 1.7 m with a slightly large head; a child's
    // body shrinks more than its head
    float target = L.child ? scale::CHILD : scale::PERSON;
    const float bodyM = 1.36f, headM = .34f;
    float headK = L.child ? 1.22f : 1.f;
    float k = target / (bodyM + headM * headK), hk = k * headK;

    std::vector<Box> boxes;
    float lift = P.bob - P.drop;
    // legs
    for (int i = 0; i < 2; i++) {
        float s = (i ? 1 : -1) * .085f * k;
        V3 hip = rig.world(0, s, (.82f) * k + lift);
        float a = P.hip[i], b = a - P.knee[i];
        V3 knee = hip + rig.fwd * (std::sin(a) * .37f * k) + V3{0, 0, -std::cos(a) * .37f * k};
        V3 ankle = knee + rig.fwd * (std::sin(b) * .36f * k) + V3{0, 0, -std::cos(b) * .36f * k};
        Color thigh = L.legs, shin = L.shorts ? L.skin : L.legs;
        boxes.push_back({hip, knee, .13f * k, .14f * k, thigh});
        if (L.shorts) {
            // the shorts stop just above the knee; the rest of the thigh is bare
            V3 hem = hip + (knee - hip) * .62f;
            boxes.back().b = hem;
            boxes.push_back({hem, knee, .1f * k, .11f * k, L.skin});
        }
        boxes.push_back({knee, ankle, .1f * k, .11f * k, shin});
        // shoe: a block reaching forward from the ankle, a light sole under it
        V3 toe = ankle + rig.fwd * (.17f * k);
        V3 heel = ankle - rig.fwd * (.05f * k);
        boxes.push_back({heel + V3{0, 0, -.02f * k}, toe + V3{0, 0, -.02f * k}, .11f * k, .09f * k, L.shoes});
        if (detail) boxes.push_back({heel + V3{0, 0, -.065f * k}, toe + V3{0, 0, -.065f * k}, .115f * k, .025f * k, draw::hex(0xf2efe6)});
    }
    // hips and torso, leaning forward from the waist
    V3 waist = rig.world(0, 0, .88f * k + lift);
    V3 up = unit(rig.fwd * std::sin(P.lean) + V3{0, 0, std::cos(P.lean)});
    boxes.push_back({rig.world(0, 0, .8f * k + lift), waist, .3f * k, .2f * k, L.legs});
    V3 chest = waist + up * (.42f * k);
    // the jacket hangs a little below the waist
    boxes.push_back({waist - up * (.07f * k), chest, .36f * k, .22f * k, L.jacket});
    // the open jacket shows a strip of shirt down the front
    if (detail) {
        boxes.push_back({waist + rig.fwd * (.113f * k) - up * (.04f * k), chest + rig.fwd * (.113f * k) - up * (.02f * k), .11f * k, .012f * k, L.shirt});
        boxes.push_back({chest - up * (.03f * k), chest + up * (.035f * k), .24f * k, .2f * k, L.shirt});   // collar
    }
    // arms
    for (int i = 0; i < 2; i++) {
        float side = i ? 1.f : -1.f;
        V3 sh = chest - up * (.05f * k) + rig.side * (side * .2f * k);
        auto limb = [&](float swing, float len) {
            float sp = P.spread[i];
            return rig.fwd * (std::sin(swing) * std::cos(sp) * len) + rig.side * (side * std::sin(sp) * len) + V3{0, 0, -std::cos(swing) * std::cos(sp) * len};
        };
        V3 elbow = sh + limb(P.shoulder[i], .28f * k);
        V3 wrist = elbow + limb(P.shoulder[i] + P.elbow[i], .25f * k);
        boxes.push_back({sh, elbow, .12f * k, .12f * k, L.jacket});
        boxes.push_back({elbow, wrist, .11f * k, .11f * k, L.jacket});
        V3 hand = wrist + limb(P.shoulder[i] + P.elbow[i], .07f * k);
        boxes.push_back({wrist, hand, .085f * k, .08f * k, L.skin});
    }
    // neck and head
    V3 neckTop = chest + up * (.05f * k);
    boxes.push_back({chest - up * (.01f * k), neckTop, .09f * k, .09f * k, L.skin});
    V3 hup = unit(up + rig.fwd * std::sin(P.head));
    float hh = .29f * hk, hw = .28f * hk, hd = .27f * hk;
    V3 headBase = neckTop, headTop = neckTop + hup * hh;
    boxes.push_back({headBase, headTop, hw, hd, L.skin, 0});
    // hair: a cap on top, plus a back panel and the style's own pieces
    V3 capB = headTop - hup * (.07f * hk), capT = headTop + hup * (.05f * hk);
    boxes.push_back({capB - rig.fwd * (.01f * hk), capT - rig.fwd * (.01f * hk), hw + .04f * hk, hd + .04f * hk, L.hair});
    float backLen = L.hairStyle == 1 ? .27f : .2f;
    V3 back = headTop - rig.fwd * (hd / 2 + .01f * hk);
    boxes.push_back({back - hup * (backLen * hk), back, hw + .03f * hk, .05f * hk, L.hair});
    // every style covers the temples
    for (float s : {-1.f, 1.f}) {
        V3 t = headTop + rig.side * (s * (hw / 2 + .012f * hk)) - rig.fwd * (.04f * hk);
        float len = L.hairStyle == 1 ? .26f : .12f;
        boxes.push_back({t - hup * (len * hk), t, .04f * hk, hd - (L.hairStyle == 1 ? .02f : .08f) * hk, L.hair});
    }
    if (L.hairStyle == 1) {
        // a bob: a deep fringe over the brow
        V3 fr = headTop + rig.fwd * (hd / 2 + .01f * hk);
        boxes.push_back({fr - hup * (.09f * hk), fr + hup * (.02f * hk), hw - .01f * hk, .03f * hk, L.hair});
    } else if (L.hairStyle == 2) {
        V3 bun = headTop - rig.fwd * (hd / 2 + .05f * hk) - hup * (.04f * hk);
        boxes.push_back({bun - hup * (.06f * hk), bun + hup * (.06f * hk), .11f * hk, .1f * hk, L.hair});
    } else {
        V3 fr = headTop + rig.fwd * (hd / 2 + .008f * hk);
        boxes.push_back({fr - hup * (.06f * hk), fr + hup * (.02f * hk), hw - .05f * hk, .025f * hk, L.hair});
    }

    // no ink lines on figures: the facets' three tones carry the shape
    draw::Ink inkWas = draw::ink;
    draw::ink.width = 0;
    // far parts first: sort by how close each box's middle is to the camera
    std::stable_sort(boxes.begin(), boxes.end(), [](const Box& a, const Box& b) { return dot((a.a + a.b) * .5f, VIEW) < dot((b.a + b.b) * .5f, VIEW); });
    for (auto& b : boxes) {
        drawBox(b, rig);
        if (b.decal == 0 && detail && dot(rig.fwd, VIEW) > 0) {
            // the face, on the front of the head
            V3 c = (b.a + b.b) * .5f + rig.fwd * (hd / 2);
            float ink = .95f;
            for (float s : {-1.f, 1.f}) {
                decal(c, rig.fwd, rig.side, hup, s * .068f * hk, -.005f * hk, .066f * hk, .07f * hk, draw::hex(0xf7f4ee));
                decal(c, rig.fwd, rig.side, hup, s * .068f * hk + .008f * hk, -.01f * hk, .04f * hk, .058f * hk, draw::hex(0x6a3e22));
                decal(c, rig.fwd, rig.side, hup, s * .068f * hk + .01f * hk, -.006f * hk, .018f * hk, .026f * hk, draw::hex(0x24180f));
                decal(c, rig.fwd, rig.side, hup, s * .066f * hk, .05f * hk, .07f * hk, .016f * hk, tone(L.hair, ink));
            }
            decal(c, rig.fwd, rig.side, hup, 0, -.085f * hk, .055f * hk, .013f * hk, tone(L.skin, .6f));
        }
    }
    draw::ink = inkWas;
}

} // namespace figure
