// Real-world sizes for everything the world draws, in metres, and the one
// conversion to world pixels. Art reads its heights from here so people,
// doors, trees and buildings stay in proportion; tests/game_tests.cpp measures
// the drawn art against this table.
//
// The ground is fixed by the projection: a tile is 2 m on a side. Heights use
// PX_PER_M, chosen so a 2 m cube looks about as tall as a tile is deep.
#pragma once

namespace scale {

constexpr float TILE_M = 2.f;
constexpr float PX_PER_M = 17.f;
constexpr float px(float metres) { return metres * PX_PER_M; }
// Along the ground, a tile's 2.83 m diagonal spans 64 screen pixels left to right.
constexpr float GROUND_PX_PER_M = 22.6f;

// people and animals
constexpr float PERSON = 1.7f;
constexpr float CHILD = 1.2f;
constexpr float WOLF = 0.8f;          // at the shoulder

// plants and stones (a size of 1; objects vary around it)
constexpr float BROADLEAF = 6.5f;
constexpr float CONIFER = 8.f;
constexpr float PALM = 7.f;
constexpr float BUSH = 1.2f;
constexpr float ROCK = 0.9f;
constexpr float TOTEM = 3.f;

// building parts
constexpr float DOOR = 1.9f;          // every doorway a grown person walks through
constexpr float WALL = 2.4f;          // plain one-storey wall
constexpr float HUT_WALL = 2.2f, HUT_ROOF = 2.4f;
constexpr float LEANTO = 2.1f;        // high side
constexpr float RACK = 1.8f;
constexpr float WELL_CURB = 0.8f, WELL_ROOF = 2.6f;
constexpr float STORE_WALL = 2.6f, STORE_ROOF = 1.8f;
constexpr float SMOKE_WALL = 2.2f, SMOKE_ROOF = 1.2f;
constexpr float LONGHOUSE_WALL = 2.4f, LONGHOUSE_ROOF = 3.f;
constexpr float SCHOOL_WALL = 2.7f, SCHOOL_ROOF = 2.f;
constexpr float FORGE_WALL = 2.5f, FORGE_ROOF = 1.3f, FORGE_CHIMNEY = 5.f;
constexpr float LOOM_WALL = 2.5f, LOOM_ROOF = 1.6f;
constexpr float INFIRMARY_WALL = 2.5f, INFIRMARY_ROOF = 1.8f;
constexpr float HERMIT_WALL = 2.2f, HERMIT_ROOF = 1.4f;
constexpr float COOK_WALL = 2.5f, COOK_ROOF = 1.8f;
constexpr float KILN = 2.f;
constexpr float TOWER = 8.5f;         // to the roof peak
constexpr float LIGHTHOUSE = 11.f;
constexpr float SIGNAL = 2.f;
constexpr float MINE_HILL = 3.5f, MINE_ENTRANCE = 2.1f;
constexpr float CIRCLE_SEAT = .45f, CIRCLE_TRIPOD = 1.05f;   // story circle: log seat height, the pot tripod over its fire
constexpr float MAST = 5.f;
constexpr float BOAT_LENGTH = 3.5f;   // the castaways' dinghy

} // namespace scale
