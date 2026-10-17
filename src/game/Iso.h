// Isometric projection: tile coordinates (x right-down, y left-down) to world pixels.
#pragma once
#include "raylib.h"
#include "sim/Sim.h"

namespace iso {
constexpr float HW = 32, HH = 16, PADY = 60;
constexpr float OX = dw::GH * HW;
constexpr float WORLD_W = (dw::GW + dw::GH) * HW;
constexpr float WORLD_H = (dw::GW + dw::GH) * HH + PADY + 20;

inline Vector2 wp(float tx, float ty) { return {(tx - ty) * HW + OX, (tx + ty) * HH + PADY}; }
inline Vector2 P(float tx, float ty, float up = 0) { Vector2 v = wp(tx, ty); v.y -= up; return v; }
inline Vector2 toTile(float wx, float wy) {
    float a = (wx - OX) / HW, b = (wy - PADY) / HH;
    return {(a + b) / 2, (b - a) / 2};
}
} // namespace iso
