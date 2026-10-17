// Buildings assembled from parts, the way a builder would: a base, walls, a
// door, windows, a roof, a chimney, a porch and a few things left outside.
// Each building type has a recipe naming the parts it must have and the ones
// it may choose; the choices (materials, styles, placement) are seeded by the
// building's id, so it always looks the same and no two are alike.
//
// Every part is its own solid with a construction stage, so a building can go
// up piece by piece and its door can swing open.
#pragma once
#include <string>
#include <vector>
#include "game/Solid.h"

namespace bld {

enum Part : unsigned char { BASE, STEPS, POST, WALL, BEAM, OPENING, DOOR, WINDOW, SHUTTER, ROOF, CHIMNEY, PORCH, EXTRA };
// The order things go up in. A part shows once construction passes its stage.
enum Stage : unsigned char { FOUNDATION, FRAME, WALLS, ROOFING, DETAILS, STAGES };
// Construction progress at which each stage starts.
constexpr float STAGE_AT[STAGES] = {0, .12f, .3f, .65f, .9f};

struct Piece {
    solid::Solid s;
    Part part;
    Stage stage;
    bool grows = false;   // rises from its base while its stage is under way (walls)
};

struct Design {
    std::string key;
    std::vector<Piece> pieces;
    int front = 0;                     // the side the door faces: 0 +x, 1 +y, 2 -y, 3 -x
    // what the recipe chose, for tests and the info card
    std::string walls, roof, doorStyle;
    int windows = 0;
    bool chimney = false, porch = false, openSide = false;
    float doorWidth = 0, top = 0;      // metres
    // the door panel turns about a vertical hinge
    solid::V3 hinge;
    float doorYaw = 0;                 // closed
    int doorPiece = -1;
    std::vector<int> panes;            // window panes, which glow at night
    solid::V3 smoke;                   // where chimney smoke rises from
    bool smokes = false;
};

// Whether a building type is assembled from parts (the rest keep hand-drawn art).
bool assembled(const std::string& key);

// Designs a building on footprint (x, y, w, h) in tiles, its door toward front.
// Heights follow Scale.h.
Design make(const std::string& key, int id, int x, int y, int w, int h, int front, bool snow);

// The side of a footprint that faces a point, for putting the door toward camp.
int sideToward(float cx, float cy, float tx, float ty);

// How many pieces show at a construction progress (0..1).
int visible(const Design& d, float progress);

// Draws a design part-built (progress 0..1, 1 finished), with its door open by
// doorOpen (0..1) and its windows lit when lit is set.
void draw(const Design& d, float progress, float doorOpen, bool lit);

} // namespace bld
