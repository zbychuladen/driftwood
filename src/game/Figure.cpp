#include "game/Figure.h"
#include <algorithm>
#include <cmath>
#include <vector>
#include "engine/Draw.h"
#include "game/Iso.h"
#include "game/Scale.h"
#include "game/Solid.h"

namespace figure {

using draw::V2;

namespace {

using solid::V3;
using solid::dot;
using solid::cross;
using solid::unit;
using solid::VIEW;
using solid::project;

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
    bool lying = false;
    float len = .85f, thick = .13f;   // half the body's length and depth, for lying down
    V3 world(float f, float s, float u) const { return at + fwd * f + side * s + V3{0, 0, u}; }
    // Lying turns the standing figure onto its back: up becomes backward along
    // the ground, forward becomes up, so the face looks at the sky.
    V3 place(V3 p) const {
        if (!lying) return p;
        V3 d = p - at;
        float f = dot(d, fwd), sd = dot(d, side), u = d.z;
        return at + fwd * (len - u) + side * sd + V3{0, 0, f + thick};
    }
    V3 turn(V3 n) const {
        if (!lying) return n;
        return fwd * (-n.z) + side * dot(n, side) + V3{0, 0, dot(n, fwd)};
    }
};


void drawBox(const Box& bx, const Rig& rig, const solid::Warp& warp) {
    solid::draw(solid::along(bx.a, bx.b, bx.w, bx.d, rig.side, bx.c), &warp);
}

// A flat patch on a box face, for eyes, brows and the mouth: offsets in metres
// across (s) and up (u) from a point on the face, pushed out along its normal.
void decal(const Rig& rig, V3 centre, V3 normal, V3 across, V3 up, float s, float u, float w, float h, Color c) {
    V3 o = centre + normal * .004f + across * s + up * u;
    auto P = [&](V3 p) { return project(rig.place(p)); };
    V2 q[4] = {P(o - across * (w / 2) + up * (h / 2)), P(o + across * (w / 2) + up * (h / 2)), P(o + across * (w / 2) - up * (h / 2)), P(o - across * (w / 2) - up * (h / 2))};
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
    // breathing, a slow shift of weight from foot to foot, and a look up or down now and then
    Pose p;
    float b = std::sin(t * 1.6f), shift = std::sin(t * .37f);
    p.bob = .004f * b;
    p.shoulder[0] = .03f * b + .02f * shift;
    p.shoulder[1] = -.03f * b + .02f * shift;
    p.hip[0] = .05f * std::max(0.f, shift);
    p.knee[0] = .08f * std::max(0.f, shift);
    p.hip[1] = .05f * std::max(0.f, -shift);
    p.knee[1] = .08f * std::max(0.f, -shift);
    p.head = .07f * std::sin(t * .23f) * std::max(0.f, std::sin(t * .11f));
    return p;
}

Pose blend(const Pose& a, const Pose& b, float k) {
    auto l = [k](float x, float y) { return x + (y - x) * k; };
    Pose p = b;
    p.drop = l(a.drop, b.drop); p.bob = l(a.bob, b.bob); p.lean = l(a.lean, b.lean); p.head = l(a.head, b.head);
    for (int i = 0; i < 2; i++) {
        p.hip[i] = l(a.hip[i], b.hip[i]); p.knee[i] = l(a.knee[i], b.knee[i]);
        p.shoulder[i] = l(a.shoulder[i], b.shoulder[i]); p.spread[i] = l(a.spread[i], b.spread[i]); p.elbow[i] = l(a.elbow[i], b.elbow[i]);
    }
    return p;
}

namespace {
float smooth(float x) { x = std::clamp(x, 0.f, 1.f); return x * x * (3 - 2 * x); }
// A tool's swing over one cycle: a slow lift, a quick strike, a moment's follow-through.
// Returns how high it is (0 down, 1 raised) and sets strike near the moment of impact.
float swing(float t, float rate, int seed, float& strike) {
    float u = std::fmod(t * rate + seed * .37f, 1.f);
    if (u < 0) u += 1;
    strike = u > .66f && u < .74f ? 1 - std::abs(u - .7f) / .04f : 0;
    if (u < .55f) return smooth(u / .55f);
    if (u < .68f) { float d = (u - .55f) / .13f; return 1 - d * d; }
    return 0;
}
} // namespace

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
    // the arm swinging forward bends more at the elbow
    p.elbow[0] = .25f + .3f * std::max(0.f, -s);
    p.elbow[1] = .25f + .3f * std::max(0.f, s);
    p.bob = .02f * std::abs(std::cos(ph));
    p.lean = .07f;
    p.head = -.03f * std::cos(2 * ph);
    return p;
}

