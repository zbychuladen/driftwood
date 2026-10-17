#include "game/Building.h"
#include <algorithm>
#include <cmath>
#include "game/Scale.h"

namespace bld {

using solid::Solid;
using solid::V3;
using solid::upright;

namespace {

// Seeded choices: the same building id always makes the same building.
struct Rng {
    uint32_t s;
    float operator()() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (s & 0xffffff) / float(0x1000000); }
    int pick(int n) { return std::min(n - 1, int((*this)() * n)); }
    bool chance(float p) { return (*this)() < p; }
};

Color hex(uint32_t c) { return draw::hex(c); }

// Materials: a colour and the marks that go with it.
struct Mat { const char* name; Color col; solid::Tex tex; };
const Mat PLANKS{"planks", hex(0x9a7452), solid::PLANKS};
const Mat BOARDS{"boards", hex(0x8e6c4c), solid::BOARDS};
const Mat LOGS{"logs", hex(0x86643f), solid::LOGS};
const Mat DAUB{"wattle and daub", hex(0xe4d9be), solid::PLAIN};
const Mat STONE{"stone", hex(0xa9a69a), solid::STONE};
const Mat TIMBER{"half-timber", hex(0xe8dec6), solid::PLAIN};   // daub with dark beams over it
const Mat THATCH{"thatch", hex(0xc9a352), solid::THATCH};
const Mat SHINGLE{"shingle", hex(0xa0573a), solid::SHINGLE};
const Mat PLANKROOF{"plank", hex(0x7a5a3a), solid::PLANKS};
const Mat SLATE{"slate", hex(0x6e7a86), solid::SLATE};

const Color FRAMEWOOD = hex(0x5e4636), DARK = hex(0x2a2018), PANE = hex(0x3d4c56), IRON = hex(0x6e7278);
const uint32_t SHUTTERS[5] = {0x5a7a8a, 0x7a5a3a, 0x6a8a5a, 0xa05a3a, 0x8a7a9a};
const V3 SIDES[4] = {{1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {-1, 0, 0}};

// Builds one design. All positions in metres.
struct Builder {
    Design& d;
    Rng& r;
    bool snow;
    V3 centre;          // middle of the footprint, on the ground
    float W, D;         // footprint size along x and y
    float base = 0;     // plinth height
    V3 wc;              // middle of the walls' base
    float wx = 0, wy = 0, wallH = 0;   // walls' half-sizes and height

    void add(const Solid& s, Part p, Stage st, bool grows = false) { d.pieces.push_back({s, p, st, grows}); }
    V3 n(int side) const { return SIDES[side]; }
    V3 t(int side) const { V3 v = SIDES[side]; return {-v.y, v.x, 0}; }
    // half the walls' size across a side, and out to it
    float across(int side) const { return SIDES[side].x != 0 ? wy : wx; }
    float out(int side) const { return SIDES[side].x != 0 ? wx : wy; }
    float yawOf(int side) const { return std::atan2(t(side).y, t(side).x); }   // length along the side's face

    void plinth(bool stone) {
        base = stone ? .25f : .32f;
        Solid s = upright(solid::BOX, centre, W - .2f, D - .2f, base, 0, stone ? hex(0xb3ad9e) : hex(0x8a6a48), stone ? solid::STONE : solid::PLANKS);
        add(s, BASE, FOUNDATION);
    }
    // walls as one block, set back a little from the front so steps and a porch fit
    void walls(const Mat& m, float h, int front, float backInset = .3f, float frontInset = .65f) {
        wallH = h;
        float shift = (frontInset - backInset) / 2;
        wc = centre - n(front) * shift + V3{0, 0, base};
        float hn = (SIDES[front].x != 0 ? W : D) / 2 - (frontInset + backInset) / 2, ht = (SIDES[front].x != 0 ? D : W) / 2 - .3f;
        if (SIDES[front].x != 0) { wx = hn; wy = ht; } else { wx = ht; wy = hn; }
        add(upright(solid::BOX, wc, wx * 2, wy * 2, h, 0, m.col, m.tex), WALL, WALLS, true);
        d.walls = m.name;
        // a frame of corner posts goes up first; it stays as the timber of half-timber walls
        for (int i = 0; i < 4; i++) {
            V3 c = wc + V3{(i & 1 ? 1 : -1) * (wx + .02f), (i & 2 ? 1 : -1) * (wy + .02f), 0};
            add(upright(solid::BOX, c, .16f, .16f, h, 0, FRAMEWOOD), POST, FRAME, true);
        }
        if (&m == &TIMBER) {
            // dark beams across the faces at sill and lintel height, and a brace on each
            for (int side = 0; side < 4; side++) {
                V3 face = wc + n(side) * (out(side) + .015f);
                for (float z : {.05f, h * .52f, h - .1f}) add(upright(solid::BOX, face + V3{0, 0, z}, across(side) * 2, .04f, .12f, yawOf(side), FRAMEWOOD), BEAM, WALLS);
                V3 a = face + t(side) * (-across(side) * .55f) + V3{0, 0, .1f}, b = face + t(side) * (-across(side) * .1f) + V3{0, 0, h * .5f};
                add(solid::along(a, b, .1f, .04f, n(side), FRAMEWOOD), BEAM, WALLS);
            }
        }
    }
    // a door on the front: a dark opening, a frame, and a panel on a hinge
    void door(int front, float width, float along, const char* style) {
        float h = std::min(scale::DOOR + .05f, wallH - .25f);
        V3 face = wc + n(front) * out(front) + t(front) * along;
        float yaw = yawOf(front);
        add(upright(solid::BOX, face + n(front) * .01f, width, .05f, h, yaw, DARK), OPENING, WALLS);
        for (float s : {-1.f, 1.f}) add(upright(solid::BOX, face + n(front) * .04f + t(front) * (s * (width / 2 + .05f)), .1f, .07f, h + .05f, yaw, FRAMEWOOD), BEAM, WALLS);
        add(upright(solid::BOX, face + n(front) * .04f + V3{0, 0, h}, width + .2f, .08f, .12f, yaw, FRAMEWOOD), BEAM, WALLS);
        bool dbl = std::string(style) == "double";
        int leaves = dbl ? 2 : 1;
        float lw = (width - .06f) / leaves;
        for (int i = 0; i < leaves; i++) {
            V3 c = face + n(front) * .05f + t(front) * (-width / 2 + .03f + lw * (i + .5f));
            add(upright(solid::BOX, c, lw - .02f, .05f, h - .04f, yaw, hex(0x7a5634), solid::BOARDS), DOOR, DETAILS);
            if (i == 0) {
                d.doorPiece = int(d.pieces.size()) - 1;
                d.hinge = face + n(front) * .05f + t(front) * (-width / 2 + .03f);
                d.doorYaw = yaw;
            }
        }
        d.doorWidth = width;
        d.doorStyle = style;
    }
    // steps up to the door when the floor is raised
    void steps(int front, float width, float along) {
        if (base < .2f) return;
        V3 foot = wc - V3{0, 0, base} + n(front) * (out(front) + .25f) + t(front) * along;
        add(upright(solid::BOX, foot, width + .3f, .35f, base * .55f, yawOf(front), hex(0xa29c8c), solid::STONE), STEPS, FOUNDATION);
    }
    // a window on a side, `along` the face; returns false if it would not fit
    bool window(int side, float along, int style) {
        float half = style == 2 ? .14f : style == 1 ? .26f : .34f;
        if (std::abs(along) + half + .2f > across(side)) return false;
        float wz = base + std::min(1.05f, wallH * .4f), hgt = style == 2 ? .7f : style == 1 ? .5f : .68f;
        V3 face = wc - V3{0, 0, base} + n(side) * out(side) + t(side) * along + V3{0, 0, wz};
        float yaw = yawOf(side);
        add(upright(solid::BOX, face + n(side) * .02f, half * 2 + .12f, .06f, hgt + .12f, yaw, FRAMEWOOD), WINDOW, WALLS);
        add(upright(solid::BOX, face + n(side) * .04f + V3{0, 0, .06f}, half * 2, .04f, hgt, yaw, PANE), WINDOW, WALLS);
        d.panes.push_back(int(d.pieces.size()) - 1);
        if (style == 0) {
            Color sh = hex(SHUTTERS[r.pick(5)]);
            for (float s : {-1.f, 1.f}) add(upright(solid::BOX, face + n(side) * .05f + t(side) * (s * (half + .2f)), .3f, .04f, hgt + .1f, yaw, sh, solid::BOARDS), SHUTTER, DETAILS);
        }
        d.windows++;
        return true;
    }
    // windows spread along the faces, never on top of the door
    void windows(int front, int want, int style, float doorAlong, float doorW) {
        int sides[4] = {front, 0, 0, 0};
        int k = 1;
        for (int s = 0; s < 4; s++) if (s != front && s != 3 - front) sides[k++] = s;
        sides[k++] = 3 - front;
        for (int i = 0; i < k && d.windows < want; i++) {
            int side = sides[i];
            float span = across(side);
            int slots = std::max(1, int(span * 2 / 1.6f));
            for (int j = 0; j < slots && d.windows < want; j++) {
                float a = -span + (j + .5f) * (span * 2 / slots);
                if (side == front && std::abs(a - doorAlong) < doorW / 2 + .55f) continue;
                window(side, a, style);
            }
        }
    }
    // a roof along the longer side, with eaves out past the walls, and gable ends in the wall material
    void roof(solid::Kind kind, const Mat& m, float h, float over, const Mat& wall) {
        bool alongX = wx >= wy;
        float len = (alongX ? wx : wy) * 2, wid = (alongX ? wy : wx) * 2;
        V3 at = wc + V3{0, 0, wallH - .04f};
        Color c = snow ? draw::mix(m.col, hex(0xeef1f2), .85f) : m.col;
        add(upright(kind, at - V3{0, 0, .16f}, len + over * 2, wid + over * 2, h, alongX ? 0 : 1.5708f, draw::shade(c, .62f)), ROOF, ROOFING);
        add(upright(kind, at, len + over * 2, wid + over * 2, h, alongX ? 0 : 1.5708f, c, m.tex), ROOF, ROOFING);
        if (kind == solid::GABLE) add(upright(solid::GABLE, at, len, wid, h * wid / (wid + over * 2), alongX ? 0 : 1.5708f, wall.col, wall.tex == solid::PLAIN ? solid::PLAIN : solid::BOARDS), WALL, ROOFING);
        d.roof = std::string(m.name) + (kind == solid::GABLE ? " gable" : kind == solid::HIP ? " hip" : "");
    }
    void chimney(float topAbove, float along) {
        bool alongX = wx >= wy;
        V3 at = wc + (alongX ? V3{along * wx, wy * .2f, 0} : V3{wx * .2f, along * wy, 0});
        float h = wallH + topAbove;
        add(upright(solid::BOX, at, .62f, .62f, h, 0, hex(0x9a958a), solid::STONE), CHIMNEY, ROOFING);
        add(upright(solid::BOX, at + V3{0, 0, h}, .74f, .74f, .14f, 0, hex(0x84807a)), CHIMNEY, ROOFING);
        d.chimney = true;
        d.smoke = at + V3{0, 0, h + .2f};
        d.smokes = true;
    }
    // a vent on the ridge, where hearth smoke leaves a thatched hall
    void smokeHole(float roofH) {
        bool alongX = wx >= wy;
        V3 at = wc + V3{0, 0, wallH + roofH - .25f};
        add(upright(solid::GABLE, at, .9f, .7f, .45f, alongX ? 0 : 1.5708f, hex(0x8a6a3a), solid::THATCH), CHIMNEY, ROOFING);
        d.smoke = at + V3{0, 0, .55f};
        d.smokes = true;
    }
    // a little roof over the door on two posts
    void porch(int front, float along, float width) {
        V3 face = wc + n(front) * out(front) + t(front) * along;
        float h = std::min(wallH * .92f, 2.3f), reach = .85f;
        for (float s : {-1.f, 1.f}) {
            V3 p = face - V3{0, 0, base} + n(front) * reach + t(front) * (s * (width / 2 + .25f));
            add(upright(solid::BOX, p, .14f, .14f, h + base, 0, FRAMEWOOD), PORCH, DETAILS);
        }
        V3 a = face + V3{0, 0, h + .35f}, b = face + n(front) * (reach + .3f) + V3{0, 0, h - .05f};
        Solid lid = solid::along(a, b, width + .9f, .1f, t(front), snow ? hex(0xeef1f2) : hex(0x7a5a3a));
        add(lid, PORCH, DETAILS);
        d.porch = true;
    }
    // things left outside: crates, barrels, a wood stack, a flower box
    void extra(int kind, int side, float along, float outBy) {
        V3 p = wc - V3{0, 0, base} + n(side) * (out(side) + outBy) + t(side) * along;
        if (kind == 0) add(upright(solid::BOX, p, .5f, .5f, .48f, .3f * r(), hex(0x9a7a52), solid::PLANKS), EXTRA, DETAILS);
        else if (kind == 1) add(upright(solid::CYLINDER, p, .5f, .5f, .62f, 0, hex(0x8a6440), solid::BOARDS), EXTRA, DETAILS);
        else if (kind == 2) add(upright(solid::BOX, p, .95f, .42f, .55f, yawOf(side), hex(0x8a6440), solid::LOGS), EXTRA, DETAILS);
        else add(upright(solid::BOX, p + V3{0, 0, base + .8f}, .62f, .16f, .14f, yawOf(side), hex(0x7a5634)), EXTRA, DETAILS);
    }
};

const char* DOOR_STYLES[3] = {"plank", "ledged", "double"};

} // namespace

bool assembled(const std::string& k) {
    static const char* keys[] = {"hut", "leanto", "longhouse", "store", "school", "forge", "smoke", "loomhouse", "infirmary"};
    for (auto s : keys) if (k == s) return true;
    return false;
}

int sideToward(float cx, float cy, float tx, float ty) {
    float dx = tx - cx, dy = ty - cy;
    if (std::abs(dx) >= std::abs(dy)) return dx >= 0 ? 0 : 3;
    return dy >= 0 ? 1 : 2;
}

Design make(const std::string& key, int id, int x, int y, int w, int h, int front, bool snow) {
    Design d;
    d.key = key;
    d.front = front;
    uint32_t seed = uint32_t(id) * 2654435761u ^ uint32_t(std::hash<std::string>{}(key));
    Rng r{seed | 1};
    for (int i = 0; i < 3; i++) r();
    Builder b{d, r, snow};
    b.W = w * scale::TILE_M;
    b.D = h * scale::TILE_M;
    b.centre = {x * scale::TILE_M + b.W / 2, y * scale::TILE_M + b.D / 2, 0};
    auto pickMat = [&](std::initializer_list<const Mat*> l) { return *(l.begin()[r.pick(int(l.size()))]); };

    if (key == "hut" && r.chance(.75f)) {
        // round: daub or board walls under a thatched cone
        b.base = 0;
        b.add(upright(solid::CYLINDER, b.centre, b.W * .78f, b.D * .78f, .14f, 0, hex(0xb3ad9e), solid::STONE), BASE, FOUNDATION);
        b.base = .14f;
        const Mat& m = r.chance(.6f) ? DAUB : BOARDS;
        float rad = std::min(b.W, b.D) / 2 - .4f, wallH = scale::HUT_WALL;
        b.wc = b.centre + V3{0, 0, b.base};
        b.wx = b.wy = rad;
        b.wallH = wallH;
        b.add(upright(solid::CYLINDER, b.wc, rad * 2, rad * 2, wallH, 0, m.col, m.tex), WALL, WALLS, true);
        d.walls = std::string(m.name) + ", round";
        for (int i = 0; i < 4; i++) {
            float a = 1.5708f * i + .785f;
            b.add(upright(solid::BOX, b.wc + V3{std::cos(a) * rad, std::sin(a) * rad, 0}, .14f, .14f, wallH, 0, FRAMEWOOD), POST, FRAME, true);
        }
        b.door(front, .85f, 0, "plank");
        if (r.chance(.6f)) b.window((front + 1 + r.pick(2)) % 4, 0, 1);
        Color roof = snow ? hex(0xeef1f2) : THATCH.col;
        b.add(upright(solid::CONE, b.wc + V3{0, 0, wallH - .26f}, (rad + .45f) * 2, (rad + .45f) * 2, scale::HUT_ROOF, 0, draw::shade(roof, .62f)), ROOF, ROOFING);
        b.add(upright(solid::CONE, b.wc + V3{0, 0, wallH - .1f}, (rad + .45f) * 2, (rad + .45f) * 2, scale::HUT_ROOF, 0, roof, solid::THATCH), ROOF, ROOFING);
        d.roof = "thatch cone";
        if (r.chance(.5f)) b.extra(r.pick(2), (front + 1 + r.pick(2)) % 4, 0, .1f);
    } else if (key == "leanto") {
        // an open shelter: a low back wall, two posts at the front, a sloping roof
        b.plinth(false);
        float hi = scale::LEANTO, lo = 1.2f;
        int back = 3 - front;
        b.wc = b.centre + V3{0, 0, b.base};
        b.wx = b.W / 2 - .2f; b.wy = b.D / 2 - .2f;
        V3 bw = b.centre + b.n(back) * (b.out(back) - .1f) + V3{0, 0, b.base};
        const Mat& m = pickMat({&LOGS, &BOARDS});
        b.add(upright(solid::BOX, bw, b.across(back) * 2, .18f, lo, b.yawOf(back), m.col, m.tex), WALL, WALLS, true);
        for (float s : {-1.f, 1.f}) {
            V3 p = b.centre + b.n(front) * (b.out(front) - .1f) + b.t(front) * (s * (b.across(front) - .1f)) + V3{0, 0, b.base};
            b.add(upright(solid::BOX, p, .16f, .16f, hi, 0, FRAMEWOOD), POST, FRAME);
        }
        V3 a = bw + V3{0, 0, lo + .1f} - b.n(back) * .2f, top = b.centre + b.n(front) * (b.out(front) + .35f) + V3{0, 0, b.base + hi + .05f};
        const Mat& rm = pickMat({&THATCH, &PLANKROOF, &SHINGLE});
        b.add(solid::along(a, top, b.across(front) * 2 + .5f, .26f, b.t(front), snow ? hex(0xeef1f2) : rm.col), ROOF, ROOFING);
        d.pieces.back().s.tex = rm.tex;
        d.walls = m.name;
        d.roof = std::string(rm.name) + " lean-to";
        d.openSide = true;
        for (int i = 0; i < 2; i++) b.add(upright(solid::CYLINDER, b.centre + b.t(front) * (-.8f + i * 1.6f) + V3{0, 0, b.base}, .7f, .35f, .2f, 0, hex(0x8a7a5a)), EXTRA, DETAILS);
    } else {
        // rectangular buildings: the recipe picks materials and how many of each part
        struct Recipe { float wall, roofH; std::vector<const Mat*> walls, roofs; int minWin, maxWin, winStyle; bool chimney, holeOk, wideDoor, porchOk; };
        Recipe R;
        if (key == "longhouse") R = {scale::LONGHOUSE_WALL, scale::LONGHOUSE_ROOF, {&LOGS, &PLANKS, &TIMBER}, {&THATCH, &SHINGLE}, 2, 4, -1, false, true, false, true};
        else if (key == "store") R = {scale::STORE_WALL, scale::STORE_ROOF, {&PLANKS, &BOARDS, &LOGS}, {&PLANKROOF, &SHINGLE}, 0, 1, 1, false, false, true, false};
        else if (key == "school") R = {scale::SCHOOL_WALL, scale::SCHOOL_ROOF, {&PLANKS, &TIMBER, &DAUB}, {&SHINGLE, &SLATE, &THATCH}, 3, 5, 0, false, false, false, true};
        else if (key == "forge") R = {scale::FORGE_WALL, scale::FORGE_ROOF, {&STONE, &TIMBER}, {&SLATE, &SHINGLE}, 0, 1, 2, true, false, true, false};
        else if (key == "smoke") R = {scale::SMOKE_WALL, scale::SMOKE_ROOF, {&BOARDS, &PLANKS}, {&PLANKROOF, &THATCH}, 0, 0, 2, false, true, false, false};
        else if (key == "loomhouse") R = {scale::LOOM_WALL, scale::LOOM_ROOF, {&TIMBER, &DAUB, &PLANKS}, {&THATCH, &SHINGLE}, 2, 3, 0, false, false, false, true};
        else if (key == "infirmary") R = {scale::INFIRMARY_WALL, scale::INFIRMARY_ROOF, {&DAUB, &TIMBER}, {&SHINGLE, &SLATE}, 2, 4, 0, false, false, false, true};
        else R = {scale::HUT_WALL, scale::HUT_ROOF, {&DAUB, &BOARDS}, {&THATCH}, 1, 2, 1, false, false, false, false};   // a square cottage hut
        const Mat& wm = *R.walls[r.pick(int(R.walls.size()))];
        const Mat& rm = *R.roofs[r.pick(int(R.roofs.size()))];
        b.plinth(key == "forge" || r.chance(.6f));
        b.walls(wm, R.wall, front);
        float span = b.across(front);
        float doorW = R.wideDoor ? 1.7f : .95f;
        float along = span > 2.2f && r.chance(.5f) ? (r.chance(.5f) ? -1 : 1) * span * .35f : 0;
        if (key == "forge") {
            // the smithy's front stands open over the hearth and anvil
            V3 face = b.wc + b.n(front) * b.out(front);
            b.add(upright(solid::BOX, face + b.n(front) * .01f, span * 1.6f, .05f, R.wall - .35f, b.yawOf(front), DARK), OPENING, WALLS);
            b.add(upright(solid::BOX, face + b.n(front) * .05f + V3{0, 0, R.wall - .38f}, span * 1.7f, .1f, .16f, b.yawOf(front), FRAMEWOOD), BEAM, WALLS);
            d.openSide = true;
            d.doorWidth = span * 1.6f;
            d.doorStyle = "open front";
            V3 anvil = face - V3{0, 0, b.base} + b.n(front) * .7f + b.t(front) * (span * .3f);
            b.add(upright(solid::CYLINDER, anvil, .45f, .45f, .5f, 0, hex(0x7a5a3a), solid::BOARDS), EXTRA, DETAILS);
            b.add(upright(solid::BOX, anvil + V3{0, 0, .5f}, .5f, .2f, .2f, b.yawOf(front), IRON), EXTRA, DETAILS);
        } else {
            b.door(front, doorW, along, R.wideDoor ? "double" : DOOR_STYLES[r.pick(2)]);
            b.steps(front, doorW, along);
        }
        int want = R.minWin + r.pick(R.maxWin - R.minWin + 1);
        int style = R.winStyle >= 0 ? R.winStyle : r.pick(2);
        b.windows(front, want, style, along, doorW);
        solid::Kind kind = rm.tex == solid::THATCH || r.chance(.35f) ? solid::HIP : solid::GABLE;
        if (key == "forge" || key == "store") kind = solid::GABLE;
        b.roof(kind, rm, R.roofH, rm.tex == solid::THATCH ? .45f : .32f, wm);
        if (R.chimney) b.chimney(scale::FORGE_CHIMNEY - R.wall, r.chance(.5f) ? -.55f : .55f);
        else if (R.holeOk && (key == "smoke" || r.chance(.5f))) b.smokeHole(R.roofH);
        else if (key == "longhouse") b.chimney(R.roofH + .15f, r.chance(.5f) ? -.6f : .6f);
        if (R.porchOk && r.chance(.55f)) b.porch(front, along, doorW);
        // a few things outside, beside the walls and clear of the door
        int extras = key == "store" ? 2 + r.pick(2) : r.pick(3);
        for (int i = 0; i < extras; i++) {
            int side = (front + 1 + r.pick(2)) % 4;
            if (side == 3 - front) side = front;
            float a = (r.chance(.5f) ? -1 : 1) * (b.across(side) - .35f);
            if (side == front && std::abs(a - along) < doorW / 2 + .6f) a = -a;
            int kind2 = key == "store" ? r.pick(2) : key == "longhouse" || key == "smoke" ? 2 * r.pick(2) : r.pick(4);
            b.extra(kind2, side, a, kind2 == 3 ? 0 : .3f);
        }
    }
    // the tallest point, for scale checks
    for (auto& p : d.pieces) {
        const Solid& s = p.s;
        float z = s.c.z + std::abs(s.ax[0].z) * s.h[0] + std::abs(s.ax[1].z) * s.h[1] + std::abs(s.ax[2].z) * s.h[2];
        d.top = std::max(d.top, z);
    }
    return d;
}

int visible(const Design& d, float progress) {
    int n = 0;
    for (auto& p : d.pieces) n += progress >= STAGE_AT[p.stage];
    return n;
}

void draw(const Design& d, float progress, float doorOpen, bool lit) {
    progress = std::clamp(progress, 0.f, 1.f);
    std::vector<Solid> show;
    show.reserve(d.pieces.size());
    for (size_t i = 0; i < d.pieces.size(); i++) {
        const Piece& p = d.pieces[i];
        float from = STAGE_AT[p.stage], to = p.stage + 1 < STAGES ? STAGE_AT[p.stage + 1] : 1.f;
        if (progress < from) continue;
        Solid s = p.s;
        if (p.grows && progress < to) {
            // walls and posts rise from the ground while their stage is under way
            float k = std::max(.04f, (progress - from) / (to - from));
            V3 baseAt = s.c - s.ax[2] * s.h[2];
            s.h[2] *= k;
            s.c = baseAt + s.ax[2] * s.h[2];
        }
        if (int(i) == d.doorPiece && doorOpen > 0) {
            // swing the panel outward about its hinge
            float a = -doorOpen * 1.75f;
            auto rot = [&](V3 v) { return V3{v.x * std::cos(a) - v.y * std::sin(a), v.x * std::sin(a) + v.y * std::cos(a), v.z}; };
            V3 rel = s.c - d.hinge;
            s.c = d.hinge + rot(rel);
            for (auto& ax : s.ax) ax = rot(ax);
        }
        if (lit && std::find(d.panes.begin(), d.panes.end(), int(i)) != d.panes.end()) s.col = draw::hex(0xffcf7a);
        show.push_back(s);
    }
    solid::drawAll(std::move(show));
}

} // namespace bld
