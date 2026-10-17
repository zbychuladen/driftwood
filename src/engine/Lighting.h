// Screen-space night lighting: a darkness layer with soft holes punched out
// by light sources, plus additive glow. Designed to be swapped for a shader
// pipeline (normal maps, colored lights) later without touching game code.
#pragma once
#include <vector>
#include "raylib.h"

struct Light {
    Vector2 pos;     // screen position
    float radius;    // screen pixels
    Color glow;      // additive tint (alpha = strength)
};

class Lighting {
public:
    void resize(int w, int h);
    void unload();
    // darkness: 0 (day) .. ~0.65 (deep night)
    void render(float darkness, Color tint, const std::vector<Light>& lights);
private:
    RenderTexture2D rt{};
    int w = 0, h = 0;
};