// Tools and loads, as boxes placed from the hands.
void props(std::vector<Box>& out, const Rig& rig, const Pose& P, const V3 hands[2], const V3 wrists[2], float k) {
    V3 hand = hands[1], along = unit(hands[1] - wrists[1]);
    V3 mid = (hands[0] + hands[1]) * .5f;
    const Color wood = draw::hex(0x7a5634), iron = draw::hex(0x8e939a), dark = draw::hex(0x4a3a2c);
    switch (P.prop) {
    case AXE: case PICK: {
        // a long handle through both hands, the head at the far end
        V3 a2 = unit(mid - (wrists[0] + wrists[1]) * .5f);
        V3 end = mid + a2 * (.55f * k), start = mid - a2 * (.12f * k);
        out.push_back({start, end, .035f * k, .035f * k, wood});
        if (P.prop == AXE) out.push_back({end - a2 * (.1f * k), end, .03f * k, .16f * k, iron});
        else out.push_back({end - a2 * (.04f * k) - rig.fwd * (.18f * k), end - a2 * (.04f * k) + rig.fwd * (.18f * k), .035f * k, .04f * k, iron});
        break;
    }
    case HAMMER: {
        V3 end = hand + along * (.25f * k);
        out.push_back({hand - along * (.04f * k), end, .03f * k, .03f * k, wood});
        out.push_back({end - rig.fwd * (.06f * k), end + rig.fwd * (.06f * k), .06f * k, .06f * k, iron});
        break;
    }
    case ROD: {
        V3 tip = mid + rig.fwd * (1.3f * k) + V3{0, 0, .9f * k};
        out.push_back({mid - rig.fwd * (.1f * k), tip, .025f * k, .025f * k, wood});
        // the line hangs from the tip to the water below
        out.push_back({tip, tip + V3{0, 0, -(tip.z - .02f)}, .006f, .006f, draw::hex(0xe8e8e0)});
        break;
    }
    case FLUTE: out.push_back({hands[0] + rig.side * (-.06f * k), hands[1] + rig.side * (.22f * k), .03f * k, .03f * k, draw::hex(0xc9a46a)}); break;
    case BOWL: out.push_back({mid + V3{0, 0, -.02f * k}, mid + V3{0, 0, .06f * k}, .16f * k, .16f * k, draw::hex(0xa0754a)}); break;
    case LOGS:
        for (int i = 0; i < 3; i++) {
            V3 c = mid + V3{0, 0, (.03f + .07f * i) * k} + rig.fwd * (.02f * k);
            out.push_back({c - rig.side * (.3f * k), c + rig.side * (.3f * k), .07f * k, .07f * k, i % 2 ? wood : draw::hex(0x8a6440)});
        }
        break;
    case STONE: out.push_back({mid - V3{0, 0, .04f * k}, mid + V3{0, 0, .12f * k}, .2f * k, .16f * k, draw::hex(0x9a9d96)}); break;
    case FISH: out.push_back({hand - V3{0, 0, .3f * k}, hand, .05f * k, .1f * k, draw::hex(0x8fb4c2)}); break;
    case BASKET:
        out.push_back({hand - V3{0, 0, .26f * k}, hand - V3{0, 0, .08f * k}, .22f * k, .18f * k, draw::hex(0xb38a4e)});
        out.push_back({hand - V3{0, 0, .1f * k}, hand - V3{0, 0, .06f * k}, .14f * k, .1f * k, draw::hex(0xc0303f)});
        break;
    case NONE: break;
    }
    (void)dark;
}

