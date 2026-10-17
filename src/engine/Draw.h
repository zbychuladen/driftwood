// 2D drawing primitives on top of raylib/rlgl: filled polygons, ellipses,
// gradients, curves, and a global alpha for fading whole objects.
#pragma once
#include <cstdint>
#include <initializer_list>
#include <vector>
#include "raylib.h"

namespace draw {

using V2 = Vector2;

extern float alpha;                     // multiplies every color's alpha

// Scale everything drawn until pop() about a pivot: p' = pivot + (p - pivot) * k.
// Lets art written in absolute coordinates be resized as a whole. Nests.
// push(at, k, from) also moves: p' = at + (p - from) * k, for art drawn around (0,0).
void push(V2 pivot, float k);
void push(V2 at, float k, V2 from);
void pop();

// Bounds recording: while a Box is set, primitives grow it instead of drawing,
// so art can be measured with no window or GPU (used by the tests).
struct Box {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    bool empty() const { return x1 < x0; }
};
void record(Box* box);                  // nullptr returns to drawing

// Hit testing: while a Probe is set (only together with record()), any
// opaque-enough triangle that contains the point sets hit. Faint shapes such
// as shadows do not count (judged against the global alpha, so a half-built
// building drawn see-through is still solid to a click).
struct Probe {
    V2 at;
    bool hit = false;
};
void probe(Probe* p);

Color hex(uint32_t rgb, float a = 1.f);
Color rgba(int r, int g, int b, float a = 1.f);
Color shade(Color c, float f);          // brighten/darken rgb, keep alpha
Color mix(Color a, Color b, float t);
Color withA(Color c, float a);          // replace alpha (0..1)
Color apply(Color c);                   // apply global alpha

void poly(const V2* pts, int n, Color c);
void poly(std::initializer_list<V2> pts, Color c);
void polyV(const std::vector<V2>& pts, Color c);
void quad(V2 a, V2 b, V2 c, V2 d, Color ca, Color cb, Color cc, Color cd);   // per-vertex colors
void tri(V2 a, V2 b, V2 c, Color col);
void ellipse(float cx, float cy, float rx, float ry, Color c, int seg = 0);
void circle(float cx, float cy, float r, Color c);
void ring(float cx, float cy, float rx, float ry, float thick, Color c);
void radial(float cx, float cy, float rx, float ry, Color inner, Color outer, int seg = 32);
void line(float x1, float y1, float x2, float y2, float w, Color c);
void rect(float x, float y, float w, float h, Color c);
void rectGradH(float x, float y, float w, float h, Color left, Color right);
// Fan-filled polygon with a horizontal color gradient between x0 and x1.
void polyGradX(const std::vector<V2>& pts, float x0, float x1, Color left, Color right);

// Point helpers for building shapes
std::vector<V2> arc(float cx, float cy, float rx, float ry, float a0, float a1, int seg = 16);
std::vector<V2> bezier(V2 a, V2 ctrl, V2 b, int seg = 10);
void curve(V2 a, V2 ctrl, V2 b, float w, Color c, int seg = 10);

} // namespace draw
