// Procedural art for every object in the world. Each function draws in world
// pixel space at the object's isometric position. These are deliberately
// isolated so they can be replaced one by one with sprite/atlas art.
#pragma once
#include "game/Building.h"
#include "sim/Sim.h"

namespace art {

struct Ctx {
    dw::Sim* sim;
    double t;        // game minutes, drives animation
    int season;
    double hour;
    bool effects = true;   // smoke and flames; off when measuring art
};

void tree(const Ctx& c, const dw::Obj& o);
void stump(const Ctx& c, const dw::Obj& o);
void rock(const Ctx& c, const dw::Obj& o);
void bush(const Ctx& c, const dw::Obj& o);
void totem(const Ctx& c, const dw::Obj& o);
void boat(const Ctx& c);
void stockpile(const Ctx& c);
void building(const Ctx& c, dw::Project& b);
bool isFlat(const std::string& key);   // drawn under everything else
void poi(const Ctx& c, const dw::Poi& p);
// A settler as a box figure. facing is a figure::Facing; detail 0 is the far-away version.
void person(const Ctx& c, const dw::Person& p, bool selected, int facing, int detail);
void wolf(const Ctx& c, const dw::Wolf& w);
float doorHeight(float wallPx);
// The assembled design of a building (null for types still drawn by hand).
const bld::Design* designFor(const dw::Sim& s, const dw::Project& b);
// How far each building's door stands open (0..1), set by the world view.
void setDoor(int id, float open);
float doorOf(int id);
void resetDoors();       // doorway height drawn in a wall of this height (world px)

} // namespace art