Pose activity(const std::string& a, float t, int seed) {
    Pose p;
    float w = std::sin(t * 1.2f + seed), up01 = (w + 1) / 2;   // a working rhythm, 0..1
    auto sitDown = [&](Pose& q) { q.drop = .66f; q.hip[0] = q.hip[1] = 1.45f; q.knee[0] = q.knee[1] = .35f; };
    if (a == "chop" || a == "mine") {
        // feet apart, both hands on the handle: up over the head, then down
        p.hip[0] = .22f; p.hip[1] = -.14f; p.knee[0] = .18f; p.knee[1] = .08f;
        float arc = a == "mine" ? 2.7f : 2.4f, strike = 0;
        float raised = swing(t, .19f, seed, strike);
        p.shoulder[0] = p.shoulder[1] = arc - 2.f * (1 - raised);
        p.elbow[0] = p.elbow[1] = .2f + .25f * raised;
        p.spread[0] = p.spread[1] = -.2f;
        p.lean = .08f + .22f * (1 - raised);
        p.knee[0] += .12f * (1 - raised);   // the knees give a little at the blow
        p.drop = .03f * (1 - raised);
        p.prop = a == "mine" ? PICK : AXE;
        p.strike = strike;
    } else if (a == "build") {
        // down on one knee, hammering with the right hand
        p.drop = .37f; p.hip[0] = 1.45f; p.knee[0] = 1.5f; p.hip[1] = -.15f; p.knee[1] = 1.7f;
        p.lean = .22f;
        float strike = 0, raised = swing(t, .45f, seed, strike);
        p.shoulder[1] = .5f + 1.4f * raised; p.elbow[1] = .5f + .5f * raised;
        p.shoulder[0] = .9f; p.elbow[0] = .5f;
        p.prop = HAMMER;
        p.strike = strike;
    } else if (a == "fish") {
        // waiting, with the odd twitch of the rod and, now and then, a fresh cast
        float u = std::fmod(t * .05f + seed * .29f, 1.f);
        float cast = u < .08f ? std::sin(u / .08f * 3.1416f) : 0;
        p.shoulder[0] = p.shoulder[1] = .75f + .03f * std::sin(t * .2f + seed) + .9f * cast;
        p.elbow[0] = p.elbow[1] = .3f + .3f * cast;
        p.lean = -.05f * cast;
        p.spread[0] = p.spread[1] = -.12f;
        p.prop = ROD;
    } else if (a == "forage" || a == "farm") {
        // squatting, reaching down with the right hand
        p.drop = .44f; p.hip[0] = p.hip[1] = 1.25f; p.knee[0] = p.knee[1] = 2.25f;
        p.lean = .4f;
        p.shoulder[1] = 1.f + .3f * w; p.elbow[1] = .2f;
        p.shoulder[0] = .6f; p.elbow[0] = .9f;
    } else if (a == "drink") {
        // on both knees, cupping water to the mouth
        p.drop = .4f; p.hip[0] = p.hip[1] = .05f; p.knee[0] = p.knee[1] = 1.6f;
        p.lean = .4f + .12f * w;
        p.shoulder[0] = p.shoulder[1] = 1.2f; p.elbow[0] = p.elbow[1] = 1.1f;
        p.spread[0] = p.spread[1] = -.15f;
    } else if (a == "eat") {
        sitDown(p);
        p.shoulder[1] = .6f + .5f * up01; p.elbow[1] = 1.5f + .5f * up01;
        p.shoulder[0] = .6f; p.elbow[0] = 1.1f;
        p.prop = BOWL;
    } else if (a == "sit") {
        sitDown(p);
        p.shoulder[0] = p.shoulder[1] = .85f; p.elbow[0] = .45f;
        p.elbow[1] = .45f + .35f * std::max(0.f, std::sin(t * .7f + seed));   // a hand moving while talking
        p.head = .05f * std::sin(t * .3f + seed);
    } else if (a == "music") {
        sitDown(p);
        p.shoulder[0] = p.shoulder[1] = 1.1f; p.elbow[0] = p.elbow[1] = 1.9f; p.spread[0] = p.spread[1] = .05f;
        p.head = .05f * std::sin(t * .9f);
        p.prop = FLUTE;
    } else if (a == "take") {
        // bending to pick something up or put it down
        p.lean = .9f; p.hip[0] = p.hip[1] = .2f; p.knee[0] = p.knee[1] = .4f;
        p.shoulder[0] = p.shoulder[1] = 1.f; p.elbow[0] = p.elbow[1] = .1f;
    } else if (a == "sleep") {
        p.lying = true;
        p.shoulder[0] = p.shoulder[1] = .1f; p.elbow[0] = p.elbow[1] = .1f;
    } else {
        return idle(t + seed);
    }
    return p;
}

bool hasPose(const std::string& a) {
    static const char* known[] = {"chop", "mine", "build", "fish", "forage", "farm", "drink", "eat", "sit", "music", "take", "sleep", "idle", ""};
    for (auto k : known) if (a == k) return true;
    return false;
}

