// Settlers drawn as little 3D box figures: head, hair, torso and jacket, arms,
// hands, hips, legs and shoes, each a box hung on a simple skeleton. Sizes are
// metres (Scale.h) and the boxes go through the same isometric projection as
// the world, so a figure turns, leans and walks in real perspective. Each box
// shows its top and the two sides that face the camera, shaded in three tones.
#pragma once
#include <string>
#include "raylib.h"
#include "sim/Sim.h"

namespace figure {

// The four directions a figure can face, along the tile axes. SE and SW face
// the camera (faces drawn), NE and NW face away (backs of heads).
enum Facing { SE, SW, NE, NW };
// The facing for a step or a look toward (dx, dy) in tiles.
Facing facingToward(float dx, float dy, Facing otherwise);

// What a figure wears. From a settler with lookOf(), or set by hand.
struct Look {
    Color jacket, shirt, legs, skin, hair, shoes;
    int hairStyle = 0;           // 0 short, 1 bob, 2 bun
    bool shorts = false, child = false;
};
Look lookOf(const dw::Person& p);

// Joint angles in radians, distances in metres. Zero is standing straight with
// arms hanging. Index 0 is the left limb, 1 the right.
struct Pose {
    float drop = 0, bob = 0;     // whole body lowered, and a small bounce
    float lean = 0, head = 0;    // torso and head pitch, forward positive
    float hip[2] = {0, 0}, knee[2] = {0, 0};
    float shoulder[2] = {0, 0}, spread[2] = {.07f, .07f}, elbow[2] = {.18f, .18f};
};

// Poses for an activity at time t (seconds) or, when walking, at the stride
// phase (radians). Activities without a clip yet stand idle.
Pose idle(float t);
Pose walk(float phase);

// Draws a figure standing at tile (x, y). detail 0 is the simple far-away
// version (no face, hands or trim), 1 the full one.
void draw(float x, float y, Facing f, const Look& look, const Pose& pose, int detail);

// The screen height in pixels below which the simple version is used.
constexpr float DETAIL_PX = 45;

} // namespace figure
