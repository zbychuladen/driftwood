// Small 3D solids drawn in the world's isometric projection: boxes, gable and
// hip roofs, cones and cylinders. Settler figures and buildings are both made
// of them, so they share one light, one perspective and one way of ordering
// parts. Units are metres; x and y run along the tile axes, z is up.
#pragma once
#include <functional>
#include <vector>
#include "engine/Draw.h"
#include "raylib.h"

namespace solid {

struct V3 { float x = 0, y = 0, z = 0; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float k) { return {a.x * k, a.y * k, a.z * k}; }
inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
V3 unit(V3 a);

// Toward the camera: the direction that projects to a single pixel. A face is
// seen when its outward normal points this way.
extern const V3 VIEW;
// Light from the upper left of the screen: tops brightest, left faces next.
extern const V3 LIGHT;

draw::V2 project(V3 p);

enum Kind : unsigned char {
    BOX,        // a box
    GABLE,      // a roof: ridge along the length, two slopes, triangular ends
    HIP,        // a roof that slopes on all four sides
    CONE,       // a round roof, or anything pointed
    CYLINDER,   // round walls, posts, barrels
};

// Surface marks drawn on the faces, in a darker shade of the colour.
enum Tex : unsigned char { PLAIN, PLANKS, BOARDS, LOGS, STONE, THATCH, SHINGLE, SLATE };

// A solid is centred on c with half-extents h along three axes: ax[0] the
// length (a roof's ridge), ax[1] the width, ax[2] up. Roofs and cones rise
// from their base at -h[2] to the ridge or tip at +h[2].
struct Solid {
    Kind kind = BOX;
    V3 c;
    V3 ax[3] = {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}};
    float h[3] = {.5f, .5f, .5f};
    Color col = WHITE;
    Tex tex = PLAIN;
};

// An upright solid standing on a point (its base at c.z), size in metres,
// turned yaw radians about the vertical.
Solid upright(Kind k, V3 base, float length, float width, float height, float yaw, Color col, Tex tex = PLAIN);
// A box from a to b, w wide and d deep, its width kept square to side.
Solid along(V3 a, V3 b, float w, float d, V3 side, Color col);

// Optional warp for whole groups of solids, such as a figure lying down:
// place moves points, turn moves normals the same way.
struct Warp {
    std::function<V3(V3)> place, turn;
};

// Draws one solid: the faces turned toward the camera, shaded by the light.
void draw(const Solid& s, const Warp* warp = nullptr);
// How near the camera a solid's middle is; draw lower values first.
float depth(const Solid& s, const Warp* warp = nullptr);
// Draws a set far to near.
void drawAll(std::vector<Solid> solids, const Warp* warp = nullptr);

} // namespace solid