void carrying(Pose& p, const std::string& what) {
    if (what == "wood" || what == "stone" || what == "meal") {
        // held in front with both arms
        p.shoulder[0] = p.shoulder[1] = .55f;
        p.elbow[0] = p.elbow[1] = 1.3f;
        p.spread[0] = p.spread[1] = -.05f;
        p.prop = what == "wood" ? LOGS : what == "stone" ? STONE : BOWL;
    } else if (what == "fish" || what == "food" || what == "berries" || what == "greens" || what == "grain" || what == "fruit") {
        p.shoulder[1] = .05f; p.elbow[1] = .05f;
        p.prop = what == "fish" ? FISH : BASKET;
    }
}

void unwell(Pose& p) {
    p.lean += .22f; p.head += .35f;
    p.shoulder[0] = p.shoulder[1] = 0; p.elbow[0] = p.elbow[1] = .08f;
}

void draw(float x, float y, Facing f, const Look& L, const Pose& P, int detail) {
    static const V3 FWD[4] = {{1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}};
    Rig rig;
    rig.at = {x * scale::TILE_M, y * scale::TILE_M, 0};
    rig.fwd = FWD[f];
    rig.side = {-rig.fwd.y, rig.fwd.x, 0};   // the figure's right hand
    rig.lying = P.lying;
    // proportions: an adult is 1.7 m with a slightly large head; a child's
    // body shrinks more than its head
    float target = L.child ? scale::CHILD : scale::PERSON;
    const float bodyM = 1.36f, headM = .34f;
    float headK = L.child ? 1.22f : 1.f;
    float k = target / (bodyM + headM * headK), hk = k * headK;
    rig.len = target / 2;
    rig.thick = .12f * k;

    std::vector<Box> boxes;
    float lift = P.bob - P.drop * k;
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
    V3 hands[2], wrists[2];
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
        hands[i] = hand;
        wrists[i] = wrist;
    }
    props(boxes, rig, P, hands, wrists, k);
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
    if (L.beard) {
        V3 chin = headBase + hup * (.02f * hk) + rig.fwd * (hd / 2 + .02f * hk);
        boxes.push_back({chin - hup * (.04f * hk), chin + hup * (.1f * hk), hw - .04f * hk, .05f * hk, draw::hex(0xd8d6d0)});
    }
    if (P.lying && detail) {
        // a blanket over the body up to the chest
        boxes.push_back({rig.world(0, 0, .02f * k), rig.world(0, 0, 1.25f * k), .42f * k, .26f * k, draw::hex(0x8a6a4a)});
    }

    // no ink lines on figures: the facets' three tones carry the shape
    draw::Ink inkWas = draw::ink;
    draw::ink.width = 0;
    // far parts first: sort by how close each box's middle is to the camera
    solid::Warp warp;
    if (rig.lying) { warp.place = [&rig](V3 p) { return rig.place(p); }; warp.turn = [&rig](V3 n) { return rig.turn(n); }; }
    std::stable_sort(boxes.begin(), boxes.end(), [&](const Box& a, const Box& b) { return dot(rig.place((a.a + a.b) * .5f), VIEW) < dot(rig.place((b.a + b.b) * .5f), VIEW); });
    for (auto& b : boxes) {
        drawBox(b, rig, warp);
        if (b.decal == 0 && detail && dot(rig.turn(rig.fwd), VIEW) > 0) {
            // the face, on the front of the head
            V3 c = (b.a + b.b) * .5f + rig.fwd * (hd / 2);
            float ink = .95f;
            for (float s : {-1.f, 1.f}) {
                decal(rig, c, rig.fwd, rig.side, hup, s * .068f * hk, -.005f * hk, .066f * hk, .07f * hk, draw::hex(0xf7f4ee));
                decal(rig, c, rig.fwd, rig.side, hup, s * .068f * hk + .008f * hk, -.01f * hk, .04f * hk, .058f * hk, draw::hex(0x6a3e22));
                decal(rig, c, rig.fwd, rig.side, hup, s * .068f * hk + .01f * hk, -.006f * hk, .018f * hk, .026f * hk, draw::hex(0x24180f));
                decal(rig, c, rig.fwd, rig.side, hup, s * .066f * hk, .05f * hk, .07f * hk, .016f * hk, tone(L.hair, ink));
            }
            decal(rig, c, rig.fwd, rig.side, hup, 0, -.085f * hk, .055f * hk, .013f * hk, tone(L.skin, .6f));
        }
    }
    draw::ink = inkWas;
}

} // namespace figure
